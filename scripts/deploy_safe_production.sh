#!/bin/zsh

set -euo pipefail

ROOT="/Users/pawelwitkowski/Documents/New project"
REMOTE="${DEPLOY_REMOTE:-deploy@wmsprinkler.pl}"
SSH_BASE=(
  ssh
  -o StrictHostKeyChecking=accept-new
)
SSH_CMD_STR="${(j: :)SSH_BASE}"
SSH_CONTROL_PATH="${DEPLOY_SSH_CONTROL_PATH:-/tmp/wms-deploy-ctrl-%C}"
SSH_BASE+=(
  -o ControlMaster=auto
  -o ControlPersist=10m
  -o ControlPath="$SSH_CONTROL_PATH"
)
SSH_CMD_STR="${(j: :)SSH_BASE}"
REMOTE_APP_ROOT="${DEPLOY_APP_ROOT:-/var/www/wmsprinkler}"
REMOTE_CLOUD_ROOT="$REMOTE_APP_ROOT/cloud"
REMOTE_STORAGE_ROOT="$REMOTE_CLOUD_ROOT/storage"
# Domyślnie backup w katalogu domowym deploy (stabilny zapis bez sudo).
REMOTE_BACKUP_ROOT="${DEPLOY_BACKUP_ROOT:-/home/deploy/wms-backups}"

if [[ -n "${DEPLOY_SSH_KEY:-}" ]]; then
  if [[ ! -f "$DEPLOY_SSH_KEY" ]]; then
    echo "[DEPLOY] Brak klucza SSH: $DEPLOY_SSH_KEY" >&2
    echo "[DEPLOY] Ustaw poprawny klucz przez DEPLOY_SSH_KEY=/sciezka/do/klucza" >&2
    exit 1
  fi
  SSH_BASE+=(
    -o IdentitiesOnly=yes
    -o IdentityAgent=none
    -i "$DEPLOY_SSH_KEY"
  )
  SSH_CMD_STR="${(j: :)SSH_BASE}"
fi

cleanup_ssh_master() {
  env -u SSH_AUTH_SOCK "${SSH_BASE[@]}" -O exit "$REMOTE" >/dev/null 2>&1 || true
}
trap cleanup_ssh_master EXIT

echo "[DEPLOY] Test polaczenia SSH do $REMOTE ..."
env -u SSH_AUTH_SOCK "${SSH_BASE[@]}" "$REMOTE" "echo [DEPLOY] SSH OK"

echo "[DEPLOY] Backup danych (cloud/storage) na serwerze ..."
env -u SSH_AUTH_SOCK "${SSH_BASE[@]}" "$REMOTE" \
  "set -euo pipefail; \
   mkdir -p '$REMOTE_BACKUP_ROOT'; \
   if [ -d '$REMOTE_STORAGE_ROOT' ]; then \
     TS=\$(date -u +%Y%m%d-%H%M%S); \
     tar -czf '$REMOTE_BACKUP_ROOT/storage-\$TS.tar.gz' -C '$REMOTE_CLOUD_ROOT' storage; \
     ls -1t '$REMOTE_BACKUP_ROOT'/storage-*.tar.gz 2>/dev/null | sed -n '31,\$p' | xargs -r rm -f; \
     echo '[DEPLOY] Backup OK:' '$REMOTE_BACKUP_ROOT/storage-'\"\$TS\"'.tar.gz'; \
   else \
     echo '[DEPLOY] UWAGA: brak katalogu storage, pomijam backup'; \
   fi"

env -u SSH_AUTH_SOCK rsync -avz --delete \
  -e "$SSH_CMD_STR" \
  "$ROOT/cloud/public/" \
  "$REMOTE:$REMOTE_CLOUD_ROOT/public/"

env -u SSH_AUTH_SOCK rsync -avz \
  -e "$SSH_CMD_STR" \
  "$ROOT/cloud/src/server.js" \
  "$REMOTE:$REMOTE_CLOUD_ROOT/src/server.js"

env -u SSH_AUTH_SOCK rsync -avz --delete \
  -e "$SSH_CMD_STR" \
  "$ROOT/data/" \
  "$REMOTE:$REMOTE_APP_ROOT/data/"

env -u SSH_AUTH_SOCK "${SSH_BASE[@]}" \
  "$REMOTE" \
  "cd '$REMOTE_CLOUD_ROOT' && sudo -u deploy pm2 restart wms-cloud --update-env"
