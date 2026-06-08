# Pipeline → engine data contract

Reference for the UE-side agent: every file the Python pipeline writes into `courses/<id>/`, and how to consume it. **This contract is stable** — when it changes, this file changes in the same commit.

## Terrain

| File | Format | Consumer |
|---|---|---|
| `heightmap.png` | 16-bit grayscale PNG, square dims (UE5-friendly: 505 / 1009 / 2017 / 4033 / 8129) | Landscape > Manage > New > Import from File |
| `heightmap.json` | `{elev_min_m, elev_max_m, elev_range_m, ue5_z_scale_pct, bbox_wgs84, size_px, backend, course_id}` | Set Landscape `RelativeScale3D.Z` from `ue5_z_scale_pct`; the rest is provenance |

## Splatmap (weight-paint for materials)

| File | Format | Consumer |
|---|---|---|
| `splatmap.png` | RGBA 8-bit (R=fairway, G=green, B=bunker, A=rough) | Single import covering all 4 layers if your material supports RGBA splat |
| `splatmap.json` | Channel legend `{R, G, B, A → layer name}` + `size_px` + `bbox_wgs84` | Sanity-check channel→layer mapping |
| `splat_{fairway,green,bunker,rough}.png` | One 8-bit grayscale per layer | UE5.7 Landscape > Manage > Import "Layers" array (one PNG per row) — this is the path that actually works in 5.7 |
| `fairway.geojson` | Polygon FeatureCollection | Vector form of the fairway areas (alongside the raster channel). Includes **synthesized** fallback corridors (see below). Use to conform fairway rendering to real shapes. |
| `green.geojson` | Polygon FeatureCollection | Vector form of the green areas. Use to place the pin at the **green centroid** (vs the `hole.geojson` centerline endpoint) and conform the green to its real shape instead of a synthetic disc. |

**Synthesized fairway corridors:** OSM coverage is uneven — some holes (par 3s especially) have no `golf=fairway` polygon, so they'd render fairway-less. For any hole whose tee→green centerline isn't already covered by an OSM fairway, the pipeline buffers the centerline into a corridor polygon and adds it to both `splat_fairway.png` and `fairway.geojson`. These carry `properties.synthesized = true` (and `osm_tags.synthesized = "yes"`, `osm_tags.source_hole_ref = <hole ref>`, `osm_way_id = null`) so a consumer can treat them differently (e.g. de-emphasize, or prefer real OSM data). Real OSM features have `synthesized = false`. Detection threshold + corridor widths live in `build_splatmap.py` constants (`SYNTH_FAIRWAY_*`) and are tunable; validate with the QA overlays.

## Extras (one per non-core feature type)

For every non-core feature type, the pipeline may emit a raster mask, a vector sidecar, or both. A layer can also opt out of raster emission entirely (`skip_raster=True`) when it's meant to be consumed only as 3D actors.

| Layer | Raster | GeoJSON | Suggested UE consumer |
|---|---|---|---|
| `tee` | `layer_tee.png` | — | Weight-paint material layer |
| `water` | — (skip_raster) | `water.geojson` (Polygon) | Flat translucent `DynamicMeshActor` per Feature (see `engine/scripts/build_water_actors.py`) |
| `cart_path` | `layer_cart_path.png` | `cart_path.geojson` (LineString) | Weight-paint **or** `LandscapeSplineActor` per Feature. Aggregates `golf=cartpath`, `highway=path`, `highway=service` |
| `trees` | `layer_trees.png` | — | Weight-paint → PCG SurfaceSampler with `AttributeFiltering` on the weight |
| `hole` | — (skip_raster) | `hole.geojson` (LineString) | Per-hole tee→green centerline (`golf=hole`). Drives scorecard, hole selector, opening camera fly-in. `osm_tags` carries `par`, `handicap`, `ref` (hole number), `name`. Survives even when OSM is missing fairway polygons — the only source of truth for hole metadata on under-mapped courses |

## GeoJSON sidecar shape (one canonical form for both line and polygon)

Both `cart_path.geojson` and `water.geojson` (and any future vector layer) use the same envelope:

```json
{
  "type": "FeatureCollection",
  "features": [
    {
      "type": "Feature",
      "properties": {
        "osm_way_id": 565589242,
        "osm_tags": { "natural": "water", "name": "Nassau County Basin #441" }
      },
      "geometry": {
        "type": "LineString" | "Polygon",
        "coordinates": [...]
      }
    }
  ]
}
```

Coordinates are WGS84 lon/lat. Polygon rings are guaranteed closed (first vertex == last vertex). Properties always have `osm_way_id` and `osm_tags` — features without those are a bug. **Polygon** features additionally carry `synthesized` (bool) — `true` only for pipeline-generated geometry (currently fallback fairway corridors); `osm_way_id` is `null` for those.

## QA overlays (visual check — not committed)

`build_qa_overlay.py` renders two images per course for eyeballing that derived features land in the right place:

| File | Basemap | Use |
|---|---|---|
| `qa_overlay_aerial.png` | Esri World Imagery (satellite) | Check features against the real ground (catches both pipeline bugs and wrong/missing OSM data) |
| `qa_overlay_osm.png` | OpenStreetMap rendered tiles | Check our lon/lat→pixel alignment against OSM's own rendering |

Each draws the derived layers (fairway/green/bunker/rough/tee/water/cart_path, including synthesized corridors) as semi-transparent fills + per-hole centerlines and numbers. Basemap tiles (Web Mercator) are reprojected onto our equirectangular grid so the overlay is trustworthy for alignment. **Both are gitignored** (they embed third-party basemap imagery and are regenerable); tiles cache under `_tilecache/` (also gitignored). Runs after `build_splatmap.py` in `example.sh`; `SKIP_TILES=1` (or `--skip-tiles`) emits a masks-only version offline.

**Recommended UE-side helper**: a single `load_layer_geojson(course_id, layer_name) -> List[FeatureDict]` function works for every current and future vector layer. The conversion from WGS84 lon/lat to landscape-local UE units needs to be done once per course (it's just an affine derived from `heightmap.json.bbox_wgs84` + the landscape's world transform).

## Adding a new layer (pipeline side)

Edit `pipeline/build_splatmap.py`:

- **New linear feature** (e.g., fences, hole boundaries): add an entry to `FEATURE_LAYERS` with `"geom": "line"` and a `"width_m": <float>`. The GeoJSON sidecar is automatic.
- **New polygon feature you want as vectors** (e.g., greens-with-pin-position): add `"emit_geojson": True` to its `FEATURE_LAYERS` entry.
- **New polygon feature that's only a weight-paint mask** (e.g., a new ground-cover type): just add it without `emit_geojson`.
- **New polygon feature consumed only as 3D actors** (no painted layer): add `"emit_geojson": True` and `"skip_raster": True` — water uses this pattern.

Cover the new behavior with a test in `pipeline/tests/test_build_splatmap.py` if it touches `rasterize_layer` or either exporter. The end-to-end `test_build_splatmap_produces_expected_files` test is the natural place to assert that any new file you expect actually shows up.
