#!/bin/zsh

set -euo pipefail

ROOT="/Users/pawelwitkowski/Documents/New project"
REMOTE="deploy@wmsprinkler.pl"
SSH_CMD='ssh -o IdentitiesOnly=yes -o IdentityAgent=none -i /Users/pawelwitkowski/.ssh/id_ed25519'

env -u SSH_AUTH_SOCK rsync -avz --delete \
  -e "$SSH_CMD" \
  "$ROOT/cloud/public/" \
  "$REMOTE:/var/www/wmsprinkler/cloud/public/"

env -u SSH_AUTH_SOCK rsync -avz \
  -e "$SSH_CMD" \
  "$ROOT/cloud/src/server.js" \
  "$REMOTE:/var/www/wmsprinkler/cloud/src/server.js"

env -u SSH_AUTH_SOCK rsync -avz --delete \
  -e "$SSH_CMD" \
  "$ROOT/data/" \
  "$REMOTE:/var/www/wmsprinkler/data/"

env -u SSH_AUTH_SOCK ssh -o IdentitiesOnly=yes -o IdentityAgent=none -i /Users/pawelwitkowski/.ssh/id_ed25519 \
  "$REMOTE" \
  "cd /var/www/wmsprinkler/cloud && sudo -u deploy pm2 restart wms-cloud --update-env"
