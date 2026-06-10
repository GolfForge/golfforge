#!/usr/bin/env python3
"""Author a pin sheet for a course (GOL-191/192).

A pin sheet is a named set of per-hole pin (cup) locations the round can play instead of the static
green-endpoint default -- the engine's Tournament pin mode loads it. This script seeds a *baseline*
sheet by matching each hole to its real green polygon (courses/<id>/green.geojson) and dropping the
pin at that green's centroid: a valid, always-on-green starting point. Real event pin sheets (e.g. a
PGA round at Bethpage Black, which golfforge-demo-black is) are then hand-tweaked from this baseline
-- nudge each lon/lat toward the actual hole location from a published pin sheet.

Output (courses/<id>/pins/<name>.json), consumed by GolfsimRound::ParsePinSheetJson with the COURSE
bbox from heightmap.json:

    {"name": "Championship Pins",
     "bbox_wgs84": [minlon, minlat, maxlon, maxlat],
     "pins": [{"hole_ref": 1, "lon": -73.45, "lat": 40.74}, ...]}

Stdlib only (no venv needed): python build_pin_sheet.py --course-id golfforge-demo-black
"""

import argparse
import json
import os

_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def _polygon_centroid(ring):
    """Area-weighted centroid of a lon/lat ring (closing-duplicate tolerant). Falls back to the
    vertex average for a degenerate ring. Lon/lat is fine here -- greens are tiny, so the planar
    approximation error is negligible and matches the engine's world-cm centroid 1:1."""
    pts = ring[:-1] if len(ring) > 1 and ring[0] == ring[-1] else ring[:]
    n = len(pts)
    if n == 0:
        return None
    if n < 3:
        return [sum(p[0] for p in pts) / n, sum(p[1] for p in pts) / n]
    a = cx = cy = 0.0
    for i in range(n):
        x0, y0 = pts[i]
        x1, y1 = pts[(i + 1) % n]
        cross = x0 * y1 - x1 * y0
        a += cross
        cx += (x0 + x1) * cross
        cy += (y0 + y1) * cross
    if abs(a) < 1e-12:
        return [sum(p[0] for p in pts) / n, sum(p[1] for p in pts) / n]
    a *= 0.5
    return [cx / (6.0 * a), cy / (6.0 * a)]


def _point_in_ring(pt, ring):
    """Even-odd ray cast; ring is a list of [lon, lat]."""
    x, y = pt
    inside = False
    n = len(ring)
    j = n - 1
    for i in range(n):
        xi, yi = ring[i]
        xj, yj = ring[j]
        if ((yi > y) != (yj > y)) and (x < (xj - xi) * (y - yi) / (yj - yi) + xi):
            inside = not inside
        j = i
    return inside


def _load_json(path):
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def _derive_track(course_id):
    """Mirror GolfsimRound::DeriveTrackName: trailing hyphen segment, capitalized
    ('golfforge-demo-black' -> 'Black'). The course's hole.geojson is shared across tracks
    (Black/Blue/Red/Green/Yellow), tagged by golf:course:name, so we filter to one track."""
    seg = course_id.rsplit("-", 1)[-1]
    return seg[:1].upper() + seg[1:] if seg else ""


def build(course_id, set_name, out_id):
    course_dir = os.path.join(_ROOT, "courses", course_id)
    holes = _load_json(os.path.join(course_dir, "hole.geojson"))
    greens = _load_json(os.path.join(course_dir, "green.geojson"))
    heightmap = _load_json(os.path.join(course_dir, "heightmap.json"))

    # Green polygons: outer ring + centroid.
    green_polys = []
    for feat in greens.get("features", []):
        geom = feat.get("geometry", {})
        if geom.get("type") != "Polygon":
            continue
        rings = geom.get("coordinates", [])
        if not rings or len(rings[0]) < 3:
            continue
        outer = rings[0]
        c = _polygon_centroid(outer)
        if c:
            green_polys.append({"ring": outer, "centroid": c})

    # Two-pass over the shared hole.geojson: keep this course's track (golf:course:name), but fall
    # back to all golf=hole features if the tag is absent (single-track courses) -- same rule the
    # engine's ParseHoleScheduleJson uses.
    track = _derive_track(course_id)
    matched, all_holes = [], []
    for feat in holes.get("features", []):
        geom = feat.get("geometry", {})
        if geom.get("type") != "LineString":
            continue
        tags = feat.get("properties", {}).get("osm_tags", {})
        if tags.get("golf") != "hole":
            continue
        coords = geom.get("coordinates", [])
        if len(coords) < 2:
            continue
        all_holes.append((tags, coords))
        if not track or (tags.get("golf:course:name", "").lower() == track.lower()):
            matched.append((tags, coords))
    hole_feats = matched if (matched or not track) else all_holes

    pins = []
    for tags, coords in hole_feats:
        ref = int(tags.get("ref", 0) or 0)
        green_pt = coords[-1]   # tee -> green endpoint

        # Match: containing polygon first, else nearest centroid.
        match = next((g for g in green_polys if _point_in_ring(green_pt, g["ring"])), None)
        if match is None and green_polys:
            match = min(green_polys, key=lambda g: (g["centroid"][0] - green_pt[0]) ** 2
                                                    + (g["centroid"][1] - green_pt[1]) ** 2)
        pin = match["centroid"] if match else green_pt
        pins.append({"hole_ref": ref, "lon": round(pin[0], 7), "lat": round(pin[1], 7)})

    pins.sort(key=lambda p: p["hole_ref"])
    sheet = {
        "name": set_name,
        "bbox_wgs84": heightmap.get("bbox_wgs84"),
        "pins": pins,
    }

    out_dir = os.path.join(course_dir, "pins")
    os.makedirs(out_dir, exist_ok=True)
    out_path = os.path.join(out_dir, out_id + ".json")
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(sheet, f, indent=2)
        f.write("\n")
    print(f"wrote {out_path}  ({len(pins)} pins, '{set_name}')")
    return out_path


def main():
    ap = argparse.ArgumentParser(description="Author a baseline pin sheet from green centroids.")
    ap.add_argument("--course-id", required=True, help="e.g. golfforge-demo-black")
    ap.add_argument("--name", default="Championship Pins", help="display name stored in the sheet")
    ap.add_argument("--out-id", default="default", help="output file id -> pins/<out-id>.json")
    args = ap.parse_args()
    build(args.course_id, args.name, args.out_id)


if __name__ == "__main__":
    main()
