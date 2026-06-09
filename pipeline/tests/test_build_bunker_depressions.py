"""Tests for pipeline/build_bunker_depressions.py (GOL-34 bunker sculpting).

Organized bottom-up: the exact distance transform, the cm->units encoding, the
sculpt profile (bowl + lip), idempotency, and mask alignment with the splatmap.
All run without UE — pure raster math.

Run from `pipeline/` with:  python3 -m pytest tests/
"""

import sys
from pathlib import Path

import numpy as np

# Add pipeline/ to sys.path so we can import the module under test.
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import build_bunker_depressions as bd  # noqa: E402


# ---------- exact distance transform ----------


def test_edt_1d_row():
    # Single feature pixel at index 0 -> squared distances 0,1,4,9 along the row.
    feature = np.array([[True, False, False, False]])
    out = bd.edt_sq(feature)
    assert np.allclose(out, [[0, 1, 4, 9]])


def test_edt_2d_corner():
    # Single feature pixel at the centre of a 5x5 -> corner squared dist = 2^2+2^2.
    feature = np.zeros((5, 5), dtype=bool)
    feature[2, 2] = True
    out = bd.edt_sq(feature)
    assert out[0, 0] == 8
    assert out[2, 2] == 0
    assert out[2, 0] == 4  # straight left, two pixels


def test_edt_matches_bruteforce():
    # Cross-check the separable EDT against an O(n^2) brute force on a small grid.
    rng = np.random.default_rng(0)
    feature = rng.random((9, 11)) < 0.25
    feature[0, 0] = True  # guarantee at least one source
    got = bd.edt_sq(feature)
    fy, fx = np.where(feature)
    for y in range(feature.shape[0]):
        for x in range(feature.shape[1]):
            exp = np.min((fy - y) ** 2 + (fx - x) ** 2)
            assert got[y, x] == exp


# ---------- cm -> 16-bit units ----------


def test_cm_to_units():
    # elev_min->0, elev_max->65535 over elev_range_m; 1 m == 65535/range units.
    rng = 51.797
    assert bd.cm_to_units(100.0, rng) == 65535.0 / rng
    assert bd.cm_to_units(50.0, rng) == (65535.0 / rng) * 0.5
    assert bd.cm_to_units(0.0, rng) == 0.0


# ---------- sculpt profile ----------


def _square_mask(size, lo, hi):
    m = np.zeros((size, size), dtype=bool)
    m[lo:hi, lo:hi] = True
    return m


def test_sculpt_bowl_and_lip():
    size = 60
    base_val = 30000
    baseline = np.full((size, size), base_val, dtype=np.uint16)
    mask = _square_mask(size, 20, 40)  # 20x20 centred-ish bunker

    out = bd.sculpt(
        baseline, mask,
        elev_range_m=50.0, cm_per_px=100.0,
        depression_depth_cm=50.0, lip_height_cm=25.0,
        lip_width_cm=300.0, floor_radius_cm=200.0,
    )
    out_i = out.astype(np.int32)

    centre = out_i[30, 30]
    rim_inside = out_i[20, 30]            # just inside the bunker edge
    just_outside = out_i[19, 30]          # one px outside the edge (in the lip band)
    far_outside = out_i[0, 0]             # well beyond the lip band

    # Floor sits below terrain; centre is the deepest part of the bowl.
    assert centre < base_val
    assert centre < rim_inside
    # Lip just outside the rim is raised above terrain.
    assert just_outside > base_val
    # Untouched terrain far from any bunker is exactly the baseline.
    assert far_outside == base_val

    # Depth magnitude ~ depression_depth_cm at the floor (within rounding).
    upm = 65535.0 / 50.0
    expected_drop = bd.cm_to_units(50.0, 50.0)
    assert abs((base_val - centre) - expected_drop) <= 2
    # Max rise ~ lip_height_cm at the very rim.
    assert abs((out_i.max() - base_val) - bd.cm_to_units(25.0, 50.0)) <= 2
    assert upm > 0  # sanity


def test_sculpt_no_bunkers_is_identity():
    baseline = np.full((20, 20), 12345, dtype=np.uint16)
    empty = np.zeros((20, 20), dtype=bool)
    out = bd.sculpt(
        baseline, empty, elev_range_m=50.0, cm_per_px=100.0,
        depression_depth_cm=50.0, lip_height_cm=25.0,
        lip_width_cm=300.0, floor_radius_cm=200.0,
    )
    assert np.array_equal(out, baseline)


def test_sculpt_does_not_mutate_baseline():
    baseline = np.full((60, 60), 30000, dtype=np.uint16)
    snapshot = baseline.copy()
    bd.sculpt(
        baseline, _square_mask(60, 20, 40), elev_range_m=50.0, cm_per_px=100.0,
        depression_depth_cm=50.0, lip_height_cm=25.0,
        lip_width_cm=300.0, floor_radius_cm=200.0,
    )
    assert np.array_equal(baseline, snapshot)


def test_sculpt_clamps_to_uint16_range():
    # A floor deeper than the baseline value must clamp at 0, not wrap.
    baseline = np.full((60, 60), 100, dtype=np.uint16)
    out = bd.sculpt(
        baseline, _square_mask(60, 20, 40), elev_range_m=1.0, cm_per_px=100.0,
        depression_depth_cm=500.0, lip_height_cm=25.0,
        lip_width_cm=300.0, floor_radius_cm=200.0,
    )
    assert out.min() >= 0
    assert out.dtype == np.uint16
    assert out[30, 30] == 0  # clamped floor


# ---------- idempotency ----------


def test_sculpt_idempotent():
    baseline = np.full((60, 60), 30000, dtype=np.uint16)
    mask = _square_mask(60, 20, 40)
    kw = dict(elev_range_m=50.0, cm_per_px=100.0, depression_depth_cm=50.0,
              lip_height_cm=25.0, lip_width_cm=300.0, floor_radius_cm=200.0)
    a = bd.sculpt(baseline, mask, **kw)
    b = bd.sculpt(baseline, mask, **kw)  # re-run off the SAME pristine baseline
    assert np.array_equal(a, b)


def test_png_roundtrip_preserves_values(tmp_path):
    arr = np.arange(16, dtype=np.uint16).reshape(4, 4) * 4000
    path = tmp_path / "h.png"
    bd.write_heightmap_png(path, arr)
    back = bd.read_heightmap_png(path)
    assert back.dtype == np.uint16
    assert np.array_equal(back, arr)


# ---------- mask alignment with the splatmap ----------


def test_rasterize_uses_splatmap_transform():
    # A bunker square in lon/lat should rasterize with its centroid inside the
    # mask and the bbox corners outside it (same lonlat_to_pixel as the splatmap).
    bbox = (-73.4555, 40.7423, -73.4345, 40.7571)
    size = 200
    minlon, minlat, maxlon, maxlat = bbox
    clon = (minlon + maxlon) / 2.0
    clat = (minlat + maxlat) / 2.0
    dlon = (maxlon - minlon) * 0.05
    dlat = (maxlat - minlat) * 0.05
    ring = [
        (clon - dlon, clat - dlat),
        (clon + dlon, clat - dlat),
        (clon + dlon, clat + dlat),
        (clon - dlon, clat + dlat),
        (clon - dlon, clat - dlat),
    ]
    mask = bd.rasterize_bunkers([ring], bbox, size)
    cx, cy = bd.lonlat_to_pixel(clon, clat, bbox, size)
    assert mask[cy, cx]            # centroid pixel painted
    assert not mask[0, 0]         # far corner untouched
    assert mask.sum() > 0


def test_load_bunker_rings(tmp_path):
    gj = {
        "type": "FeatureCollection",
        "features": [
            {"type": "Feature", "properties": {},
             "geometry": {"type": "Polygon",
                          "coordinates": [[[0, 0], [1, 0], [1, 1], [0, 1], [0, 0]]]}},
            {"type": "Feature", "properties": {},
             "geometry": {"type": "LineString", "coordinates": [[0, 0], [1, 1]]}},
        ],
    }
    import json
    p = tmp_path / "bunker.geojson"
    p.write_text(json.dumps(gj))
    rings = bd.load_bunker_rings(p)
    assert len(rings) == 1                 # LineString skipped
    assert rings[0][0] == (0.0, 0.0)
