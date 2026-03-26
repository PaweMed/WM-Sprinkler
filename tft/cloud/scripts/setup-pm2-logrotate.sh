#!/usr/bin/env bash
set -euo pipefail

# Uruchamiaj jako user deploy (ten sam co PM2)
pm2 install pm2-logrotate
pm2 set pm2-logrotate:max_size 20M
pm2 set pm2-logrotate:retain 14
pm2 set pm2-logrotate:compress true
pm2 set pm2-logrotate:workerInterval 30
pm2 set pm2-logrotate:rotateInterval '0 0 * * *'
pm2 save

echo "pm2-logrotate configured"
