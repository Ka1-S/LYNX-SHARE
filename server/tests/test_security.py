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


if __name__ == "__main__":
    unittest.main()
