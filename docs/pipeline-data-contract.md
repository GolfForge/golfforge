# Pipeline → engine data contract

Reference for the UE-side agent: every file the Python pipeline writes into `courses/<id>/`, and how to consume it. **This contract is stable** — when it changes, this file changes in the same commit.

## Terrain

| File | Format | Consumer |
|---|---|---|
| `heightmap.png` | 16-bit grayscale PNG, square dims (UE5-friendly: 505 / 1009 / 2017 / 4033 / 8129) | Landscape > Manage > New > Import from File. The pristine raw-DEM baseline — also the idempotency source for `build_bunker_depressions.py` (never overwritten) |
| `heightmap.json` | `{elev_min_m, elev_max_m, elev_range_m, ue5_z_scale_pct, bbox_wgs84, size_px, backend, course_id}` | Set Landscape `RelativeScale3D.Z` from `ue5_z_scale_pct`; the rest is provenance |
| `heightmap_bunkers.png` | 16-bit grayscale PNG, **same dims + same `elev_min->0 / elev_max->65535` encoding** as `heightmap.png` | The UE import target **once bunker sculpting has been run** (`pipeline/build_bunker_depressions.py` — depressed sand floors + raised lips, GOL-34). Re-import **heightmap-only** (Landscape > Manage > Import, Layers untouched); the encoding is unchanged so **Z scale stays the same**. Absent until the script is run |

## Splatmap (weight-paint for materials)

| File | Format | Consumer |
|---|---|---|
| `splatmap.png` | RGBA 8-bit (R=fairway, G=green, B=bunker, A=rough) | Single import covering all 4 layers if your material supports RGBA splat |
| `splatmap.json` | Channel legend `{R, G, B, A → layer name}` + `size_px` + `bbox_wgs84` | Sanity-check channel→layer mapping |
| `splat_{fairway,green,bunker,rough}.png` | One 8-bit grayscale per layer | UE5.7 Landscape > Manage > Import "Layers" array (one PNG per row) — this is the path that actually works in 5.7 |
| `fairway.geojson` | Polygon FeatureCollection | Vector form of the fairway areas (alongside the raster channel). Includes **synthesized** fallback corridors (see below). Use to conform fairway rendering to real shapes. |
| `green.geojson` | Polygon FeatureCollection | Vector form of the green areas. Use to place the pin at the **green centroid** (vs the `hole.geojson` centerline endpoint) and conform the green to its real shape instead of a synthetic disc. |
| `bunker.geojson` | Polygon FeatureCollection | Vector form of the bunkers. Consumed by `pipeline/build_bunker_depressions.py` (GOL-34) to sculpt the heightmap (depression + raised lip) into believable sand traps → emits `heightmap_bunkers.png` (above). Rings rasterize with the *same* `lonlat_to_pixel` transform as the splatmap, so the sculpt aligns pixel-perfect with `splat_bunker.png`. |

**Synthesized fairway corridors:** OSM coverage is uneven — some holes (par 3s especially) have no `golf=fairway` polygon, so they'd render fairway-less. For any hole whose tee→green centerline isn't already covered by an OSM fairway, the pipeline buffers the centerline into a corridor polygon and adds it to both `splat_fairway.png` and `fairway.geojson`. These carry `properties.synthesized = true` (and `osm_tags.synthesized = "yes"`, `osm_tags.source_hole_ref = <hole ref>`, `osm_way_id = null`) so a consumer can treat them differently (e.g. de-emphasize, or prefer real OSM data). Real OSM features have `synthesized = false`. Detection threshold + corridor widths live in `build_splatmap.py` constants (`SYNTH_FAIRWAY_*`) and are tunable; validate with the QA overlays.

**Mutually exclusive (weight-blend-safe) masks (GOL-171):** every raster mask — the `splatmap.png` channels, `splat_*.png`, and the extras `layer_*.png` below — is **priority-resolved so each pixel belongs to exactly one layer** (order bunker > green > tee > fairway > cart_path > trees > rough, matching the engine lie-classifier `CourseSurface.cpp`, `MaskThresh=128`). This is required for UE5 Landscape import: UE weight-**blends** layers and normalizes overlapping weights to a tie, which silently erases small features (greenside bunkers, greens) wherever layers overlapped. Because of this, `splat_rough.png` is the **exclusive** rough (≈27% on the demo course), *not* the full inside-the-course mask it used to be. Since the exclusion mirrors the classifier's first-match-wins, the runtime ball-lie is byte-identical whether it samples these or the old overlapping masks. Implemented by `resolve_layer_overlap()`.

## Extras (one per non-core feature type)

For every non-core feature type, the pipeline may emit a raster mask, a vector sidecar, or both. A layer can also opt out of raster emission entirely (`skip_raster=True`) when it's meant to be consumed only as 3D actors.

| Layer | Raster | GeoJSON | Suggested UE consumer |
|---|---|---|---|
| `tee` | `layer_tee.png` | — | Weight-paint material layer |
| `water` | — (skip_raster) | `water.geojson` (Polygon) | Flat translucent `DynamicMeshActor` per Feature (see `engine/scripts/build_water_actors.py`) |
| `cart_path` | `layer_cart_path.png` | `cart_path.geojson` (LineString) | Weight-paint **or** `LandscapeSplineActor` per Feature. Aggregates `golf=cartpath`, `highway=path`, `highway=service` |
| `trees` | `layer_trees.png` | — | Weight-paint → PCG SurfaceSampler with `AttributeFiltering` on the weight |
| `hole` | — (skip_raster) | `hole.geojson` (LineString) | Per-hole tee→green centerline (`golf=hole`). Drives scorecard, hole selector, opening camera fly-in. `osm_tags` carries `par`, `handicap`, `ref` (hole number), `name`. Survives even when OSM is missing fairway polygons — the only source of truth for hole metadata on under-mapped courses |

## Minimap (HUD basemap, GOL-209)

| File | Format | Consumer |
|---|---|---|
| `minimap.png` | RGB 8-bit PNG, **same dims + same `bbox_wgs84` as `splatmap.png`** | The in-round HUD hole-map card (`UHoleMapView`). Colorized top-down composite of the layer masks (rough base, fairway/green/tee/bunker/cart_path/trees palette fills, water rasterized from `water.geojson`) multiplied by a heightmap hillshade. Because it shares the splatmap georeference, the engine maps world XY in `[-100800, +100800]` cm linearly onto pixel `[0, N-1]` — the same affine as `FCourseSurfaceSampler::ClassifyAt`. Built by `pipeline/build_minimap.py` (standalone, no network; runs after the heightmap + splatmap stages). Absent ⇒ the HUD falls back to a flat fill with markers only |

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

## Pin sheets (`pins/<id>.json` — named hole-location sets, GOL-191/192)

A pin sheet is a named set of per-hole pin (cup) positions the round can play instead of the static
`hole.geojson` green endpoint — the engine's **Tournament** pin mode loads `courses/<id>/pins/<PinSetId>.json`
(default id `default`). Shape:

```json
{
  "name": "Championship Pins",
  "bbox_wgs84": [minlon, minlat, maxlon, maxlat],
  "pins": [ { "hole_ref": 1, "lon": -73.4514, "lat": 40.7451 }, ... ]
}
```

Pin `lon`/`lat` are WGS84 and projected with the **course** bbox (`heightmap.json.bbox_wgs84`) — the
`bbox_wgs84` stored in the sheet is informational; the engine ignores it and uses the course's. One
pin per `hole_ref`. **Authoring:** `pipeline/build_pin_sheet.py --course-id <id>` seeds a baseline
sheet (each pin at its matched green's centroid, so every pin is guaranteed on-green); real event
sheets are hand-tweaked from that baseline toward published hole locations. Engine consumers:
`GolfsimRound::ParsePinSheetJson` / `LoadPinSheet` / `ResolvePinPositions`. Committed (small) and
staged with `courses/<id>/` in cooked builds.

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
