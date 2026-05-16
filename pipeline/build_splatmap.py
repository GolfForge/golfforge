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
import math
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Tuple

import numpy as np
import requests
from PIL import Image, ImageDraw


# Overpass main server is often slow or returning 504; try mirrors in order.
# Order roughly: kumi (fastest historically) → main → coffee → de.
OVERPASS_MIRRORS = [
    "https://overpass.kumi.systems/api/interpreter",
    "https://overpass-api.de/api/interpreter",
    "https://overpass.private.coffee/api/interpreter",
    "https://overpass.osm.ch/api/interpreter",
]

# Overpass usage policy requires a descriptive User-Agent on every request.
HTTP_HEADERS = {
    "User-Agent": "golfsim/0.1 (https://github.com/pucho/golfsim) python-requests",
}

# OSM tag → splatmap channel index (R=0, G=1, B=2, A=3) or "extra" for separate PNG.
#
# `geom` is "polygon" (area feature, force-closed if open) or "line" (linear feature
# rendered as a stroke with real-world `width_m`). Mixing these up is what caused the
# original cart-path "huge white areas" bug — open linestrings got force-closed into
# bounding-box polygons and flood-filled.
FEATURE_LAYERS = {
    "fairway": {"channel": 0,    "osm_tag": ["golf=fairway"],                       "geom": "polygon"},
    "green":   {"channel": 1,    "osm_tag": ["golf=green"],                         "geom": "polygon"},
    "bunker":  {"channel": 2,    "osm_tag": ["golf=bunker", "natural=sand"],        "geom": "polygon"},
    "rough":   {"channel": 3,    "osm_tag": ["golf=rough"],                         "geom": "polygon"},  # also implicit-fill
    # Extras — written as separate PNGs. `emit_geojson` opts a polygon layer into
    # also emitting a `<name>.geojson` sidecar so the UE side can choose between
    # weight-painting the raster and spawning vector-based actors (e.g.
    # WaterBodyLake from a spline). Linear features always emit geojson.
    "tee":         {"channel": None, "osm_tag": ["golf=tee"],                       "geom": "polygon"},
    "water":       {"channel": None, "osm_tag": ["golf=water_hazard", "natural=water", "golf=lateral_water_hazard"],
                                                                                    "geom": "polygon", "emit_geojson": True},
    "cart_path":   {"channel": None, "osm_tag": ["golf=cartpath", "highway=path"],  "geom": "line", "width_m": 3.0},
    "trees":       {"channel": None, "osm_tag": ["natural=wood", "landuse=forest"], "geom": "polygon"},
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

    last_err: Exception | None = None
    for url in OVERPASS_MIRRORS:
        try:
            print(f"[overpass] querying {url}  bbox={bbox_str}")
            r = requests.post(url, data={"data": query}, headers=HTTP_HEADERS, timeout=120)
            r.raise_for_status()
            return r.json()
        except (requests.RequestException, ValueError) as e:
            print(f"[overpass]   mirror failed: {e.__class__.__name__}: {e}")
            last_err = e
    raise RuntimeError(f"all Overpass mirrors failed; last error: {last_err}")


def osm_element_to_polygons(elem: dict) -> List[List[Tuple[float, float]]]:
    """Extract polygon rings from an OSM way/relation element. Returns list of (lon, lat) lists.

    Only call this for features that are *semantically* areas (e.g., golf=fairway).
    Linear features must NOT be force-closed; use osm_element_to_lines() for those.
    """
    rings: List[List[Tuple[float, float]]] = []
    if elem["type"] == "way" and "geometry" in elem:
        coords = [(p["lon"], p["lat"]) for p in elem["geometry"]]
        if len(coords) >= 3 and coords[0] == coords[-1]:
            rings.append(coords)
        elif len(coords) >= 3:
            rings.append(coords + [coords[0]])  # close it
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


def osm_element_to_lines(elem: dict) -> List[List[Tuple[float, float]]]:
    """Extract polylines from an OSM way/relation element. Returns list of (lon, lat) lists.

    For linear features. Never force-closes. Closed ways (where first == last) are
    still returned as-is, which is correct — a closed cart-path loop should still
    render as a stroke along the closed path, not flood-filled.
    """
    lines: List[List[Tuple[float, float]]] = []
    if elem["type"] == "way" and "geometry" in elem:
        coords = [(p["lon"], p["lat"]) for p in elem["geometry"]]
        if len(coords) >= 2:
            lines.append(coords)
    elif elem["type"] == "relation" and "members" in elem:
        for m in elem["members"]:
            if "geometry" in m:
                coords = [(p["lon"], p["lat"]) for p in m["geometry"]]
                if len(coords) >= 2:
                    lines.append(coords)
    return lines


def meters_per_pixel(bbox: Tuple[float, float, float, float], size_px: int) -> float:
    """Approximate meters-per-pixel for the bbox at the given image size.

    Uses the mean of x-resolution and y-resolution since the image is square but
    the bbox typically isn't. At mid-latitude this is within a few percent of true.
    """
    minlon, minlat, maxlon, maxlat = bbox
    mid_lat_rad = math.radians((minlat + maxlat) / 2.0)
    width_m = (maxlon - minlon) * 111320.0 * math.cos(mid_lat_rad)
    height_m = (maxlat - minlat) * 110540.0
    return ((width_m / size_px) + (height_m / size_px)) / 2.0


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
                    size: int,
                    geom: str = "polygon",
                    width_m: float = 0.0) -> np.ndarray:
    """Rasterize all elements assigned to `layer_name` into an 8-bit mask.

    geom == "polygon": flood-fill each ring (use for area features).
    geom == "line":    stroke each polyline at `width_m` meters wide (use for cart paths).
    """
    mask = Image.new("L", (size, size), 0)
    draw = ImageDraw.Draw(mask)

    if geom == "line":
        line_width_px = max(1, round(width_m / meters_per_pixel(bbox, size)))
        for elem in elements_by_layer.get(layer_name, []):
            for line in osm_element_to_lines(elem):
                pts = [lonlat_to_pixel(lon, lat, bbox, size) for lon, lat in line]
                if len(pts) >= 2:
                    draw.line(pts, fill=255, width=line_width_px, joint="curve")
                    # Stroke endcaps — draw.line in PIL doesn't round-cap, so we
                    # plant small filled circles at each vertex to smooth joints.
                    r = line_width_px // 2
                    if r >= 1:
                        for x, y in pts:
                            draw.ellipse((x - r, y - r, x + r, y + r), fill=255)
    else:
        for elem in elements_by_layer.get(layer_name, []):
            for ring in osm_element_to_polygons(elem):
                pixel_ring = [lonlat_to_pixel(lon, lat, bbox, size) for lon, lat in ring]
                if len(pixel_ring) >= 3:
                    draw.polygon(pixel_ring, fill=255)

    return np.array(mask, dtype=np.uint8)


def export_lines_geojson(elements_by_layer: Dict[str, List[dict]],
                         layer_name: str,
                         out_path: Path) -> int:
    """Write the layer's polylines as a GeoJSON FeatureCollection for spline-aware consumers."""
    features = []
    for elem in elements_by_layer.get(layer_name, []):
        for line in osm_element_to_lines(elem):
            features.append({
                "type": "Feature",
                "properties": {
                    "osm_way_id": elem.get("id"),
                    "osm_tags": elem.get("tags", {}),
                },
                "geometry": {
                    "type": "LineString",
                    "coordinates": [[lon, lat] for lon, lat in line],
                },
            })
    fc = {"type": "FeatureCollection", "features": features}
    out_path.write_text(json.dumps(fc))
    return len(features)


def export_polygons_geojson(elements_by_layer: Dict[str, List[dict]],
                            layer_name: str,
                            out_path: Path) -> int:
    """Write the layer's polygons as a GeoJSON FeatureCollection for vector-aware consumers.

    Sources polygons directly from the original OSM elements (not from the raster
    mask), preserving full vertex precision and OSM provenance. Multi-ring elements
    (relations with outer rings) yield one Feature per outer ring.
    """
    features = []
    for elem in elements_by_layer.get(layer_name, []):
        for ring in osm_element_to_polygons(elem):
            features.append({
                "type": "Feature",
                "properties": {
                    "osm_way_id": elem.get("id"),
                    "osm_tags": elem.get("tags", {}),
                },
                "geometry": {
                    "type": "Polygon",
                    "coordinates": [[[lon, lat] for lon, lat in ring]],
                },
            })
    fc = {"type": "FeatureCollection", "features": features}
    out_path.write_text(json.dumps(fc))
    return len(features)


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

    # Rasterize core layers into the 4-channel splatmap (all polygons).
    course_mask = rasterize_layer(elements_by_layer, "course", bbox, size, geom="polygon")
    fairway = rasterize_layer(elements_by_layer, "fairway", bbox, size, geom="polygon")
    green = rasterize_layer(elements_by_layer, "green", bbox, size, geom="polygon")
    bunker = rasterize_layer(elements_by_layer, "bunker", bbox, size, geom="polygon")
    explicit_rough = rasterize_layer(elements_by_layer, "rough", bbox, size, geom="polygon")

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

    # Per-channel single-channel PNGs — UE5 Landscape Paint mode imports one layer at a time.
    for i, layer_name in enumerate(("fairway", "green", "bunker", "rough")):
        out_layer = out_dir / f"splat_{layer_name}.png"
        Image.fromarray(rgba[:, :, i], mode="L").save(out_layer)
        print(f"[splatmap] wrote {out_layer}")
        written[f"splat_{layer_name}"] = str(out_layer)

    # Extras as single-channel PNGs. Linear features always emit a GeoJSON sidecar
    # (spline-aware UE consumption). Polygon features opt in via emit_geojson=True
    # (e.g. water → UE Water plugin's WaterBodyLake spline).
    mpp = meters_per_pixel(bbox, size)
    for layer_name, cfg in FEATURE_LAYERS.items():
        if cfg["channel"] is not None:
            continue
        geom = cfg.get("geom", "polygon")
        width_m = cfg.get("width_m", 0.0)
        mask = rasterize_layer(elements_by_layer, layer_name, bbox, size,
                               geom=geom, width_m=width_m)
        if mask.max() == 0:
            continue
        out_path = out_dir / f"layer_{layer_name}.png"
        Image.fromarray(mask, mode="L").save(out_path)
        if geom == "line":
            stroke_px = max(1, round(width_m / mpp))
            print(f"[splatmap] wrote {out_path}  (line, width={width_m}m ≈ {stroke_px}px @ {mpp:.2f} m/px)")
            gj_path = out_dir / f"{layer_name}.geojson"
            n = export_lines_geojson(elements_by_layer, layer_name, gj_path)
            print(f"[splatmap] wrote {gj_path}  ({n} features)")
            written[f"{layer_name}_geojson"] = str(gj_path)
        else:
            print(f"[splatmap] wrote {out_path}")
            if cfg.get("emit_geojson"):
                gj_path = out_dir / f"{layer_name}.geojson"
                n = export_polygons_geojson(elements_by_layer, layer_name, gj_path)
                print(f"[splatmap] wrote {gj_path}  ({n} polygon features)")
                written[f"{layer_name}_geojson"] = str(gj_path)
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

    cache_path = out_dir / "osm_raw.json"
    osm_data = None

    # Try the explicit cache file or the implicit per-course cache, but never trust
    # an empty result — those happen on broken mirrors and should not be persisted.
    for candidate in [args.osm_cache, cache_path if cache_path.exists() else None]:
        if not candidate:
            continue
        path = Path(candidate)
        if not path.exists():
            continue
        cached = json.loads(path.read_text())
        if cached.get("elements"):
            print(f"[overpass] loaded cached response from {path} ({len(cached['elements'])} elements)")
            osm_data = cached
            break
        else:
            print(f"[overpass] ignoring empty cache at {path} (broken mirror response?)")

    if osm_data is None:
        osm_data = overpass_query(args.bbox_wgs84)
        if not osm_data.get("elements"):
            print("[overpass] WARNING: server returned 0 elements — not caching, splatmap will be empty.")
        else:
            cache_path.parent.mkdir(parents=True, exist_ok=True)
            cache_path.write_text(json.dumps(osm_data))
            print(f"[overpass] cached raw response to {cache_path}")

    build_splatmap(osm_data, args.bbox_wgs84, args.size, out_dir)


if __name__ == "__main__":
    main()
