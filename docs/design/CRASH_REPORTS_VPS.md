# Plan: move the crash-report API from Hostinger (PHP) to the LYNX SHARE VPS

Status: **plan only** (round 168v, 2026-11-02). Nothing is switched: the app and the site still use
`https://lynx-share.com/api/crash.php` and `reports.php`, and they keep working unchanged until the user OKs the steps
below. LYNX-own infrastructure (no FL Studio counterpart).

## Today

- App: `CrashReporter::endpoint()` (`src/ui/CrashReporter.cpp`) posts gzip JSON (schema 1) to
  `https://lynx-share.com/api/crash.php`; `LYNX_CRASH_ENDPOINT` overrides it (tests). Opt-in per report (PRIVACY.md).
- Server: `website/api/crash.php` + `_common.php` on Hostinger shared hosting; reports stored outside `public_html` as
  `<YYYY-MM>/<id>.json.gz` + `index.jsonl`; limits 30 per hashed address per day, 3000 per day, 50 000 per month; the
  address only as a salted daily HMAC used for counting. Admin: `reports.php` (list / get / delete, bearer key whose
  SHA-256 is in the file); the maintainer's `crash-fetch.sh` downloads them.
- Weak points: shared hosting PHP limits, the reports live on the same account as the website, no backups of the
  reports, the CDN in front of the site.

## Target

`lynx-api` on the VPS gains the same two endpoints with the **same contract**, so the app does not care which
server answers:

| New | Same as |
|---|---|
| `POST https://api.lynx-share.com/v1/crash` | `crash.php` — same schema-1 validation, gzip handling (1 MB compressed / 4 MB decoded caps, inflate with a running size check), same answers `{"ok":true,"id":…}` / `{"ok":false,"error":…}` and status codes, same limits |
| `GET/POST https://api.lynx-share.com/v1/crash/admin?action=list|get|delete` | `reports.php` — same actions and output; the key's SHA-256 lives in `/etc/lynx/lynx.env` (`LYNX_CRASH_ADMIN_SHA256`), not in the repo |

Storage: `/var/lib/lynx/crash/<YYYY-MM>/<id>.json.gz` + `index.jsonl` (same layout, so the files can be copied over
as they are). They are included in the daily backup (`server/ops/backup.sh` already backs up `/var/lib/lynx` except
share blobs), rotated 14 days. The address-hash salt stays a server secret in `lynx.env`.

## Steps (each one reversible; the user OKs steps 3 and 5)

1. **Implement** `crash.py` routes in `server/app/` (port of the PHP rules, unit-tested with the same reports the PHP
   accepts / refuses: a test feeds `tests/` fixture reports and fuzzed bodies to both and compares answers on a local
   `php -S` + local `lynx_api.py`). Deploy (`server/deploy.sh`); the endpoint is live but unused.
2. **Copy the existing reports** once: `crash-fetch.sh` already mirrors them to `/mnt/d/lynx-work/crash-reports`;
   upload that tree to `/var/lib/lynx/crash/` (`tar | ssh`), rebuild `index.jsonl`. `crash-fetch.sh` gets
   `API=${CRASH_API:-…}` so it can read either server.
3. **Forward on Hostinger** (user OK): `crash.php` becomes a thin proxy that re-posts the body to
   `https://api.lynx-share.com/v1/crash` (PHP `curl`, 10 s timeout) and, if the VPS does not answer, stores it locally
   as today. Every released LYNX (which has the old URL built in) then reaches the VPS with no app update, and a VPS
   outage loses nothing.
4. **App default** (next release after step 3 has run a week without errors): `CrashReporter::endpoint()` →
   `https://api.lynx-share.com/v1/crash`, still overridable by `LYNX_CRASH_ENDPOINT`; PRIVACY.md and `docs/STORE.md`
   name the new host (same data, same rules; the Store answer does not change: "crash reports, opt-in").
5. **Retire** (user OK, months later): when no report has come through `crash.php` for 60 days (`index.jsonl` on
   Hostinger), `crash.php` answers `410` with the new URL, and the PHP storage is deleted after a final copy.

## Risks and checks

- **TLS / DNS**: `api.lynx-share.com` is an A record to the VPS with a Let's Encrypt certificate renewed by Caddy;
  `lynx-monitor.timer` checks the public HTTPS path every 5 minutes. The PHP fallback in step 3 covers outages.
- **Privacy**: the VPS access log has no client addresses (Caddyfile `format filter`), the services log no addresses;
  the counting hash is in memory or salted daily as today.
- **Old builds**: covered by the proxy (step 3) for as long as it runs.
- **Load**: a few reports a day; negligible next to share uploads.
