# LYNX SHARE server API — share links (Online Sessions P1) and relay (P2/P4 skeleton)

Status: **live since round 168v (2026-11-02)**, not used by the app yet. The app round that ships *Send project to a
friend…* (ONLINE_SESSIONS.md §9 P1) and later the relay link builds on this contract. Server code and deployment:
`server/` (README there). This is a LYNX-own feature (no FL Studio equivalent, ONLINE_SESSIONS.md §0).

| Service | Base URL | Runs as |
|---|---|---|
| API (health, share links) | `https://api.lynx-share.com` | `lynx-api.service`, 127.0.0.1:8081 behind Caddy |
| Relay (WebSocket rooms) | `https://relay.lynx-share.com` / `wss://relay.lynx-share.com` | `lynx-relay.service`, 127.0.0.1:8082 behind Caddy |

Conventions: JSON bodies and answers (`Content-Type: application/json`), errors are `{"ok": false, "error": "<text
for a human>"}` with a meaningful status code. Tokens always travel in `Authorization: Bearer <token>`, never in a
URL (URLs end up in logs and chat apps). `api` in `/health` is the API version (`"1"`); a client that sees a newer
major version than it knows should tell the user to update. Every limit below is a server setting
(`/etc/lynx/lynx.env`, defaults in `server/env.example`).

---

## 1. Health

`GET /health` on either host → `200`

```json
{"ok":true,"service":"lynx-api","api":"1","uptime":3600,"shares":2,"shareBytes":81234567,"diskFree":101094068224}
{"ok":true,"service":"lynx-relay","api":"1","uptime":3600,"rooms":1,"peers":2,"maxPeers":4,"frames":1234}
```

The VPS checks both every 5 minutes (`lynx-monitor.timer`, local and through public HTTPS) and restarts what hangs.

---

## 2. Share links (P1: *Send project to a friend…*)

The host's LYNX uploads a `.lynxpack` (the zip of ONLINE_SESSIONS.md §9 P1); the server answers a link the user
pastes into WhatsApp / e-mail; the friend opens the link (a small page with a Download button) or pastes it into
LYNX's *Open a friend's project…*, which downloads it directly.

### 2.1 Lifecycle

```
POST   /v1/shares                  create (declare name, size, optional sha256, days)  -> id, token, uploadUrl
PATCH  /v1/shares/<id>             upload bytes from Upload-Offset (repeat; resumable)  [token]
GET    /v1/shares/<id>             metadata incl. "received" (resume point) and "complete"
GET    /v1/shares/<id>/download    the file (Range supported)                          (public, by unguessable id)
GET    /s/<id>                     human page with a Download button                   (the link users share)
DELETE /v1/shares/<id>             delete now                                          [token]
```

- **id**: 128 random bits, 26 lowercase base-32 chars (`[a-z2-7]{26}`); knowing the link = being allowed to
  download. There is no listing of any kind.
- **token** (returned once, at create): 256 random bits. It is the upload *and* delete key; the server keeps only its
  SHA-256. The app stores it next to the share (in the profile's `shares.json`) so the user can *Stop sharing* later.
- **Expiry**: `days` 1…30 (default 7). Expired shares and uploads not finished within 24 h are deleted
  automatically (checked every 10 minutes and on every access).

### 2.2 Create

`POST /v1/shares`

```json
{"name": "My song.lynxpack", "size": 81234567, "days": 7, "sha256": "<64 hex, optional but recommended>"}
```

- `name`: a file name ending in `.lynxpack`, at most 120 characters, no slashes or control characters.
- `size`: exact byte count, at most **2 GiB** (`2147483648`).

`201 Created`

```json
{"ok":true,"id":"ohqg5s…","name":"My song.lynxpack","size":81234567,"received":0,"complete":false,
 "created":1793645658,"expires":1794250458,
 "url":"https://api.lynx-share.com/s/ohqg5s…","downloadUrl":"https://api.lynx-share.com/v1/shares/ohqg5s…/download",
 "token":"<keep secret>","uploadUrl":"https://api.lynx-share.com/v1/shares/ohqg5s…","offset":0,"chunkSize":8388608}
```

Errors: `400` bad field · `413` over 2 GiB · `429` limits (`Retry-After`): 20 creates per hour and 8 GiB declared per
day per client · `507` the server's share storage (60 GiB) or disk is full.

### 2.3 Upload (resumable, tus-like)

`PATCH /v1/shares/<id>` with `Authorization: Bearer <token>`, `Upload-Offset: <n>`,
`Content-Type: application/offset+octet-stream`, body = the bytes from offset `n` on (any length up to the rest of
the file; `chunkSize` is only a suggestion — 8 MiB pieces give a smooth progress bar and lose little on a drop).

- `200` → the metadata with `"offset": <new offset>` (also in the `Upload-Offset` header). When the offset reaches
  `size`, the server checks the file (zip signature, and the `sha256` if one was declared) and answers
  `"complete": true, "sha256": "<hex>"`.
- **Resume after a drop**: `GET /v1/shares/<id>` → `"received"` is where to continue (what arrived before the drop is
  kept).
- `409` + `Upload-Offset: <server's offset>`: the offset does not match (send again from that offset), or another
  upload to the same share is running, or it is already complete · `401` wrong token · `413` more bytes than declared ·
  `415` the file does not start like a zip · `422` the finished file failed the check (sha256 / format): the share is
  deleted, start again · `404` unknown or expired.

### 2.4 Download

`GET /v1/shares/<id>/download` → the file, `Content-Disposition: attachment; filename*=UTF-8''<name>`, with
`Content-Length`, `Range` / `206` support (resumable downloads) and `ETag`/`Last-Modified`. `409` while the upload is
not finished, `404` unknown/expired. The app verifies the `sha256` from `GET /v1/shares/<id>` after downloading.

`GET /s/<id>` is the page people see when they open the link in a browser: name, size, expiry, a Download button
and "Open it with LYNX SHARE". Later (P3) it can also offer a `lynx://open?share=<id>` button.

### 2.5 Delete

`DELETE /v1/shares/<id>` with the token → `204`. `401` wrong token, `404` unknown/expired.

### 2.6 What the app round should do (P1)

1. *File › Send project to a friend…*: build the `.lynxpack`, hash it (SHA-256) while writing, `POST`, then `PATCH`
   in 8 MiB pieces on a background thread with a progress bar and a Cancel button (Cancel = `DELETE`). On a network
   error, retry with back-off from `received`. Show the `url` with *Copy link* and the expiry date.
2. Keep `{id, token, name, expires}` in the profile (`shares.json`); *Manage shared projects…* lists them with
   *Copy link* / *Stop sharing*.
3. *File › Open a friend's project…* accepts a link `https://api.lynx-share.com/s/<id>` (or the bare id), reads
   the metadata, downloads with resume (`Range`), checks `sha256`, then opens the package as a local `.lynxpack`.
4. Use JUCE `URL` / `WebInputStream` (already used by the crash reporter) — no new dependency. Test against a local
   `python3 server/app/lynx_api.py` (env `LYNX_DATA_DIR`, `LYNX_PUBLIC_URL`, `LYNX_API_PORT`), never the live server.
5. PRIVACY.md / Store answers: the project and its samples are uploaded **only when the user sends a project**, kept
   until the chosen expiry (or *Stop sharing*), readable by whoever has the link; no address is stored (the access log
   has no client addresses, rate limits use an in-memory salted hash).

---

## 3. Relay (P2/P4 skeleton)

A dumb, end-to-end-blind forwarder for sessions whose peers cannot reach each other directly (ONLINE_SESSIONS.md
§6.1 `RelayLink`, §9 P4). LYNX encrypts every frame before it reaches the relay (§6.4); the relay only routes by a
one-byte peer number. Rooms live in memory: a relay restart drops them (clients reconnect and the host opens a new
room). Today it only forwards/echoes; the session protocol on top is the app's (§6.2).

### 3.1 Rooms

`POST /v1/rooms` (no body, or `{"token": "<43..128 characters A-Z a-z 0-9 _ ->"}`) → `201`

```json
{"ok":true,"room":"2syq75e66t","token":"<room token>","hostToken":"<host token>","maxPeers":4,
 "expires":1793688858,"ws":"wss://relay.lynx-share.com/v1/rooms/2syq75e66t/ws","subprotocol":"lynx-relay.v1"}
```

- **A token the host chose** (round 169b, how LYNX SHARE's live sessions use it): the body's `token` becomes the room
  token and the answer's `token` is `""` (the relay never hands it back). LYNX derives it from the invite code's
  secret (ONLINE_SESSIONS.md §6.4 as built), so every guest computes the same token from the code alone and the relay
  never learns the secret or the session key. `400` for a body that is not a JSON object or a token of the wrong shape.
- The **room token** goes to everyone the host invites (inside the encrypted invite / knock flow, never in clear on
  a public channel); the **host token** stays with the host and closes the room:
  `DELETE /v1/rooms/<room>` with `Authorization: Bearer <hostToken>` → `204` (every peer is disconnected, close code
  4000).
- A room expires 12 h after its last connection ended. Limits: 10 rooms per hour per client, 200 rooms on the relay
  (`503` when full), **4 peers per room** (ONLINE_SESSIONS.md §10 Q3: 4 proposed).

### 3.2 Connect

`GET wss://relay.lynx-share.com/v1/rooms/<room>/ws`, subprotocol `lynx-relay.v1`, room token as
`Authorization: Bearer <token>` (native clients) or, for browsers that cannot set headers, as a second subprotocol
`token.<token>`. Refusals before the upgrade: `401` wrong token, `404` unknown room, `409` room full, `429` too many
connection attempts (60 per minute per client).

On connect the relay assigns the lowest free **peer number 1…4** and sends text frames:

| Server → client (text, JSON) | When |
|---|---|
| `{"type":"welcome","peer":2,"peers":[1,2],"maxPeers":4}` | right after connecting |
| `{"type":"join","peer":3}` / `{"type":"leave","peer":3}` | another peer connects / disconnects |
| `{"type":"pong","t":<echo>}` | answer to `{"type":"ping","t":…}` |
| `{"type":"peers","peers":[1,2,3]}` | answer to `{"type":"peers"}` |
| `{"type":"error","error":"…"}` | unknown control message |

**Data = binary frames**: byte 0 is the destination peer (`0` = every other peer; your own number = echo back to
yourself, for tests and RTT), bytes 1… are the payload. The relay delivers `[source peer][payload]`. At most 1 MiB per
frame (`LYNX_RELAY_MAX_FRAME`); each peer may send 4 MiB/s on average (beyond that the relay slows the reader
down instead of dropping, so the stream stays reliable and ordered). WebSocket pings every 25 s keep NAT mappings
alive.

### 3.3 Mapping to ONLINE_SESSIONS.md

- §6.1 `RelayLink` = one WebSocket per peer; the `SessionLink` framing (`u32 length | u8 type | u8 stream |
  payload`, Noise-encrypted) is the payload of the relay frame, so the relay never sees message types.
- §6.3 rendezvous: the PHP mailbox planned on Hostinger can move here as plain HTTP endpoints of `lynx-api` (no PHP
  time limits, real long polling or a WebSocket) — decided in P3.
- §6.5 privacy: through the relay, peers do **not** see each other's IP address (only the relay does, and it logs
  none).
