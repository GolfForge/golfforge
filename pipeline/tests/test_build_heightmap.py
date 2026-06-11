"""Tests for pipeline/build_heightmap.py.

Scope today is narrow: freeze the OpenTopography API-key resolution contract
(GOL-50). The DEM-fetching + raster-conversion code paths hit the network and
big rasterio dependencies and are intentionally left for end-to-end testing.

Run from `pipeline/` with:  python3 -m pytest tests/
"""

import sys
from pathlib import Path

import pytest


# Allow `import build_heightmap` when pytest is invoked from the pipeline dir.
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from build_heightmap import (  # noqa: E402
    OPENTOPO_ENV_VAR,
    fetch_dem_cog,
    resolve_opentopo_key,
    vsi_path,
)


class TestResolveOpentopoKey:
    """Priority: CLI flag > env var > friendly SystemExit."""

    def test_cli_flag_wins(self):
        # CLI value takes precedence even when the env var is set.
        env = {OPENTOPO_ENV_VAR: "from-env"}
        assert resolve_opentopo_key("from-cli", env=env) == "from-cli"

    def test_env_var_used_when_no_cli(self):
        env = {OPENTOPO_ENV_VAR: "from-env"}
        assert resolve_opentopo_key(None, env=env) == "from-env"

    def test_empty_cli_falls_through_to_env(self):
        # argparse may give us "" rather than None if a user passes --opentopo-key ""
        env = {OPENTOPO_ENV_VAR: "from-env"}
        assert resolve_opentopo_key("", env=env) == "from-env"

    def test_whitespace_env_treated_as_missing(self):
        env = {OPENTOPO_ENV_VAR: "   "}
        with pytest.raises(SystemExit) as exc:
            resolve_opentopo_key(None, env=env)
        assert exc.value.code == 2

    def test_missing_everywhere_exits_with_help(self, capsys):
        with pytest.raises(SystemExit) as exc:
            resolve_opentopo_key(None, env={})
        assert exc.value.code == 2
        # Help text on stderr must point contributors at the signup URL + env var.
        err = capsys.readouterr().err
        assert "portal.opentopography.org" in err
        assert OPENTOPO_ENV_VAR in err
        assert "--opentopo-key" in err

    def test_no_demo_key_leakage(self):
        """Regression: the old hardcoded demo key 'demoapikeyot2022' must not
        sneak back in as a fallback. GOL-50 removed it explicitly so contributors
        don't share the demo's rate limit."""
        with pytest.raises(SystemExit):
            resolve_opentopo_key(None, env={})
        # Walk the module's globals for the old constant name.
        import build_heightmap

        assert not hasattr(build_heightmap, "OPENTOPO_DEMO_KEY")
        # And no module-level string literal that happens to match.
        for name, value in vars(build_heightmap).items():
            if isinstance(value, str):
                assert "demoapikeyot2022" not in value, (
                    f"Demo key leaked back into module attribute {name!r}"
                )


class TestVsiPath:
    """The COG source -> GDAL virtual-filesystem mapping (GOL-199)."""

    def test_s3_to_vsis3(self):
        assert vsi_path("s3://srsp-open-data/phase5/dtm/NO51.tif") == "/vsis3/srsp-open-data/phase5/dtm/NO51.tif"

    def test_https_to_vsicurl(self):
        assert vsi_path("https://example.com/dtm.tif") == "/vsicurl/https://example.com/dtm.tif"

    def test_local_path_unchanged(self):
        assert vsi_path("/data/dtm.tif") == "/data/dtm.tif"
        assert vsi_path("dtm.tif") == "dtm.tif"

    def test_already_vsi_unchanged(self):
        assert vsi_path("/vsicurl/https://x/dtm.tif") == "/vsicurl/https://x/dtm.tif"
        assert vsi_path("/vsis3/bucket/key.tif") == "/vsis3/bucket/key.tif"


class TestFetchDemCog:
    """The cog backend reads a non-WGS84 DTM and reprojects+windows to the bbox (GOL-199).

    Hermetic: synthesizes an EPSG:27700 (British National Grid, the Scottish DTM CRS) GeoTIFF on
    disk and reads it back through fetch_dem_cog — exercises the WarpedVRT reproject + windowed read
    with no network. Skips cleanly if rasterio isn't importable in the test env.
    """

    def test_reprojects_bng_and_clips_to_bbox(self, tmp_path):
        rasterio = pytest.importorskip("rasterio")
        import numpy as np
        from rasterio.transform import from_origin
        from rasterio.warp import transform_bounds

        # Synthetic 100x100 @ 1 m DTM in British National Grid, a south->north elevation ramp 0..10 m.
        res = 1.0
        east0, north0 = 320000.0, 700000.0   # arbitrary point well inside the BNG domain
        width = height = 100
        # from_origin takes the TOP-left corner; north edge = north0 + height*res.
        transform = from_origin(east0, north0 + height * res, res, res)
        ramp = np.linspace(0.0, 10.0, height, dtype="float32").reshape(-1, 1)
        elev = np.tile(ramp, (1, width))
        src_tif = tmp_path / "src_bng.tif"
        with rasterio.open(
            src_tif, "w", driver="GTiff", height=height, width=width, count=1,
            dtype="float32", crs="EPSG:27700", transform=transform,
        ) as dst:
            dst.write(elev, 1)

        # Full tile extent in WGS84, then an inner 50% sub-bbox to prove clipping.
        w, s, e, n = transform_bounds(
            "EPSG:27700", "EPSG:4326",
            east0, north0, east0 + width * res, north0 + height * res,
        )
        dlon, dlat = (e - w) * 0.25, (n - s) * 0.25
        bbox = (w + dlon, s + dlat, e - dlon, n - dlat)

        out_tif = tmp_path / "dem.tif"
        fetch_dem_cog(bbox, str(src_tif), out_tif)

        assert out_tif.exists()
        with rasterio.open(out_tif) as r:
            assert r.crs.to_epsg() == 4326            # reprojected out of BNG
            assert r.width > 0 and r.height > 0
            # Output covers (roughly) the requested inner bbox.
            assert r.bounds.left <= bbox[0] + 1e-4
            assert r.bounds.right >= bbox[2] - 1e-4
            arr = r.read(1, masked=True)
            # Values stay within the source ramp (no garbage from the warp/fill).
            assert float(arr.min()) >= -0.5
            assert float(arr.max()) <= 10.5

    def test_nonoverlapping_bbox_exits(self, tmp_path):
        rasterio = pytest.importorskip("rasterio")
        import numpy as np
        from rasterio.transform import from_origin

        transform = from_origin(320000.0, 700100.0, 1.0, 1.0)
        src_tif = tmp_path / "src_bng.tif"
        with rasterio.open(
            src_tif, "w", driver="GTiff", height=100, width=100, count=1,
            dtype="float32", crs="EPSG:27700", transform=transform,
        ) as dst:
            dst.write(np.zeros((100, 100), dtype="float32"), 1)

        # A bbox in the Pacific — nowhere near the Scottish tile.
        with pytest.raises(SystemExit):
            fetch_dem_cog((-150.0, 10.0, -149.9, 10.1), str(src_tif), tmp_path / "dem.tif")
