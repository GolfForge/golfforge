"""Tests for pipeline/build_splatmap.py.

The tests are organized roughly bottom-up: geometry helpers first, then
classification, then rasterization, then exporters, finally end-to-end.

Two categories of tests carry their weight:

- **Regression tests for known bugs** (the cart-path flood-fill class). These
  freeze the bug-fix contract: line-mode rasterization must NOT fill polygon
  bounding regions, polygon export must produce closed rings, etc.

- **Cross-machine contract tests** for the GeoJSON sidecars consumed by the
  UE side. The Windows agent depends on the shape of `cart_path.geojson` and
  `water.geojson`; these tests freeze that shape.

Run from `pipeline/` with:  python3 -m pytest tests/
"""

import json
import math
import sys
from pathlib import Path

import numpy as np
import pytest
from PIL import Image

# Add pipeline/ to sys.path so we can import build_splatmap as a module.
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import build_splatmap as bs  # noqa: E402


# Real bbox we use as the reference for unit-scale sanity checks.
# History:
#   - original (pre-GOL-33): -73.4540, 40.7423, -73.4374, 40.7549
#   - GOL-33: widened E for Black hole 8 -> -73.4540, 40.7423, -73.4350, 40.7549
#   - GOL-108: widened W + E + N to land all 18 Black hole tees + greens on
#     the landscape (visual verify in GOL-85 found 5 endpoints off-edge)
BBOX_GOLFFORGE_DEMO = (-73.4555, 40.7423, -73.4345, 40.7571)


# ---------- helpers ----------


def way(id_, coords, tags=None):
    """Construct a synthetic OSM way element."""
    return {
        "type": "way",
        "id": id_,
        "tags": tags or {},
        "geometry": [{"lon": lon, "lat": lat} for lon, lat in coords],
    }


def relation(id_, members, tags=None):
    """Construct a synthetic OSM relation element."""
    return {"type": "relation", "id": id_, "tags": tags or {}, "members": members}


# ---------- osm_element_to_polygons ----------


def test_closed_way_remains_one_ring():
    elem = way(1, [(0, 0), (1, 0), (1, 1), (0, 0)])
    rings = bs.osm_element_to_polygons(elem)
    assert len(rings) == 1
    assert rings[0][0] == rings[0][-1]
    assert len(rings[0]) == 4


def test_open_way_gets_force_closed_for_polygon_consumers():
    elem = way(2, [(0, 0), (1, 0), (1, 1)])
    rings = bs.osm_element_to_polygons(elem)
    assert len(rings) == 1
    assert rings[0][0] == rings[0][-1]
    assert len(rings[0]) == 4  # 3 originals + 1 close


def test_short_way_returns_no_polygons():
    elem = way(3, [(0, 0), (1, 0)])
    assert bs.osm_element_to_polygons(elem) == []


def test_relation_outer_rings_only():
    outer = {
        "type": "way", "ref": 100, "role": "outer",
        "geometry": [{"lon": 0, "lat": 0}, {"lon": 1, "lat": 0}, {"lon": 1, "lat": 1}],
    }
    inner = {
        "type": "way", "ref": 99, "role": "inner",
        "geometry": [{"lon": 0.2, "lat": 0.2}, {"lon": 0.3, "lat": 0.2}, {"lon": 0.3, "lat": 0.3}],
    }
    rel = relation(4, [outer, inner])
    rings = bs.osm_element_to_polygons(rel)
    assert len(rings) == 1  # inner is ignored
    assert rings[0][0] == rings[0][-1]


# ---------- osm_element_to_lines ----------


def test_open_line_is_not_force_closed():
    """Regression: the cart-path bug was caused by force-closing open ways
    before flood-filling them. Line mode must preserve the open shape.
    """
    elem = way(5, [(0, 0), (1, 0), (1, 1)])
    lines = bs.osm_element_to_lines(elem)
    assert len(lines) == 1
    assert lines[0] == [(0, 0), (1, 0), (1, 1)]
    assert lines[0][0] != lines[0][-1]


def test_closed_line_stays_closed():
    elem = way(6, [(0, 0), (1, 0), (1, 1), (0, 0)])
    lines = bs.osm_element_to_lines(elem)
    assert lines[0][0] == lines[0][-1]


def test_two_point_line_is_valid():
    elem = way(7, [(0, 0), (1, 0)])
    lines = bs.osm_element_to_lines(elem)
    assert lines == [[(0, 0), (1, 0)]]


# ---------- meters_per_pixel ----------


def test_meters_per_pixel_golfforge_demo_within_expected_range():
    mpp = bs.meters_per_pixel(BBOX_GOLFFORGE_DEMO, 2017)
    # Empirically ~0.84 m/px at the GOL-108 bbox / 2017 size; allow some slack.
    # (Was ~0.69 m/px before bbox widening — see BBOX_GOLFFORGE_DEMO history above.)
    assert 0.7 < mpp < 1.0


def test_meters_per_pixel_scales_inversely_with_size():
    mpp_low = bs.meters_per_pixel(BBOX_GOLFFORGE_DEMO, 1009)
    mpp_high = bs.meters_per_pixel(BBOX_GOLFFORGE_DEMO, 4033)
    assert mpp_high < mpp_low
    # 4033/1009 ≈ 4×, so mpp should be ~4× finer
    assert math.isclose(mpp_low / mpp_high, 4.0, rel_tol=0.05)


# ---------- lonlat_to_pixel ----------


def test_lonlat_to_pixel_top_left_corner_is_minlon_maxlat():
    bbox = (-1.0, 40.0, 1.0, 41.0)
    assert bs.lonlat_to_pixel(-1.0, 41.0, bbox, 100) == (0, 0)


def test_lonlat_to_pixel_bottom_right_corner_is_maxlon_minlat():
    bbox = (-1.0, 40.0, 1.0, 41.0)
    assert bs.lonlat_to_pixel(1.0, 40.0, bbox, 100) == (99, 99)


def test_lonlat_to_pixel_center_with_odd_size():
    bbox = (-1.0, 40.0, 1.0, 41.0)
    assert bs.lonlat_to_pixel(0.0, 40.5, bbox, 101) == (50, 50)


# ---------- classify_element ----------


def test_classify_golf_fairway():
    assert "fairway" in bs.classify_element(way(10, [], {"golf": "fairway"}))


def test_classify_natural_water_routes_to_water_layer():
    assert "water" in bs.classify_element(way(11, [], {"natural": "water"}))


def test_classify_course_outline_yields_synthetic_course_class():
    assert "course" in bs.classify_element(way(12, [], {"leisure": "golf_course"}))


def test_classify_unrelated_tags_yields_no_match():
    assert bs.classify_element(way(13, [], {"random": "tag"})) == []


def test_classify_natural_sand_matches_bunker():
    assert "bunker" in bs.classify_element(way(14, [], {"natural": "sand"}))


# ---------- rasterize_layer ----------


def test_rasterize_polygon_fills_interior():
    bbox = (0.0, 0.0, 1.0, 1.0)
    elements = {
        "fairway": [way(20, [(0.25, 0.25), (0.75, 0.25), (0.75, 0.75), (0.25, 0.75)])],
    }
    mask = bs.rasterize_layer(elements, "fairway", bbox, 100, geom="polygon")
    assert mask[50, 50] == 255  # interior filled
    assert mask[10, 10] == 0    # outside empty
    assert mask[90, 90] == 0


def test_rasterize_line_does_not_flood_fill_bounding_region():
    """The cart-path bug regression: an L-shaped open polyline must NOT
    fill the implicit bounding triangle of its endpoints.
    """
    bbox = (0.0, 0.0, 1.0, 1.0)
    elements = {
        "cart_path": [way(30, [(0.1, 0.5), (0.9, 0.5), (0.9, 0.1)])],
    }
    mask = bs.rasterize_layer(elements, "cart_path", bbox, 200,
                              geom="line", width_m=5.0)
    # Interior point of the L's bounding triangle (around lon=0.5, lat=0.3),
    # which would be filled if line rasterization fell back to flood-fill.
    interior_x, interior_y = bs.lonlat_to_pixel(0.5, 0.3, bbox, 200)
    assert mask[interior_y, interior_x] == 0, (
        "Line mode is flood-filling the bounding region — the cart-path bug returned"
    )


def test_rasterize_line_strokes_along_the_path():
    """Sanity: line mode does mark pixels ON the line itself."""
    bbox = (0.0, 0.0, 1.0, 1.0)
    elements = {
        "cart_path": [way(31, [(0.1, 0.5), (0.9, 0.5)])],
    }
    # Width 50m is huge relative to the 1×1-deg bbox — guarantees the stroke
    # covers a few pixels at any reasonable image size.
    mask = bs.rasterize_layer(elements, "cart_path", bbox, 100,
                              geom="line", width_m=50.0)
    mid_x, mid_y = bs.lonlat_to_pixel(0.5, 0.5, bbox, 100)
    assert mask[mid_y, mid_x] == 255


# ---------- export_lines_geojson / export_polygons_geojson ----------


def test_export_lines_geojson_round_trip(tmp_path):
    elements = {
        "cart_path": [way(40, [(0, 0), (1, 1)], tags={"golf": "cartpath"})],
    }
    out = tmp_path / "cart_path.geojson"
    n = bs.export_lines_geojson(elements, "cart_path", out)
    assert n == 1
    fc = json.loads(out.read_text())
    assert fc["type"] == "FeatureCollection"
    feat = fc["features"][0]
    assert feat["geometry"]["type"] == "LineString"
    assert feat["properties"]["osm_way_id"] == 40
    assert feat["properties"]["osm_tags"] == {"golf": "cartpath"}


def test_export_polygons_geojson_round_trip(tmp_path):
    elements = {
        "water": [way(50, [(0, 0), (1, 0), (1, 1), (0, 0)], tags={"natural": "water"})],
    }
    out = tmp_path / "water.geojson"
    n = bs.export_polygons_geojson(elements, "water", out)
    assert n == 1
    fc = json.loads(out.read_text())
    feat = fc["features"][0]
    assert feat["geometry"]["type"] == "Polygon"
    assert feat["properties"]["osm_way_id"] == 50


def test_export_polygons_geojson_emits_closed_rings_even_for_open_ways(tmp_path):
    """Polygon export must always produce closed rings (UE WaterBody splines
    expect the first and last vertex to coincide).
    """
    elements = {
        "water": [way(51, [(0, 0), (1, 0), (1, 1)])],  # open
    }
    out = tmp_path / "water.geojson"
    bs.export_polygons_geojson(elements, "water", out)
    fc = json.loads(out.read_text())
    ring = fc["features"][0]["geometry"]["coordinates"][0]
    assert ring[0] == ring[-1]


# ---------- synthesized fairway corridors ----------


def test_point_in_ring_inside_and_outside():
    ring = [(0, 0), (2, 0), (2, 2), (0, 2), (0, 0)]
    assert bs._point_in_ring(1, 1, ring) is True
    assert bs._point_in_ring(3, 1, ring) is False
    assert bs._point_in_ring(-1, 1, ring) is False


def test_sample_polyline_endpoints_and_count():
    line = [(0.0, 0.0), (1.0, 0.0)]
    pts = bs._sample_polyline(line, 5)
    assert len(pts) == 5
    assert pts[0] == (0.0, 0.0)
    assert pts[-1] == (1.0, 0.0)
    # midpoint of a straight 2-vertex line
    assert math.isclose(pts[2][0], 0.5, abs_tol=1e-9)


def test_synthesize_corridor_ring_is_closed_and_has_width():
    """An east-west centerline buffered by 18m half-width should span ~36m
    (~3.3e-4 deg) in latitude and produce a closed ring."""
    line = [(-0.001, 40.75), (0.001, 40.75)]
    ring = bs.synthesize_corridor_ring(line, 18.0, 40.75)
    assert ring[0] == ring[-1]
    lats = [la for _, la in ring]
    assert (max(lats) - min(lats)) > 2e-4
    # near-zero width contribution in longitude for an E-W line is expected;
    # the ring should still extend along the centerline in longitude.
    lons = [lo for lo, _ in ring]
    assert (max(lons) - min(lons)) > 1e-3


# ---------- end-to-end build_splatmap ----------


def _synthetic_course_osm():
    """A minimal but complete fake OSM payload exercising every layer type."""
    return {"elements": [
        # Course outline (closed polygon)
        way(101, [(-0.9, 40.1), (0.9, 40.1), (0.9, 40.9), (-0.9, 40.9), (-0.9, 40.1)],
            tags={"leisure": "golf_course"}),
        # Fairway, green, bunker
        way(102, [(-0.5, 40.3), (0.5, 40.3), (0.5, 40.7), (-0.5, 40.7), (-0.5, 40.3)],
            tags={"golf": "fairway"}),
        way(103, [(0.0, 40.5), (0.1, 40.5), (0.1, 40.6), (0.0, 40.6), (0.0, 40.5)],
            tags={"golf": "green"}),
        way(104, [(-0.3, 40.4), (-0.25, 40.4), (-0.25, 40.45), (-0.3, 40.45), (-0.3, 40.4)],
            tags={"golf": "bunker"}),
        # Cart path (open linestring) — exercises line+geojson path
        way(105, [(-0.5, 40.5), (-0.4, 40.5), (-0.4, 40.6)],
            tags={"golf": "cartpath"}),
        # Water polygon — exercises polygon+emit_geojson path
        way(106, [(0.3, 40.4), (0.35, 40.4), (0.35, 40.45), (0.3, 40.45), (0.3, 40.4)],
            tags={"natural": "water"}),
    ]}


def test_build_splatmap_produces_expected_files(tmp_path):
    bbox = (-1.0, 40.0, 1.0, 41.0)
    out_dir = tmp_path / "fake-course"
    bs.build_splatmap(_synthetic_course_osm(), bbox, 256, out_dir)

    assert (out_dir / "splatmap.png").exists()
    assert (out_dir / "splatmap.json").exists()
    for layer in ("fairway", "green", "bunker", "rough"):
        assert (out_dir / f"splat_{layer}.png").exists()
    assert (out_dir / "layer_cart_path.png").exists()
    assert (out_dir / "cart_path.geojson").exists()
    # Water is geojson-only (skip_raster=True); see test_skip_raster_*.
    assert not (out_dir / "layer_water.png").exists()
    assert (out_dir / "water.geojson").exists()
    # Fairway/green emit BOTH a splat channel and a geojson sidecar (vector
    # consumers: pin-at-green-centroid, green/fairway conforming on the course).
    assert (out_dir / "fairway.geojson").exists()
    assert (out_dir / "green.geojson").exists()


def test_fairway_and_green_emit_geojson_sidecars(tmp_path):
    """fairway (channel 0) and green (channel 1) are channel-bearing polygon
    layers — the extras loop skips channel layers, so their geojson must be
    emitted from the core block. Freeze the sidecar shape vector consumers rely on.
    """
    bbox = (-1.0, 40.0, 1.0, 41.0)
    out_dir = tmp_path / "c"
    bs.build_splatmap(_synthetic_course_osm(), bbox, 256, out_dir)
    for name in ("fairway", "green"):
        fc = json.loads((out_dir / f"{name}.geojson").read_text())
        assert fc["type"] == "FeatureCollection"
        assert len(fc["features"]) >= 1
        feat = fc["features"][0]
        assert feat["geometry"]["type"] == "Polygon"
        # closed ring
        ring = feat["geometry"]["coordinates"][0]
        assert ring[0] == ring[-1]
        # real OSM features are not flagged synthesized
        assert feat["properties"]["synthesized"] is False


def test_build_splatmap_splatmap_is_4_channel_rgba_with_data(tmp_path):
    bbox = (-1.0, 40.0, 1.0, 41.0)
    out_dir = tmp_path / "fake-course"
    bs.build_splatmap(_synthetic_course_osm(), bbox, 256, out_dir)

    sp = np.array(Image.open(out_dir / "splatmap.png"))
    assert sp.shape == (256, 256, 4)
    for i, layer in enumerate(("fairway", "green", "bunker", "rough")):
        assert (sp[:, :, i] > 0).sum() > 0, f"channel {layer} is empty"


def test_implicit_rough_does_not_overlap_explicit_features(tmp_path):
    """The 'rough' channel is the catch-all inside the course polygon minus
    fairway/green/bunker. It must not double-paint pixels covered by those.
    """
    bbox = (-1.0, 40.0, 1.0, 41.0)
    osm = {"elements": [
        way(201, [(-0.5, 40.4), (0.5, 40.4), (0.5, 40.6), (-0.5, 40.6), (-0.5, 40.4)],
            tags={"leisure": "golf_course"}),
        way(202, [(-0.1, 40.45), (0.1, 40.45), (0.1, 40.55), (-0.1, 40.55), (-0.1, 40.45)],
            tags={"golf": "fairway"}),
    ]}
    out_dir = tmp_path / "test"
    bs.build_splatmap(osm, bbox, 200, out_dir)

    rough = np.array(Image.open(out_dir / "splat_rough.png"))
    fairway = np.array(Image.open(out_dir / "splat_fairway.png"))
    overlap = (rough > 0) & (fairway > 0)
    assert overlap.sum() == 0


def test_skip_raster_suppresses_layer_png_but_keeps_geojson(tmp_path, monkeypatch):
    """When FEATURE_LAYERS[layer]['skip_raster'] is True, no layer_<name>.png
    should be emitted, but the GeoJSON sidecar still must be (water consumer
    is the DynamicMesh actor script, not the painted material).
    """
    bbox = (-1.0, 40.0, 1.0, 41.0)
    osm = {"elements": [
        way(401, [(-0.5, 40.4), (0.5, 40.4), (0.5, 40.6), (-0.5, 40.6), (-0.5, 40.4)],
            tags={"leisure": "golf_course"}),
        way(402, [(0.3, 40.45), (0.35, 40.45), (0.35, 40.5), (0.3, 40.5), (0.3, 40.45)],
            tags={"natural": "water"}),
    ]}
    out_dir = tmp_path / "test"
    bs.build_splatmap(osm, bbox, 200, out_dir)

    assert not (out_dir / "layer_water.png").exists(), (
        "water has skip_raster=True; the painted PNG must NOT be emitted"
    )
    assert (out_dir / "water.geojson").exists(), (
        "water has emit_geojson=True; the sidecar must still be emitted"
    )
    fc = json.loads((out_dir / "water.geojson").read_text())
    assert len(fc["features"]) == 1


def test_hole_layer_emits_geojson_with_metadata_and_no_raster(tmp_path):
    """`golf=hole` features are linestrings carrying per-hole metadata
    (par, handicap, name, ref). They must:

    - emit a `hole.geojson` sidecar with osm_way_id + osm_tags preserved
      (the game-loop/scorecard consumer needs par/handicap/ref/name);
    - NOT emit a `layer_hole.png` (the centerline is not paintable terrain);
    - NOT contaminate any splatmap channel.

    This is the GOL-33 contract for under-mapped courses where fairway
    polygons aren't drawn in OSM but per-hole tee→green centerlines are.
    """
    bbox = (-1.0, 40.0, 1.0, 41.0)
    osm = {"elements": [
        way(501, [(-0.5, 40.4), (0.5, 40.4), (0.5, 40.6), (-0.5, 40.6), (-0.5, 40.4)],
            tags={"leisure": "golf_course"}),
        way(502, [(-0.2, 40.5), (-0.1, 40.52), (0.0, 40.5), (0.1, 40.48), (0.2, 40.5)],
            tags={"golf": "hole", "par": "4", "handicap": "7", "ref": "3", "name": "Long Iron"}),
    ]}
    out_dir = tmp_path / "test"
    bs.build_splatmap(osm, bbox, 200, out_dir)

    # Raster suppressed (skip_raster=True; linestring centerlines aren't terrain).
    assert not (out_dir / "layer_hole.png").exists(), (
        "hole has skip_raster=True; no painted PNG must be emitted"
    )
    # GeoJSON sidecar present with metadata.
    assert (out_dir / "hole.geojson").exists(), (
        "hole has emit_geojson=True; sidecar must be emitted"
    )
    fc = json.loads((out_dir / "hole.geojson").read_text())
    assert len(fc["features"]) == 1
    feat = fc["features"][0]
    assert feat["geometry"]["type"] == "LineString"
    assert feat["properties"]["osm_way_id"] == 502
    # Per-hole metadata must survive end-to-end (this is the contract the
    # engine relies on for scorecard + future tee/pin placement).
    tags = feat["properties"]["osm_tags"]
    assert tags["par"] == "4"
    assert tags["handicap"] == "7"
    assert tags["ref"] == "3"
    assert tags["name"] == "Long Iron"
    # The hole linestring itself must not leak/flood-fill into green or bunker.
    for ch_layer in ("green", "bunker"):
        ch = np.array(Image.open(out_dir / f"splat_{ch_layer}.png"))
        assert ch.sum() == 0, f"hole linestring leaked into splat_{ch_layer}.png"
    # The fairway channel now carries a *synthesized* corridor for this otherwise
    # fairway-less hole (the under-mapped-course fallback), but it must be a thin
    # corridor — not a flood-filled bounding region.
    fairway = np.array(Image.open(out_dir / "splat_fairway.png"))
    painted_frac = (fairway > 0).sum() / fairway.size
    assert painted_frac < 0.15, "fairway corridor flood-filled instead of a thin strip"
    fc_fw = json.loads((out_dir / "fairway.geojson").read_text())
    assert any(f["properties"].get("synthesized") for f in fc_fw["features"])


def test_synthesized_corridor_for_uncovered_hole(tmp_path):
    """A hole with no OSM fairway gets one synthesized corridor, flagged
    synthesized, and rasterized into the fairway channel. Uses a realistic
    ~1.6km bbox so the ~36m corridor spans several pixels.
    """
    bbox = BBOX_GOLFFORGE_DEMO
    osm = {"elements": [
        way(601, [(-73.4555, 40.7423), (-73.4345, 40.7423), (-73.4345, 40.7571),
                  (-73.4555, 40.7571), (-73.4555, 40.7423)],
            tags={"leisure": "golf_course"}),
        way(602, [(-73.452, 40.745), (-73.448, 40.749)],
            tags={"golf": "hole", "par": "4", "ref": "1"}),
    ]}
    out_dir = tmp_path / "c"
    bs.build_splatmap(osm, bbox, 512, out_dir)

    fc = json.loads((out_dir / "fairway.geojson").read_text())
    synth = [f for f in fc["features"] if f["properties"].get("synthesized")]
    assert len(synth) == 1
    assert synth[0]["properties"]["osm_tags"].get("source_hole_ref") == "1"
    fw = np.array(Image.open(out_dir / "splat_fairway.png"))
    assert (fw > 0).sum() > 0, "synthesized corridor was not rasterized"


def test_existing_fairway_suppresses_synthesis(tmp_path):
    """When an OSM fairway already covers the hole centerline, no corridor
    is synthesized."""
    bbox = BBOX_GOLFFORGE_DEMO
    osm = {"elements": [
        way(701, [(-73.4555, 40.7423), (-73.4345, 40.7423), (-73.4345, 40.7571),
                  (-73.4555, 40.7571), (-73.4555, 40.7423)],
            tags={"leisure": "golf_course"}),
        way(702, [(-73.453, 40.744), (-73.447, 40.744), (-73.447, 40.750),
                  (-73.453, 40.750), (-73.453, 40.744)],
            tags={"golf": "fairway"}),
        way(703, [(-73.452, 40.745), (-73.448, 40.749)],
            tags={"golf": "hole", "par": "4", "ref": "1"}),
    ]}
    out_dir = tmp_path / "c"
    bs.build_splatmap(osm, bbox, 512, out_dir)

    fc = json.loads((out_dir / "fairway.geojson").read_text())
    assert not any(f["properties"].get("synthesized") for f in fc["features"])


def test_shared_track_geometry_deduped(tmp_path):
    """Holes that share identical geometry across tracks (Black/Red/Green)
    must yield only ONE synthesized corridor, not one per track."""
    bbox = BBOX_GOLFFORGE_DEMO
    coords = [(-73.452, 40.745), (-73.448, 40.749)]
    osm = {"elements": [
        way(801, [(-73.4555, 40.7423), (-73.4345, 40.7423), (-73.4345, 40.7571),
                  (-73.4555, 40.7571), (-73.4555, 40.7423)],
            tags={"leisure": "golf_course"}),
        way(802, coords, tags={"golf": "hole", "par": "4", "ref": "1", "name": "Black 1"}),
        way(803, coords, tags={"golf": "hole", "par": "4", "ref": "1", "name": "Red 1"}),
    ]}
    out_dir = tmp_path / "c"
    bs.build_splatmap(osm, bbox, 512, out_dir)

    fc = json.loads((out_dir / "fairway.geojson").read_text())
    synth = [f for f in fc["features"] if f["properties"].get("synthesized")]
    assert len(synth) == 1


def test_rough_does_not_paint_outside_the_course_outline(tmp_path):
    bbox = (-1.0, 40.0, 1.0, 41.0)
    osm = {"elements": [
        way(301, [(-0.5, 40.4), (0.5, 40.4), (0.5, 40.6), (-0.5, 40.6), (-0.5, 40.4)],
            tags={"leisure": "golf_course"}),
    ]}
    out_dir = tmp_path / "test"
    bs.build_splatmap(osm, bbox, 200, out_dir)

    rough = np.array(Image.open(out_dir / "splat_rough.png"))
    # A pixel at (lon=-0.9, lat=40.1) is well outside the course outline.
    edge_x, edge_y = bs.lonlat_to_pixel(-0.9, 40.1, bbox, 200)
    assert rough[edge_y, edge_x] == 0
