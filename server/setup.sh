#!/usr/bin/env bash
# LYNX SHARE server setup — idempotent; run as root ON THE VPS from a copy of the repo's server/ folder.
# Normally started by server/deploy.sh from a workstation (copies server/ to /opt/lynx/src and runs this).
# Safe to run again at any time: it installs/updates packages, code, Caddy config, units and timers, never touches
# SSH settings, keeps /etc/lynx/lynx.env (secrets) and all data.
set -euo pipefail
[ "$(id -u)" = 0 ] || { echo "run as root"; exit 1; }
SRC=$(cd "$(dirname "$0")" && pwd)
export DEBIAN_FRONTEND=noninteractive

echo "== packages"
need=()
for p in caddy python3-aiohttp curl logrotate ufw; do dpkg -s "$p" >/dev/null 2>&1 || need+=("$p"); done
if [ ${#need[@]} -gt 0 ]; then apt-get update -q; apt-get install -y -q --no-install-recommends "${need[@]}"; fi

echo "== user and folders"
id lynx >/dev/null 2>&1 || useradd --system --home-dir /var/lib/lynx --shell /usr/sbin/nologin lynx
install -d -m 755 /opt/lynx /opt/lynx/app /opt/lynx/ops
install -d -m 700 -o lynx -g lynx /var/lib/lynx /var/lib/lynx/shares
install -d -m 700 /etc/lynx /var/backups/lynx
install -d -m 750 -g adm /var/log/lynx
install -d -m 755 -o caddy -g caddy /var/log/caddy

echo "== secrets (/etc/lynx/lynx.env, kept if present)"
if [ ! -f /etc/lynx/lynx.env ]; then
  umask 077
  sed "s/^LYNX_SALT=.*/LYNX_SALT=$(head -c 32 /dev/urandom | od -An -tx1 | tr -d ' \n')/" "$SRC/env.example" > /etc/lynx/lynx.env
fi
chmod 600 /etc/lynx/lynx.env; chown root:root /etc/lynx/lynx.env
# new keys added to env.example later are appended with their defaults (existing values are never changed)
while IFS= read -r line; do
  k=${line%%=*}; case "$line" in ''|\#*) continue;; esac
  grep -q "^$k=" /etc/lynx/lynx.env || echo "$line" >> /etc/lynx/lynx.env
done < "$SRC/env.example"

echo "== code"
changed=0
for f in "$SRC"/app/*.py; do
  cmp -s "$f" "/opt/lynx/app/$(basename "$f")" || { install -m 644 "$f" /opt/lynx/app/; changed=1; }
done
install -m 755 "$SRC"/ops/backup.sh "$SRC"/ops/monitor.sh /opt/lynx/ops/
install -m 644 "$SRC"/ops/logrotate-lynx /etc/logrotate.d/lynx
install -d /etc/systemd/journald.conf.d
if ! cmp -s "$SRC/ops/journald-lynx.conf" /etc/systemd/journald.conf.d/lynx.conf; then
  install -m 644 "$SRC/ops/journald-lynx.conf" /etc/systemd/journald.conf.d/lynx.conf
  systemctl restart systemd-journald
fi
python3 -m py_compile /opt/lynx/app/*.py && rm -rf /opt/lynx/app/__pycache__

echo "== systemd units"
units_changed=0
for f in "$SRC"/systemd/*; do
  cmp -s "$f" "/etc/systemd/system/$(basename "$f")" || { install -m 644 "$f" /etc/systemd/system/; units_changed=1; }
done
systemctl daemon-reload
systemctl enable -q lynx-api lynx-relay lynx-backup.timer lynx-monitor.timer caddy
if [ $changed = 1 ] || [ $units_changed = 1 ]; then systemctl restart lynx-api lynx-relay; else systemctl start lynx-api lynx-relay; fi
systemctl start lynx-backup.timer lynx-monitor.timer

echo "== firewall (22 stays open; adds 80/443 for Caddy)"
ufw allow 22/tcp >/dev/null
ufw allow 80/tcp >/dev/null
ufw allow 443/tcp >/dev/null
ufw allow 443/udp >/dev/null   # HTTP/3
ufw status | grep -q 'Status: active' || { echo "ufw inactive: enable it by hand after checking (ufw enable)"; }

echo "== caddy"
caddy validate --config "$SRC/caddy/Caddyfile" --adapter caddyfile >/dev/null 2>&1 || { caddy validate --config "$SRC/caddy/Caddyfile" --adapter caddyfile; exit 1; }
if ! cmp -s "$SRC/caddy/Caddyfile" /etc/caddy/Caddyfile; then
  install -m 644 "$SRC/caddy/Caddyfile" /etc/caddy/Caddyfile
  systemctl reload-or-restart caddy
fi
systemctl is-active -q caddy || systemctl start caddy

echo "== local health"
for i in 1 2 3 4 5 6 7 8 9 10; do
  if curl -sf -m 5 http://127.0.0.1:8081/health >/dev/null && curl -sf -m 5 http://127.0.0.1:8082/health >/dev/null; then break; fi
  sleep 1
done
curl -sf -m 5 http://127.0.0.1:8081/health; echo
curl -sf -m 5 http://127.0.0.1:8082/health; echo
echo "setup done"
