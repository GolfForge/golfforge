"""Tests for pipeline/build_qa_overlay.py.

All offline — no network. Covers the Web-Mercator tile math, zoom selection,
the equirectangular<-mercator reprojection orientation, and the masks-only
fallback end-to-end (with tile fetching monkeypatched out).

Run from `pipeline/` with:  python3 -m pytest tests/
"""

import json
import sys
from pathlib import Path

import numpy as np
import pytest
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import build_splatmap as bs  # noqa: E402
import build_qa_overlay as qa  # noqa: E402


BBOX_GOLFFORGE_DEMO = (-73.4555, 40.7423, -73.4345, 40.7571)


def way(id_, coords, tags=None):
    """Construct a synthetic OSM way element (mirrors the splatmap test helper)."""
    return {
        "type": "way",
        "id": id_,
        "tags": tags or {},
        "geometry": [{"lon": lon, "lat": lat} for lon, lat in coords],
    }


def _fake_stitched_for_bbox(bbox, z):
    """A synthetic stitched basemap covering the bbox tile grid at zoom z, with a
    left->right brightness gradient. Returns (PIL RGB image, gx0, gy0)."""
    minlon, minlat, maxlon, maxlat = bbox
    x0t, y0t = qa._lonlat_to_tilexy(minlon, maxlat, z)  # NW corner
    x1t, y1t = qa._lonlat_to_tilexy(maxlon, minlat, z)  # SE corner
    xmin, xmax = int(x0t), int(x1t)
    ymin, ymax = int(y0t), int(y1t)
    cols, rows = xmax - xmin + 1, ymax - ymin + 1
    w, h = cols * 256, rows * 256
    grad = np.tile(np.linspace(0, 255, w, dtype=np.uint8), (h, 1))
    rgb = np.stack([grad, grad, grad], axis=-1)
    return Image.fromarray(rgb, "RGB"), xmin * 256, ymin * 256


# ---------- Web Mercator tile math ----------


def test_lonlat_tilexy_roundtrip():
    lon, lat, z = -73.45, 40.75, 16
    xt, yt = qa._lonlat_to_tilexy(lon, lat, z)
    lon2, lat2 = qa._tilexy_to_lonlat(xt, yt, z)
    assert abs(lon - lon2) < 1e-6
    assert abs(lat - lat2) < 1e-6


def test_tilexy_x_increases_with_lon():
    z = 14
    x_west, _ = qa._lonlat_to_tilexy(-74.0, 40.75, z)
    x_east, _ = qa._lonlat_to_tilexy(-73.0, 40.75, z)
    assert x_east > x_west


def test_tilexy_y_increases_southward():
    """In XYZ tiles, y grows toward the south (top-left origin)."""
    z = 14
    _, y_north = qa._lonlat_to_tilexy(-73.45, 41.0, z)
    _, y_south = qa._lonlat_to_tilexy(-73.45, 40.0, z)
    assert y_south > y_north


def test_choose_zoom_monotonic_with_size():
    z_small = qa._choose_zoom(BBOX_GOLFFORGE_DEMO, 512)
    z_big = qa._choose_zoom(BBOX_GOLFFORGE_DEMO, 4000)
    assert z_big >= z_small


def test_choose_zoom_covers_requested_size():
    """The chosen zoom must give a mercator span of at least the (capped) size."""
    bbox = BBOX_GOLFFORGE_DEMO
    size = 1500
    z = qa._choose_zoom(bbox, size)
    x0, _ = qa._lonlat_to_tilexy(bbox[0], bbox[3], z)
    x1, _ = qa._lonlat_to_tilexy(bbox[2], bbox[1], z)
    assert (x1 - x0) * 256 >= min(size, qa.QA_MAX_CANVAS_PX)


# ---------- reprojection ----------


def test_reproject_shape_and_orientation():
    """Reprojecting a left->right brightness gradient must preserve W->E
    orientation on our equirectangular grid (west pixels darker than east)."""
    z = qa._choose_zoom(BBOX_GOLFFORGE_DEMO, 256)
    # Build a synthetic stitched basemap: a horizontal gradient over the tile grid
    # that exactly covers the bbox.
    stitched, gx0, gy0 = _fake_stitched_for_bbox(BBOX_GOLFFORGE_DEMO, z)
    out = qa.reproject_basemap_to_grid(stitched, gx0, gy0, z, BBOX_GOLFFORGE_DEMO, 256)
    assert out.shape == (256, 256, 4)
    west_col = out[:, 5, 0].mean()
    east_col = out[:, 250, 0].mean()
    assert east_col > west_col


# ---------- masks-only overlay (offline) ----------


def test_overlay_masks_only_when_skip_tiles(tmp_path, monkeypatch):
    """With tiles unavailable, both overlay PNGs are still written from a gray
    base + the derived masks (the offline/cached-run philosophy)."""
    # Build a small course so masks + geojson exist.
    bbox = (-1.0, 40.0, 1.0, 41.0)
    course_dir = tmp_path / "c"
    osm = {"elements": [
        way(901, [(-0.5, 40.4), (0.5, 40.4), (0.5, 40.6), (-0.5, 40.6), (-0.5, 40.4)],
               tags={"leisure": "golf_course"}),
        way(902, [(-0.3, 40.45), (0.3, 40.45), (0.3, 40.55), (-0.3, 40.55), (-0.3, 40.45)],
               tags={"golf": "fairway"}),
        way(903, [(-0.4, 40.5), (0.4, 40.5)], tags={"golf": "hole", "par": "4", "ref": "1"}),
    ]}
    bs.build_splatmap(osm, bbox, 128, course_dir)

    written = qa.build_overlays(course_dir, skip_tiles=True)
    assert (course_dir / "qa_overlay_aerial.png").exists()
    assert (course_dir / "qa_overlay_osm.png").exists()
    # Non-empty images of the right size.
    img = np.array(Image.open(course_dir / "qa_overlay_aerial.png").convert("RGB"))
    assert img.shape == (128, 128, 3)
    assert img.sum() > 0
