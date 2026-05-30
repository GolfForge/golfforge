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
./setup.sh
source .venv/bin/activate
```

`setup.sh` auto-detects [uv](https://github.com/astral-sh/uv) (preferred — much faster) and falls back to `python3 -m venv` + `pip` if it isn't installed. Install uv once with `brew install uv` to get the fast path.

For the higher-quality `--backend pdal` path you also need PDAL installed system-wide:
```bash
brew install pdal gdal       # macOS
pip install pdal             # python bindings
```

## OpenTopography API key (required for the default backend)

The default `opentopo` backend fetches DEMs from OpenTopography, which requires a free
per-contributor API key — there's no shared default. One-time setup:

1. Sign up at https://portal.opentopography.org/myopentopo and request an API key
   ("MyOpenTopo" → "Request an API Key"). It's free and usually instant.
2. Export the key in your shell so every pipeline run picks it up:
   ```bash
   export OPENTOPO_API_KEY=<your-key>
   # Persist it: add the line to ~/.zshrc, ~/.bashrc, or equivalent.
   ```

You can also pass it per-run with `--opentopo-key <your-key>`, which wins over the env var.

If neither is set, `build_heightmap.py` exits with a friendly message pointing at this setup.

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

The `opentopo` backend needs an OpenTopography API key — see the setup section above.

For the `pdal` backend you also need the EPT (Entwine Point Tile) URL of the USGS 3DEP project covering your bbox — look it up at https://usgs.entwine.io/.

## UE5 import quick reference

Once `heightmap.png` exists, in UE5:

1. Create a Landscape; import heightmap as the height source.
2. Set Landscape `RelativeScale3D.Z` to the `ue5_z_scale_pct` value in `heightmap.json` (the script computes this for you).
3. Create a Landscape Material with at least 4 weight layers: fairway, green, bunker, rough.
4. Import `splatmap.png` as a weightmap; UE5 will detect the 4 channels.
5. Assign layer 0 (R) = fairway, 1 (G) = green, 2 (B) = bunker, 3 (A) = rough.
