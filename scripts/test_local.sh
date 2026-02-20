#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT_DIR"

mkdir -p artifacts

echo "[LOCAL] Running native tests via PlatformIO..."
pio test -e native_test -v | tee artifacts/test-output-local.log

echo "[LOCAL] Test logs saved to artifacts/test-output-local.log"
