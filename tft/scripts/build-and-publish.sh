#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="/Users/pawelwitkowski/Documents/New project"
LATEST_DIR="$PROJECT_DIR/artifacts/firmware/latest"
SONOFF_FW="$LATEST_DIR/sonoff4ch/firmware.bin"

cd "$PROJECT_DIR"

echo "===== BUILD + PUBLISH START ====="
"$SCRIPT_DIR/build-firmware-all.sh"
echo "===== BUILD + PUBLISH DONE ====="

[[ -f "$SONOFF_FW" ]] || {
  echo "ERROR: Brak najnowszego firmware Sonoff 4CH: $SONOFF_FW" >&2
  exit 1
}

echo "Sonoff 4CH FW: $SONOFF_FW"
