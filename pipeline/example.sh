#!/usr/bin/env bash
# Sample pipeline run against a small test bbox.
#
# Default course is GolfForge Demo Coast holes 7–9 (small, well-tagged in OSM, dramatic terrain).
# Override by passing your own bbox + course-id.

set -euo pipefail

COURSE_ID="${COURSE_ID:-golfforge-demo-coast}"
BBOX="${BBOX:--121.9530,36.5660,-121.9370,36.5800}"  # minlon,minlat,maxlon,maxlat
SIZE="${SIZE:-4033}"
BACKEND="${BACKEND:-opentopo}"
OUT_DIR="${OUT_DIR:-../courses}"

# build_heightmap.py needs OPENTOPO_API_KEY (or --opentopo-key) for the default
# backend. Fail fast with a friendly message rather than reaching the API call.
if [[ "$BACKEND" == "opentopo" && -z "${OPENTOPO_API_KEY:-}" ]]; then
    echo "OPENTOPO_API_KEY is not set." >&2
    echo "  Get a free key at https://portal.opentopography.org/myopentopo, then:" >&2
    echo "    export OPENTOPO_API_KEY=<your-key>" >&2
    echo "  See pipeline/README.md for details." >&2
    exit 2
fi

# NOTE: --bbox-wgs84=... uses '=' notation because negative longitudes start with '-'
# and argparse would otherwise interpret them as another flag.

echo "Building heightmap..."
python3 build_heightmap.py \
    --course-id "$COURSE_ID" \
    --bbox-wgs84="$BBOX" \
    --size "$SIZE" \
    --backend "$BACKEND" \
    --out-dir "$OUT_DIR"

echo
echo "Building splatmap..."
python3 build_splatmap.py \
    --course-id "$COURSE_ID" \
    --bbox-wgs84="$BBOX" \
    --size "$SIZE" \
    --out-dir "$OUT_DIR"

echo
echo "Done. Outputs in: $OUT_DIR/$COURSE_ID/"
echo "  heightmap.png + heightmap.json"
echo "  splatmap.png  + splatmap.json"
echo "  layer_*.png   (extras)"
echo
echo "Import heightmap.png into UE5 as a Landscape (16-bit grayscale),"
echo "use the Z scale from heightmap.json,"
echo "import splatmap.png as a Landscape weightmap with the RGBA legend in splatmap.json."
