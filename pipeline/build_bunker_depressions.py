#!/usr/bin/env python3
"""
build_bunker_depressions.py — sculpt believable sand-trap geometry into the
heightmap from the pipeline's bunker polygons (GOL-34).

Today's bunkers are flat sand-colored polygons painted on the splatmap; they look
like sand but a ball rolls straight across them. This step reads each bunker
polygon and writes a MODIFIED heightmap with a depressed sand floor + a raised
rim ("lip") so the ball rolls into the trap and the lip blocks line-of-sight.

Inputs (from courses/<id>/, produced by build_heightmap.py / build_splatmap.py):
  heightmap.png   — 16-bit grayscale baseline terrain (NEVER modified here)
  heightmap.json  — bbox_wgs84, size_px, elev_range_m (the elevation encoding)
  bunker.geojson  — Polygon FeatureCollection in WGS84 lon/lat

Output:
  heightmap_bunkers.png — 16-bit grayscale, same dims/encoding as heightmap.png,
                          with depressions + lips sculpted in. THIS is the UE
                          import target once sculpting has been run.

Why a sibling file (not in-place): re-running must derive from a pristine
baseline, never from an already-sculpted map. Keeping heightmap.png untouched
makes the step idempotent by construction.

Why pipeline-side (not engine/scripts): UE5.7 cannot import heightmaps via
Python (see docs/ue5-cookbook.md), so the only deliverable is a PNG a human
re-imports. This is georeferenced raster math — exactly what the pipeline does —
and reusing build_splatmap.lonlat_to_pixel makes the depression align
pixel-perfect with splat_bunker.png.

Encoding note: heightmap.png maps elev_min_m -> 0 and elev_max_m -> 65535, so
units_per_metre = 65535 / elev_range_m. We subtract units inside bunkers and add
units in the rim band, then clamp to [0, 65535]. We do NOT re-normalize: keeping
the encoding fixed means heightmap.json and the landscape's already-set Z scale
stay valid, so the UE re-import is heightmap-only (no Z change).

Usage:
  python build_bunker_depressions.py --course-id golfforge-demo-black
  # then in UE: Landscape > Manage > Import heightmap_bunkers.png
  #             (Heightmap File checked, Layers untouched, Z scale unchanged)
"""

import argparse
import json
import sys
from pathlib import Path
from typing import List, Tuple

import numpy as np
from PIL import Image, ImageDraw

# build_bunker_depressions.py sits in pipeline/ next to build_splatmap.py, so a
# plain import works when run from anywhere; make it robust to cwd regardless.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from build_splatmap import lonlat_to_pixel  # noqa: E402

# The course Landscape is always placed at world +/-100800 cm (build_water_actors
# WORLD_HALF_XY_CM), so the heightmap's full width maps to 2 * that across size_px
# pixels. This is the horizontal pixel size used to convert the cm-based knobs to
# pixels — keep it in sync with the engine's landscape placement.
WORLD_HALF_XY_CM = 100800.0

Ring = List[Tuple[float, float]]


# -------------------- IO --------------------


def load_meta(course_dir: Path) -> dict:
    with (course_dir / "heightmap.json").open(encoding="utf-8") as fh:
        return json.load(fh)


def read_heightmap_png(path: Path) -> np.ndarray:
    """Return the baseline heightmap as a 2D uint16 array (shape = (size, size))."""
    arr = np.array(Image.open(path))
    if arr.ndim != 2:
        raise ValueError(f"{path} is not single-channel grayscale (got shape {arr.shape})")
    # Pillow may report a 16-bit PNG as mode "I" (int32) or "I;16" (uint16);
    # both decode to values in [0, 65535]. Normalize to uint16.
    return arr.astype(np.uint16)


def write_heightmap_png(path: Path, arr: np.ndarray) -> None:
    """Write a 2D uint16 array as a 16-bit grayscale PNG (UE5 Landscape import).

    Mirrors build_heightmap.py: frombuffer("I;16", ...) is the supported route to
    a 16-bit unsigned grayscale PNG in modern Pillow.
    """
    h, w = arr.shape
    data = arr.astype("<u2").tobytes()
    Image.frombuffer("I;16", (w, h), data, "raw", "I;16", 0, 1).save(path)


def load_bunker_rings(geojson_path: Path) -> List[Ring]:
    """Outer ring (lon, lat) of every Polygon feature. Interior rings (holes) are
    ignored — bunkers fill solid for v1."""
    with geojson_path.open(encoding="utf-8") as fh:
        gj = json.load(fh)
    rings: List[Ring] = []
    for feat in gj.get("features", []):
        geom = feat.get("geometry") or {}
        if geom.get("type") != "Polygon":
            continue
        coords = geom.get("coordinates") or []
        if not coords:
            continue
        outer = [(float(c[0]), float(c[1])) for c in coords[0]]
        if len(outer) >= 3:
            rings.append(outer)
    return rings


# -------------------- rasterization --------------------


def rasterize_bunkers(rings: List[Ring],
                      bbox: Tuple[float, float, float, float],
                      size: int) -> np.ndarray:
    """Union mask (bool, shape (size, size)) of all bunker polygons, using the
    SAME lon/lat->pixel transform as the splatmap so the sculpt lines up with
    splat_bunker.png. Rasterizing all polygons into one mask merges overlapping /
    adjacent pot-bunker clusters automatically."""
    img = Image.new("L", (size, size), 0)
    draw = ImageDraw.Draw(img)
    for ring in rings:
        pix = [lonlat_to_pixel(lon, lat, bbox, size) for lon, lat in ring]
        if len(pix) >= 3:
            draw.polygon(pix, fill=255)
    return np.array(img, dtype=np.uint8) > 0


# -------------------- distance transform (pure numpy, no scipy) --------------------


def _edt_1d_sq(f: np.ndarray) -> np.ndarray:
    """1-D squared Euclidean distance transform of sampled function f
    (Felzenszwalb & Huttenlocher 2012). Returns, for each index q,
    min_p (q - p)^2 + f[p]. Feature pixels have f = 0; others f = +inf."""
    n = len(f)
    d = np.empty(n, dtype=np.float64)
    v = np.zeros(n, dtype=np.intp)      # parabola vertex locations
    z = np.empty(n + 1, dtype=np.float64)  # envelope boundaries
    k = 0
    v[0] = 0
    z[0] = -np.inf
    z[1] = np.inf
    for q in range(1, n):
        s = ((f[q] + q * q) - (f[v[k]] + v[k] * v[k])) / (2.0 * q - 2.0 * v[k])
        while s <= z[k]:
            k -= 1
            s = ((f[q] + q * q) - (f[v[k]] + v[k] * v[k])) / (2.0 * q - 2.0 * v[k])
        k += 1
        v[k] = q
        z[k] = s
        z[k + 1] = np.inf
    k = 0
    for q in range(n):
        while z[k + 1] < q:
            k += 1
        dist = q - v[k]
        d[q] = dist * dist + f[v[k]]
    return d


def edt_sq(feature: np.ndarray) -> np.ndarray:
    """2-D squared Euclidean distance to the nearest True pixel in `feature`,
    via the separable 1-D transform (columns then rows). Exact."""
    INF = 1e20
    f = np.where(feature, 0.0, INF).astype(np.float64)
    for x in range(f.shape[1]):
        f[:, x] = _edt_1d_sq(f[:, x])
    for y in range(f.shape[0]):
        f[y, :] = _edt_1d_sq(f[y, :])
    return f


# -------------------- profile --------------------


def cm_to_units(cm: float, elev_range_m: float) -> float:
    """Heightmap 16-bit units for a vertical distance in cm, under the fixed
    elev_min->0 / elev_max->65535 encoding."""
    return (cm / 100.0) * (65535.0 / elev_range_m)


def _smoothstep(t: np.ndarray) -> np.ndarray:
    t = np.clip(t, 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def cm_per_px(size: int) -> float:
    return (2.0 * WORLD_HALF_XY_CM) / size


def sculpt(baseline: np.ndarray,
           mask: np.ndarray,
           *,
           elev_range_m: float,
           cm_per_px: float,
           depression_depth_cm: float,
           lip_height_cm: float,
           lip_width_cm: float,
           floor_radius_cm: float) -> np.ndarray:
    """Return a new uint16 heightmap = baseline with a depressed floor inside
    `mask` and a raised lip in a band just outside it.

    - Inside: depth ramps (smoothstep) from 0 at the rim to depression_depth_cm
      once `floor_radius_cm` inward, giving a ~flat floor with sloped walls.
    - Outside: a rim raised lip_height_cm at the very edge, falling linearly to 0
      at lip_width_cm out.

    `cm_per_px` is the horizontal pixel size (the cm knobs are converted to pixels
    with it) — main() derives it from the landscape's world extent; tests pass it
    directly. Idempotent: depends only on `baseline` + `mask`, never on prior
    output. Distances are computed on a crop around the bunkers (padded by the lip
    band) so the exact EDT runs over a small region, not the whole landscape.
    """
    out = baseline.astype(np.float64)
    if not mask.any():
        return baseline.copy()

    floor_radius_px = max(floor_radius_cm / cm_per_px, 1e-6)
    lip_width_px = max(lip_width_cm / cm_per_px, 1e-6)
    depth_u = cm_to_units(depression_depth_cm, elev_range_m)
    lip_u = cm_to_units(lip_height_cm, elev_range_m)

    # Crop to the bunker bounding box, padded by the lip band (+ a margin) so the
    # outside-distance band is fully contained and the inside-distance always
    # finds background within the crop.
    h, w = baseline.shape
    pad = int(np.ceil(lip_width_px)) + 2
    ys, xs = np.where(mask)
    y0 = max(int(ys.min()) - pad, 0)
    y1 = min(int(ys.max()) + pad + 1, h)
    x0 = max(int(xs.min()) - pad, 0)
    x1 = min(int(xs.max()) + pad + 1, w)
    m = mask[y0:y1, x0:x1]

    # Inside depth: distance from each foreground pixel to nearest background.
    d_in = np.sqrt(edt_sq(~m))
    bowl = depth_u * _smoothstep(d_in / floor_radius_px)
    bowl = np.where(m, bowl, 0.0)

    # Outside lip: distance from each background pixel to nearest foreground.
    # The rim ring (the nearest band of pixels, d_out ~ 1) reaches the full
    # nominal lip height and ramps linearly to 0 at lip_width_px out, so
    # lip_height_cm is actually achieved at the rim rather than discounted by the
    # 1-px offset of the first background ring.
    d_out = np.sqrt(edt_sq(m))
    in_band = (~m) & (d_out > 0.0) & (d_out <= lip_width_px)
    denom = max(lip_width_px - 1.0, 1e-6)
    t_out = np.clip((d_out - 1.0) / denom, 0.0, 1.0)
    lip = np.where(in_band, lip_u * (1.0 - t_out), 0.0)

    out[y0:y1, x0:x1] += (-bowl + lip)
    return np.clip(out, 0.0, 65535.0).astype(np.uint16)


# -------------------- CLI --------------------


def main() -> None:
    p = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--course-id", required=True, help="Course identifier (folder under --courses-dir)")
    p.add_argument("--courses-dir", default=str(Path(__file__).resolve().parent.parent / "courses"),
                   help="Directory holding courses/<id>/ (default: ../courses)")
    p.add_argument("--depression-depth-cm", type=float, default=50.0,
                   help="Sand floor depth below surrounding terrain (default 50)")
    p.add_argument("--lip-height-cm", type=float, default=25.0,
                   help="Raised rim height above surrounding terrain (default 25)")
    p.add_argument("--lip-width-cm", type=float, default=300.0,
                   help="Width of the raised rim band outside the bunker (default 300; "
                        "at ~1 m/px a narrower lip is too thin to read)")
    p.add_argument("--floor-radius-cm", type=float, default=200.0,
                   help="Inward distance over which depth ramps to full (default 200)")
    args = p.parse_args()

    course_dir = Path(args.courses_dir) / args.course_id
    hm_png = course_dir / "heightmap.png"
    bunkers_png = course_dir / "heightmap_bunkers.png"
    geojson = course_dir / "bunker.geojson"
    for required in (hm_png, course_dir / "heightmap.json"):
        if not required.exists():
            print(f"ERROR: missing {required}", file=sys.stderr)
            raise SystemExit(2)

    meta = load_meta(course_dir)
    bbox = tuple(meta["bbox_wgs84"])
    size = int(meta["size_px"])
    elev_range_m = float(meta["elev_range_m"])

    baseline = read_heightmap_png(hm_png)
    if baseline.shape != (size, size):
        print(f"WARNING: heightmap.png is {baseline.shape}, heightmap.json says size_px={size}; "
              "using the PNG's actual size.", file=sys.stderr)
        size = baseline.shape[0]

    rings = load_bunker_rings(geojson) if geojson.exists() else []
    if not rings:
        print(f"No bunker polygons for {args.course_id} "
              f"({'no bunker.geojson' if not geojson.exists() else 'empty'}); "
              "writing an unmodified copy.")
    mask = rasterize_bunkers(rings, bbox, size)

    result = sculpt(
        baseline, mask,
        elev_range_m=elev_range_m,
        cm_per_px=cm_per_px(size),
        depression_depth_cm=args.depression_depth_cm,
        lip_height_cm=args.lip_height_cm,
        lip_width_cm=args.lip_width_cm,
        floor_radius_cm=args.floor_radius_cm,
    )
    write_heightmap_png(bunkers_png, result)

    delta = result.astype(np.int32) - baseline.astype(np.int32)
    upm = 65535.0 / elev_range_m
    changed = int(np.count_nonzero(delta))
    print()
    print(f"  course:        {args.course_id}")
    print(f"  bunkers:       {len(rings)} polygon(s), {int(mask.sum())} mask px")
    print(f"  changed px:    {changed} ({100.0 * changed / baseline.size:.2f}% of terrain)")
    if changed:
        print(f"  max drop:      {-int(delta.min())} units (~{-delta.min() / upm * 100.0:.0f} cm)")
        print(f"  max rise:      {int(delta.max())} units (~{delta.max() / upm * 100.0:.0f} cm)")
    print(f"  wrote:         {bunkers_png}")
    print()
    print("Next: in UE, Landscape > Manage > Import this heightmap_bunkers.png")
    print("      (Heightmap File checked, Layers untouched, Z scale unchanged).")


if __name__ == "__main__":
    main()
