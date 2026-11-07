"""Read live Codex/ChatGPT status from the Windows desktop's local app-tools pipe.

Only list_threads is called. No messages, credentials, or conversation contents
are read. SQLite supplies an existing local chat ID for request attribution; its
cached history status is deliberately NOT used as live activity. One watch-loop
owner polls this adapter. This desktop-internal interface may change on upgrade.
"""
from __future__ import annotations

import ctypes
from dataclasses import dataclass, field
import json
import math
import os
from pathlib import Path
import sqlite3
import struct
import time
import uuid

POLL_SECONDS = 3
STALE_SECONDS = 15
REQUEST_TIMEOUT_SECONDS = 3
MAX_FRAME_BYTES = 8 * 1024 * 1024
HISTORY_INTERVAL_SECONDS = 30
HISTORY_STALE_SECONDS = 120
PIPE_PREFIX = "codex-app-tools-"


class DesktopClosed(OSError):
    """There is no live desktop app endpoint."""


@dataclass
class DesktopTask:
    source: str
    thread_id: str
    state: str
    updated_at: float
    host_id: str = "local"


@dataclass
class DesktopStatus:
    available: bool = False
    observed_at: float = 0
    threads: list[DesktopTask] = field(default_factory=list)


def find_pipes() -> list[str]:
    if os.name != "nt":
        return []
    try:
        paths = ["\\\\.\\pipe\\" + name for name in os.listdir("\\\\.\\pipe\\")
                 if name.startswith(PIPE_PREFIX)]
    except OSError:
        paths = []
    inherited = os.environ.get("CODEX_APP_TOOLS_PIPE_PATH", "")
    if inherited.startswith("\\\\.\\pipe\\" + PIPE_PREFIX):
        paths = [inherited, *[path for path in paths if path != inherited]]
    return paths


def caller_thread_id() -> str:
    inherited = os.environ.get("CODEX_THREAD_ID", "")
    try:
        return str(uuid.UUID(inherited))
    except ValueError:
        pass
    codex_home = Path(os.environ.get("CODEX_HOME", Path.home() / ".codex"))
    database = Path(os.environ.get("CODEX_SQLITE_HOME", codex_home / "sqlite")) / "codex.db"
    # mode=ro never creates or modifies the desktop's database.
    connection = sqlite3.connect(database.resolve().as_uri() + "?mode=ro", uri=True, timeout=0.1)
    try:
        row = connection.execute(
            "SELECT thread_id FROM local_thread_catalog WHERE host_id = 'local' "
            "ORDER BY source_updated_at DESC LIMIT 1").fetchone()
    finally:
        connection.close()
    if not row:
        raise ValueError("No local Codex chat is available for desktop status requests")
    return str(uuid.UUID(row[0]))


def pipe_request(path: str, request: dict, deadline: float) -> dict:
    import msvcrt

    if time.monotonic() >= deadline:
        raise TimeoutError("Desktop status request timed out")
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    peek = kernel.PeekNamedPipe
    peek.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_ulong,
                     ctypes.c_void_p, ctypes.POINTER(ctypes.c_ulong), ctypes.c_void_p]
    peek.restype = ctypes.c_int
    body = json.dumps(request, separators=(",", ":")).encode("utf-8")
    try:
        pipe = open(path, "r+b", buffering=0)
    except FileNotFoundError as exc:
        raise DesktopClosed() from exc
    with pipe:
        frame = struct.pack("<I", len(body)) + body
        if pipe.write(frame) != len(frame):
            raise OSError("Incomplete desktop status request")
        handle = msvcrt.get_osfhandle(pipe.fileno())
        data = bytearray()
        length = None
        while time.monotonic() < deadline:
            available = ctypes.c_ulong()
            if not peek(handle, None, 0, None, ctypes.byref(available), None):
                raise ctypes.WinError(ctypes.get_last_error())
            if available.value:
                data.extend(pipe.read(min(available.value, 64_000)))
                if length is None and len(data) >= 4:
                    length = struct.unpack("<I", data[:4])[0]
                    if not 0 < length <= MAX_FRAME_BYTES:
                        raise ValueError("Invalid desktop status frame size")
                if length is not None and len(data) >= length + 4:
                    result = json.loads(data[4:length + 4])
                    if not isinstance(result, dict) or result.get("id") != request["id"]:
                        raise ValueError("Invalid desktop status response")
                    return result
            time.sleep(0.02)
    raise TimeoutError("Desktop status request timed out")


def decode_listing(response: dict) -> dict:
    result = response.get("result")
    if not isinstance(result, dict) or result.get("success") is not True:
        raise ValueError("Desktop rejected the read-only task listing")
    for item in result.get("contentItems", []):
        if isinstance(item, dict) and item.get("type") == "inputText":
            listing = json.loads(item.get("text", ""))
            if isinstance(listing, dict) and isinstance(listing.get("threads"), list):
                return listing
    raise ValueError("Desktop returned no task listing")


class DesktopClient:
    def __init__(self):
        self.path = None
        self.anchor = None
        self.history: dict[str, tuple[float, dict]] = {}
        self.history_seen: set[str] = set()
        self.history_cursor = None
        self.history_due = 0.0

    def listing(self, path: str, arguments: dict, deadline: float) -> dict:
        if self.anchor is None:
            self.anchor = caller_thread_id()
        request = {"id": 1, "jsonrpc": "2.0", "method": "tools/call", "params": {
            "namespace": "codex_app", "tool": "list_threads", "arguments": arguments,
            "callerSource": "codex", "hostId": "local", "threadId": self.anchor,
            "turnId": "agent-pet-status", "callId": "agent-pet-" + str(uuid.uuid4()),
        }}
        return decode_listing(pipe_request(path, request, deadline))

    def fetch(self) -> list[dict]:
        paths = find_pipes()
        if not paths:
            raise DesktopClosed()
        if self.path in paths:
            paths.remove(self.path)
            paths.insert(0, self.path)
        deadline = time.monotonic() + REQUEST_TIMEOUT_SECONDS
        error = None
        recent = None
        for path in paths:
            try:
                recent = self.listing(path, {"limit": 50}, deadline)
                break
            except (OSError, ValueError) as exc:
                error = exc
                if time.monotonic() >= deadline:
                    break
        if recent is None:
            raise error or DesktopClosed()
        if path != self.path:
            self.history.clear()
            self.history_seen.clear()
            self.history_cursor = None
            self.history_due = 0
            self.path = path
        now = time.monotonic()
        fresh = [*(recent.get("pinnedThreads") or []), *recent["threads"]]
        # Recent mode includes pinned chats. Also sweep ChatGPT history a page
        # at a time so older, quiet active chats are not lost behind the first 50.
        needs_history = len(recent["threads"]) >= 50 or bool(self.history) or self.history_cursor is not None
        if needs_history and now >= self.history_due:
            arguments = {"mode": "history", "source": "chatgpt", "limit": 50}
            if self.history_cursor is not None:
                arguments["cursor"] = self.history_cursor
            try:
                page = self.listing(path, arguments, deadline)
                if self.history_cursor is None:
                    self.history_seen.clear()
                for row in page["threads"]:
                    if isinstance(row, dict) and isinstance(row.get("id"), str):
                        self.history[row["id"]] = (now, row)
                        self.history_seen.add(row["id"])
                self.history_cursor = page.get("nextCursor")
                if self.history_cursor is None:
                    self.history = {key: value for key, value in self.history.items()
                                    if key in self.history_seen}
                    self.history_due = now + HISTORY_INTERVAL_SECONDS
            except (OSError, ValueError):
                # A failed history sweep must not discard a fresh live listing.
                self.history_due = now + POLL_SECONDS
        for row in fresh:
            if (isinstance(row, dict) and row.get("kind") == "chatgpt"
                    and isinstance(row.get("id"), str) and row["id"] in self.history):
                self.history[row["id"]] = (now, row)
        rows = [row for stamp, row in self.history.values() if now - stamp < HISTORY_STALE_SECONDS]
        rows.extend(fresh)
        return rows


class DesktopActivity:
    def __init__(self, client=None):
        self.client = client if client is not None else DesktopClient()
        self.current = DesktopStatus()
        self.last_poll = float("-inf")
        self.last_error = ""

    def read(self, now: float) -> DesktopStatus:
        if 0 <= now - self.last_poll < POLL_SECONDS:
            return self.current
        self.last_poll = now
        try:
            rows = self.client.fetch()
            threads = {}
            for row in rows:
                if not isinstance(row, dict) or not isinstance(row.get("id"), str):
                    continue
                kind, status = row.get("kind"), row.get("status")
                if not isinstance(kind, str) or not isinstance(status, str):
                    continue
                source = {"codex": "Codex", "chatgpt": "ChatGPT"}.get(kind)
                state = {"active": "think", "idle": "idle"}.get(status)
                if source is None or state is None:
                    continue
                stamp = row.get("updatedAt")
                stamp = float(stamp) if isinstance(stamp, (int, float)) else now
                if not math.isfinite(stamp):
                    stamp = now
                host = row.get("hostId", "local")
                task = DesktopTask(source, row["id"], state, stamp, host)
                threads[(source, host, task.thread_id)] = task
            self.current = DesktopStatus(True, now, list(threads.values()))
            self.last_error = ""
        except DesktopClosed:
            self.current = DesktopStatus()
            self.last_error = "Desktop app is closed"
        except (OSError, ValueError, sqlite3.Error) as exc:
            self.last_error = type(exc).__name__ + ": " + str(exc)
            previous = self.current
            retained = previous.threads if 0 <= now - previous.observed_at < STALE_SECONDS else []
            self.current = DesktopStatus(False, previous.observed_at, retained)
        return self.current
