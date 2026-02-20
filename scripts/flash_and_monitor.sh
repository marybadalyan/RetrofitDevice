#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "Usage: $0 <env_name> [monitor_baud]"
  echo "Example: $0 retrofit 115200"
  exit 1
fi

ENV_NAME="$1"
BAUD="${2:-115200}"

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT_DIR"

mkdir -p artifacts

echo "[LOCAL] Uploading environment: ${ENV_NAME}"
pio run -e "$ENV_NAME" -t upload

echo "[LOCAL] Opening serial monitor (Ctrl+C to stop)..."
pio device monitor -b "$BAUD" | tee "artifacts/${ENV_NAME}-serial.log"
