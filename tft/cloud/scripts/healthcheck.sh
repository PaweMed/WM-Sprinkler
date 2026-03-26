#!/usr/bin/env bash
set -euo pipefail

HEALTH_URL="${HEALTH_URL:-http://127.0.0.1:8080/health}"
ALERT_WEBHOOK_URL="${ALERT_WEBHOOK_URL:-}"
STATE_DIR="${STATE_DIR:-/tmp}"
STATE_FILE="$STATE_DIR/wms-healthcheck.state"
TS="$(date '+%Y-%m-%d %H:%M:%S')"

mkdir -p "$STATE_DIR"

status=0
body=""
if ! body="$(curl -fsS --max-time 5 "$HEALTH_URL" 2>/dev/null)"; then
  status=1
fi

if [[ "$status" -eq 0 ]]; then
  # oczekujemy "\"ok\":true"
  if ! grep -q '"ok"[[:space:]]*:[[:space:]]*true' <<<"$body"; then
    status=1
  fi
fi

prev="ok"
if [[ -f "$STATE_FILE" ]]; then
  prev="$(cat "$STATE_FILE" 2>/dev/null || echo ok)"
fi

if [[ "$status" -eq 0 ]]; then
  echo "ok" > "$STATE_FILE"
  echo "[$TS] HEALTH OK"
  exit 0
fi

echo "down" > "$STATE_FILE"
msg="[$TS] HEALTH FAIL: cloud/mqtt unavailable"
echo "$msg" >&2

# Alert tylko przy zmianie stanu (ok -> down)
if [[ "$prev" != "down" && -n "$ALERT_WEBHOOK_URL" ]]; then
  curl -fsS -X POST "$ALERT_WEBHOOK_URL" \
    -H 'Content-Type: application/json' \
    -d "{\"source\":\"wms-healthcheck\",\"timestamp\":\"$TS\",\"message\":\"cloud/mqtt health failed\"}" >/dev/null || true
fi

exit 1
