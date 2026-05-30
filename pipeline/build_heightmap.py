#!/usr/bin/env python3
"""
build_heightmap.py — bbox + LIDAR source → 16-bit PNG heightmap for UE5 Landscape.

Two backends:
  --backend opentopo  (default) — pulls a pre-processed DEM from OpenTopography REST API.
                                  Easy, lower-resolution, good for fast iteration.
  --backend pdal                — pulls raw 3DEP point cloud from AWS Entwine,
                                  ground-classifies, rasterizes via PDAL.
                                  Higher quality. Requires PDAL installed.

Output:
  <out_dir>/heightmap.png  — 16-bit grayscale PNG sized for UE5 Landscape (default 4033x4033)
  <out_dir>/heightmap.json — metadata (elevation range, suggested UE5 landscape scale)

Usage:
  # Get a free OpenTopography API key first:
  #   https://portal.opentopography.org/myopentopo (sign up, request a key)
  export OPENTOPO_API_KEY=<your-key>

  python build_heightmap.py \\
      --bbox-wgs84 -121.951,36.566,-121.937,36.580 \\
      --course-id pebble-beach \\
      --size 4033 \\
      --backend opentopo

UE5 import notes:
  - The PNG is grayscale 16-bit. UE5 Landscape import takes this directly.
  - The accompanying .json file tells you the actual elevation range so you can
    set the Landscape's Z scale correctly. Formula: Z_scale_percent = (elev_range_m / 512) * 100.
  - For a 50m elevation range, Z scale = ~9.77.
"""

import argparse
import json
import os
import subprocess
import sys
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Tuple

import numpy as np
import requests
from PIL import Image


# UE5 Landscape recommended dimensions. Heightmap should match one of these.
# (2^N) + 1 — landscape sections snap cleanly to these sizes.
UE5_LANDSCAPE_SIZES = [505, 1009, 2017, 4033, 8129]

# Environment variable name that holds the contributor's OpenTopography API key.
# We deliberately do NOT ship a default key — sign up for a free one at
# https://portal.opentopography.org/myopentopo and either export this env var
# or pass --opentopo-key.
OPENTOPO_ENV_VAR = "OPENTOPO_API_KEY"

OPENTOPO_KEY_HELP = (
    "No OpenTopography API key found.\n"
    "  Get a free key at https://portal.opentopography.org/myopentopo (sign up,\n"
    "  then 'Request an API Key'), and either:\n"
    f"    export {OPENTOPO_ENV_VAR}=<your-key>\n"
    "  or pass --opentopo-key <your-key> on the command line.\n"
    "  (The pipeline used to ship a public demo key; it was removed when the\n"
    "  repo went public so contributors don't share the demo's rate limit.)"
)


@dataclass
class HeightmapMeta:
    course_id: str
    bbox_wgs84: Tuple[float, float, float, float]  # (minlon, minlat, maxlon, maxlat)
    size_px: int
    elev_min_m: float
    elev_max_m: float
    elev_range_m: float
    ue5_z_scale_pct: float
    backend: str


# -------------------- key resolution --------------------


def resolve_opentopo_key(cli_value: str | None, env: dict | None = None) -> str:
    """
    Resolve the OpenTopography API key, in priority order:
    1. --opentopo-key flag (cli_value, if non-empty)
    2. OPENTOPO_API_KEY environment variable
    3. raise SystemExit with the help text.

    `env` is injectable for testing; defaults to os.environ.
    """
    if cli_value:
        return cli_value
    env = env if env is not None else os.environ
    env_value = env.get(OPENTOPO_ENV_VAR, "").strip()
    if env_value:
        return env_value
    print(OPENTOPO_KEY_HELP, file=sys.stderr)
    raise SystemExit(2)


# -------------------- backends --------------------


def fetch_dem_opentopo(bbox: Tuple[float, float, float, float], out_tif: Path, dataset: str, api_key: str) -> None:
    """
    Fetch a pre-processed DEM GeoTIFF from OpenTopography REST API.

    dataset: USGS10m, USGS30m, SRTMGL1 (worldwide 30m), SRTMGL3 (worldwide 90m), etc.
    """
    minlon, minlat, maxlon, maxlat = bbox
    params = {
        "datasetName": dataset,
        "south": minlat,
        "north": maxlat,
        "west": minlon,
        "east": maxlon,
        "outputFormat": "GTiff",
        "API_Key": api_key,
    }
    url = "https://portal.opentopography.org/API/usgsdem"
    print(f"[opentopo] requesting {dataset} for {bbox}")
    r = requests.get(url, params=params, stream=True, timeout=120)
    r.raise_for_status()
    out_tif.parent.mkdir(parents=True, exist_ok=True)
    with out_tif.open("wb") as f:
        for chunk in r.iter_content(chunk_size=1 << 16):
            f.write(chunk)
    print(f"[opentopo] wrote {out_tif} ({out_tif.stat().st_size} bytes)")


def fetch_dem_pdal(bbox: Tuple[float, float, float, float], ept_url: str, out_tif: Path, resolution_m: float = 0.5) -> None:
    """
    Pull 3DEP point cloud from AWS Entwine, ground-classify, rasterize to DEM via PDAL.

    Requires `pdal` CLI in PATH. ept_url is the EPT (Entwine Point Tile) index URL
    for the USGS 3DEP project covering your bbox — look it up via
    https://usgs.entwine.io/  or  https://portal.opentopography.org/
    """
    minlon, minlat, maxlon, maxlat = bbox
    # NOTE: most EPT indexes use a projected CRS, so we'd transform bbox here.
    # For now we pass WGS84 and assume the reader handles reprojection (PDAL's
    # readers.ept supports `bounds` in the source CRS — verify your EPT's projection).

    pipeline = {
        "pipeline": [
            {
                "type": "readers.ept",
                "filename": ept_url,
                "bounds": f"([{minlon}, {maxlon}], [{minlat}, {maxlat}])",
            },
            {
                "type": "filters.range",
                "limits": "Classification[2:2]",  # ground returns only
            },
            {
                "type": "filters.outlier",
                "method": "statistical",
                "mean_k": 8,
                "multiplier": 2.5,
            },
            {
                "type": "writers.gdal",
                "filename": str(out_tif),
                "resolution": resolution_m,
                "output_type": "idw",
                "gdaldriver": "GTiff",
                "nodata": -9999,
            },
        ]
    }

    out_tif.parent.mkdir(parents=True, exist_ok=True)
    print(f"[pdal] running pipeline against {ept_url}")
    proc = subprocess.run(
        ["pdal", "pipeline", "--stdin"],
        input=json.dumps(pipeline),
        text=True,
        capture_output=True,
    )
    if proc.returncode != 0:
        print(f"[pdal] FAILED: {proc.stderr}", file=sys.stderr)
        sys.exit(1)
    print(f"[pdal] wrote {out_tif}")


# -------------------- DEM → 16-bit PNG --------------------


def dem_to_heightmap(tif_path: Path, out_png: Path, size: int) -> HeightmapMeta:
    """
    Convert a single-band DEM GeoTIFF to a square 16-bit PNG heightmap.

    Elevation min/max in the DEM map to 0/65535 in the PNG.
    UE5 imports 16-bit grayscale PNGs natively for Landscape.
    """
    import rasterio
    from rasterio.warp import Resampling, calculate_default_transform, reproject

    with rasterio.open(tif_path) as src:
        # Read first band, mask nodata
        band = src.read(1, masked=True).astype(np.float32)
        # Replace masked values with min of valid pixels (so the heightmap floor is the actual min)
        if np.ma.is_masked(band):
            valid_min = float(band.min())
            band = band.filled(valid_min)
        elev = band

    valid_min = float(np.min(elev))
    valid_max = float(np.max(elev))
    elev_range = max(valid_max - valid_min, 1e-6)

    # Resample to square `size` × `size` (UE5 likes power-of-two-plus-one dimensions)
    pil = Image.fromarray(elev, mode="F")
    pil = pil.resize((size, size), Image.BILINEAR)
    elev_resampled = np.asarray(pil, dtype=np.float32)

    # Normalize to 16-bit range
    norm = (elev_resampled - valid_min) / elev_range
    norm = np.clip(norm, 0.0, 1.0)
    heightmap = (norm * 65535).astype(np.uint16)

    out_png.parent.mkdir(parents=True, exist_ok=True)
    # Pillow 13 deprecates the `mode` arg here; frombuffer is the supported route
    # to a 16-bit unsigned greyscale PNG.
    Image.frombuffer("I;16", (size, size), heightmap.tobytes(),
                     "raw", "I;16", 0, 1).save(out_png)

    # UE5 Z scale derivation:
    # At Z scale = 100, the heightmap spans 512m vertically (UE5 default).
    # To map our actual elev_range to the full 0..65535 range we use:
    #   Z_scale_percent = (elev_range_m / 512) * 100
    ue5_z_scale = (elev_range / 512.0) * 100.0

    return HeightmapMeta(
        course_id="",  # filled by caller
        bbox_wgs84=(0, 0, 0, 0),  # filled by caller
        size_px=size,
        elev_min_m=valid_min,
        elev_max_m=valid_max,
        elev_range_m=elev_range,
        ue5_z_scale_pct=ue5_z_scale,
        backend="",  # filled by caller
    )


# -------------------- CLI --------------------


def parse_bbox(s: str) -> Tuple[float, float, float, float]:
    parts = [float(x) for x in s.split(",")]
    if len(parts) != 4:
        raise argparse.ArgumentTypeError("bbox must be 'minlon,minlat,maxlon,maxlat'")
    return tuple(parts)  # type: ignore


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--bbox-wgs84", required=True, type=parse_bbox,
                   help="Bounding box as 'minlon,minlat,maxlon,maxlat' (WGS84 / EPSG:4326)")
    p.add_argument("--course-id", required=True, help="Course identifier (used for output paths)")
    p.add_argument("--out-dir", default="out", help="Output directory (default: ./out)")
    p.add_argument("--size", type=int, default=4033, choices=UE5_LANDSCAPE_SIZES,
                   help="Heightmap output size in pixels (must be UE5-friendly)")
    p.add_argument("--backend", choices=["opentopo", "pdal"], default="opentopo")
    p.add_argument("--opentopo-dataset", default="USGS10m",
                   help="OpenTopography dataset name (opentopo backend only)")
    p.add_argument("--opentopo-key", default=None,
                   help=f"OpenTopography API key. Falls back to ${OPENTOPO_ENV_VAR}. "
                        "Get a free key at https://portal.opentopography.org/myopentopo.")
    p.add_argument("--pdal-ept-url", default=None,
                   help="USGS 3DEP EPT (Entwine) URL covering the bbox (pdal backend only)")
    p.add_argument("--pdal-resolution", type=float, default=0.5,
                   help="DEM grid resolution in meters (pdal backend only)")
    args = p.parse_args()

    out_dir = Path(args.out_dir) / args.course_id
    out_dir.mkdir(parents=True, exist_ok=True)

    intermediate_tif = out_dir / "dem.tif"
    out_png = out_dir / "heightmap.png"
    out_meta = out_dir / "heightmap.json"

    if args.backend == "opentopo":
        api_key = resolve_opentopo_key(args.opentopo_key)
        fetch_dem_opentopo(args.bbox_wgs84, intermediate_tif,
                           dataset=args.opentopo_dataset, api_key=api_key)
    else:
        if not args.pdal_ept_url:
            print("--pdal-ept-url required for pdal backend", file=sys.stderr)
            print("Find the EPT URL covering your bbox at https://usgs.entwine.io/", file=sys.stderr)
            sys.exit(2)
        fetch_dem_pdal(args.bbox_wgs84, args.pdal_ept_url, intermediate_tif, args.pdal_resolution)

    meta = dem_to_heightmap(intermediate_tif, out_png, args.size)
    meta.course_id = args.course_id
    meta.bbox_wgs84 = args.bbox_wgs84
    meta.backend = args.backend

    out_meta.write_text(json.dumps(asdict(meta), indent=2))

    print()
    print(f"  heightmap: {out_png}")
    print(f"  metadata:  {out_meta}")
    print(f"  elevation: {meta.elev_min_m:.2f}m – {meta.elev_max_m:.2f}m  (range {meta.elev_range_m:.2f}m)")
    print(f"  UE5 Landscape Z scale: {meta.ue5_z_scale_pct:.4f}%")
    print()
    print("Next: build_splatmap.py for the same bbox, then import both into UE5 Landscape.")


if __name__ == "__main__":
    main()
