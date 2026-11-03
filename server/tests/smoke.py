"""End-to-end smoke test of the LYNX SHARE server (share links + relay).

    python3 server/tests/smoke.py                          # the live server (api./relay.lynx-share.com)
    python3 server/tests/smoke.py http://127.0.0.1:8081 ws://127.0.0.1:8082    # local services

Needs aiohttp (pip install aiohttp, or apt install python3-aiohttp). Creates one small share and one room, checks
them, and deletes both. Exit code 0 = all good.
"""
from __future__ import annotations

import asyncio
import hashlib
import io
import json
import os
import sys
import zipfile

import aiohttp

API = sys.argv[1] if len(sys.argv) > 1 else "https://api.lynx-share.com"
RELAY = sys.argv[2] if len(sys.argv) > 2 else "wss://relay.lynx-share.com"
RELAY_HTTP = RELAY.replace("wss://", "https://").replace("ws://", "http://")
failures = 0


def check(cond: bool, what: str) -> None:
    global failures
    print(("ok   " if cond else "FAIL ") + what)
    if not cond:
        failures += 1


def make_pack(size: int) -> bytes:
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_STORED) as z:
        z.writestr("project.lynx", json.dumps({"smoke": True}))
        z.writestr("Samples/noise.bin", os.urandom(size))
    return buf.getvalue()


async def shares(s: aiohttp.ClientSession) -> None:
    async with s.get(f"{API}/health") as r:
        h = await r.json()
        check(r.status == 200 and h.get("ok") and h.get("service") == "lynx-api", f"api /health {r.status} {h}")

    pack = make_pack(3 * 1024 * 1024 + 123)
    sha = hashlib.sha256(pack).hexdigest()

    # bad requests are refused
    async with s.post(f"{API}/v1/shares", json={"name": "x.exe", "size": 10}) as r:
        check(r.status == 400, f"wrong extension refused ({r.status})")
    async with s.post(f"{API}/v1/shares", json={"name": "big.lynxpack", "size": 3 * 1024 ** 3}) as r:
        check(r.status == 413, f"over 2 GB refused ({r.status})")

    async with s.post(f"{API}/v1/shares", json={"name": "Smoke test.lynxpack", "size": len(pack), "days": 1,
                                                 "sha256": sha}) as r:
        c = await r.json()
        check(r.status == 201 and len(c["id"]) == 26 and c["token"], f"create {r.status}")
    sid, tok = c["id"], c["token"]
    auth = {"Authorization": f"Bearer {tok}"}

    async with s.get(f"{API}/v1/shares/{sid}/download") as r:
        check(r.status == 409, f"download before upload finished refused ({r.status})")
    async with s.patch(c["uploadUrl"], data=pack[:100], headers={"Upload-Offset": "0"}) as r:
        check(r.status == 401, f"upload without token refused ({r.status})")
    async with s.patch(c["uploadUrl"], data=b"MZ" + pack[2:100], headers={**auth, "Upload-Offset": "0"}) as r:
        check(r.status == 415, f"non-zip refused ({r.status})")

    # resumable upload in two parts, with a wrong offset in between
    half = len(pack) // 2
    async with s.patch(c["uploadUrl"], data=pack[:half], headers={**auth, "Upload-Offset": "0"}) as r:
        j = await r.json()
        check(r.status == 200 and j["offset"] == half and not j["complete"], f"part 1 ({r.status})")
    async with s.patch(c["uploadUrl"], data=pack[half:], headers={**auth, "Upload-Offset": "0"}) as r:
        check(r.status == 409 and r.headers.get("Upload-Offset") == str(half), f"wrong offset refused ({r.status})")
    async with s.get(f"{API}/v1/shares/{sid}") as r:
        j = await r.json()
        check(j["received"] == half, f"received after part 1 = {j['received']}")
    async with s.patch(c["uploadUrl"], data=pack[half:], headers={**auth, "Upload-Offset": str(half)}) as r:
        j = await r.json()
        check(r.status == 200 and j["complete"] and j["sha256"] == sha, f"part 2 completes, sha256 ok ({r.status})")

    async with s.get(j["downloadUrl"]) as r:
        body = await r.read()
        check(r.status == 200 and body == pack, f"download identical ({len(body)} bytes)")
        check("attachment" in r.headers.get("Content-Disposition", ""), "download is an attachment")
    async with s.get(j["downloadUrl"], headers={"Range": "bytes=10-19"}) as r:
        body = await r.read()
        check(r.status == 206 and body == pack[10:20], f"range request ({r.status})")
    async with s.get(j["url"]) as r:
        text = await r.text()
        check(r.status == 200 and "Smoke test.lynxpack" in text and "Download" in text, f"link page ({r.status})")
    async with s.get(f"{API}/v1/shares/{'a' * 26}") as r:
        check(r.status == 404, f"unknown id 404 ({r.status})")
    async with s.get(f"{API}/v1/shares/") as r:
        check(r.status == 404, f"no listing ({r.status})")

    async with s.delete(f"{API}/v1/shares/{sid}") as r:
        check(r.status == 401, f"delete without token refused ({r.status})")
    async with s.delete(f"{API}/v1/shares/{sid}", headers=auth) as r:
        check(r.status == 204, f"delete ({r.status})")
    async with s.get(j["downloadUrl"]) as r:
        check(r.status == 404, f"gone after delete ({r.status})")


async def relay(s: aiohttp.ClientSession) -> None:
    async with s.get(f"{RELAY_HTTP}/health") as r:
        h = await r.json()
        check(r.status == 200 and h.get("service") == "lynx-relay", f"relay /health {r.status} {h}")
    async with s.post(f"{RELAY_HTTP}/v1/rooms") as r:
        room = await r.json()
        check(r.status == 201 and room["maxPeers"] == 4, f"room created ({r.status})")
    url = f"{RELAY}/v1/rooms/{room['room']}/ws"
    auth = {"Authorization": f"Bearer {room['token']}"}

    try:
        async with s.ws_connect(url, headers={"Authorization": "Bearer wrong"}):
            check(False, "wrong room token refused")
    except aiohttp.WSServerHandshakeError as e:
        check(e.status == 401, f"wrong room token refused ({e.status})")

    peers = []
    for i in range(4):
        if i == 3:  # the browser way: token as a second subprotocol
            ws = await s.ws_connect(url, protocols=("lynx-relay.v1", "token." + room["token"]))
        else:
            ws = await s.ws_connect(url, headers=auth, protocols=("lynx-relay.v1",))
        w = await ws.receive_json(timeout=5)
        check(w["type"] == "welcome" and w["peer"] == i + 1, f"peer {i + 1} welcome {w}")
        peers.append(ws)
    try:
        async with s.ws_connect(url, headers=auth):
            check(False, "5th peer refused")
    except aiohttp.WSServerHandshakeError as e:
        check(e.status == 409, f"5th peer refused ({e.status})")

    async def next_binary(ws):
        while True:
            m = await ws.receive(timeout=5)
            if m.type == aiohttp.WSMsgType.BINARY:
                return m.data

    # echo (destination = own peer id), unicast, broadcast
    await peers[0].send_bytes(bytes([1]) + b"echo")
    check(await next_binary(peers[0]) == bytes([1]) + b"echo", "echo to self")
    await peers[0].send_bytes(bytes([3]) + b"to three")
    check(await next_binary(peers[2]) == bytes([1]) + b"to three", "peer 1 -> peer 3")
    big = os.urandom(512 * 1024)
    await peers[1].send_bytes(bytes([0]) + big)
    got = [await next_binary(p) for p in (peers[0], peers[2], peers[3])]
    check(all(g == bytes([2]) + big for g in got), "peer 2 broadcast 512 KB to the 3 others")
    await peers[3].send_json({"type": "ping", "t": 7})
    while True:
        m = await peers[3].receive_json(timeout=5)
        if m["type"] == "pong":
            break
    check(m["t"] == 7, "ping/pong")

    # a room whose token the host chose (LYNX derives it from the invite code): not echoed back, and it works
    chosen = "lynx-smoke-" + os.urandom(24).hex()
    async with s.post(f"{RELAY_HTTP}/v1/rooms", json={"token": "short"}) as r:
        check(r.status == 400, f"a too short chosen token refused ({r.status})")
    async with s.post(f"{RELAY_HTTP}/v1/rooms", json={"token": chosen}) as r:
        room2 = await r.json()
        check(r.status == 201 and room2["token"] == "", f"room with a chosen token ({r.status})")
    url2 = f"{RELAY}/v1/rooms/{room2['room']}/ws"
    try:
        async with s.ws_connect(url2, headers=auth):
            check(False, "the other room's token refused")
    except aiohttp.WSServerHandshakeError as e:
        check(e.status == 401, f"the other room's token refused ({e.status})")
    ws2 = await s.ws_connect(url2, headers={"Authorization": f"Bearer {chosen}"}, protocols=("lynx-relay.v1",))
    w = await ws2.receive_json(timeout=5)
    check(w["type"] == "welcome" and w["peer"] == 1, f"chosen token connects {w}")
    async with s.delete(f"{RELAY_HTTP}/v1/rooms/{room2['room']}",
                        headers={"Authorization": f"Bearer {room2['hostToken']}"}) as r:
        check(r.status == 204, f"room 2 closed ({r.status})")
    await ws2.close()

    async with s.delete(f"{RELAY_HTTP}/v1/rooms/{room['room']}") as r:
        check(r.status == 401, f"close without host token refused ({r.status})")
    async with s.delete(f"{RELAY_HTTP}/v1/rooms/{room['room']}",
                        headers={"Authorization": f"Bearer {room['hostToken']}"}) as r:
        check(r.status == 204, f"room closed by host ({r.status})")
    for ws in peers:
        while (await ws.receive(timeout=5)).type not in (aiohttp.WSMsgType.CLOSE, aiohttp.WSMsgType.CLOSED,
                                                         aiohttp.WSMsgType.CLOSING):
            pass
        await ws.close()
    check(all(ws.closed for ws in peers), "every peer disconnected")


async def main() -> int:
    async with aiohttp.ClientSession(timeout=aiohttp.ClientTimeout(total=120)) as s:
        await shares(s)
        await relay(s)
    print("FAILURES:", failures)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))
