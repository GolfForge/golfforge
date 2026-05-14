#!/usr/bin/env bash
# pipeline/setup.sh — one-shot Python env setup for the data pipeline.
#
# Detects `uv` (preferred — much faster, better dependency resolver) and falls
# back to stdlib `python3 -m venv` + `pip`. Run from anywhere; idempotent.
#
#   ./setup.sh
#   source .venv/bin/activate
#   ./example.sh

set -euo pipefail

cd "$(dirname "$0")"

VENV_DIR=".venv"
REQS="requirements.txt"

if [ ! -f "$REQS" ]; then
    echo "ERROR: $REQS not found in $(pwd)" >&2
    exit 1
fi

if command -v uv >/dev/null 2>&1; then
    echo ">>> uv detected — using fast path."
    uv venv "$VENV_DIR"
    uv pip install --python "$VENV_DIR/bin/python" -r "$REQS"
else
    echo ">>> uv not found — falling back to python3 -m venv + pip."
    echo ">>> (Install uv with 'brew install uv' for ~10x faster setup.)"
    python3 -m venv "$VENV_DIR"
    "$VENV_DIR/bin/pip" install --upgrade pip
    "$VENV_DIR/bin/pip" install -r "$REQS"
fi

echo
echo "------------------------------------------------------------"
echo " Pipeline Python env is ready."
echo
echo " Activate it:   source $(pwd)/$VENV_DIR/bin/activate"
echo " Run pipeline:  ./example.sh"
echo "------------------------------------------------------------"
