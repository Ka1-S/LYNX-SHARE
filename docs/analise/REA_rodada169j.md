# Round 169j — Online Sessions, phase 2, second slice (0.1.24)

LYNX-own feature (FL Studio 2026 has no live collaboration: [REA_rodada167r.md](REA_rodada167r.md)); nothing in FL to
match, so no FL reverse engineering. Design and what was built: [docs/design/ONLINE_SESSIONS.md](../design/ONLINE_SESSIONS.md)
§9 "P2, second slice (round 169j)". No relay change was needed (rooms already outlive their connections for 12 h).

| Asked (brief 169j) | Now |
|---|---|
| A dropped guest / host rejoins by itself and resyncs without losing pending edits | *Reconnecting...* / *Waiting for the host...*; `seek` → signed `host` → `resume`; replay of the last 4096 commits or a resume snapshot; `Replica::resend` / `resume` keep unconfirmed (and, after a host crash, lost own-area) edits |
| Host journal: a restarted host resumes the session | `<profile>/Live session/journal.json` every ≤ 2 s; start-up question *Resume* / *End it*; `--live-resume` |
| Live stand-ins rendered by the owner, updated when the part changes | `need` / `needs`, `live::unplayable` / `toRender`, P1's `sessionpack::render` on a worker, stems as an ordinary edit; re-rendered when P1's source hash changes |
| Session chat | *Session chat...* window, unread count, hint-bar lines |
| Follow transport | *Follow the host's transport* (menu + chat window), `transport` messages, `live::follow` |
| Host changes permissions per participant during the session | *People and permissions...*: own area / + tempo & settings / everything / listen only; what each keeps |
| Hash large samples off the message thread | files > 1 MiB hashed and checked on a worker; changes and snapshots wait for their hashes |
| FLAC transfer for media | `engine/MediaPack`: 16/24-bit PCM WAV as a byte-exact FLAC pack (`'F'` frames) |
| Core testable without network | `tests/Live169jTests.inc` (Live169bNet with links that drop, host restarts) |
| `--livetest` over the real relay with reconnect + stand-in + chat | extended (see Results) |
| Windows GUI with two instances | see Results |

## Results

- **Core** (`tests/Live169jTests.inc`, build-local): 24/24 seeded sessions over links that drop, with host restarts,
  converge (67 replays, 58 resume snapshots, 33 host restarts) and keep 183/183 offline edits; whole suite 1963796
  checks, 0 failures. Before host epochs only 16 of 24 converged (a host restarted from an older journal reused
  revision numbers its guests had already seen).
- **Windows `--selftest`**: `live_session=OK`, FLAC packs s16 17 %, s24 35 % of the WAV (byte-exact), f32 and tiny
  sent raw.
- **Windows `--livetest` on relay.lynx-share.com with `--plugin` (LYNX Test Synth.vst3)**: 37/37 ok, three runs in
  a row (31.7 s, 31.2 s, 31.4 s). Typical numbers: room 1.1–1.5 s, two guests in 1.6 s, late guest 1.8 s, chat to
  everyone 0.29 s, Ana's 1.2 MB WAV hashed off the thread and sent as a FLAC pack (955 KiB saved), guest drop →
  resumed by replay 2.65 s, host drop → everyone resumed 2.6 s, live stand-in rendered and at Rui 5.1 s, re-rendered
  after a note change 4.4 s, host crash → restarted from its journal (2 guests, revision 17) and both guests resumed
  in 1.8 s, journal deleted at the end.
- **Bug found by the livetest** (1 run in 3 before the fix): a local edit made between two 100 ms polls was
  swallowed when a commit or an arriving file updated the working song (the poll stamp moved on), so Lia's refused
  edit never went out. `applyUpdate` / `finishMedia` now move the stamp only when nothing local was waiting.
- **Windows GUI, two instances** (Kauan hosts with `--live-chat`, Ana joins with `--live-edit --live-follow
  --live-chat`; screenshots in E:\lynx-work\online169j): chat window with both lines in order; host killed →
  Ana's title *[Waiting for the host...]* within 4 s; `--live-resume` → host *[LIVE]* again, Ana *[LIVE]*; both
  closed by PID, desktop clean. The presence badge is not drawn when the toolbar layout has no room beside the menus
  (the 1690-px window here), so the session state now also goes in the window title.
- No relay change (rooms outlive their connections for 12 h already).

## Where I stopped

- Everything above done; next: commit, `coord/train.sh online169j`, notes-0.1.24.
