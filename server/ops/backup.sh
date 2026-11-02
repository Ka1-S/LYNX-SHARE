#!/usr/bin/env bash
# Daily backup of the LYNX SHARE service data (run by lynx-backup.timer as root).
# Keeps LYNX_BACKUP_KEEP (default 14) daily archives in /var/backups/lynx (chmod 700, archives 600).
# Share blobs (/var/lib/lynx/shares/*/blob) are NOT backed up: they are temporary (they expire in days), can be up to
# 60 GB in total and the host re-uploads a link that was lost. Their metadata is backed up.
set -euo pipefail
DEST=/var/backups/lynx
KEEP=${LYNX_BACKUP_KEEP:-14}
install -d -m 700 "$DEST"
STAMP=$(date -u +%Y%m%d-%H%M)
OUT="$DEST/lynx-$STAMP.tar.gz"
umask 077
paths=()
for p in /etc/lynx /etc/caddy /var/lib/caddy /var/lib/lynx; do [ -e "$p" ] && paths+=("$p"); done
tar --create --gzip --file "$OUT.part" --warning=no-file-changed --exclude='/var/lib/lynx/shares/*/blob' \
    --exclude='*.tmp' "${paths[@]}" 2>/dev/null || [ $? -eq 1 ]
mv "$OUT.part" "$OUT"
# rotation: newest $KEEP archives stay
ls -1t "$DEST"/lynx-*.tar.gz 2>/dev/null | tail -n +"$((KEEP + 1))" | xargs -r rm -f
echo "$(date -u +%FT%TZ) backup $OUT $(stat -c %s "$OUT") bytes, $(ls -1 "$DEST"/lynx-*.tar.gz | wc -l) kept" >> /var/log/lynx/backup.log
