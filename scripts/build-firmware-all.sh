#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

# Allow override via env vars.
ESP32_PROJECT_DIR="${ESP32_PROJECT_DIR:-$ROOT_DIR}"
ESP32_ENV_NAME="${ESP32_ENV_NAME:-esp32dev}"
C6_PROJECT_DIR="${C6_PROJECT_DIR:-$ROOT_DIR/../New project C6}"
C6_ENV_NAME="${C6_ENV_NAME:-esp32c6}"
C6_TFT_PROJECT_DIR="${C6_TFT_PROJECT_DIR:-$ROOT_DIR/tft}"
C6_TFT_ENV_NAME="${C6_TFT_ENV_NAME:-esp32c6_tft_test}"
PLUG_PROJECT_DIR="${PLUG_PROJECT_DIR:-$ROOT_DIR/smartplug_bwshp6}"
PLUG_ENV_NAME="${PLUG_ENV_NAME:-bw_shp6}"
SONOFF_PROJECT_DIR="${SONOFF_PROJECT_DIR:-/Users/pawelwitkowski/Desktop/OpenSprinkler-Sonoff4ch/wms-sonoff4ch}"
SONOFF_ENV_NAME="${SONOFF_ENV_NAME:-sonoff4ch}"
OUT_ROOT="${OUT_ROOT:-$ROOT_DIR/artifacts/firmware}"
FAIL_ON_BUILD_FS_ERROR="${FAIL_ON_BUILD_FS_ERROR:-0}"

timestamp() { date +"%Y-%m-%d %H:%M:%S"; }
log() { printf "[%s] %s\n" "$(timestamp)" "$*"; }
die() { printf "ERROR: %s\n" "$*" >&2; exit 1; }
print_artifact() {
  local label="$1"
  local path="$2"
  if [[ -f "$path" ]]; then
    echo "  $label: $path"
  else
    echo "  $label: (brak)"
  fi
}

find_pio() {
  if command -v pio >/dev/null 2>&1; then
    command -v pio
    return 0
  fi
  if [[ -x "$HOME/.platformio/penv/bin/pio" ]]; then
    printf "%s\n" "$HOME/.platformio/penv/bin/pio"
    return 0
  fi
  return 1
}

copy_if_exists() {
  local src="$1"
  local dst="$2"
  if [[ -f "$src" ]]; then
    cp -f "$src" "$dst"
  fi
}

build_one() {
  local name="$1"
  local project_dir="$2"
  local env_name="$3"
  local out_dir="$4"
  local build_dir="$project_dir/.pio/build/$env_name"

  [[ -d "$project_dir" ]] || die "Brak katalogu projektu: $project_dir"
  [[ -f "$project_dir/platformio.ini" ]] || die "Brak platformio.ini w: $project_dir"

  log "Budowanie $name ($env_name) w: $project_dir"
  "$PIO" run -d "$project_dir" -e "$env_name"

  local data_dir="$project_dir/data"
  if [[ -d "$data_dir" ]] && [[ -n "$(find "$data_dir" -mindepth 1 -print -quit 2>/dev/null)" ]]; then
    if ! "$PIO" run -d "$project_dir" -e "$env_name" -t buildfs; then
      rm -f "$build_dir/littlefs.bin"
      if [[ "$FAIL_ON_BUILD_FS_ERROR" == "1" ]]; then
        die "buildfs nie powiódł się dla $name ($env_name)"
      fi
      log "WARN: buildfs nie powiódł się dla $name ($env_name) - kontynuuję z samym firmware."
    fi
  else
    rm -f "$build_dir/littlefs.bin"
    log "Pomijam buildfs dla $name: brak katalogu data lub jest pusty"
  fi

  [[ -d "$build_dir" ]] || die "Brak katalogu build: $build_dir"
  [[ -f "$build_dir/firmware.bin" ]] || die "Brak firmware.bin: $build_dir/firmware.bin"

  mkdir -p "$out_dir"
  copy_if_exists "$build_dir/firmware.bin" "$out_dir/firmware.bin"
  copy_if_exists "$build_dir/littlefs.bin" "$out_dir/littlefs.bin"
  copy_if_exists "$build_dir/bootloader.bin" "$out_dir/bootloader.bin"
  copy_if_exists "$build_dir/partitions.bin" "$out_dir/partitions.bin"
  copy_if_exists "$build_dir/firmware.elf" "$out_dir/firmware.elf"
  copy_if_exists "$build_dir/firmware.map" "$out_dir/firmware.map"

  log "$name gotowe -> $out_dir"
}

PIO="$(find_pio || true)"
[[ -n "$PIO" ]] || die "Nie znaleziono PlatformIO (pio). Zainstaluj lub dodaj do PATH."

[[ -d "$C6_PROJECT_DIR" ]] || die "Nie znaleziono projektu C6: $C6_PROJECT_DIR"
[[ -d "$C6_TFT_PROJECT_DIR" ]] || die "Nie znaleziono projektu ESP32-C6 TFT: $C6_TFT_PROJECT_DIR"
[[ -d "$PLUG_PROJECT_DIR" ]] || die "Nie znaleziono projektu gniazdka: $PLUG_PROJECT_DIR"
[[ -d "$SONOFF_PROJECT_DIR" ]] || die "Nie znaleziono projektu Sonoff 4CH: $SONOFF_PROJECT_DIR"

BUILD_STAMP="$(date +%Y%m%d-%H%M%S)"
BUILD_DIR="$OUT_ROOT/build-$BUILD_STAMP"
LATEST_DIR="$OUT_ROOT/latest"

mkdir -p "$BUILD_DIR"

log "Używam pio: $PIO"
build_one "ESP32" "$ESP32_PROJECT_DIR" "$ESP32_ENV_NAME" "$BUILD_DIR/esp32"
build_one "ESP32-C6" "$C6_PROJECT_DIR" "$C6_ENV_NAME" "$BUILD_DIR/esp32c6"
build_one "ESP32-C6 TFT" "$C6_TFT_PROJECT_DIR" "$C6_TFT_ENV_NAME" "$BUILD_DIR/esp32c6_tft"
build_one "Gniazdko BW-SHP6" "$PLUG_PROJECT_DIR" "$PLUG_ENV_NAME" "$BUILD_DIR/smartplug_bwshp6"
build_one "Sonoff 4CH" "$SONOFF_PROJECT_DIR" "$SONOFF_ENV_NAME" "$BUILD_DIR/sonoff4ch"

mkdir -p "$LATEST_DIR"
rsync -a --delete "$BUILD_DIR/" "$LATEST_DIR/"

(
  cd "$BUILD_DIR"
  if command -v shasum >/dev/null 2>&1; then
    find . -type f \( -name "*.bin" -o -name "*.elf" -o -name "*.map" \) -print0 \
      | sort -z \
      | xargs -0 shasum -a 256 > checksums.sha256
  fi
)

log "Build zakończony."
log "Katalog wersji: $BUILD_DIR"
log "Katalog latest: $LATEST_DIR"
echo
echo "Pliki do uploadu OTA:"
print_artifact "ESP32 FW" "$LATEST_DIR/esp32/firmware.bin"
print_artifact "ESP32 FS" "$LATEST_DIR/esp32/littlefs.bin"
print_artifact "ESP32-C6 FW" "$LATEST_DIR/esp32c6/firmware.bin"
print_artifact "ESP32-C6 FS" "$LATEST_DIR/esp32c6/littlefs.bin"
print_artifact "ESP32-C6 TFT FW" "$LATEST_DIR/esp32c6_tft/firmware.bin"
print_artifact "ESP32-C6 TFT FS" "$LATEST_DIR/esp32c6_tft/littlefs.bin"
print_artifact "Gniazdko FW" "$LATEST_DIR/smartplug_bwshp6/firmware.bin"
print_artifact "Gniazdko FS" "$LATEST_DIR/smartplug_bwshp6/littlefs.bin"
print_artifact "Sonoff FW" "$LATEST_DIR/sonoff4ch/firmware.bin"
