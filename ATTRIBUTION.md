# Data attribution

GolfForge's course data is derived from open geographic data sources. Each source carries its own
license and attribution requirement, independent of this project's own code license
(`LICENSE` / `COMMERCIAL.md`) and of the third-party engine assets (`docs/ue5-cookbook.md`).

Packaged builds and any public reuse of the course data must carry the attributions below.

## OpenStreetMap — golf course features

The course feature geometry (fairways, greens, bunkers, tees, fairway/rough edges, water, cart
paths) is extracted from **OpenStreetMap** via the Overpass API (`pipeline/build_splatmap.py`).

- **Attribution:** © OpenStreetMap contributors — https://www.openstreetmap.org/copyright
- **License:** Open Database License (ODbL) v1.0 — https://opendatacommons.org/licenses/odbl/1-0/

ODbL obligations as they apply here:

- The per-course `courses/<id>/*.geojson` files are a **Derivative Database** of OpenStreetMap
  (extracted features, with `osm_way_id` + `osm_tags` provenance preserved). We make them available
  under the **ODbL** (share-alike), the same license as the source.
- The rasterized **splatmap PNGs** derived from that geometry are **Produced Works** under the ODbL,
  which require **attribution** but are not themselves subject to share-alike.
- Anyone redistributing the course data or a build containing it must keep the "© OpenStreetMap
  contributors" attribution and the ODbL notice.

## Elevation data — heightmaps

Heightmaps are built from public-domain elevation data (`pipeline/build_heightmap.py`):

- **USGS 3DEP** (3D Elevation Program) — 10 m / 30 m DEMs, and 3DEP LiDAR point clouds. As a work of
  the U.S. Government, this is in the **public domain**. Courtesy citation: U.S. Geological Survey,
  3D Elevation Program. https://www.usgs.gov/3d-elevation-program
- **SRTM** (Shuttle Radar Topography Mission, datasets `SRTMGL1` / `SRTMGL3`) — NASA / USGS,
  **public domain**.

Access methods used by the pipeline:

- **OpenTopography** REST API (default elevation backend) — an NSF-funded data facility. No license
  restriction on the underlying public-domain DEMs, but please **acknowledge OpenTopography** and
  cite the relevant dataset DOI when reusing data fetched through it. https://opentopography.org
- **AWS Entwine / USGS 3DEP** point-cloud tiles (alternate `pdal` backend) — https://usgs.entwine.io/

## Audio — ambient SFX (GOL-166)

The ambient sound layer (birdsong + the optional wind/distant-traffic beds) imported into
`engine/Golfsim/Content/Audio/Ambient/` (sources kept in `_src/`) is sourced from **BigSoundBank**
(Joseph SARDIN), released under a **public-domain-equivalent license (CC0)** — free for commercial
and personal use, no attribution required, redistribution allowed. License:
https://bigsoundbank.com/licenses.html. CC0 was chosen specifically so the clips can live in this
AGPL repo and ship in builds without attribution friction (unlike Fab/Marketplace audio, whose EULA
forbids redistributing the raw assets).

Courtesy credit (not legally required) for the clips in use:

| Asset | Source clip | URL |
|---|---|---|
| `SW_AmbientBird_Forest` | Forest (s0100) | https://bigsoundbank.com/forest-s0100.html |
| `SW_AmbientBird_Countryside` | Countryside (s0097) | https://bigsoundbank.com/countryside-s0097.html |
| `SW_AmbientBird_ForestEdge` | Forest: On the Edge (s0905) | https://bigsoundbank.com/forest-on-the-edge-s0905.html |
| `SW_AmbientWind_Trees` | Wind in the Trees (s0904) | https://bigsoundbank.com/forest-wind-in-the-trees-s0904.html |
| `SW_AmbientMurmur_Distant` | Parisian Ring Road #2 (s3027) | https://bigsoundbank.com/parisian-ring-road-2-s3027.html |

## Carrying attribution in distributed builds

A packaged GolfForge build that ships course data must surface the OpenStreetMap attribution and
ODbL notice in a reasonable place (for example, an in-app credits/attribution screen). This is a
release requirement tracked alongside packaging (Linear GOL-49).
