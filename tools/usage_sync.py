"""Independent Codex collection; the pet loop alone owns the USB client.

Only display data is cached. No credentials or raw account replies are stored.
The stable per-user cache is outside the launcher's versioned runtime directory.
"""
from __future__ import annotations

from copy import deepcopy
from datetime import datetime
import json
import logging
import os
from pathlib import Path
import subprocess
import threading
import time

from usage_agent import codex_rpc, codex_source, payload, snapshot

REFRESH_SECONDS = 300
POLL_SECONDS = 2
RETRY_SECONDS = 5


def local_data_root() -> Path:
    return Path(os.environ.get("LOCALAPPDATA", Path.home() / "AppData" / "Local"))


def display_document(doc: dict) -> dict:
    payload.validate(doc)
    return {"generated_at": doc.get("generated_at", ""),
            "updated_text": doc.get("updated_text", ""), "stale": doc.get("stale", False),
            "codex": {k: deepcopy(v) for k, v in doc["codex"].items()
                      if k in codex_source.EMPTY and k != "tokens_total"}}


def save_document(path: Path, doc: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(".json.tmp")
    temporary.write_text(json.dumps(doc, ensure_ascii=False, indent=1), encoding="utf-8")
    temporary.replace(path)


class UsageSync:
    """Single background producer. peek/request never wait for account or disk I/O."""

    def __init__(self, *, cache_path=None, legacy_path=None, collector=None,
                 interval=REFRESH_SECONDS):
        self.cache_path = Path(cache_path) if cache_path is not None else local_data_root() / "MosaicoAgentPet" / "usage.json"
        legacy = Path(legacy_path) if legacy_path is not None else local_data_root() / "mosaico-usage" / "usage.json"
        self.interval = interval
        self._collector = collector or self._collect_account
        self._lock = threading.Lock()
        self._wake = threading.Event()
        self._stop = threading.Event()
        self._thread = None
        self._busy = False
        self._revision = 0
        self._doc = None
        saved = snapshot.load(self.cache_path)
        migrate = saved is None and not self.cache_path.exists()
        if migrate:
            saved = snapshot.load(legacy)
        if saved is not None:
            self._doc = display_document(saved)
            self._doc["stale"] = True
            self._revision = 1
            if migrate:
                self._save(self._doc)

    def _save(self, doc):
        try:
            save_document(self.cache_path, doc)
        except OSError as error:
            logging.warning("Usage cache unavailable: %s", type(error).__name__)

    def _collect_account(self):
        now = datetime.now().astimezone()
        account = codex_source.collect(now=now, fetch=lambda executable: codex_rpc.read_account(
            codex_rpc.command(executable), cancel_event=self._stop))
        return {"generated_at": now.isoformat(timespec="seconds"),
                "updated_text": now.strftime("%Y%m%d %H:%M"), "codex": account}

    def start(self):
        if self._thread is not None:
            return
        self._wake.set()
        self._thread = threading.Thread(target=self._run, name="codex-usage", daemon=True)
        self._thread.start()

    def request(self):
        with self._lock:
            if self._busy or self._wake.is_set() or self._stop.is_set():
                return False
            self._wake.set()
            return True

    def peek(self):
        with self._lock:
            return self._revision, deepcopy(self._doc)

    def _run(self):
        while not self._stop.is_set():
            self._wake.wait(self.interval)
            with self._lock:
                if self._stop.is_set():
                    return
                self._wake.clear()
                self._busy = True
                previous = self._doc
            try:
                current = display_document(self._collector())
            except Exception as error:
                logging.warning("Usage collection unavailable: %s", type(error).__name__)
                current = {"updated_text": "", "generated_at": "",
                           "codex": {**codex_source.EMPTY, "reason": "读取失败"}}
            result = snapshot.merge(current, previous)
            if self._stop.is_set():
                return
            self._save(result)
            with self._lock:
                self._doc = result
                self._revision += 1
                self._busy = False

    def close(self):
        self._stop.set()
        self._wake.set()
        if self._thread is not None:
            self._thread.join(timeout=4)


class UsageDelivery:
    """Pet-loop consumer. Account errors never become USB connection errors."""

    def __init__(self, worker):
        self.worker = worker
        self.disconnected()

    def disconnected(self):
        self.enabled = False
        self._refresh = None
        self._boot = None
        self._sent = -1
        self._poll_at = 0.0
        self._retry_at = 0.0
        self._last_error = None

    def observe(self, reply):
        if not isinstance(reply, dict) or reply.get("usage_protocol") != 1:
            return
        self.enabled = True
        boot = reply.get("boot_id")
        if isinstance(boot, (str, int)) and not isinstance(boot, bool) and boot != self._boot:
            self._boot = boot
            self._sent = -1
            self._retry_at = 0.0
            self._refresh = None
        sequence = reply.get("usage_refresh")
        if isinstance(sequence, int) and not isinstance(sequence, bool) and sequence != self._refresh:
            self._refresh = sequence
            self.worker.request()

    def tick(self, client, now=None):
        if not self.enabled:
            return
        now = time.monotonic() if now is None else now
        try:
            if now >= self._poll_at:
                self._poll_at = now + POLL_SECONDS
                self.observe(json.loads(client.rpc("0x0002", timeout=4)))
            revision, doc = self.worker.peek()
            if doc is not None and revision != self._sent and now >= self._retry_at:
                self._retry_at = now + RETRY_SECONDS
                response = client.rpc("0x0001", payload.encode(doc), service="0x5550", timeout=4)
                if response.strip() != "ok=1":
                    raise RuntimeError("Usage update was rejected")
                self._sent = revision
                self._last_error = None
        except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as error:
            # Never include arbitrary server/CLI output in the tray or logs.
            kind = type(error).__name__
            if kind != self._last_error:
                logging.warning("Usage delivery unavailable: %s; pet sync continues", kind)
                self._last_error = kind
