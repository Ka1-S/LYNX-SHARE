#!/usr/bin/env bash
# Deploy the repo's server/ folder to the LYNX SHARE VPS and run setup.sh there (idempotent).
#   server/deploy.sh [ssh-host]      default host: lynx-vps (alias in ~/.ssh/config)
# Then: python3 server/tests/smoke.py   (end-to-end check against the live URLs)
set -euo pipefail
HOST=${1:-lynx-vps}
cd "$(dirname "$0")"
tar --create --gzip --exclude='__pycache__' --exclude='*.pyc' --exclude='*.env' . \
  | ssh "$HOST" 'set -e; rm -rf /opt/lynx/src.new; mkdir -p /opt/lynx/src.new; tar -xzf - -C /opt/lynx/src.new;
                 rm -rf /opt/lynx/src; mv /opt/lynx/src.new /opt/lynx/src; bash /opt/lynx/src/setup.sh'
