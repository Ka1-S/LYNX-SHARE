"""LYNX SHARE relay service: Online Sessions P2/P4 WebSocket relay skeleton (rooms of up to 4 peers).

Runs on 127.0.0.1:8082 behind Caddy (relay.lynx-share.com). The relay only forwards frames: it never reads them.
LYNX encrypts every session frame end to end (docs/design/ONLINE_SESSIONS.md §6.4), so the relay sees opaque bytes.
Protocol reference: docs/design/SHARE_API.md ("Relay").
LYNX SHARE (AGPL-3.0-or-later).
"""
from __future__ import annotations

import asyncio
import json
import time

from aiohttp import WSMsgType, web

from lynx_common import (VERSION, RateLimiter, bearer, client_key, env_int, env_str, errors_middleware, fail,
                         json_response, random_id, random_token, setup_logging, token_hash, token_matches)

log = setup_logging("lynx-relay")

MAX_PEERS = env_int("LYNX_RELAY_MAX_PEERS", 4)
MAX_ROOMS = env_int("LYNX_RELAY_MAX_ROOMS", 200)
ROOM_HOURS = env_int("LYNX_RELAY_ROOM_HOURS", 12)          # a room lives this long after its last connection
MAX_FRAME = env_int("LYNX_RELAY_MAX_FRAME", 1024 * 1024)   # bytes per WebSocket message
PEER_BYTES_PER_SEC = env_int("LYNX_RELAY_PEER_BPS", 4 * 1024 * 1024)
ROOMS_PER_HOUR = env_int("LYNX_RELAY_ROOMS_PER_HOUR", 10)
SUBPROTOCOL = "lynx-relay.v1"

STARTED = time.time()
room_limiter = RateLimiter(ROOMS_PER_HOUR, 3600)
connect_limiter = RateLimiter(60, 60)
frames_total = 0


class Room:
    def __init__(self, rid: str, token: str, host_token: str):
        self.id = rid
        self.token_hash = token_hash(token)
        self.host_hash = token_hash(host_token)
        self.peers: dict[int, web.WebSocketResponse] = {}
        self.created = time.time()
        self.touched = self.created

    def expires(self) -> float:
        return self.touched + ROOM_HOURS * 3600

    def free_slot(self) -> int | None:
        for n in range(1, MAX_PEERS + 1):
            if n not in self.peers:
                return n
        return None


rooms: dict[str, Room] = {}


def room_token_from(request: web.Request) -> str:
    """Native clients send Authorization: Bearer <token>; browsers cannot, so the token may also come as a second
    WebSocket subprotocol "token.<token>" (never in the URL, which ends up in logs)."""
    tok = bearer(request)
    if tok:
        return tok
    for p in request.headers.get("Sec-WebSocket-Protocol", "").split(","):
        p = p.strip()
        if p.startswith("token."):
            return p[6:]
    return ""


async def health(request: web.Request) -> web.Response:
    return json_response(200, {
        "ok": True, "service": "lynx-relay", "api": VERSION, "uptime": int(time.time() - STARTED),
        "rooms": len(rooms), "peers": sum(len(r.peers) for r in rooms.values()), "maxPeers": MAX_PEERS,
        "frames": frames_total,
    })


async def create_room(request: web.Request) -> web.Response:
    if not room_limiter.allow(client_key(request)):
        return fail(429, "too many rooms, try again later", {"Retry-After": "600"})
    if len(rooms) >= MAX_ROOMS:
        return fail(503, "relay is full, try again later", {"Retry-After": "300"})
    rid = random_id(7)[:10]
    while rid in rooms:
        rid = random_id(7)[:10]
    token, host_token = random_token(), random_token()
    room = Room(rid, token, host_token)
    rooms[rid] = room
    log.info("room created %s… (%d rooms)", rid[:4], len(rooms))
    base = env_str("LYNX_RELAY_PUBLIC_URL", "wss://relay.lynx-share.com").rstrip("/")
    return json_response(201, {"ok": True, "room": rid, "token": token, "hostToken": host_token,
                               "maxPeers": MAX_PEERS, "expires": int(room.expires()),
                               "ws": f"{base}/v1/rooms/{rid}/ws", "subprotocol": SUBPROTOCOL})


async def close_room(request: web.Request) -> web.Response:
    room = rooms.get(request.match_info["room"])
    if not room:
        return fail(404, "no such room")
    if not token_matches(bearer(request), room.host_hash):
        return fail(401, "missing or wrong host token")
    await drop_room(room, "closed by host")
    return web.Response(status=204)


async def drop_room(room: Room, reason: str) -> None:
    rooms.pop(room.id, None)
    for ws in list(room.peers.values()):
        await ws.close(code=4000, message=reason.encode()[:120])
    room.peers.clear()


async def send_json(ws: web.WebSocketResponse, msg: dict) -> None:
    if not ws.closed:
        await ws.send_str(json.dumps(msg, separators=(",", ":")))


async def broadcast(room: Room, msg: dict, skip: int | None = None) -> None:
    for n, ws in list(room.peers.items()):
        if n != skip:
            await send_json(ws, msg)


async def connect(request: web.Request) -> web.StreamResponse:
    if not connect_limiter.allow(client_key(request)):
        return fail(429, "slow down", {"Retry-After": "60"})
    room = rooms.get(request.match_info["room"])
    if not room:
        return fail(404, "no such room")
    if not token_matches(room_token_from(request), room.token_hash):
        return fail(401, "missing or wrong room token")
    slot = room.free_slot()
    if slot is None:
        return fail(409, f"room is full ({MAX_PEERS} peers)")

    ws = web.WebSocketResponse(protocols=(SUBPROTOCOL,), heartbeat=25, max_msg_size=MAX_FRAME)
    await ws.prepare(request)
    room.peers[slot] = ws
    room.touched = time.time()
    await send_json(ws, {"type": "welcome", "peer": slot, "peers": sorted(room.peers), "maxPeers": MAX_PEERS})
    await broadcast(room, {"type": "join", "peer": slot}, skip=slot)
    log.info("room %s…: peer %d joined (%d/%d)", room.id[:4], slot, len(room.peers), MAX_PEERS)

    global frames_total
    budget, last = float(PEER_BYTES_PER_SEC), time.monotonic()
    try:
        async for msg in ws:
            if msg.type == WSMsgType.BINARY:
                data: bytes = msg.data
                now = time.monotonic()
                budget = min(PEER_BYTES_PER_SEC, budget + (now - last) * PEER_BYTES_PER_SEC)
                last = now
                budget -= len(data)
                if budget < 0:  # over the rate: wait instead of dropping (the stream is reliable)
                    await asyncio.sleep(-budget / PEER_BYTES_PER_SEC)
                if len(data) < 1:
                    continue
                dest = data[0]
                frames_total += 1
                out = bytes([slot]) + data[1:]
                targets = [n for n in room.peers if n != slot] if dest == 0 else [dest]
                for n in targets:
                    peer = room.peers.get(n)
                    if peer is not None and not peer.closed:
                        await peer.send_bytes(out)
            elif msg.type == WSMsgType.TEXT:
                try:
                    ctl = json.loads(msg.data)
                except ValueError:
                    ctl = {}
                if ctl.get("type") == "ping":
                    await send_json(ws, {"type": "pong", "t": ctl.get("t")})
                elif ctl.get("type") == "peers":
                    await send_json(ws, {"type": "peers", "peers": sorted(room.peers)})
                else:
                    await send_json(ws, {"type": "error", "error": "unknown control message"})
            elif msg.type == WSMsgType.ERROR:
                break
    finally:
        if room.peers.get(slot) is ws:
            del room.peers[slot]
        room.touched = time.time()
        await broadcast(room, {"type": "leave", "peer": slot})
        log.info("room %s…: peer %d left (%d/%d)", room.id[:4], slot, len(room.peers), MAX_PEERS)
    return ws


async def index(request: web.Request) -> web.Response:
    return json_response(200, {"ok": True, "service": "lynx-relay", "api": VERSION,
                               "docs": "https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/design/SHARE_API.md"})


async def gc_loop(app: web.Application) -> None:
    while True:
        await asyncio.sleep(300)
        now = time.time()
        for room in list(rooms.values()):
            if not room.peers and now >= room.expires():
                rooms.pop(room.id, None)
                log.info("room %s… expired", room.id[:4])


async def on_startup(app: web.Application) -> None:
    app["gc"] = asyncio.create_task(gc_loop(app))


async def on_shutdown(app: web.Application) -> None:
    for room in list(rooms.values()):
        await drop_room(room, "relay restarting")
    app["gc"].cancel()


def make_app() -> web.Application:
    app = web.Application(middlewares=[errors_middleware], client_max_size=16 * 1024)
    app.router.add_get("/", index)
    app.router.add_get("/health", health)
    app.router.add_post("/v1/rooms", create_room)
    app.router.add_delete("/v1/rooms/{room}", close_room)
    app.router.add_get("/v1/rooms/{room}/ws", connect)
    app.on_startup.append(on_startup)
    app.on_shutdown.append(on_shutdown)
    return app


if __name__ == "__main__":
    web.run_app(make_app(), host=env_str("LYNX_RELAY_HOST", "127.0.0.1"), port=env_int("LYNX_RELAY_PORT", 8082),
                access_log=None)
