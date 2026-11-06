"""Shared helpers of the LYNX SHARE server services (lynx_api.py, lynx_relay.py).

LYNX SHARE (AGPL-3.0-or-later). Standard library + aiohttp only (Ubuntu package python3-aiohttp).
"""
from __future__ import annotations

import base64
import hashlib
import hmac
import ipaddress
import json
import logging
import os
import secrets
import time
from collections import OrderedDict

from aiohttp import web

VERSION = "1"  # bumped when the HTTP API changes in a way clients must know about
MAX_CLIENTS = 50000


def env_int(name: str, default: int) -> int:
    try:
        return int(os.environ.get(name, default))
    except ValueError:
        return default


def env_str(name: str, default: str) -> str:
    return os.environ.get(name, default)


def setup_logging(service: str) -> logging.Logger:
    logging.basicConfig(level=env_str("LYNX_LOG_LEVEL", "INFO"),
                        format="%(levelname)s " + service + ": %(message)s")
    # aiohttp's own access log carries the client address; Caddy keeps the (address-free) access log instead.
    logging.getLogger("aiohttp.access").setLevel(logging.WARNING)
    return logging.getLogger(service)


def random_id(nbytes: int = 16) -> str:
    """Unguessable lowercase base32 id (16 bytes = 128 bits = 26 chars)."""
    return base64.b32encode(secrets.token_bytes(nbytes)).decode("ascii").rstrip("=").lower()


def random_token() -> str:
    """256-bit bearer token, URL-safe."""
    return secrets.token_urlsafe(32)


def token_hash(token: str) -> str:
    return hashlib.sha256(token.encode("utf-8")).hexdigest()


def token_matches(token: str, stored_hash: str) -> bool:
    return bool(token) and hmac.compare_digest(token_hash(token), stored_hash)


def bearer(request: web.Request) -> str:
    auth = request.headers.get("Authorization", "")
    if auth.startswith("Bearer "):
        return auth[7:].strip()
    return ""


def client_key(request: web.Request) -> str:
    """Who is asking, for rate limits only. Never stored or logged: a salted hash kept in memory.

    The services listen on 127.0.0.1 behind Caddy, which overwrites X-Forwarded-For with the real peer address
    (no trusted_proxies configured), so the last element is the client."""
    xff = request.headers.get("X-Forwarded-For", "")
    ip = xff.split(",")[-1].strip() if xff else (request.remote or "")
    try:
        address = ipaddress.ip_address(ip)
        if isinstance(address, ipaddress.IPv6Address):
            address = address.ipv4_mapped or ipaddress.ip_network((address, 64), strict=False).network_address
        ip = str(address)
    except ValueError:
        ip = request.remote or ""
    salt = env_str("LYNX_SALT", "dev-salt")
    return hmac.new(salt.encode(), ip.encode(), hashlib.sha256).hexdigest()[:24]


class RateLimiter:
    """Token buckets per client key: `rate` events per `per` seconds, burst = rate. In memory (one process)."""

    def __init__(self, rate: float, per: float):
        self.rate, self.per = float(rate), float(per)
        self.buckets: OrderedDict[str, tuple[float, float]] = OrderedDict()

    def allow(self, key: str, cost: float = 1.0) -> bool:
        now = time.monotonic()
        while self.buckets:
            _, (_, last) = next(iter(self.buckets.items()))
            if now - last < self.per:
                break
            self.buckets.popitem(last=False)
        if key not in self.buckets and len(self.buckets) >= MAX_CLIENTS:
            return False  # preserve active clients' limits when the table is full
        tokens, last = self.buckets.get(key, (self.rate, now))
        tokens = min(self.rate, tokens + (now - last) * self.rate / self.per)
        allowed = tokens >= cost
        self.buckets[key] = (tokens - cost if allowed else tokens, now)
        self.buckets.move_to_end(key)
        return allowed


def json_response(status: int, body: dict, headers: dict | None = None) -> web.Response:
    h = {"Cache-Control": "no-store", "X-Content-Type-Options": "nosniff"}
    if headers:
        h.update(headers)
    return web.Response(status=status, text=json.dumps(body, separators=(",", ":")),
                        content_type="application/json", headers=h)


def fail(status: int, error: str, headers: dict | None = None) -> web.Response:
    return json_response(status, {"ok": False, "error": error}, headers)


@web.middleware
async def errors_middleware(request: web.Request, handler):
    try:
        return await handler(request)
    except web.HTTPException as e:
        if e.status >= 400 and not isinstance(e, web.HTTPMethodNotAllowed):
            return fail(e.status, e.reason.lower())
        raise
