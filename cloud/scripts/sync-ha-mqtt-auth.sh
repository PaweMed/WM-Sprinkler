#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
node "$SCRIPT_DIR/sync-ha-mqtt-auth.js"

if command -v systemctl >/dev/null 2>&1; then
  if systemctl is-active --quiet mosquitto; then
    systemctl reload mosquitto || systemctl restart mosquitto
    echo "[sync-ha-mqtt-auth] mosquitto reloaded"
  else
    systemctl start mosquitto
    echo "[sync-ha-mqtt-auth] mosquitto started"
  fi
fi

echo "[sync-ha-mqtt-auth] completed"
