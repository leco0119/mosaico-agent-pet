"""Read account statistics through Codex's own authenticated app-server.

Only initialize, account/rateLimits/read and account/usage/read are sent. The
client never reads credentials, starts a conversation or invokes a model.
"""
from __future__ import annotations

import json
import os
import queue
import shutil
import subprocess
import threading
import time
from pathlib import Path


class CodexRPCError(RuntimeError):
    """A bounded, credential-free account query could not complete."""


def command(executable: str | None = None) -> list[str]:
    if executable:
        binary = shutil.which(executable) or str(Path(executable).expanduser())
    else:
        binary = shutil.which("codex.exe") or shutil.which("codex")
        if binary is None and os.name == "nt":
            local = Path(os.environ.get("LOCALAPPDATA", Path.home() / "AppData" / "Local"))
            candidates = list((local / "OpenAI" / "Codex" / "bin").glob("*/codex.exe"))
            if candidates:
                binary = str(max(candidates, key=lambda p: p.stat().st_mtime_ns))
    if not binary:
        raise CodexRPCError("Codex client is unavailable")
    return [binary, "app-server", "--listen", "stdio://"]


def read_account(argv: list[str], timeout_s: float = 20.0, *, cancel_event=None) -> dict:
    """Query both metrics with one handshake; retain independently valid replies."""
    deadline = time.monotonic() + timeout_s
    process = subprocess.Popen(
        argv, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
        text=True, encoding="utf-8", errors="replace", bufsize=1,
        creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
    )
    messages: queue.Queue = queue.Queue()

    def receive() -> None:
        try:
            for line in process.stdout:
                try:
                    value = json.loads(line)
                except ValueError:
                    continue
                if isinstance(value, dict):
                    messages.put(value)
        finally:
            messages.put(None)

    reader = threading.Thread(target=receive, daemon=True)
    reader.start()

    def send(value: dict) -> None:
        process.stdin.write(json.dumps(value) + "\n")
        process.stdin.flush()

    def next_message() -> dict:
        while True:
            if cancel_event is not None and cancel_event.is_set():
                raise CodexRPCError("Codex account query cancelled")
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise CodexRPCError("Codex account query timed out")
            try:
                value = messages.get(timeout=min(remaining, 0.2))
            except queue.Empty:
                continue
            if value is None:
                raise CodexRPCError("Codex account connection closed")
            return value

    try:
        send({"id": 0, "method": "initialize", "params": {
            "clientInfo": {"name": "mosaico_agent_pet", "title": "Mosaico Agent Pet", "version": "1.3.0"},
        }})
        while True:
            message = next_message()
            if message.get("id") == 0:
                if "error" in message or not isinstance(message.get("result"), dict):
                    raise CodexRPCError("Codex account initialization failed")
                break
        send({"method": "initialized", "params": {}})
        requests = {1: "limits", 2: "usage"}
        send({"id": 1, "method": "account/rateLimits/read"})
        send({"id": 2, "method": "account/usage/read"})
        result = {"limits": None, "usage": None}
        while requests:
            try:
                message = next_message()
            except CodexRPCError:
                if any(value is not None for value in result.values()):
                    break
                raise
            key = requests.pop(message.get("id"), None)
            if key is not None and "error" not in message and isinstance(message.get("result"), dict):
                result[key] = message["result"]
        return result
    finally:
        if process.poll() is None:
            process.terminate()
        try:
            process.wait(timeout=2)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=2)
        reader.join(timeout=1)
        process.stdin.close()
        process.stdout.close()
