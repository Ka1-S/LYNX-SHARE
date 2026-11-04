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

(filled in as the runs finish)

## Where I stopped

- Done: core, engine, UI, core tests, docs, translations; build-local bl2 green (live169j: 24/24 sessions converge
  over dropping links, 183/183 offline edits kept; 1963796 checks, 0 failures).
- Windows `--livetest` live2 found a real bug (a local edit made between two 100 ms polls was swallowed when a commit
  or an arriving file updated the working song: Lia's refused edit never went out, 3 FAILs). Fixed in
  `applyUpdate` / `finishMedia` (the poll stamp moves on only when nothing local waited); live3 passed.
- Next: Windows rebuild (win5) + livetest ×3, `--selftest`, GUI two-instance check, commit, `coord/train.sh online169j`.
