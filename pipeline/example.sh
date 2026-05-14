#!/usr/bin/env bash
# Sample pipeline run against a small test bbox.
#
# Default course is Pebble Beach holes 7–9 (small, well-tagged in OSM, dramatic terrain).
# Override by passing your own bbox + course-id.

set -euo pipefail

COURSE_ID="${COURSE_ID:-pebble-beach-test}"
BBOX="${BBOX:--121.9530,36.5660,-121.9370,36.5800}"  # minlon,minlat,maxlon,maxlat
SIZE="${SIZE:-4033}"
BACKEND="${BACKEND:-opentopo}"
OUT_DIR="${OUT_DIR:-../courses}"

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
