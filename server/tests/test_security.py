"""Offline regressions: python3 -m unittest discover -s server/tests -p 'test_*.py'."""
import asyncio
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "app"))
import lynx_api as api
import lynx_common as common
import lynx_relay as relay


def request(ip):
    return SimpleNamespace(headers={"X-Forwarded-For": ip}, remote="127.0.0.1")


class ClientKeyTests(unittest.TestCase):
    def test_ipv6_same_network_and_equivalent_notation(self):
        key = common.client_key(request("2001:db8::1"))
        self.assertEqual(key, common.client_key(request("2001:db8::2")))
        self.assertEqual(key, common.client_key(request("2001:0db8:0000:0000::2")))
        self.assertNotEqual(key, common.client_key(request("2001:db8:0:1::1")))

    def test_mapped_ipv4_and_proxy_chain(self):
        key = common.client_key(request("192.0.2.1"))
        self.assertEqual(key, common.client_key(request("::ffff:192.0.2.1")))
        self.assertEqual(key, common.client_key(request("198.51.100.1, 192.0.2.1")))
        self.assertNotEqual(key, common.client_key(request("192.0.2.2")))

    def test_invalid_address_falls_back_to_peer(self):
        self.assertEqual(common.client_key(request("bad")), common.client_key(request("127.0.0.1")))


class RateLimiterTests(unittest.TestCase):
    def test_idle_buckets_expire_before_the_table_fills(self):
        limiter = common.RateLimiter(2, 60)
        with patch.object(common.time, "monotonic", return_value=0):
            self.assertTrue(limiter.allow("old"))
        with patch.object(common.time, "monotonic", return_value=60):
            self.assertTrue(limiter.allow("new"))
            self.assertEqual(list(limiter.buckets), ["new"])

    def test_burst_refill_and_denials_preserve_tokens(self):
        limiter = common.RateLimiter(2, 60)
        with patch.object(common.time, "monotonic", return_value=0):
            self.assertTrue(limiter.allow("a"))
            self.assertTrue(limiter.allow("a"))
            self.assertFalse(limiter.allow("a"))
        with patch.object(common.time, "monotonic", return_value=15):
            self.assertFalse(limiter.allow("a"))
        with patch.object(common.time, "monotonic", return_value=30):
            self.assertTrue(limiter.allow("a"))

    def test_capacity_fails_closed_then_idle_clients_expire(self):
        limiter = common.RateLimiter(2, 60)
        with patch.object(common, "MAX_CLIENTS", 2), patch.object(common.time, "monotonic", return_value=0):
            self.assertTrue(limiter.allow("a"))
            self.assertTrue(limiter.allow("b"))
            self.assertFalse(limiter.allow("c"))
            self.assertTrue(limiter.allow("a"))
            self.assertFalse(limiter.allow("a"))
            self.assertEqual(len(limiter.buckets), 2)
        with patch.object(common.time, "monotonic", return_value=60):
            self.assertTrue(limiter.allow("c"))
            self.assertEqual(list(limiter.buckets), ["c"])

    def test_recent_denial_does_not_expire_before_older_clients(self):
        limiter = common.RateLimiter(1, 60)
        with patch.object(common.time, "monotonic", return_value=0):
            limiter.allow("a")
            limiter.allow("b")
        with patch.object(common.time, "monotonic", return_value=30):
            self.assertFalse(limiter.allow("a"))
        with patch.object(common.time, "monotonic", return_value=60):
            self.assertTrue(limiter.allow("c"))
            self.assertEqual(list(limiter.buckets), ["a", "c"])


class HandlerTests(unittest.IsolatedAsyncioTestCase):
    async def test_concurrent_creates_respect_room_capacity(self):
        release, both_waiting = asyncio.Event(), asyncio.Event()
        waiting = 0

        class Request:
            headers = {}
            remote = "127.0.0.1"
            can_read_body = True

            async def json(self):
                nonlocal waiting
                waiting += 1
                if waiting == 2:
                    both_waiting.set()
                await release.wait()
                return {}

        with patch.object(relay, "MAX_ROOMS", 1), patch.object(relay, "rooms", {}), \
                patch.object(relay, "room_limiter", common.RateLimiter(10, 3600)):
            tasks = [asyncio.create_task(relay.create_room(Request())) for _ in range(2)]
            try:
                await asyncio.wait_for(both_waiting.wait(), timeout=3)
                release.set()
                responses = await asyncio.gather(*tasks)
                self.assertEqual(sorted(r.status for r in responses), [201, 503])
                self.assertEqual(len(relay.rooms), 1)
            finally:
                for task in tasks:
                    task.cancel()
                await asyncio.gather(*tasks, return_exceptions=True)

    async def test_daily_accounting_is_bounded_and_resets_at_midnight(self):
        class Request:
            headers = {}
            remote = "127.0.0.1"

            async def json(self):
                return {"name": "test.lynxpack", "size": 4}

        with tempfile.TemporaryDirectory() as folder, \
                patch.object(api, "DATA_DIR", folder), patch.object(api, "SHARES_DIR", folder), \
                patch.object(api, "MIN_FREE_BYTES", 0), patch.object(api, "MAX_CLIENTS", 1), \
                patch.object(api, "daily_bytes", {"other": 4}), patch.object(api, "daily_day", "2000-01-01"), \
                patch.object(api, "create_limiter", common.RateLimiter(10, 3600)), \
                patch.object(api.time, "strftime", return_value="2000-01-01") as day:
            self.assertEqual((await api.create_share(Request())).status, 503)
            self.assertEqual(len(api.daily_bytes), 1)
            day.return_value = "2000-01-02"
            self.assertEqual((await api.create_share(Request())).status, 201)
            self.assertEqual(len(api.daily_bytes), 1)
            self.assertEqual(sum(api.daily_bytes.values()), 4)
            self.assertEqual(api.daily_day, "2000-01-02")


class RelayLimitTests(unittest.IsolatedAsyncioTestCase):
    """Round 190ah: the relay cannot be filled or used as a free tunnel."""

    def test_day_budget_counts_a_window_per_client(self):
        budget = relay.DayBudget(100, 60)
        with patch.object(relay.time, "monotonic", return_value=0):
            self.assertTrue(budget.spend("a", 60))
            self.assertTrue(budget.spend("a", 40))
            self.assertFalse(budget.spend("a", 1))
            self.assertTrue(budget.spend("b", 100))
        with patch.object(relay.time, "monotonic", return_value=59):
            self.assertFalse(budget.spend("a", 1))
        with patch.object(relay.time, "monotonic", return_value=61):
            self.assertTrue(budget.spend("a", 100))  # a new window
            budget.sweep()
            self.assertEqual(list(budget.used), ["a"])

    def test_room_limits(self):
        with patch.object(relay, "ROOM_BYTES", 1000), patch.object(relay, "ROOM_MAX_HOURS", 72), \
                patch.object(relay, "ROOM_IDLE_HOURS", 12):
            room = relay.Room("abcdefghij", "t" * 43, "h" * 43, "client")
            now = room.created
            self.assertEqual(room.over_limit(now), "")
            room.bytes = 1001
            self.assertEqual(room.over_limit(now), "traffic limit")
            room.bytes = 0
            self.assertEqual(room.over_limit(now + 73 * 3600), "open too long")
            # silent for 13 h: only a room somebody is connected to is closed for it (an empty one just expires)
            self.assertEqual(room.over_limit(now + 13 * 3600), "")
            room.peers[1] = object()
            self.assertEqual(room.over_limit(now + 13 * 3600), "idle too long")
            room.last_frame = now + 12 * 3600
            self.assertEqual(room.over_limit(now + 13 * 3600), "")

    async def test_rooms_open_per_client(self):
        class Request:
            headers = {"X-Forwarded-For": "192.0.2.7"}
            remote = "127.0.0.1"
            can_read_body = False

        class Other(Request):
            headers = {"X-Forwarded-For": "192.0.2.8"}

        with patch.object(relay, "ROOMS_PER_CLIENT", 2), patch.object(relay, "rooms", {}), \
                patch.object(relay, "room_limiter", common.RateLimiter(100, 3600)):
            self.assertEqual((await relay.create_room(Request())).status, 201)
            self.assertEqual((await relay.create_room(Request())).status, 201)
            self.assertEqual((await relay.create_room(Request())).status, 429)
            self.assertEqual((await relay.create_room(Other())).status, 201)  # somebody else is not affected
            # closing one frees a place
            relay.rooms.pop(next(k for k, r in relay.rooms.items() if r.creator == common.client_key(Request())))
            self.assertEqual((await relay.create_room(Request())).status, 201)

    async def test_sweep_closes_rooms_over_a_limit_with_the_limit_code(self):
        closed = []

        class Socket:
            closed = False

            async def close(self, code=1000, message=b""):
                closed.append((code, message))

        with patch.object(relay, "rooms", {}), patch.object(relay, "ROOM_BYTES", 10), patch.object(relay, "limits_hit", 0):
            busy = relay.Room("aaaaaaaaaa", "t" * 43, "h" * 43)
            busy.peers[1] = Socket()
            busy.bytes = 11
            fine = relay.Room("bbbbbbbbbb", "t" * 43, "h" * 43)
            fine.peers[1] = Socket()
            empty = relay.Room("cccccccccc", "t" * 43, "h" * 43)
            relay.rooms.update({r.id: r for r in (busy, fine, empty)})
            await relay.sweep(busy.created + 1)
            self.assertEqual(sorted(relay.rooms), ["bbbbbbbbbb", "cccccccccc"])
            self.assertEqual(closed, [(relay.CLOSE_LIMIT, b"traffic limit")])
            self.assertEqual(relay.limits_hit, 1)
            # an empty room nobody came back to is forgotten after ROOM_HOURS, as before
            await relay.sweep(empty.created + relay.ROOM_HOURS * 3600 + 1)
            self.assertNotIn("cccccccccc", relay.rooms)

class RelayOutboxTests(unittest.IsolatedAsyncioTestCase):
    """Round 190oh: a peer that reads slowly holds up nobody else (no head-of-line blocking) and is closed past its limit."""

    class Socket:
        def __init__(self, gate=None):
            self.closed = False
            self.got = []
            self.gate = gate
            self.close_code = None

        async def send_bytes(self, data):
            if self.gate is not None:
                await self.gate.wait()
            self.got.append(data)

        async def send_str(self, text):
            self.got.append(text)

        async def close(self, code=1000, message=b""):
            self.closed = True
            self.close_code = code

    async def test_a_slow_reader_does_not_delay_the_others(self):
        gate = asyncio.Event()
        slow, fast = self.Socket(gate), self.Socket()
        slow_box, fast_box = relay.Outbox(slow, limit=10_000), relay.Outbox(fast, limit=10_000)
        for i in range(5):
            self.assertTrue(slow_box.put(b"m" * 1000))       # never waits, even though nothing is read
            self.assertTrue(fast_box.put(bytes([i]) * 10))
        await asyncio.sleep(0.05)
        self.assertEqual(len(fast.got), 5)                   # all of them, in order
        self.assertEqual([g[0] for g in fast.got], [0, 1, 2, 3, 4])
        self.assertEqual(slow.got, [])
        gate.set()
        await asyncio.sleep(0.05)
        self.assertEqual(len(slow.got), 5)
        self.assertEqual(slow_box.waiting, 0)
        await slow_box.finish()
        await fast_box.finish()

    async def test_a_peer_over_its_queue_limit_is_closed(self):
        gate = asyncio.Event()
        slow = self.Socket(gate)
        box = relay.Outbox(slow, limit=2500)
        self.assertTrue(box.put(b"x" * 1000))
        self.assertTrue(box.put(b"x" * 1000))
        self.assertFalse(box.put(b"x" * 1000))               # over 2500 waiting: closed as too slow
        await asyncio.sleep(0.01)
        self.assertTrue(slow.closed)
        self.assertEqual(slow.close_code, relay.CLOSE_SLOW)
        self.assertFalse(box.put(b"y"))
        gate.set()
        await box.finish()

    async def test_control_messages_keep_their_place_among_frames(self):
        sock = self.Socket()
        room = relay.Room("abcdefghij", "t" * 43, "h" * 43, "client")
        room.peers[2] = sock
        room.outboxes[2] = relay.Outbox(sock)
        room.outboxes[2].put(b"frame-1")
        await relay.broadcast(room, {"type": "join", "peer": 3})
        room.outboxes[2].put(b"frame-2")
        await asyncio.sleep(0.02)
        self.assertEqual(sock.got[0], b"frame-1")
        self.assertIn('"join"', sock.got[1])
        self.assertEqual(sock.got[2], b"frame-2")
        await room.outboxes[2].finish()



class RelayKickTests(unittest.IsolatedAsyncioTestCase):
    """Round 190on: the host removes a guest (POST /v1/rooms/<room>/kick): its slot is freed, its address stays out of
    the room, and the room token can change for new joins while the peers already in stay connected."""

    async def asyncSetUp(self):
        from aiohttp.test_utils import TestClient, TestServer
        self.patches = [patch.object(relay, "rooms", {}), patch.object(relay, "room_limiter", common.RateLimiter(100, 3600)),
                        patch.object(relay, "connect_limiter", common.RateLimiter(1000, 60))]
        for p in self.patches:
            p.start()
        self.client = TestClient(TestServer(relay.make_app()))
        await self.client.start_server()

    async def asyncTearDown(self):
        await self.client.close()
        for p in reversed(self.patches):
            p.stop()

    async def open_room(self, token):
        r = await self.client.post("/v1/rooms", json={"token": token}, headers={"X-Forwarded-For": "192.0.2.1"})
        self.assertEqual(r.status, 201)
        body = await r.json()
        return body["room"], body["hostToken"]

    async def join(self, room, token, ip):
        ws = await self.client.ws_connect(f"/v1/rooms/{room}/ws", protocols=(relay.SUBPROTOCOL,),
                                          headers={"Authorization": f"Bearer {token}", "X-Forwarded-For": ip})
        welcome = await ws.receive_json(timeout=3)
        self.assertEqual(welcome["type"], "welcome")
        return ws, welcome["peer"]

    async def try_join(self, room, token, ip):
        from aiohttp import WSServerHandshakeError
        try:
            ws, _ = await self.join(room, token, ip)
        except WSServerHandshakeError as e:
            return e.status
        await ws.close()
        return 101

    async def kick(self, room, host_token, body, ip="192.0.2.1"):
        r = await self.client.post(f"/v1/rooms/{room}/kick", json=body,
                                   headers={"Authorization": f"Bearer {host_token}", "X-Forwarded-For": ip})
        return r.status, (await r.json() if r.status == 200 else None)

    async def next_binary(self, ws):
        while True:
            msg = await ws.receive(timeout=3)
            if msg.type.name == "BINARY":
                return msg.data
            if msg.type.name in ("CLOSE", "CLOSED", "CLOSING", "ERROR"):
                return None

    async def test_remove_with_a_new_code_for_new_joins_only(self):
        old, new = "a" * 43, "b" * 43
        room, host_token = await self.open_room(old)
        host, host_peer = await self.join(room, old, "192.0.2.1")
        ana, ana_peer = await self.join(room, old, "198.51.100.7")
        lia, lia_peer = await self.join(room, old, "203.0.113.9")
        self.assertEqual(await self.kick(room, "x" * 43, {"peer": ana_peer}), (401, None))
        status, body = await self.kick(room, host_token, {"peer": ana_peer, "token": new})
        self.assertEqual(status, 200)
        self.assertTrue(body["removed"])
        # Ana's socket is closed with 4003; her slot is free at once
        while True:
            msg = await ana.receive(timeout=3)
            if msg.type.name in ("CLOSE", "CLOSED"):
                break
        self.assertEqual(ana.close_code, relay.CLOSE_REMOVED)
        for _ in range(100):  # (the relay's side of the connection ends right after)
            if ana_peer not in relay.rooms[room].peers:
                break
            await asyncio.sleep(0.01)
        self.assertNotIn(ana_peer, relay.rooms[room].peers)
        # the host and Lia stay connected and still reach each other
        await host.send_bytes(bytes([lia_peer]) + b"still here")
        self.assertEqual(await self.next_binary(lia), bytes([host_peer]) + b"still here")
        # the old code no longer lets anybody in; Ana is refused with the new one too (her address)
        self.assertEqual(await self.try_join(room, old, "192.0.2.200"), 401)
        self.assertEqual(await self.try_join(room, new, "198.51.100.7"), 403)
        # somebody new with the new code takes the free slot
        mia, mia_peer = await self.join(room, new, "192.0.2.77")
        self.assertEqual(mia_peer, ana_peer)
        for ws in (host, lia, mia):
            await ws.close()

    async def test_remove_keeping_the_code(self):
        code = "c" * 43
        room, host_token = await self.open_room(code)
        host, _ = await self.join(room, code, "192.0.2.1")
        ana, ana_peer = await self.join(room, code, "198.51.100.7")
        self.assertEqual((await self.kick(room, host_token, {"peer": ana_peer}))[0], 200)
        await ana.receive(timeout=3)
        # the code still works for others, not for the removed guest's address
        self.assertEqual(await self.try_join(room, code, "198.51.100.7"), 403)
        self.assertEqual(await self.try_join(room, code, "203.0.113.50"), 101)
        # without "ban" the guest may come back (the host only freed the slot)
        lia, lia_peer = await self.join(room, code, "203.0.113.9")
        self.assertEqual((await self.kick(room, host_token, {"peer": lia_peer, "ban": False}))[0], 200)
        await lia.receive(timeout=3)
        self.assertEqual(await self.try_join(room, code, "203.0.113.9"), 101)
        await host.close()

    async def test_a_guest_on_the_hosts_network_is_removed_without_locking_the_host_out(self):
        code = "d" * 43
        room, host_token = await self.open_room(code)
        host, _ = await self.join(room, code, "192.0.2.1")
        same, same_peer = await self.join(room, code, "192.0.2.1")
        self.assertEqual((await self.kick(room, host_token, {"peer": same_peer}))[0], 200)
        await same.receive(timeout=3)
        self.assertEqual(relay.rooms[room].banned, set())
        self.assertEqual(await self.try_join(room, code, "192.0.2.1"), 101)
        await host.close()

    async def test_kick_rejects_bad_bodies(self):
        room, host_token = await self.open_room("e" * 43)
        for body in ({"peer": "1"}, {"peer": 9}, {"peer": -1}, {"peer": True}, {"peer": 1, "ban": "yes"}, {"peer": 1, "token": "short"}):
            self.assertEqual((await self.kick(room, host_token, body))[0], 400, body)
        self.assertEqual((await self.kick("zzzzzzzzzz", host_token, {"peer": 1}))[0], 404)
        # peer 0 with a token only changes the token; nobody is removed
        status, body = await self.kick(room, host_token, {"peer": 0, "token": "f" * 43})
        self.assertEqual((status, body["removed"]), (200, False))
        self.assertEqual(await self.try_join(room, "e" * 43, "203.0.113.1"), 401)
        self.assertEqual(await self.try_join(room, "f" * 43, "203.0.113.1"), 101)


if __name__ == "__main__":
    unittest.main()
