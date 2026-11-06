#!/usr/bin/env python3
"""Independent standard-library client for the opt-in LYNX Remote Control API.

Defaults to read-only status. An edit requires an explicitly saved project
snapshot and a JSON Patch file; writes are sent once and are never retried.
"""
import argparse
import base64
import hashlib
import hmac
import json
import os
from pathlib import Path
import secrets
import socket
import stat
import sys

PROTOCOL = "lynx-control/1"
MAX_FRAME = 1024 * 1024
MAX_DOCUMENT = 64 * 1024 * 1024


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def default_discovery():
    if os.environ.get("LYNX_CONTROL_DISCOVERY"):
        return Path(os.environ["LYNX_CONTROL_DISCOVERY"])
    if os.environ.get("LYNX_PROFILE_DIR"):
        return Path(os.environ["LYNX_PROFILE_DIR"]) / "LYNX/control/connection.json"
    if os.name == "nt":
        require(bool(os.environ.get("APPDATA")), "Set --discovery when APPDATA is unavailable")
        folder = Path(os.environ["APPDATA"]) / "LYNX"
    elif sys.platform == "darwin":
        folder = Path.home() / "Library/Application Support/LYNX"
    else:
        config = Path(os.environ.get("XDG_CONFIG_HOME", str(Path.home() / ".config")))
        if not config.is_absolute():
            config = Path.home() / ".config"
        folder = config / "LYNX"
    return folder / "control/connection.json"


def windows_private_bytes(path):
    """Open the leaf without following a reparse point; inspect that same handle's ACL."""
    import ctypes as c
    from ctypes import wintypes as w

    kernel = c.WinDLL("kernel32", use_last_error=True)
    security = c.WinDLL("advapi32", use_last_error=True)
    kernel.CreateFileW.argtypes = [w.LPCWSTR, w.DWORD, w.DWORD, c.c_void_p, w.DWORD, w.DWORD, w.HANDLE]
    kernel.CreateFileW.restype = w.HANDLE
    kernel.CloseHandle.argtypes = [w.HANDLE]
    kernel.GetCurrentProcess.restype = w.HANDLE
    kernel.LocalFree.argtypes = [c.c_void_p]
    kernel.GetFileInformationByHandleEx.argtypes = [w.HANDLE, c.c_int, c.c_void_p, w.DWORD]
    kernel.ReadFile.argtypes = [w.HANDLE, c.c_void_p, w.DWORD, c.POINTER(w.DWORD), c.c_void_p]
    security.OpenProcessToken.argtypes = [w.HANDLE, w.DWORD, c.POINTER(w.HANDLE)]
    security.GetTokenInformation.argtypes = [w.HANDLE, c.c_int, c.c_void_p, w.DWORD, c.POINTER(w.DWORD)]
    security.GetSecurityInfo.argtypes = [w.HANDLE, c.c_int, w.DWORD, c.POINTER(c.c_void_p),
                                       c.c_void_p, c.POINTER(c.c_void_p), c.c_void_p, c.POINTER(c.c_void_p)]
    security.EqualSid.argtypes = [c.c_void_p, c.c_void_p]
    security.GetAclInformation.argtypes = [c.c_void_p, c.c_void_p, w.DWORD, c.c_int]
    security.GetAce.argtypes = [c.c_void_p, w.DWORD, c.POINTER(c.c_void_p)]
    security.ConvertSidToStringSidW.argtypes = [c.c_void_p, c.POINTER(w.LPWSTR)]
    handle = kernel.CreateFileW(str(path), 0x80000000 | 0x00020000, 7, None, 3, 0x00200000, None)
    require(handle != w.HANDLE(-1).value, "Cannot open the private discovery file")
    descriptor = c.c_void_p()
    process_token = w.HANDLE()
    try:
        attrs = (w.DWORD * 2)()
        require(kernel.GetFileInformationByHandleEx(handle, 9, attrs, c.sizeof(attrs)), "Cannot inspect discovery attributes")
        require(not attrs[0] & (0x400 | 0x10), "Discovery must be a regular file without a reparse point")
        owner, acl = c.c_void_p(), c.c_void_p()
        require(security.GetSecurityInfo(handle, 1, 0x5, c.byref(owner), None, c.byref(acl), None, c.byref(descriptor)) == 0,
                "Cannot inspect discovery ownership and DACL")
        require(acl.value, "Discovery must have an explicit private DACL")
        require(security.OpenProcessToken(kernel.GetCurrentProcess(), 8, c.byref(process_token)), "Cannot inspect current user")
        needed = w.DWORD()
        security.GetTokenInformation(process_token, 1, None, 0, c.byref(needed))
        token = c.create_string_buffer(needed.value)
        require(security.GetTokenInformation(process_token, 1, token, needed, c.byref(needed)), "Cannot inspect current user SID")
        current_sid = c.cast(token, c.POINTER(c.c_void_p))[0]
        require(security.EqualSid(owner, current_sid), "Discovery must belong to the current user")
        info = (w.DWORD * 3)()
        require(security.GetAclInformation(acl, info, c.sizeof(info), 2), "Cannot inspect discovery DACL entries")
        for index in range(info[0]):
            ace = c.c_void_p()
            require(security.GetAce(acl, index, c.byref(ace)), "Cannot inspect a discovery DACL entry")
            header = c.cast(ace, c.POINTER(c.c_ubyte))
            if header[1] & 0x08:  # inherit-only ACE does not grant access to this file
                continue
            require(header[0] in (0, 1), "Unsupported discovery DACL entry")
            if header[0] == 1:  # deny ACE grants nobody access
                continue
            sid = c.c_void_p(ace.value + 8)
            if security.EqualSid(sid, current_sid):
                continue
            readable = w.LPWSTR()
            require(security.ConvertSidToStringSidW(sid, c.byref(readable)), "Cannot inspect discovery DACL SID")
            try:
                require(readable.value == "S-1-5-18", "Discovery grants access to another user or group")
            finally:
                kernel.LocalFree(c.cast(readable, c.c_void_p))
        data = c.create_string_buffer(16385)
        count = w.DWORD()
        require(kernel.ReadFile(handle, data, len(data), c.byref(count), None), "Cannot read discovery")
        require(count.value <= 16384, "Discovery exceeds 16 KiB")
        return data.raw[:count.value]
    finally:
        if process_token:
            kernel.CloseHandle(process_token)
        if descriptor:
            kernel.LocalFree(descriptor)
        kernel.CloseHandle(handle)


def private_bytes(path):
    require(path.is_absolute(), "Discovery path must be absolute")
    if os.name == "nt":
        return windows_private_bytes(path)
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    try:
        info = os.fstat(fd)
        require(stat.S_ISREG(info.st_mode) and info.st_uid == os.geteuid() and not info.st_mode & 0o077,
                "Discovery must be a regular file owned by this user with mode 0600")
        require(info.st_size <= 16384, "Discovery exceeds 16 KiB")
        data = os.read(fd, 16385)
        require(len(data) <= 16384, "Discovery exceeds 16 KiB")
        return data
    finally:
        os.close(fd)


def lower_hex(value, size):
    return isinstance(value, str) and len(value) == size and all(c in "0123456789abcdef" for c in value)


def bounded_json(data, limit, depth):
    require(len(data) <= limit, "Server frame exceeds its size bound")
    level, quoted, escaped = 0, False, False
    for byte in data:
        if quoted:
            if escaped:
                escaped = False
            elif byte == 92:
                escaped = True
            elif byte == 34:
                quoted = False
        elif byte == 34:
            quoted = True
        elif byte in (91, 123):
            level += 1
            require(level <= depth, "Server frame exceeds its nesting bound")
        elif byte in (93, 125):
            level -= 1
            require(level >= 0, "Server frame nesting is invalid")
    return json.loads(data)


class Client:
    def __init__(self, discovery):
        record = json.loads(private_bytes(discovery))
        require(isinstance(record, dict), "Discovery must be an object")
        require(record.get("protocol") == PROTOCOL and record.get("host") == "127.0.0.1", "Incompatible discovery protocol or address")
        require(type(record.get("port")) is int and 0 < record["port"] <= 65535, "Invalid discovery port")
        require(type(record.get("pid")) is int and record["pid"] > 0, "Invalid discovery PID")
        require(lower_hex(record.get("token"), 64), "Invalid discovery credential format")
        self.record = record

    def call(self, operation, args=None, *, write=False):
        token = self.record["token"]
        nonce = secrets.token_hex(16)

        def proof(role, server):
            message = f"{PROTOCOL}\n{role}\n{nonce}\n{server}".encode("ascii")
            return hmac.new(token.encode("ascii"), message, hashlib.sha256).hexdigest()

        def send(connection, value, limit):
            data = json.dumps(value, separators=(",", ":"), allow_nan=False).encode("utf-8")
            require(len(data) <= limit, "Request exceeds its size bound")
            connection.sendall(data + b"\n")

        def frame(stream, limit, depth):
            data = stream.readline(limit + 2)
            require(data.endswith(b"\n"), "Missing or oversized server frame")
            value = bounded_json(data[:-1], limit, depth)
            require(isinstance(value, dict), "Server frame must be an object")
            return value

        with socket.create_connection(("127.0.0.1", self.record["port"]), timeout=2) as connection:
            connection.settimeout(2)
            with connection.makefile("rb") as stream:
                send(connection, {"type": "hello", "protocol": PROTOCOL, "nonce": nonce}, 1024)
                challenge = frame(stream, 1024, 8)
                server = challenge.get("nonce")
                require(challenge.get("type") == "challenge" and challenge.get("protocol") == PROTOCOL
                        and lower_hex(server, 32), "Invalid server challenge")
                require(isinstance(challenge.get("proof"), str)
                        and hmac.compare_digest(challenge["proof"], proof("server", server)),
                        "Server proof failed; no client proof or operation was sent")
                send(connection, {"type": "auth", "proof": proof("client", server)}, 1024)
                ready = frame(stream, 1024, 8)
                require(ready.get("type") == "ready" and ready.get("protocol") == PROTOCOL, "Client authentication refused")
                connection.settimeout(70)
                try:
                    send(connection, {"op": operation, "args": args or {}}, MAX_FRAME)
                    response = frame(stream, MAX_FRAME, 64)
                except (OSError, ValueError, RuntimeError) as error:
                    if write:
                        raise RuntimeError("The write outcome may be unknown. Inspect the project before any retry.") from error
                    raise
        if response.get("ok") is not True:
            message = str(response.get("error", "Operation failed")).replace(token, "[redacted]")
            if response.get("uncertain") is True:
                message += " Inspect the project; do not repeat this write blindly."
            raise RuntimeError(message)
        return response["result"]

    def read_project(self):
        request, chunks, expected = {}, [], None
        while True:
            page = self.call("project.read", request)
            identity = {key: page[key] for key in ("snapshotId", "documentHash", "projectId", "controlEpoch", "revision", "totalBytes")}
            require(expected is None or identity == expected, "Project pages changed snapshot identity")
            expected = identity
            require(page.get("encoding") == "base64" and page.get("offset") == sum(map(len, chunks)), "Invalid project page offset/encoding")
            require(type(page["totalBytes"]) is int and 0 <= page["totalBytes"] <= MAX_DOCUMENT, "Invalid project size")
            chunk = base64.b64decode(page["data"], validate=True)
            require(len(chunk) <= 65536, "Oversized project page")
            chunks.append(chunk)
            size = sum(map(len, chunks))
            require(size <= page["totalBytes"], "Project pages exceed declared document size")
            if "nextOffset" not in page:
                require(size == page["totalBytes"], "Incomplete project document")
                break
            require(chunk and page["nextOffset"] == size, "Project page made no progress")
            request = {"snapshotId": page["snapshotId"], "expectedDocumentHash": page["documentHash"], "offset": size}
        document = b"".join(chunks)
        require(hashlib.sha256(document).hexdigest() == expected["documentHash"], "Project document hash differs")
        bounded_json(document, MAX_DOCUMENT, 64)
        return document, expected


def write_new(path, data):
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, "wb") as output:
        output.write(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--discovery", type=Path)
    commands = parser.add_subparsers(dest="command")
    commands.add_parser("status")
    commands.add_parser("describe")
    commands.add_parser("stop")
    changes = commands.add_parser("changes")
    changes.add_argument("--from-revision", type=int, required=True)
    read = commands.add_parser("read")
    read.add_argument("--output", type=Path, required=True)
    read.add_argument("--metadata", type=Path, required=True)
    edit = commands.add_parser("edit")
    edit.add_argument("--snapshot", type=Path, required=True)
    edit.add_argument("--patch", type=Path, required=True)
    options = parser.parse_args()
    client = Client(options.discovery or default_discovery())
    if options.command == "read":
        document, metadata = client.read_project()
        require(not options.output.exists() and not options.metadata.exists(), "Choose new output and metadata file names")
        write_new(options.output, document)
        write_new(options.metadata, (json.dumps(metadata, indent=2) + "\n").encode())
        print("Saved a complete project snapshot and its edit fence.")
        return
    if options.command == "edit":
        metadata = json.loads(options.snapshot.read_bytes())
        patch_data = options.patch.read_bytes()
        patch = bounded_json(patch_data, MAX_FRAME - 4096, 64)
        require(isinstance(patch, list) and 1 <= len(patch) <= 128, "Patch must contain 1..128 operations")
        result = client.call("project.edit", {
            "snapshotId": metadata["snapshotId"], "expectedDocumentHash": metadata["documentHash"], "patch": patch,
            "expectedProjectId": metadata["projectId"], "expectedEpoch": metadata["controlEpoch"],
            "expectedRevision": metadata["revision"],
        }, write=True)
    elif options.command == "describe":
        result = client.call("api.describe")
    elif options.command == "changes":
        result = client.call("changes", {"fromRevision": options.from_revision})
    elif options.command == "stop":
        result = client.call("transport.stop", write=True)
    else:
        result = client.call("status")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, RuntimeError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
