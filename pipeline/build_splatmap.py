#!/usr/bin/env python3
"""
build_splatmap.py — bbox + OpenStreetMap golf features → 4-channel PNG splatmap.

For a given geographic bbox, queries OSM via the Overpass API for golf-related
polygons (fairway, green, bunker, rough, tee, water hazard, cart path), rasterizes
each class into its own channel, and outputs a 4-channel (RGBA) PNG that aligns
with the heightmap.

Output channel mapping (configurable):
  R = fairway
  G = green
  B = bunker
  A = rough (catch-all — default for any pixel inside the course polygon)

Tee boxes, water, cart paths are emitted as separate single-channel PNGs because
UE5 Landscape weightmaps support only 4 layers per material slot. You can wire
the secondary ones into a second material layer in UE5.

Usage:
  python build_splatmap.py \\
      --bbox-wgs84 -121.951,36.566,-121.937,36.580 \\
      --course-id pebble-beach \\
      --size 4033

Notes:
  - OSM coverage for golf courses is uneven. Some courses are fully tagged
    (Augusta National, St Andrews, Pebble Beach), others have just the outer
    polygon. Inspect what you got and hand-fix in QGIS or by editing GeoJSON
    if needed.
  - This script is intentionally pure-python with no GDAL CLI dependency.
"""

import argparse
import json
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Tuple

import numpy as np
import requests
from PIL import Image, ImageDraw


OVERPASS_URL = "https://overpass-api.de/api/interpreter"

# Overpass usage policy requires a descriptive User-Agent on every request.
HTTP_HEADERS = {
    "User-Agent": "golfsim/0.1 (https://github.com/-/golfsim) python-requests",
}

# OSM tag → splatmap channel index (R=0, G=1, B=2, A=3) or "extra" for separate PNG
FEATURE_LAYERS = {
    "fairway": {"channel": 0, "osm_tag": ["golf=fairway"]},
    "green":   {"channel": 1, "osm_tag": ["golf=green"]},
    "bunker":  {"channel": 2, "osm_tag": ["golf=bunker", "natural=sand"]},
    "rough":   {"channel": 3, "osm_tag": ["golf=rough"]},  # also implicit-fill (below)
    # Extras — written as separate PNGs:
    "tee":         {"channel": None, "osm_tag": ["golf=tee"]},
    "water":       {"channel": None, "osm_tag": ["golf=water_hazard", "natural=water"]},
    "cart_path":   {"channel": None, "osm_tag": ["golf=cartpath", "highway=path"]},
    "trees":       {"channel": None, "osm_tag": ["natural=wood", "landuse=forest"]},
}


def overpass_query(bbox: Tuple[float, float, float, float]) -> dict:
    """
    Query OSM Overpass for all golf-related features in the bbox.

    bbox = (minlon, minlat, maxlon, maxlat)  (WGS84)
    """
    minlon, minlat, maxlon, maxlat = bbox
    bbox_str = f"{minlat},{minlon},{maxlat},{maxlon}"  # Overpass uses S,W,N,E

    # We grab the course outline (leisure=golf_course) plus all common golf-feature tags.
    query = f"""
    [out:json][timeout:60];
    (
      way["leisure"="golf_course"]({bbox_str});
      relation["leisure"="golf_course"]({bbox_str});
      way["golf"]({bbox_str});
      relation["golf"]({bbox_str});
      way["natural"="sand"]({bbox_str});
      way["natural"="water"]({bbox_str});
      way["natural"="wood"]({bbox_str});
      way["landuse"="forest"]({bbox_str});
      way["highway"="path"]({bbox_str});
    );
    out geom;
    """

    print(f"[overpass] querying {bbox_str}")
    r = requests.post(OVERPASS_URL, data={"data": query}, headers=HTTP_HEADERS, timeout=120)
    r.raise_for_status()
    return r.json()


def osm_element_to_polygons(elem: dict) -> List[List[Tuple[float, float]]]:
    """Extract polygon rings from an OSM way/relation element. Returns list of (lon, lat) lists."""
    rings: List[List[Tuple[float, float]]] = []
    if elem["type"] == "way" and "geometry" in elem:
        coords = [(p["lon"], p["lat"]) for p in elem["geometry"]]
        if len(coords) >= 3 and coords[0] == coords[-1]:
            rings.append(coords)
        elif len(coords) >= 3:
            # Unclosed way — treat as polygon if it's a golf feature
            rings.append(coords + [coords[0]])
    elif elem["type"] == "relation" and "members" in elem:
        # Multipolygon — only handle the simple case of outer rings here.
        for m in elem["members"]:
            if m.get("role") == "outer" and "geometry" in m:
                coords = [(p["lon"], p["lat"]) for p in m["geometry"]]
                if len(coords) >= 3:
                    if coords[0] != coords[-1]:
                        coords.append(coords[0])
                    rings.append(coords)
    return rings


def classify_element(elem: dict) -> List[str]:
    """Return the list of FEATURE_LAYERS keys this element belongs to."""
    tags = elem.get("tags", {})
    matches: List[str] = []

    if tags.get("leisure") == "golf_course":
        # Used as the implicit "rough" fill so we know what's *inside* the course at all.
        matches.append("course")

    for layer_name, cfg in FEATURE_LAYERS.items():
        for tag_pair in cfg["osm_tag"]:
            k, v = tag_pair.split("=", 1)
            if tags.get(k) == v:
                matches.append(layer_name)
                break
    return matches


def lonlat_to_pixel(lon: float, lat: float, bbox: Tuple[float, float, float, float], size: int) -> Tuple[int, int]:
    """Linear interpolation from lon/lat to pixel coordinates."""
    minlon, minlat, maxlon, maxlat = bbox
    x = (lon - minlon) / (maxlon - minlon) * (size - 1)
    y = (1.0 - (lat - minlat) / (maxlat - minlat)) * (size - 1)  # flip Y for image space
    return int(round(x)), int(round(y))


def rasterize_layer(elements_by_layer: Dict[str, List[dict]],
                    layer_name: str,
                    bbox: Tuple[float, float, float, float],
                    size: int) -> np.ndarray:
    """Rasterize all elements assigned to `layer_name` into an 8-bit mask."""
    mask = Image.new("L", (size, size), 0)
    draw = ImageDraw.Draw(mask)
    for elem in elements_by_layer.get(layer_name, []):
        for ring in osm_element_to_polygons(elem):
            pixel_ring = [lonlat_to_pixel(lon, lat, bbox, size) for lon, lat in ring]
            if len(pixel_ring) >= 3:
                draw.polygon(pixel_ring, fill=255)
    return np.array(mask, dtype=np.uint8)


def build_splatmap(osm_data: dict,
                   bbox: Tuple[float, float, float, float],
                   size: int,
                   out_dir: Path) -> Dict[str, str]:
    """Build the 4-channel splatmap PNG and any extra single-channel layer PNGs."""
    out_dir.mkdir(parents=True, exist_ok=True)

    # Bucket elements by layer
    elements_by_layer: Dict[str, List[dict]] = {k: [] for k in list(FEATURE_LAYERS) + ["course"]}
    for elem in osm_data.get("elements", []):
        for layer in classify_element(elem):
            elements_by_layer.setdefault(layer, []).append(elem)

    print(f"[splatmap] feature counts: " + ", ".join(
        f"{k}={len(v)}" for k, v in elements_by_layer.items() if len(v) > 0
    ))

    # Rasterize core layers into the 4-channel splatmap
    course_mask = rasterize_layer(elements_by_layer, "course", bbox, size)
    fairway = rasterize_layer(elements_by_layer, "fairway", bbox, size)
    green = rasterize_layer(elements_by_layer, "green", bbox, size)
    bunker = rasterize_layer(elements_by_layer, "bunker", bbox, size)
    explicit_rough = rasterize_layer(elements_by_layer, "rough", bbox, size)

    # Implicit rough: anywhere inside the course polygon that's not fairway/green/bunker
    occupied = (fairway > 0) | (green > 0) | (bunker > 0)
    implicit_rough = (course_mask > 0) & ~occupied
    rough = np.maximum(explicit_rough, implicit_rough.astype(np.uint8) * 255)

    # Stack into RGBA
    rgba = np.stack([fairway, green, bunker, rough], axis=-1)
    out_splat = out_dir / "splatmap.png"
    Image.fromarray(rgba, mode="RGBA").save(out_splat)
    print(f"[splatmap] wrote {out_splat}")

    written = {"splatmap": str(out_splat)}

    # Extras as single-channel PNGs
    for layer_name, cfg in FEATURE_LAYERS.items():
        if cfg["channel"] is not None:
            continue
        mask = rasterize_layer(elements_by_layer, layer_name, bbox, size)
        if mask.max() == 0:
            continue
        out_path = out_dir / f"layer_{layer_name}.png"
        Image.fromarray(mask, mode="L").save(out_path)
        print(f"[splatmap] wrote {out_path}")
        written[layer_name] = str(out_path)

    # Channel legend (UE5 setup help)
    legend = {
        "splatmap.png": {
            "R": "fairway",
            "G": "green",
            "B": "bunker",
            "A": "rough (catch-all)",
        },
        "size_px": size,
        "bbox_wgs84": list(bbox),
    }
    (out_dir / "splatmap.json").write_text(json.dumps(legend, indent=2))

    return written


def parse_bbox(s: str) -> Tuple[float, float, float, float]:
    parts = [float(x) for x in s.split(",")]
    if len(parts) != 4:
        raise argparse.ArgumentTypeError("bbox must be 'minlon,minlat,maxlon,maxlat'")
    return tuple(parts)  # type: ignore


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--bbox-wgs84", required=True, type=parse_bbox)
    p.add_argument("--course-id", required=True)
    p.add_argument("--out-dir", default="out")
    p.add_argument("--size", type=int, default=4033)
    p.add_argument("--osm-cache", default=None,
                   help="Path to a cached Overpass response JSON (skips network query)")
    args = p.parse_args()

    out_dir = Path(args.out_dir) / args.course_id

    if args.osm_cache and Path(args.osm_cache).exists():
        print(f"[overpass] loading cached response from {args.osm_cache}")
        osm_data = json.loads(Path(args.osm_cache).read_text())
    else:
        osm_data = overpass_query(args.bbox_wgs84)
        cache_path = out_dir / "osm_raw.json"
        cache_path.parent.mkdir(parents=True, exist_ok=True)
        cache_path.write_text(json.dumps(osm_data))
        print(f"[overpass] cached raw response to {cache_path}")

    build_splatmap(osm_data, args.bbox_wgs84, args.size, out_dir)


if __name__ == "__main__":
    main()
