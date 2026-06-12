"""Tests for pipeline/build_minimap.py (GOL-209).

The minimap is the HUD's basemap: a colorized composite of the course's existing
layer masks + a hillshade, sharing the splatmap georeference exactly. These tests
freeze that contract with a synthetic course folder (no network, no real course):

- compositing: palette colors land where the masks say, priority order holds
  (water > bunker > green > fairway > rough base), water comes from water.geojson
- hillshade: terrain relief modulates brightness; flat terrain doesn't
- guardrails: mask size mismatch dies loudly instead of silently misregistering

Run from `pipeline/` with:  python3 -m pytest tests/
"""

import json
import sys
from pathlib import Path

import numpy as np
import pytest
from PIL import Image

# Add pipeline/ to sys.path so we can import build_minimap as a module.
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import build_minimap as bm  # noqa: E402

SIZE = 64
BBOX = (-1.0, 40.0, -0.9, 40.1)


def _write_mask(path: Path, box: tuple) -> None:
    """Write an 8-bit mask with a filled rectangle (x0, y0, x1, y1)."""
    arr = np.zeros((SIZE, SIZE), dtype=np.uint8)
    x0, y0, x1, y1 = box
    arr[y0:y1, x0:x1] = 255
    Image.fromarray(arr, "L").save(path)


def _make_course(tmp_path: Path, with_heightmap: bool = False,
                 heightmap16: np.ndarray = None) -> Path:
    """Synthetic course folder: splatmap meta + a few disjoint masks + water polygon.

    Layout (pixel space): water x[0,8), fairway x[12,32), green x[36,48),
    bunker x[50,60) — all full-height stripes; everything else is rough base.
    """
    course = tmp_path / "test-course"
    course.mkdir()
    (course / "splatmap.json").write_text(json.dumps({"size_px": SIZE, "bbox_wgs84": list(BBOX)}))

    _write_mask(course / "splat_fairway.png", (12, 0, 32, SIZE))
    _write_mask(course / "splat_green.png", (36, 0, 48, SIZE))
    _write_mask(course / "splat_bunker.png", (50, 0, 60, SIZE))

    # Water polygon over pixel x[0,8): lon span maps linearly onto x[0, SIZE-1].
    minlon, minlat, maxlon, maxlat = BBOX
    lon_at = lambda px: minlon + (px / (SIZE - 1)) * (maxlon - minlon)
    ring = [[lon_at(0), minlat], [lon_at(8), minlat], [lon_at(8), maxlat],
            [lon_at(0), maxlat], [lon_at(0), minlat]]
    (course / "water.geojson").write_text(json.dumps({
        "type": "FeatureCollection",
        "features": [{"type": "Feature", "properties": {},
                      "geometry": {"type": "Polygon", "coordinates": [ring]}}],
    }))

    if with_heightmap:
        # The synthetic bbox is ~10 km wide (~150 m/px), so the elevation range must
        # be mountainous for the ramp to produce a visible hillshade slope.
        (course / "heightmap.json").write_text(json.dumps(
            {"elev_range_m": 3000.0, "bbox_wgs84": list(BBOX), "size_px": SIZE}))
        Image.fromarray(heightmap16).save(course / "heightmap.png")

    return course


# Only the layers the fixture actually paints — tee/trees/cart_path hues sit close
# to green/fairway by design, so classifying against the full palette would be flaky.
_FIXTURE_LAYERS = ("water", "fairway", "green", "bunker", "rough")


def _nearest_layer(pixel) -> str:
    """Classify a pixel by nearest fixture-layer color (robust to uniform shading)."""
    return min(_FIXTURE_LAYERS,
               key=lambda n: sum((int(c) - int(p)) ** 2 for c, p in zip(bm.PALETTE[n], pixel)))


def test_minimap_composites_palette_and_water(tmp_path):
    """No heightmap -> flat-lit, exact palette colors in each stripe; water rasterized
    from water.geojson; rough is the base everywhere else."""
    course = _make_course(tmp_path)
    out = bm.build_minimap(course)

    assert out.name == "minimap.png"
    img = Image.open(out)
    assert img.mode == "RGB"
    assert img.size == (SIZE, SIZE)

    arr = np.array(img)
    mid = SIZE // 2
    assert tuple(arr[mid, 4]) == bm.PALETTE["water"]
    assert tuple(arr[mid, 20]) == bm.PALETTE["fairway"]
    assert tuple(arr[mid, 40]) == bm.PALETTE["green"]
    assert tuple(arr[mid, 55]) == bm.PALETTE["bunker"]
    assert tuple(arr[mid, 62]) == bm.PALETTE["rough"]   # base fill


def test_minimap_hillshade_modulates_relief(tmp_path):
    """Half-flat / half-ramp heightmap: the sloped half must render at a different
    brightness than the flat half (hillshade applied), and classification survives
    the shading (nearest-palette still identifies each stripe)."""
    h = np.zeros((SIZE, SIZE), dtype=np.uint16)
    ramp = np.linspace(0, 65535, SIZE // 2, dtype=np.uint16)
    h[SIZE // 2:, :] = ramp[:, None]          # south half ramps up to the south
    course = _make_course(tmp_path, with_heightmap=True, heightmap16=h)
    out = bm.build_minimap(course)
    arr = np.array(Image.open(out)).astype(int)

    # Same surface (bunker stripe, x=55 — the brightest palette entry, so the
    # shading delta is largest), flat north vs sloped south: brightness differs.
    flat_px = arr[4, 55]
    slope_px = arr[SIZE - 8, 55]
    assert abs(int(flat_px.sum()) - int(slope_px.sum())) > 10

    # Shading must not break classification.
    mid = SIZE // 2 - 4   # in the flat half
    assert _nearest_layer(arr[mid, 20]) == "fairway"
    assert _nearest_layer(arr[mid, 40]) == "green"
    assert _nearest_layer(arr[mid, 55]) == "bunker"


def test_minimap_dies_on_mask_size_mismatch(tmp_path):
    """A mask at the wrong resolution means the course folder is inconsistent —
    refuse to composite rather than silently misregister the basemap."""
    course = _make_course(tmp_path)
    _write_mask(course / "splat_fairway.png", (0, 0, 10, 10))   # overwrite below
    bad = np.zeros((SIZE * 2, SIZE * 2), dtype=np.uint8)
    Image.fromarray(bad, "L").save(course / "splat_fairway.png")

    with pytest.raises(SystemExit):
        bm.build_minimap(course)


def test_minimap_tolerates_missing_masks_and_water(tmp_path):
    """A sparse course folder (only splatmap.json) still yields a rough-colored
    basemap — missing layers are skipped, not fatal."""
    course = tmp_path / "sparse-course"
    course.mkdir()
    (course / "splatmap.json").write_text(json.dumps({"size_px": SIZE, "bbox_wgs84": list(BBOX)}))

    out = bm.build_minimap(course)
    arr = np.array(Image.open(out))
    assert tuple(arr[SIZE // 2, SIZE // 2]) == bm.PALETTE["rough"]
