#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TESTS_DIR="$(dirname "$SCRIPT_DIR")"
VENV_DIR="$TESTS_DIR/.venv"

python3 -m venv "$VENV_DIR"
"$VENV_DIR/bin/pip" install --upgrade pip
"$VENV_DIR/bin/pip" install -r "$TESTS_DIR/requirements.txt"
echo "venv ready at $VENV_DIR"
