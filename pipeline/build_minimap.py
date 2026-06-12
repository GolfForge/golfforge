#!/usr/bin/env python3
"""
build_minimap.py — composite courses/<id>/ rasters into a HUD-ready minimap.png (GOL-209).

The in-round HUD's hole-map card needs a real top-down image of the course. This
step composites one from data earlier stages already produced — no network, no
re-fetch — so it can run standalone on any course folder with full outputs:

Inputs (from courses/<id>/, produced by build_heightmap.py / build_splatmap.py):
  splatmap.json     — size_px + bbox_wgs84 (the canonical raster georeference)
  heightmap.json    — elevation encoding (elev_range_m) for the hillshade
  heightmap.png     — 16-bit grayscale terrain, hillshade source
  splat_*.png       — mutually-exclusive layer masks (fairway/green/bunker/rough)
  layer_*.png       — extra masks (tee/cart_path/trees)
  water.geojson     — water polygons (never rasterized by build_splatmap because the
                      engine builds 3D water actors instead; rasterized here only
                      for the minimap)

Output:
  minimap.png — RGB 8-bit, same size_px/bbox as the splatmap, so it shares the
                splatmap georeference exactly: the engine maps world XY in
                [-100800, +100800] cm linearly onto pixel [0, N-1] (the same
                affine as FCourseSurfaceSampler::ClassifyAt). Palette colors per
                surface, multiplied by a lambertian hillshade so dunes/slopes read.

Usage:
  python build_minimap.py --course-id oldandre
"""

import argparse
import json
import math
import sys
from pathlib import Path
from typing import Dict, Optional, Tuple

import numpy as np
from PIL import Image, ImageDraw

# build_minimap.py sits in pipeline/ next to build_splatmap.py, so a plain import
# works when run from anywhere; make it robust to cwd regardless.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from build_splatmap import lonlat_to_pixel, meters_per_pixel  # noqa: E402


# Surface palette (sRGB). Muted natural tones tuned to read on the dark glass HUD
# card without fighting the accent green of the UI theme.
PALETTE: Dict[str, Tuple[int, int, int]] = {
    "rough":     (58, 82, 54),     # dark course green — also the base fill
    "trees":     (42, 62, 42),
    "cart_path": (156, 146, 128),
    "fairway":   (96, 142, 76),
    "tee":       (108, 158, 88),
    "green":     (128, 188, 108),  # brightest grass so targets pop at minimap scale
    "bunker":    (214, 197, 153),
    "water":     (62, 106, 136),
}

# Paint order, low to high priority: later entries paint over earlier ones. This is
# the reverse of build_splatmap.LAYER_PRIORITY (bunker > green > tee > fairway >
# cart_path > trees > rough) with water on top — water polygons aren't part of the
# mask resolve, and visually water always wins.
PAINT_ORDER = ["rough", "trees", "cart_path", "fairway", "tee", "green", "bunker", "water"]

# Mask filename per layer (None = not mask-backed; water comes from water.geojson).
MASK_FILES = {
    "fairway":   "splat_fairway.png",
    "green":     "splat_green.png",
    "bunker":    "splat_bunker.png",
    "rough":     "splat_rough.png",
    "tee":       "layer_tee.png",
    "cart_path": "layer_cart_path.png",
    "trees":     "layer_trees.png",
}

HILLSHADE_AZIMUTH_DEG = 315.0   # light from the northwest (cartographic standard)
HILLSHADE_ALTITUDE_DEG = 45.0
HILLSHADE_BLEND = (0.65, 1.0)   # hillshade [0,1] maps to this multiply range —
                                # keeps even fully-shadowed slopes readable


def load_mask(path: Path, size: int) -> Optional[np.ndarray]:
    """Load an 8-bit layer mask, or None if the file is absent. Errors on size
    mismatch — masks are written by build_splatmap at splatmap.json:size_px, so a
    mismatch means the course folder is inconsistent, not something to paper over."""
    if not path.exists():
        return None
    img = Image.open(path).convert("L")
    if img.size != (size, size):
        raise SystemExit(f"error: {path.name} is {img.size}, expected {size}x{size} "
                         f"(splatmap.json:size_px) — regenerate the course rasters")
    return np.array(img, dtype=np.uint8)


def rasterize_water(water_geojson: Path,
                    bbox: Tuple[float, float, float, float],
                    size: int) -> Optional[np.ndarray]:
    """Flood-fill water.geojson polygon outer rings into an 8-bit mask (build_splatmap
    skips water rasterization on purpose — the engine builds 3D water actors — so the
    minimap rasterizes it locally)."""
    if not water_geojson.exists():
        return None
    fc = json.loads(water_geojson.read_text())
    mask = Image.new("L", (size, size), 0)
    draw = ImageDraw.Draw(mask)
    count = 0
    for feature in fc.get("features", []):
        geom = feature.get("geometry") or {}
        if geom.get("type") != "Polygon":
            continue
        rings = geom.get("coordinates") or []
        if not rings:
            continue
        ring = [lonlat_to_pixel(lon, lat, bbox, size) for lon, lat in rings[0]]
        if len(ring) >= 3:
            draw.polygon(ring, fill=255)
            count += 1
    return np.array(mask, dtype=np.uint8) if count else None


def compute_hillshade(elev16: np.ndarray,
                      elev_range_m: float,
                      bbox: Tuple[float, float, float, float]) -> np.ndarray:
    """Lambertian hillshade in [0,1] from a 16-bit elevation raster (row 0 = north,
    matching lonlat_to_pixel's Y flip). Standard formula, azimuth/altitude from the
    module constants."""
    size = elev16.shape[0]
    elev_m = elev16.astype(np.float64) * (elev_range_m / 65535.0)
    mpp = meters_per_pixel(bbox, size)
    dzdy, dzdx = np.gradient(elev_m, mpp)

    slope = np.arctan(np.hypot(dzdx, dzdy))
    aspect = np.arctan2(dzdy, -dzdx)
    zenith = math.radians(90.0 - HILLSHADE_ALTITUDE_DEG)
    azimuth = math.radians((360.0 - HILLSHADE_AZIMUTH_DEG + 90.0) % 360.0)

    hs = (math.cos(zenith) * np.cos(slope)
          + math.sin(zenith) * np.sin(slope) * np.cos(azimuth - aspect))
    return np.clip(hs, 0.0, 1.0)


def composite_minimap(masks: Dict[str, Optional[np.ndarray]],
                      shade: Optional[np.ndarray],
                      size: int) -> np.ndarray:
    """Paint palette colors per PAINT_ORDER onto a rough-colored base, then multiply
    by the hillshade (mapped into HILLSHADE_BLEND). Returns an (H, W, 3) uint8 array."""
    img = np.empty((size, size, 3), dtype=np.float64)
    img[:] = PALETTE["rough"]
    for name in PAINT_ORDER:
        if name == "rough":
            continue   # rough is the base fill
        mask = masks.get(name)
        if mask is None:
            continue
        img[mask > 128] = PALETTE[name]

    if shade is not None:
        lo, hi = HILLSHADE_BLEND
        img *= (lo + shade * (hi - lo))[..., None]

    return np.clip(np.round(img), 0, 255).astype(np.uint8)


def build_minimap(course_dir: Path) -> Path:
    """Composite courses/<id>/minimap.png from the course folder's existing outputs."""
    splat_meta = json.loads((course_dir / "splatmap.json").read_text())
    size = int(splat_meta["size_px"])
    bbox = tuple(splat_meta["bbox_wgs84"])

    masks: Dict[str, Optional[np.ndarray]] = {
        name: load_mask(course_dir / fname, size) for name, fname in MASK_FILES.items()
    }
    masks["water"] = rasterize_water(course_dir / "water.geojson", bbox, size)

    # Hillshade is best-effort: a course folder without heightmap outputs still gets
    # a flat-lit minimap rather than no minimap.
    shade = None
    height_meta_path = course_dir / "heightmap.json"
    height_png_path = course_dir / "heightmap.png"
    if height_meta_path.exists() and height_png_path.exists():
        height_meta = json.loads(height_meta_path.read_text())
        elev16 = np.array(Image.open(height_png_path), dtype=np.uint16)
        shade = compute_hillshade(elev16, float(height_meta["elev_range_m"]), bbox)
        if shade.shape[0] != size:
            # Contract says the rasters align, but resample defensively rather than die.
            shade = np.array(
                Image.fromarray((shade * 255).astype(np.uint8)).resize((size, size), Image.BILINEAR),
                dtype=np.float64) / 255.0

    img = composite_minimap(masks, shade, size)
    out_path = course_dir / "minimap.png"
    Image.fromarray(img, "RGB").save(out_path)
    return out_path


def main() -> None:
    p = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--course-id", required=True, help="Course identifier (folder under --courses-dir)")
    p.add_argument("--courses-dir", default=str(Path(__file__).resolve().parent.parent / "courses"),
                   help="Directory holding courses/<id>/ (default: ../courses)")
    args = p.parse_args()

    course_dir = Path(args.courses_dir) / args.course_id
    if not (course_dir / "splatmap.json").exists():
        raise SystemExit(f"error: {course_dir} has no splatmap.json — run build_splatmap.py first")

    out_path = build_minimap(course_dir)
    out_px = Image.open(out_path).size
    print(f"minimap: {out_path} ({out_px[0]}x{out_px[1]} RGB)")


if __name__ == "__main__":
    main()
