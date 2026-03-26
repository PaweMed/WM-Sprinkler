#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="/Users/pawelwitkowski/Documents/New project"
LATEST_DIR="$PROJECT_DIR/artifacts/firmware/latest"

cd "$PROJECT_DIR"

echo "===== BUILD + PUBLISH START ====="
"$SCRIPT_DIR/build-firmware-all.sh"
echo "===== BUILD + PUBLISH DONE ====="

require_file() {
  local label="$1"
  local path="$2"
  [[ -f "$path" ]] || {
    echo "ERROR: Brak artefaktu ($label): $path" >&2
    exit 1
  }
}

show_optional() {
  local label="$1"
  local path="$2"
  if [[ -f "$path" ]]; then
    echo "  $label: $path"
  else
    echo "  $label: (brak)"
  fi
}

require_file "ESP32 FW" "$LATEST_DIR/esp32/firmware.bin"
require_file "ESP32-C6 FW" "$LATEST_DIR/esp32c6/firmware.bin"
require_file "ESP32-C6 TFT FW" "$LATEST_DIR/esp32c6_tft/firmware.bin"
require_file "Gniazdko FW" "$LATEST_DIR/smartplug_bwshp6/firmware.bin"
require_file "Sonoff 4CH FW" "$LATEST_DIR/sonoff4ch/firmware.bin"

echo "Artefakty firmware gotowe:"
echo "  ESP32:            $LATEST_DIR/esp32/firmware.bin"
echo "  ESP32-C6:         $LATEST_DIR/esp32c6/firmware.bin"
echo "  ESP32-C6 TFT:     $LATEST_DIR/esp32c6_tft/firmware.bin"
echo "  Gniazdko BW-SHP6: $LATEST_DIR/smartplug_bwshp6/firmware.bin"
echo "  Sonoff 4CH:       $LATEST_DIR/sonoff4ch/firmware.bin"
echo "Artefakty FS (opcjonalne):"
show_optional "  ESP32 FS" "$LATEST_DIR/esp32/littlefs.bin"
show_optional "  ESP32-C6 FS" "$LATEST_DIR/esp32c6/littlefs.bin"
show_optional "  ESP32-C6 TFT FS" "$LATEST_DIR/esp32c6_tft/littlefs.bin"
show_optional "  Gniazdko FS" "$LATEST_DIR/smartplug_bwshp6/littlefs.bin"
