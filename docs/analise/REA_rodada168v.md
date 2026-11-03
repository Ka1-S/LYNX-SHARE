# Round 168v — LYNX SHARE VPS foundation (infrastructure)

LYNX-own infrastructure (no FL Studio counterpart, no REA needed: ONLINE_SESSIONS.md §0 already shows FL has no
collaboration feature). The user pays a VPS since 2026-11-02 ("put everything LYNX there").

| Item | Asked | Before | Now |
|---|---|---|---|
| Server as code | `server/` with idempotent setup, Caddy + TLS, units, README | nothing on the VPS but base hardening | `server/setup.sh` (idempotent, verified by a second run: no restarts, env kept), `deploy.sh`, `caddy/Caddyfile`, `systemd/*`, `ops/*`, `README.md` |
| DNS | subdomains to the VPS via Hostinger DNS API, touch only new records | zone: `@` ALIAS, `www` CNAME, `ftp` A | + `api` A and `relay` A → 179.66.129.177 (TTL 300), added with `overwrite:false` after `/validate`; the 3 old records unchanged (checked after). ufw: + 80/tcp, 443/tcp, 443/udp |
| Health + uptime | `/health` + check | — | `GET /health` on both hosts; `lynx-monitor.timer` every 5 min (local + public HTTPS, restarts on 2 failures, resource line in `/var/log/lynx/monitor.log`) |
| Share links (P1) | upload .lynxpack, cap, expiry, random id, delete token, download by link, API doc | — | `/v1/shares` (create / resumable PATCH / metadata / download with Range / delete), `/s/<id>` page; 2 GiB cap, 1–30 days, 128-bit id, 256-bit token (hash stored), zip + sha256 check, rate limits; doc `docs/design/SHARE_API.md` |
| Relay skeleton (P2) | WS rooms, max 4 peers, room token, echo/forward | — | `relay.lynx-share.com`: `POST /v1/rooms`, `wss://…/v1/rooms/<room>/ws` (Bearer or `token.` subprotocol), peer numbers 1–4, unicast / broadcast / echo by first byte, join/leave/ping control, host token closes |
| Crash reports | plan the move, don't switch | PHP on Hostinger | plan `docs/design/CRASH_REPORTS_VPS.md` (same contract at `/v1/crash`, PHP proxy step for old builds); nothing switched |
| Ops | backups + rotation, log rotation, monitoring, restarts | — | `lynx-backup.timer` daily (14 kept, share blobs excluded), logrotate + journald cap, Caddy log roll, `Restart=always` + monitor, memory caps, sandboxed units |

## Evidence (2026-11-02, from WSL)

- `python3 server/tests/smoke.py` against the live URLs: 35 checks, 0 failures (bad name 400, >2 GB 413, upload without
  token 401, non-zip 415, wrong offset 409 with the server offset, resume, sha256, identical download, Range 206, page,
  no listing 404, delete; relay health, wrong token 401, 4 peers (one by subprotocol token), 5th 409, echo, unicast,
  512 KB broadcast, ping/pong, host close disconnects all). Same test against local services: 0 failures.
- 200 MB upload through Caddy: 31 s (~7 MB/s, the workstation's uplink), download 22 s, byte-identical; the API's
  anonymous memory stayed at 31 MB (streamed, not buffered).
- Rate limits: share create → 429 after the hourly budget (20); relay rooms → 429 after 10.
- `systemctl kill -s KILL lynx-api` → back in 3 s (NRestarts=1). Backup run → archive 600 in a 700 folder with
  `/etc/lynx`, Caddy certificates. Caddy access log: no `remote_ip`, no headers.
- TLS: Let's Encrypt certificates for both names (http-01), HTTP → HTTPS 308.

## Where I stopped

- Done: everything above deployed and tested on the VPS; docs written; committed on feature/vps168v (pushed as wip/vps168v).
- PAUSED 2026-11-02 by the coordinator (round 168w speeds up landings) before landing: the land-safe.sh waiter was
  cancelled while waiting on the merge lock. On RESUME: `~/.claude/jobs/f8789b49/tmp/coord/land-safe.sh vps168v`.
  The VPS services keep running (not affected by the pause).
- Next (other rounds): app side of P1 (SHARE_API.md §2.6), crash move steps 1–2, off-machine backups, AAAA records,
  SSH `PasswordAuthentication no` after the user changes the root password.
- Rerun: `server/deploy.sh` then `python3 server/tests/smoke.py` (aiohttp needed locally, e.g. a venv).
