# Round 169b — Online Sessions, phase 2: live sessions over the relay

LYNX-own feature (FL Studio 2026 has no live collaboration: [REA_rodada167r.md](REA_rodada167r.md)); nothing in FL to
match, so no FL reverse engineering. Design and "P2 as built": [docs/design/ONLINE_SESSIONS.md](../design/ONLINE_SESSIONS.md)
(§6.1 why the relay first, §6.4 the encryption as built, §9 P2 as built). Relay API: [SHARE_API.md](../design/SHARE_API.md) §3.1.

| Asked (brief 169b) | Now |
|---|---|
| Host creates a live session → invite code | *File › Online session › Start live session...* → `LYNX-RRRRR-RRRRR-SSSS-SSSS-SSSS-SSSS` (room + 80-bit secret) |
| Up to 3 guests join through the relay (WSS) | `engine/LiveLink` (WinHTTP / libcurl WebSocket), relay room ≤ 4 peers; knock prompt on the host |
| Host-authoritative replication: notes, clips, channel / mixer params, pattern / track add / rename | `core/LiveSession` (`Authority`, `Replica`, entity diff / apply by uid) — every Song entity, not only those |
| Late join with snapshot + media, P1 path safety | tokenized snapshot + plugin manifest; media by sha256 in 256 KiB pieces, `sessionpack::confine` on everything received, hash checked before use |
| Per-user undo | `AudioEngine` session steps + `live::selectiveRevert` |
| Presence | LIVE badge + people (colour, name, rec) in the toolbar |
| E2E encryption with the invite-code key | Argon2id → session key + relay token (BLAKE2b domains), XChaCha20-Poly1305 frames with replay check (Monocypher 4.0.2) |
| Guest leaving / host leaving | `bye` / relay `leave` / `end` + room `DELETE`; guests keep their part (or a full copy if allowed) |
| Relay changes, tests, deploy, smoke | `POST /v1/rooms {"token"}` (host-chosen token), `server/tests/smoke.py` extended, deployed, smoke 0 failures |
| Core testable without network | `tests/Live169bTests.inc` (in-process fake network, 24 seeds) |
| Headless 2–3 peer test over the real relay | `LYNX --livetest report.txt` (host + 3 guests) |
| Windows GUI check with two instances | two fresh `LYNX_PROFILE_DIR`s, `--live-host code.txt` / `--live-join @code.txt` |

## Results

- `scripts/build-local.sh`: 1,963,633 checks, 0 failures (`tests/Live169bTests.inc`: 9 tests).
- Windows `--selftest`: `result=OK`, `live_session=OK (31 entities, 2 not kept by the format: Master [m:0]; Insert 2
  [m:2])` — float parameters the JSON rounds; the host rounds its song once at the start, so every copy holds the same
  values.
- Windows `LYNX --livetest` over `wss://relay.lynx-share.com`: 19/19 checks, `result=OK`, 9.9 s in all (room 1.1 s,
  two guests in 1.6 s, everyone's edits 2.8 s, a late guest with both samples 2.0 s; 1.2 MB sample fragmented and
  forwarded by the host).
- GUI: two LYNX instances (fresh `LYNX_PROFILE_DIR`s, `LYNX_TEST_MODE=1`), `--live-host code.txt --live-name Kauan`
  and `--live-join @code.txt --live-name Ana --live-edit`: both show LIVE + both people, Ana's channel arrives on the
  host; the host closed → the guest shows "The host left the live session. Your part of the project stays here." and
  keeps only her channel. Screenshots: `E:\lynx-work\online169b\gui-two-instances.png`, `gui-host-left.png`,
  `desktop-after.png`.
- A bug found by the livetest and fixed: the host's own song still had float values the JSON rounds (Master's limiter
  0.974999964 vs 0.975000024 on the guests): never converged on those inserts. `start()` now puts the host's song
  through the same round trip first.

## Where I stopped

Everything in the brief is built and tested; not landed (the coordinator lands it after 0.1.23). Later phases:
ONLINE_SESSIONS.md §9 "P2 as built", last bullet.
