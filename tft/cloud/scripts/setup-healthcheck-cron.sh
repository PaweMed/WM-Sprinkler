#!/usr/bin/env bash
set -euo pipefail

APP_DIR="${APP_DIR:-/var/www/wmsprinkler/cloud}"
CRON_LINE="*/1 * * * * HEALTH_URL=http://127.0.0.1:8080/health ALERT_WEBHOOK_URL=${ALERT_WEBHOOK_URL:-} $APP_DIR/scripts/healthcheck.sh >> /var/log/wms-healthcheck.log 2>&1"

( crontab -l 2>/dev/null | grep -v 'healthcheck.sh' ; echo "$CRON_LINE" ) | crontab -
echo "healthcheck cron installed"
