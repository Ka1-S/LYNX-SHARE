"""LYNX SHARE API service: /health and the Online Sessions P1 share links (/v1/shares, /s/<id>).

Runs on 127.0.0.1:8081 behind Caddy (api.lynx-share.com). API reference: docs/design/SHARE_API.md.
LYNX SHARE (AGPL-3.0-or-later).
"""
from __future__ import annotations

import asyncio
import hashlib
import html
import json
import os
import re
import shutil
import time
import urllib.parse

from aiohttp import web

from lynx_common import (VERSION, RateLimiter, bearer, client_key, env_int, env_str, errors_middleware, fail,
                         json_response, random_id, random_token, setup_logging, token_hash, token_matches)

log = setup_logging("lynx-api")

GiB = 1024 ** 3
DATA_DIR = env_str("LYNX_DATA_DIR", "/var/lib/lynx")
SHARES_DIR = os.path.join(DATA_DIR, "shares")
PUBLIC_URL = env_str("LYNX_PUBLIC_URL", "https://api.lynx-share.com").rstrip("/")
MAX_SIZE = env_int("LYNX_SHARE_MAX_BYTES", 2 * GiB)
MAX_DAYS = env_int("LYNX_SHARE_MAX_DAYS", 30)
DEFAULT_DAYS = env_int("LYNX_SHARE_DEFAULT_DAYS", 7)
TOTAL_CAP = env_int("LYNX_SHARE_TOTAL_BYTES", 60 * GiB)        # all active shares together (declared sizes)
CLIENT_DAILY_BYTES = env_int("LYNX_SHARE_CLIENT_DAILY_BYTES", 8 * GiB)
CREATES_PER_HOUR = env_int("LYNX_SHARE_CREATES_PER_HOUR", 20)
INCOMPLETE_HOURS = env_int("LYNX_SHARE_INCOMPLETE_HOURS", 24)  # unfinished uploads are dropped after this
MIN_FREE_BYTES = env_int("LYNX_MIN_FREE_BYTES", 10 * GiB)      # keep this much disk free for everything else
CHUNK = 1024 * 1024

ID_RE = re.compile(r"^[a-z2-7]{26}$")
NAME_RE = re.compile(r"^[^/\\\x00-\x1f\x7f]{1,120}\.lynxpack$", re.IGNORECASE)
SHA_RE = re.compile(r"^[0-9a-f]{64}$")
ZIP_MAGIC = b"PK\x03\x04"

STARTED = time.time()
create_limiter = RateLimiter(CREATES_PER_HOUR, 3600)
patch_limiter = RateLimiter(600, 60)      # upload requests (not bytes) per client
read_limiter = RateLimiter(300, 60)       # metadata / page / download requests per client
daily_bytes: dict[str, tuple[str, int]] = {}  # client key -> (UTC day, declared bytes created that day)
locks: dict[str, asyncio.Lock] = {}


# ---- storage --------------------------------------------------------------------------------------------------

def share_dir(sid: str) -> str:
    return os.path.join(SHARES_DIR, sid)


def blob_path(sid: str) -> str:
    return os.path.join(share_dir(sid), "blob")


def read_meta(sid: str) -> dict | None:
    if not ID_RE.match(sid):
        return None
    try:
        with open(os.path.join(share_dir(sid), "meta.json"), encoding="utf-8") as f:
            meta = json.load(f)
    except (OSError, ValueError):
        return None
    if is_dead(meta):
        delete_share(sid)
        return None
    return meta


def write_meta(meta: dict) -> None:
    path = os.path.join(share_dir(meta["id"]), "meta.json")
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(meta, f, separators=(",", ":"))
    os.chmod(tmp, 0o600)
    os.replace(tmp, path)


def delete_share(sid: str) -> None:
    shutil.rmtree(share_dir(sid), ignore_errors=True)
    locks.pop(sid, None)


def is_dead(meta: dict, now: float | None = None) -> bool:
    now = now or time.time()
    if now >= meta["expires"]:
        return True
    return not meta["complete"] and now >= meta["created"] + INCOMPLETE_HOURS * 3600


def all_metas() -> list[dict]:
    out = []
    try:
        names = os.listdir(SHARES_DIR)
    except OSError:
        return out
    for sid in names:
        m = read_meta(sid)
        if m:
            out.append(m)
        elif ID_RE.match(sid) and not os.path.exists(os.path.join(share_dir(sid), "meta.json")):
            # a folder without metadata is a create that died half-way
            if time.time() - os.path.getmtime(share_dir(sid)) > 3600:
                delete_share(sid)
    return out


def received_bytes(sid: str) -> int:
    try:
        return os.path.getsize(blob_path(sid))
    except OSError:
        return 0


def public_view(meta: dict) -> dict:
    sid = meta["id"]
    v = {
        "ok": True,
        "id": sid,
        "name": meta["name"],
        "size": meta["size"],
        "received": meta["size"] if meta["complete"] else received_bytes(sid),
        "complete": meta["complete"],
        "created": int(meta["created"]),
        "expires": int(meta["expires"]),
        "url": f"{PUBLIC_URL}/s/{sid}",
        "downloadUrl": f"{PUBLIC_URL}/v1/shares/{sid}/download",
    }
    if meta["complete"]:
        v["sha256"] = meta["sha256"]
    return v


def file_sha256(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while True:
            b = f.read(CHUNK)
            if not b:
                break
            h.update(b)
    return h.hexdigest()


# ---- handlers -------------------------------------------------------------------------------------------------

async def health(request: web.Request) -> web.Response:
    du = shutil.disk_usage(DATA_DIR)
    metas = all_metas()
    return json_response(200, {
        "ok": True, "service": "lynx-api", "api": VERSION,
        "uptime": int(time.time() - STARTED),
        "shares": len(metas),
        "shareBytes": sum(m["size"] for m in metas),
        "diskFree": du.free,
    })


async def create_share(request: web.Request) -> web.Response:
    key = client_key(request)
    if not create_limiter.allow(key):
        return fail(429, "too many shares, try again later", {"Retry-After": "600"})
    try:
        body = await request.json()
    except (ValueError, UnicodeDecodeError):
        return fail(400, "body must be JSON")
    if not isinstance(body, dict):
        return fail(400, "body must be a JSON object")
    name = body.get("name")
    size = body.get("size")
    days = body.get("days", DEFAULT_DAYS)
    sha = body.get("sha256")
    if not isinstance(name, str) or not NAME_RE.match(name):
        return fail(400, "name must be a file name ending in .lynxpack (at most 120 characters)")
    if not isinstance(size, int) or isinstance(size, bool) or size < len(ZIP_MAGIC):
        return fail(400, "size must be the file size in bytes")
    if size > MAX_SIZE:
        return fail(413, f"file too large (at most {MAX_SIZE} bytes)")
    if not isinstance(days, int) or isinstance(days, bool) or not 1 <= days <= MAX_DAYS:
        return fail(400, f"days must be 1..{MAX_DAYS}")
    if sha is not None and (not isinstance(sha, str) or not SHA_RE.match(sha.lower())):
        return fail(400, "sha256 must be 64 hex digits")

    today = time.strftime("%Y-%m-%d", time.gmtime())
    day, used = daily_bytes.get(key, (today, 0))
    if day != today:
        used = 0
    if used + size > CLIENT_DAILY_BYTES:
        return fail(429, "daily upload volume reached, try again tomorrow", {"Retry-After": "3600"})
    metas = all_metas()
    if sum(m["size"] for m in metas) + size > TOTAL_CAP or shutil.disk_usage(DATA_DIR).free - size < MIN_FREE_BYTES:
        return fail(507, "server storage is full, try again later")

    sid = random_id()
    token = random_token()
    now = time.time()
    meta = {
        "id": sid, "name": name, "size": size, "expectedSha256": sha.lower() if sha else None, "sha256": None,
        "complete": False, "created": now, "expires": now + days * 86400, "tokenHash": token_hash(token),
        "downloads": 0,
    }
    os.makedirs(share_dir(sid), mode=0o700)
    open(blob_path(sid), "wb").close()
    os.chmod(blob_path(sid), 0o600)
    write_meta(meta)
    daily_bytes[key] = (today, used + size)
    log.info("share created id=%s… size=%d days=%d", sid[:6], size, days)
    view = public_view(meta)
    view.update({"token": token, "uploadUrl": f"{PUBLIC_URL}/v1/shares/{sid}", "offset": 0, "chunkSize": 8 * CHUNK})
    return json_response(201, view, {"Location": view["uploadUrl"]})


async def get_share(request: web.Request) -> web.Response:
    if not read_limiter.allow(client_key(request)):
        return fail(429, "slow down", {"Retry-After": "60"})
    meta = read_meta(request.match_info["id"])
    if not meta:
        return fail(404, "no such share (it may have expired)")
    return json_response(200, public_view(meta))


async def upload_chunk(request: web.Request) -> web.Response:
    sid = request.match_info["id"]
    if not patch_limiter.allow(client_key(request)):
        return fail(429, "slow down", {"Retry-After": "60"})
    meta = read_meta(sid)
    if not meta:
        return fail(404, "no such share (it may have expired)")
    if not token_matches(bearer(request), meta["tokenHash"]):
        return fail(401, "missing or wrong token")
    if meta["complete"]:
        return fail(409, "upload already complete")
    try:
        offset = int(request.headers["Upload-Offset"])
    except (KeyError, ValueError):
        return fail(400, "Upload-Offset header required")
    lock = locks.setdefault(sid, asyncio.Lock())
    if lock.locked():
        return fail(409, "another upload to this share is running")
    async with lock:
        have = received_bytes(sid)
        if offset != have:
            return fail(409, "offset mismatch", {"Upload-Offset": str(have)})
        path = blob_path(sid)
        written = have
        first = have == 0
        with open(path, "r+b") as f:
            f.seek(have)
            try:
                async for chunk in request.content.iter_chunked(CHUNK):
                    if first:
                        if len(chunk) < len(ZIP_MAGIC) and written + len(chunk) < meta["size"]:
                            pass  # tiny first piece: checked when the file completes
                        elif not (chunk[:len(ZIP_MAGIC)] == ZIP_MAGIC):
                            f.truncate(have)
                            return fail(415, "not a .lynxpack (zip) file")
                        first = False
                    if written + len(chunk) > meta["size"]:
                        f.truncate(have)
                        return fail(413, "more bytes than the declared size")
                    f.write(chunk)
                    written += len(chunk)
            finally:
                f.flush()
        # a dropped connection leaves what arrived: the client resumes from GET /v1/shares/<id> "received"
        if written == meta["size"]:
            digest = await asyncio.get_running_loop().run_in_executor(None, file_sha256, path)
            with open(path, "rb") as f:
                magic = f.read(len(ZIP_MAGIC))
            if magic != ZIP_MAGIC or (meta["expectedSha256"] and digest != meta["expectedSha256"]):
                delete_share(sid)
                return fail(422, "file check failed (sha256 or format); the share was deleted, start again")
            meta["complete"] = True
            meta["sha256"] = digest
            write_meta(meta)
            log.info("share complete id=%s… size=%d", sid[:6], written)
        view = public_view(meta)
        view["offset"] = written
        return json_response(200, view, {"Upload-Offset": str(written)})


async def download(request: web.Request) -> web.StreamResponse:
    if not read_limiter.allow(client_key(request)):
        return fail(429, "slow down", {"Retry-After": "60"})
    sid = request.match_info["id"]
    meta = read_meta(sid)
    if not meta:
        return fail(404, "no such share (it may have expired)")
    if not meta["complete"]:
        return fail(409, "upload not finished yet")
    if "Range" not in request.headers:
        meta["downloads"] = meta.get("downloads", 0) + 1
        write_meta(meta)
    quoted = urllib.parse.quote(meta["name"])
    ascii_name = re.sub(r'[^A-Za-z0-9._ -]', "_", meta["name"])
    return web.FileResponse(blob_path(sid), headers={
        "Content-Type": "application/octet-stream",
        "Content-Disposition": f"attachment; filename=\"{ascii_name}\"; filename*=UTF-8''{quoted}",
        "Cache-Control": "private, no-store",
        "X-Content-Type-Options": "nosniff",
        "X-Robots-Tag": "noindex",
    })


async def delete(request: web.Request) -> web.Response:
    sid = request.match_info["id"]
    meta = read_meta(sid)
    if not meta:
        return fail(404, "no such share (it may have expired)")
    if not token_matches(bearer(request), meta["tokenHash"]):
        return fail(401, "missing or wrong token")
    delete_share(sid)
    log.info("share deleted id=%s…", sid[:6])
    return web.Response(status=204)


def human_size(n: int) -> str:
    for unit in ("bytes", "KB", "MB", "GB"):
        if n < 1024 or unit == "GB":
            return f"{n:.0f} {unit}" if unit == "bytes" else f"{n:.1f} {unit}"
        n /= 1024
    return str(n)


PAGE = """<!DOCTYPE html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="robots" content="noindex"><title>{title} - LYNX SHARE</title>
<style>body{{margin:0;min-height:100vh;display:grid;place-items:center;background:#111214;color:#e8e8ea;
font:16px/1.5 system-ui,sans-serif}}main{{max-width:28rem;padding:2rem;border:1px solid #2a2b30;border-radius:12px;
background:#18191c}}h1{{font-size:1.2rem;margin:0 0 .5rem;word-break:break-all}}p{{color:#a0a1a8;margin:.25rem 0}}
a.b{{display:inline-block;margin-top:1.2rem;padding:.6rem 1.2rem;border-radius:8px;background:#e5383b;color:#fff;
text-decoration:none;font-weight:600}}a{{color:#e5383b}}</style></head><body><main>
<p>LYNX SHARE project</p><h1>{title}</h1>{body}
<p style="margin-top:1.5rem;font-size:.85rem">Open it with <a href="https://lynx-share.com">LYNX SHARE</a> (File &rsaquo; Open).</p>
</main></body></html>"""


async def page(request: web.Request) -> web.Response:
    if not read_limiter.allow(client_key(request)):
        return fail(429, "slow down", {"Retry-After": "60"})
    meta = read_meta(request.match_info["id"])
    headers = {"Content-Security-Policy": "default-src 'none'; style-src 'unsafe-inline'", "X-Robots-Tag": "noindex",
               "Cache-Control": "no-store", "Referrer-Policy": "no-referrer"}
    if not meta:
        text = PAGE.format(title="Link expired", body="<p>This share does not exist or has expired.</p>")
        return web.Response(status=404, text=text, content_type="text/html", headers=headers)
    if not meta["complete"]:
        body = "<p>The upload is not finished yet. Try again in a moment.</p>"
    else:
        expires = time.strftime("%Y-%m-%d %H:%M UTC", time.gmtime(meta["expires"]))
        body = (f"<p>{human_size(meta['size'])} &middot; expires {expires}</p>"
                f"<a class=\"b\" href=\"/v1/shares/{meta['id']}/download\">Download</a>")
    return web.Response(text=PAGE.format(title=html.escape(meta["name"]), body=body), content_type="text/html",
                        headers=headers)


async def index(request: web.Request) -> web.Response:
    return json_response(200, {"ok": True, "service": "lynx-api", "api": VERSION,
                               "docs": "https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/design/SHARE_API.md"})


# ---- housekeeping ---------------------------------------------------------------------------------------------

async def gc_loop(app: web.Application) -> None:
    while True:
        try:
            before = len(os.listdir(SHARES_DIR))
            left = len(all_metas())  # all_metas() deletes dead shares as it reads them
            if before != left:
                log.info("gc: %d shares removed, %d active", before - left, left)
        except Exception:  # noqa: BLE001 - never let housekeeping kill the service
            log.exception("gc failed")
        await asyncio.sleep(600)


async def on_startup(app: web.Application) -> None:
    os.makedirs(SHARES_DIR, mode=0o700, exist_ok=True)
    app["gc"] = asyncio.create_task(gc_loop(app))


async def on_cleanup(app: web.Application) -> None:
    app["gc"].cancel()


def make_app() -> web.Application:
    app = web.Application(middlewares=[errors_middleware], client_max_size=64 * 1024)
    app.router.add_get("/", index)
    app.router.add_get("/health", health)
    app.router.add_post("/v1/shares", create_share)
    app.router.add_get("/v1/shares/{id}", get_share)
    app.router.add_patch("/v1/shares/{id}", upload_chunk)
    app.router.add_delete("/v1/shares/{id}", delete)
    app.router.add_get("/v1/shares/{id}/download", download)
    app.router.add_get("/s/{id}", page)
    app.on_startup.append(on_startup)
    app.on_cleanup.append(on_cleanup)
    return app


if __name__ == "__main__":
    web.run_app(make_app(), host=env_str("LYNX_API_HOST", "127.0.0.1"), port=env_int("LYNX_API_PORT", 8081),
                access_log=None)
