# pipeline — course data pipeline

Turns a geographic bounding box into UE5-ready inputs for one golf course or hole:

- `heightmap.png` — 16-bit grayscale terrain (Landscape import)
- `heightmap.json` — elevation range + suggested UE5 Landscape Z scale
- `splatmap.png` — 4-channel RGBA mask (R=fairway, G=green, B=bunker, A=rough)
- `splatmap.json` — channel legend
- `layer_*.png` — extras (tee, water, cart paths, trees) for additional UE5 material layers

Runs on Mac / Linux. Output ends up in `../courses/<course-id>/`.

## Install

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
```

(Optional, faster) Use [uv](https://github.com/astral-sh/uv): `uv venv && uv pip install -r requirements.txt`.

For the higher-quality `--backend pdal` path you also need PDAL installed system-wide:
```bash
brew install pdal gdal       # macOS
pip install pdal             # python bindings
```

## Use

```bash
./example.sh                  # runs the full pipeline against a test bbox
```

Or call the scripts directly:

```bash
python3 build_heightmap.py \
    --course-id my-course \
    --bbox-wgs84=-121.953,36.566,-121.937,36.580 \
    --size 4033 \
    --backend opentopo

python3 build_splatmap.py \
    --course-id my-course \
    --bbox-wgs84=-121.953,36.566,-121.937,36.580 \
    --size 4033
```

Note the `=` syntax for `--bbox-wgs84`. Negative longitudes start with `-` and argparse otherwise treats them as another flag.

## Backends

`build_heightmap.py` supports two LIDAR sources:

| Backend | Source | Speed | Quality | When to use |
|---|---|---|---|---|
| `opentopo` (default) | OpenTopography REST API | fast | lower (10–30m resolution) | iteration, prototyping, picking bbox |
| `pdal` | USGS 3DEP point cloud via AWS Entwine | slow | high (sub-meter) | building a course you actually care about |

The OpenTopography demo API key is rate-limited; get a free real key at portal.opentopography.org for sustained use.

For the `pdal` backend you also need the EPT (Entwine Point Tile) URL of the USGS 3DEP project covering your bbox — look it up at https://usgs.entwine.io/.

## UE5 import quick reference

Once `heightmap.png` exists, in UE5:

1. Create a Landscape; import heightmap as the height source.
2. Set Landscape `RelativeScale3D.Z` to the `ue5_z_scale_pct` value in `heightmap.json` (the script computes this for you).
3. Create a Landscape Material with at least 4 weight layers: fairway, green, bunker, rough.
4. Import `splatmap.png` as a weightmap; UE5 will detect the 4 channels.
5. Assign layer 0 (R) = fairway, 1 (G) = green, 2 (B) = bunker, 3 (A) = rough.
