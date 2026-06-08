#!/usr/bin/env python3
"""
build_qa_overlay.py — QA overlay PNGs: derived golf features over a real basemap.

Renders two visual-check images per course into `out/<course-id>/`:

  qa_overlay_aerial.png  — features over Esri World Imagery (satellite/aerial)
  qa_overlay_osm.png     — features over the standard OpenStreetMap rendered map

Each draws the pipeline's derived layers (fairway / green / bunker / rough / tee /
water / cart_path, including any *synthesized* fairway corridors) as distinct
semi-transparent fills, plus per-hole centerlines + hole-number labels, on top of
a real basemap. The aerial image checks features against the actual ground; the
OSM image checks our lon/lat->pixel alignment against OSM's own rendering.

The basemap tiles are Web Mercator (EPSG:3857) but our masks live on a linear
lon/lat (equirectangular) grid (build_splatmap.lonlat_to_pixel), so the basemap is
reprojected per-pixel onto our grid — otherwise the overlay would mislead about
alignment.

Offline-friendly: with no network (or --skip-tiles, or too many tiles) it falls
back to a gray base and still emits the masks-only overlays. Tiles are cached under
`_tilecache/`. Respects provider tile policy: descriptive User-Agent, a tile-count
guard, and a short pause between network fetches — intended for occasional, modest
per-course use, not bulk scraping.

Usage:
  python build_qa_overlay.py --course-id golfforge-demo-black --out-dir out
  python build_qa_overlay.py --course-id golfforge-demo-black --skip-tiles
"""

import argparse
import json
import math
import time
from pathlib import Path
from typing import Dict, List, Optional, Tuple

import numpy as np
import requests
from PIL import Image, ImageDraw, ImageFont

import build_splatmap as bs


# Basemap tile providers. NOTE the differing axis order in the templates.
ESRI_AERIAL_TEMPLATE = (
    "https://server.arcgisonline.com/ArcGIS/rest/services/"
    "World_Imagery/MapServer/tile/{z}/{y}/{x}"
)
OSM_TEMPLATE = "https://tile.openstreetmap.org/{z}/{x}/{y}.png"

# The overlay is resampled onto our full-res grid regardless, so the basemap only
# needs enough resolution to read feature placement — cap the zoom low to keep the
# tile count polite (OSM policy discourages bulk; tiles are cached per course).
QA_MAX_CANVAS_PX = 1024  # cap the basemap zoom (≈ z17 for a ~1.6km course)
QA_MAX_TILES = 120       # refuse runaway fetches (policy + politeness) -> masks-only
TILE_PX = 256

# RGBA fills per layer, drawn bottom->top in DRAW_ORDER.
QA_LAYER_COLORS = {
    "rough":     (120, 160, 60, 70),
    "fairway":   (60, 200, 60, 110),
    "bunker":    (235, 220, 120, 140),
    "tee":       (200, 80, 200, 150),
    "green":     (0, 255, 120, 150),
    "water":     (60, 120, 235, 130),
    "cart_path": (210, 210, 210, 170),
}
DRAW_ORDER = ["rough", "fairway", "bunker", "tee", "green", "water", "cart_path"]
HOLE_LINE_COLOR = (255, 60, 60, 230)
HOLE_LABEL_COLOR = (255, 255, 255, 255)
GRAY_BASE = (40, 40, 40, 255)


# ---------- Web Mercator tile math ----------


def _lonlat_to_tilexy(lon: float, lat: float, z: int) -> Tuple[float, float]:
    """Lon/lat -> fractional XYZ tile coordinates at zoom z (Web Mercator)."""
    n = 2 ** z
    xt = (lon + 180.0) / 360.0 * n
    lat_rad = math.radians(lat)
    yt = (1.0 - math.asinh(math.tan(lat_rad)) / math.pi) / 2.0 * n
    return xt, yt


def _tilexy_to_lonlat(xt: float, yt: float, z: int) -> Tuple[float, float]:
    """Fractional XYZ tile coordinates -> lon/lat (inverse of _lonlat_to_tilexy)."""
    n = 2 ** z
    lon = xt / n * 360.0 - 180.0
    lat = math.degrees(math.atan(math.sinh(math.pi * (1.0 - 2.0 * yt / n))))
    return lon, lat


def _choose_zoom(bbox: Tuple[float, float, float, float], size: int,
                 max_dim: int = QA_MAX_CANVAS_PX) -> int:
    """Smallest zoom whose mercator pixel span covers min(size, max_dim)."""
    minlon, minlat, maxlon, maxlat = bbox
    target = min(size, max_dim)
    for z in range(0, 20):
        x0, _ = _lonlat_to_tilexy(minlon, maxlat, z)
        x1, _ = _lonlat_to_tilexy(maxlon, minlat, z)
        if (x1 - x0) * TILE_PX >= target:
            return z
    return 19


# ---------- tile fetch + stitch ----------


def _get_tile(tx: int, ty: int, z: int, url_template: str, provider: str,
              cache_dir: Path) -> Optional[Image.Image]:
    """Load one tile from disk cache or network. None on failure."""
    cache_path = cache_dir / f"{provider}_{z}_{tx}_{ty}.png"
    if cache_path.exists():
        try:
            return Image.open(cache_path).convert("RGB")
        except OSError:
            pass  # corrupt cache -> refetch
    url = url_template.format(z=z, x=tx, y=ty)
    try:
        r = requests.get(url, headers=bs.HTTP_HEADERS, timeout=30)
        r.raise_for_status()
        cache_path.write_bytes(r.content)
        time.sleep(0.1)  # politeness between network fetches
        return Image.open(cache_path).convert("RGB")
    except (requests.RequestException, OSError) as e:
        print(f"[qa] tile fetch failed ({provider} {z}/{tx}/{ty}): {e.__class__.__name__}")
        return None


def fetch_tiles(bbox: Tuple[float, float, float, float], z: int, url_template: str,
                cache_dir: Path, provider: str,
                skip_tiles: bool = False) -> Optional[Tuple[Image.Image, int, int]]:
    """Stitch the tile grid covering bbox at zoom z.

    Returns (stitched RGB image, gx0, gy0) where (gx0, gy0) is the global-pixel
    offset of the stitched image's top-left, or None if tiles are unavailable
    (skip_tiles, network down, or the tile-count guard trips).
    """
    if skip_tiles:
        return None
    minlon, minlat, maxlon, maxlat = bbox
    x0t, y0t = _lonlat_to_tilexy(minlon, maxlat, z)  # NW corner
    x1t, y1t = _lonlat_to_tilexy(maxlon, minlat, z)  # SE corner
    xmin, xmax = int(x0t), int(x1t)
    ymin, ymax = int(y0t), int(y1t)
    cols, rows = xmax - xmin + 1, ymax - ymin + 1
    if cols * rows > QA_MAX_TILES:
        print(f"[qa] {provider}: tile grid {cols}x{rows} exceeds QA_MAX_TILES="
              f"{QA_MAX_TILES}; using masks-only base.")
        return None

    cache_dir.mkdir(parents=True, exist_ok=True)
    canvas = Image.new("RGB", (cols * TILE_PX, rows * TILE_PX))
    for ty in range(ymin, ymax + 1):
        for tx in range(xmin, xmax + 1):
            tile = _get_tile(tx, ty, z, url_template, provider, cache_dir)
            if tile is None:
                return None  # bail to masks-only on any failure
            canvas.paste(tile, ((tx - xmin) * TILE_PX, (ty - ymin) * TILE_PX))
    return canvas, xmin * TILE_PX, ymin * TILE_PX


# ---------- reprojection ----------


def reproject_basemap_to_grid(stitched: Image.Image, gx0: int, gy0: int, z: int,
                              bbox: Tuple[float, float, float, float],
                              size: int) -> np.ndarray:
    """Resample a Web-Mercator stitched basemap onto our equirectangular grid.

    For every output pixel (on the same lon/lat grid build_splatmap.lonlat_to_pixel
    uses) we inverse-transform to lon/lat, forward-project to a mercator global
    pixel, and bilinear-sample the stitched image. Returns an (size, size, 4) RGBA
    uint8 array. Fully vectorized.
    """
    minlon, minlat, maxlon, maxlat = bbox
    cols = np.arange(size)
    rows = np.arange(size)
    J, I = np.meshgrid(cols, rows)  # J=col (x), I=row (y)

    # Inverse of lonlat_to_pixel (equirectangular):
    lon = minlon + (J / (size - 1)) * (maxlon - minlon)
    lat = minlat + (1.0 - I / (size - 1)) * (maxlat - minlat)

    # Forward Web Mercator -> global pixel.
    n = 2 ** z
    xt = (lon + 180.0) / 360.0 * n
    lat_rad = np.radians(lat)
    yt = (1.0 - np.arcsinh(np.tan(lat_rad)) / np.pi) / 2.0 * n
    gpx = xt * TILE_PX - gx0
    gpy = yt * TILE_PX - gy0

    src = np.asarray(stitched, dtype=np.float32)  # (H, W, 3)
    h, w = src.shape[:2]

    x0 = np.floor(gpx).astype(np.intp)
    y0 = np.floor(gpy).astype(np.intp)
    fx = (gpx - x0).astype(np.float32)
    fy = (gpy - y0).astype(np.float32)
    x0c = np.clip(x0, 0, w - 1)
    x1c = np.clip(x0 + 1, 0, w - 1)
    y0c = np.clip(y0, 0, h - 1)
    y1c = np.clip(y0 + 1, 0, h - 1)

    out = np.zeros((size, size, 4), dtype=np.uint8)
    for c in range(3):
        ch = src[:, :, c]
        top = ch[y0c, x0c] * (1 - fx) + ch[y0c, x1c] * fx
        bot = ch[y1c, x0c] * (1 - fx) + ch[y1c, x1c] * fx
        out[:, :, c] = np.clip(top * (1 - fy) + bot * fy, 0, 255).astype(np.uint8)
    out[:, :, 3] = 255
    return out


# ---------- inputs ----------


def load_masks(course_dir: Path, size: int) -> Dict[str, np.ndarray]:
    """Load the derived layer masks present in the course dir, keyed by layer name.

    Reads splat_<core>.png (fairway/green/bunker/rough) and layer_<extra>.png
    (tee/cart_path); rasterizes water from water.geojson (it ships geojson-only).
    """
    masks: Dict[str, np.ndarray] = {}
    for name in ("fairway", "green", "bunker", "rough"):
        p = course_dir / f"splat_{name}.png"
        if p.exists():
            masks[name] = np.array(Image.open(p).convert("L"))
    for name in ("tee", "cart_path"):
        p = course_dir / f"layer_{name}.png"
        if p.exists():
            masks[name] = np.array(Image.open(p).convert("L"))
    return masks


def _rasterize_geojson_polygons(geojson_path: Path,
                                bbox: Tuple[float, float, float, float],
                                size: int) -> Optional[np.ndarray]:
    """Fill a polygon-geojson sidecar into an L mask (for water, which is geojson-only)."""
    if not geojson_path.exists():
        return None
    fc = json.loads(geojson_path.read_text())
    img = Image.new("L", (size, size), 0)
    draw = ImageDraw.Draw(img)
    any_drawn = False
    for feat in fc.get("features", []):
        geom = feat.get("geometry", {})
        if geom.get("type") != "Polygon":
            continue
        for ring in geom.get("coordinates", []):
            pts = [bs.lonlat_to_pixel(lon, lat, bbox, size) for lon, lat in ring]
            if len(pts) >= 3:
                draw.polygon(pts, fill=255)
                any_drawn = True
    return np.array(img) if any_drawn else None


def load_hole_features(course_dir: Path) -> List[dict]:
    """Return [{coords:[(lon,lat)...], ref:str}] from hole.geojson, if present."""
    p = course_dir / "hole.geojson"
    if not p.exists():
        return []
    fc = json.loads(p.read_text())
    holes = []
    for feat in fc.get("features", []):
        geom = feat.get("geometry", {})
        if geom.get("type") != "LineString":
            continue
        coords = [(c[0], c[1]) for c in geom.get("coordinates", [])]
        ref = feat.get("properties", {}).get("osm_tags", {}).get("ref")
        holes.append({"coords": coords, "ref": ref})
    return holes


# ---------- compositing ----------


def build_overlay_image(base_rgba: np.ndarray, masks: Dict[str, np.ndarray],
                        hole_features: List[dict],
                        bbox: Tuple[float, float, float, float],
                        size: int) -> Image.Image:
    """Composite the colored masks + hole centerlines/labels over the base."""
    canvas = Image.fromarray(base_rgba, "RGBA")
    for name in DRAW_ORDER:
        mask = masks.get(name)
        if mask is None:
            continue
        layer = np.zeros((size, size, 4), dtype=np.uint8)
        layer[mask > 0] = QA_LAYER_COLORS[name]
        canvas = Image.alpha_composite(canvas, Image.fromarray(layer, "RGBA"))

    draw = ImageDraw.Draw(canvas, "RGBA")
    line_w = max(2, size // 700)
    font_size = max(10, size // 60)
    try:
        font = ImageFont.load_default(size=font_size)
    except TypeError:  # very old Pillow without size arg
        font = ImageFont.load_default()
    for hole in hole_features:
        pts = [bs.lonlat_to_pixel(lon, lat, bbox, size) for lon, lat in hole["coords"]]
        if len(pts) >= 2:
            draw.line(pts, fill=HOLE_LINE_COLOR, width=line_w, joint="curve")
        ref = hole.get("ref")
        if ref and pts:
            lx, ly = pts[len(pts) // 2]
            draw.text((lx, ly), str(ref), fill=HOLE_LABEL_COLOR, font=font)
    return canvas.convert("RGB")


def build_overlays(course_dir: Path, skip_tiles: bool = False) -> Dict[str, str]:
    """Build qa_overlay_aerial.png + qa_overlay_osm.png for a built course dir."""
    meta = json.loads((course_dir / "splatmap.json").read_text())
    bbox = tuple(meta["bbox_wgs84"])
    size = int(meta["size_px"])

    masks = load_masks(course_dir, size)
    water = _rasterize_geojson_polygons(course_dir / "water.geojson", bbox, size)
    if water is not None:
        masks["water"] = water
    holes = load_hole_features(course_dir)

    written: Dict[str, str] = {}
    for provider, template, fname in (
        ("aerial", ESRI_AERIAL_TEMPLATE, "qa_overlay_aerial.png"),
        ("osm", OSM_TEMPLATE, "qa_overlay_osm.png"),
    ):
        z = _choose_zoom(bbox, size)
        tiles = fetch_tiles(bbox, z, template, course_dir / "_tilecache",
                            provider, skip_tiles=skip_tiles)
        if tiles is not None:
            base = reproject_basemap_to_grid(*tiles, z, bbox, size)
        else:
            base = np.full((size, size, 4), GRAY_BASE, dtype=np.uint8)
        img = build_overlay_image(base, masks, holes, bbox, size)
        out_path = course_dir / fname
        img.save(out_path)
        print(f"[qa] wrote {out_path} ({'basemap' if tiles else 'masks-only'})")
        written[provider] = str(out_path)
    return written


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--course-id", required=True)
    p.add_argument("--out-dir", default="out")
    p.add_argument("--skip-tiles", action="store_true",
                   help="Skip basemap tile fetching; emit masks-only overlays.")
    args = p.parse_args()

    course_dir = Path(args.out_dir) / args.course_id
    if not (course_dir / "splatmap.json").exists():
        raise SystemExit(f"[qa] no splatmap.json in {course_dir}; run build_splatmap.py first")
    build_overlays(course_dir, skip_tiles=args.skip_tiles)


if __name__ == "__main__":
    main()
