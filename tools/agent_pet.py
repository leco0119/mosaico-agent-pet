#!/usr/bin/env python3
"""Desk pet: mirror the live state of an AI coding agent on the Mosaico status card.

It watches concurrent session logs from:
  WorkBuddy    ~/.workbuddy/projects/*/*.jsonl
  Claude Code  ~/.claude/projects/*/*.jsonl
  Codex        ~/.codex/sessions/**/*.jsonl
  PandaCode    ~/.panda/projects/*/sessions/*.jsonl
so switching tools needs no configuration. Working sessions take precedence
over completed ones; recency selects the displayed activity within each group.

Each record is reduced to one of a closed set of states whose words are baked
into the firmware's font_charset. Changes and periodic heartbeats are pushed over USB.

  python agent_pet.py            # run until Ctrl+C
  python agent_pet.py --once     # print the detected state and exit (no device)
"""
from __future__ import annotations

import json
import time
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path

from desktop_activity import DesktopActivity


HOME = Path.home()
PANDA_SOURCE = "PandaCode"
SOURCES = {
    "WorkBuddy": [HOME / ".workbuddy/projects"],
    "Claude": [HOME / ".claude/projects"],
    "Codex": [HOME / ".codex/sessions"],
    PANDA_SOURCE: [HOME / ".panda/projects"],
}

READ = {"read", "grep", "glob", "glob_file_search", "ls", "viewimage", "notebookread", "read_file", "list_dir"}
WRITE = {"edit", "write", "multiedit", "notebookedit", "apply_patch", "write_file", "edit_file", "delete_file"}
SHELL = {"bash", "powershell", "shell", "exec", "exec_command", "local_shell", "run_command", "run_shell"}
WEB = {"websearch", "webfetch", "web_search", "domain_search", "run"}
ASK = {"askuserquestion", "ask_question", "request_user_input", "request_user_input_async"}
PANDA_TERMINAL_EVENTS = {"completed": "reply", "failed": "error", "cancelled": "idle"}

DONE_DISPLAY_SECONDS = 5
REPLY_SETTLE_SECONDS = 8
LOG_ACTIVITY_TIMEOUT_SECONDS = 600

# Screen-only states: animation 0 idle, 1 working, 2 waiting, 3 done.
STATES = {
    "idle": ("空闲", 0), "think": ("思考中", 1),
    "read": ("读取文件", 1), "write": ("写入文件", 1),
    "shell": ("运行命令", 1), "web": ("搜索网页", 1),
    "tool": ("正在执行", 1), "ask": ("等你确认", 2),
    "done": ("已完成", 3), "error": ("出错了", 2),
}

@dataclass
class Snapshot:
    source: str
    state: str
    tool: str
    calls: int
    age: float


@dataclass
class Session:
    source: str
    path: Path
    signature: tuple[int, int]
    activity: float
    events: list[tuple[str, str, float]]


def event_time(records: list[dict], fallback: float) -> float:
    for record in reversed(records):
        value = record.get("timestamp")
        try:
            if isinstance(value, (float, int)):
                return value / 1000 if value > 1e12 else value
            if isinstance(value, str):
                stamp = datetime.fromisoformat(value.replace("Z", "+00:00"))
                if stamp.tzinfo is None:
                    stamp = stamp.replace(tzinfo=timezone.utc)
                return stamp.timestamp()
        except (ValueError, OverflowError, OSError):
            pass
    return fallback


class SessionLogs:
    """One watch-loop owner; cache tails until file size or mtime changes.

    Windows can keep mtime unchanged while a writer holds its file open. Log
    event timestamps also let a resumed older session outrank a newer idle one.
    """
    def __init__(self):
        self.cache: dict[Path, Session] = {}

    def sessions(self) -> list[Session]:
        seen = set()
        for source, roots in SOURCES.items():
            for root in roots:
                if not root.is_dir():
                    continue
                paths = root.glob("*/sessions/*.jsonl") if source == PANDA_SOURCE else root.rglob("*.jsonl")
                for path in paths:
                    try:
                        stat = path.stat()
                        signature = (stat.st_size, stat.st_mtime_ns)
                        old = self.cache.get(path)
                        if old is None or old.signature != signature or old.source != source:
                            records = tail_records(path)
                            if source == PANDA_SOURCE:
                                events = panda_events(records)
                                activity = events[-1][2] if events else 0.0
                            else:
                                fallback = time.time() if old else stat.st_mtime
                                activity = event_time(records, fallback)
                                events = [
                                    (event[0], event[1] if isinstance(event[1], str) else "",
                                     event_time([record], fallback))
                                    for record in records
                                    if (event := classify(record)) is not None
                                ]
                            self.cache[path] = Session(source, path, signature, activity, events)
                        seen.add(path)
                    except OSError:
                        continue
        self.cache = {path: entry for path, entry in self.cache.items() if path in seen}
        return list(self.cache.values())

    def newest(self) -> tuple[str, Path] | None:
        best = max(self.sessions(), key=lambda session: session.activity, default=None)
        return (best.source, best.path) if best and best.activity else None


_session_logs = SessionLogs()
_desktop_activity = DesktopActivity()


def newest_log() -> tuple[str, Path] | None:
    return _session_logs.newest()


def tail_records(path: Path, max_bytes: int = 400_000) -> list[dict]:
    with path.open("rb") as fh:
        fh.seek(0, 2)
        size = fh.tell()
        fh.seek(max(0, size - max_bytes))
        chunk = fh.read().decode("utf-8", "replace")
    out = []
    for line in chunk.splitlines()[1:] if size > max_bytes else chunk.splitlines():
        try:
            record = json.loads(line)
        except ValueError:
            continue
        if isinstance(record, dict):
            out.append(record)
    return out


def panda_events(records: list[dict]) -> list[tuple[str, str, float]]:
    """Read PandaCode's transcript events using their actual event times."""
    events = []
    metrics = []
    for record in records:
        kind = record.get("type")
        if kind == "session":
            metrics = record.get("turnMetrics", metrics)
        elif kind == "session_meta" and record.get("field") == "turnMetrics":
            metrics = record.get("value")
        if kind != "message":
            continue

        if record.get("encoding") == "shared":
            message, view = record.get("sharedBody"), record.get("uiExtras")
        elif record.get("encoding") == "dual":
            view = record.get("ui")
            message = record.get("tch") or view
        else:
            continue
        # A TCH-only row has no UI timestamp and can duplicate the user's input.
        if not isinstance(view, dict) or not isinstance(message, dict):
            continue
        stamp = event_time([view], 0)
        if not stamp:
            continue
        role = message.get("role")
        if role == "user":
            events.append(("user", "", stamp))
        elif role in ("assistant", "thinking"):
            calls = message.get("toolCalls")
            names = []
            if isinstance(calls, list):
                for call in calls:
                    if isinstance(call, dict):
                        name = call.get("name")
                        if isinstance(name, str) and name:
                            names.append(name)
            if names:
                events.extend(("call", name, stamp) for name in names)
            else:
                events.append(("thinking", "", stamp))
        elif role == "tool":
            results = message.get("toolResults")
            failed = isinstance(results, list) and any(
                isinstance(result, dict) and result.get("isError") is True for result in results
            )
            events.append(("error" if failed else "result", "", stamp))

    # The file can re-append old metrics when a session is opened. Only the
    # original completedAt timestamp counts, and partial assistant text is not a completion.
    if isinstance(metrics, list):
        for metric in metrics:
            if not isinstance(metric, dict) or not isinstance(metric.get("status"), str):
                continue
            terminal = PANDA_TERMINAL_EVENTS.get(metric["status"])
            stamp = event_time([{"timestamp": metric.get("completedAt")}], 0)
            if terminal and stamp:
                events.append((terminal, "", stamp))
    return sorted(events, key=lambda event: event[2])


def tool_state(name: str) -> str:
    n = name.lower().split("__")[-1].rsplit(".", 1)[-1]
    if n in ASK:
        return "ask"
    if n in READ:
        return "read"
    if n in WRITE:
        return "write"
    if n in SHELL:
        return "shell"
    if n in WEB:
        return "web"
    return "tool"


def classify(rec: dict) -> tuple[str, str] | None:
    """Map one log record to (event, tool). Events: user, call, result, error, reply."""
    t = rec.get("type")
    # WorkBuddy
    if t == "function_call":
        return "call", rec.get("name", "")
    if t == "function_call_result":
        return ("error" if rec.get("status") not in (None, "completed") else "result"), rec.get("name", "")
    if t == "message" and rec.get("role") in ("user", "assistant"):
        return ("user" if rec["role"] == "user" else "reply"), ""
    # Claude Code
    if t in ("user", "assistant") and isinstance(rec.get("message"), dict):
        content = rec["message"].get("content")
        if isinstance(content, list):
            for part in reversed(content):
                if not isinstance(part, dict):
                    continue
                if part.get("type") == "tool_use":
                    return "call", part.get("name", "")
                if part.get("type") == "tool_result":
                    return ("error" if part.get("is_error") else "result"), ""
        return ("user" if t == "user" else "reply"), ""
    # Codex
    if t == "event_msg" and isinstance(rec.get("payload"), dict):
        event = rec["payload"].get("type")
        if event in ("task_started", "user_message"):
            return "user", ""
        if event in ("task_complete", "task_aborted"):
            return ("reply" if event == "task_complete" else "error"), ""
    if t == "response_item" and isinstance(rec.get("payload"), dict):
        p = rec["payload"]
        if p.get("type") in ("function_call", "custom_tool_call", "local_shell_call"):
            return "call", p.get("name", "shell")
        if p.get("type") in ("function_call_output", "custom_tool_call_output"):
            return "result", ""
        if p.get("type") == "message":
            if p.get("role") == "user":
                return "user", ""
            if p.get("role") == "assistant":
                return ("reply" if p.get("channel") == "final" else "thinking"), ""
    return None


def session_snapshot(session: Session, now: float, confirmed_active: bool = False) -> Snapshot:
    source, events = session.source, session.events
    # Metadata writes must not revive an abandoned turn or postpone completion.
    age = max(0, now - (events[-1][2] if events else session.activity))
    if not events:
        return Snapshot(source, "think" if confirmed_active else "idle", "", 0, age)
    # Tool calls since the latest user message = progress counter.
    calls = 0
    for ev, _, _ in reversed(events):
        if ev == "user":
            break
        calls += ev == "call"
    ev, tool, stamp = events[-1]
    if ev == "call":
        last_tool = tool
    else:
        last_tool = next((t for e, t, _ in reversed(events) if e == "call"), "")
    if ev == "idle":
        state = "idle"
    elif ev in ("user", "result", "thinking"):
        state = "think"
    elif ev == "call":
        state = tool_state(tool)
    elif ev == "error":
        state = "error"
    else:
        # WorkBuddy and Claude can log intermediate text as a reply.
        reply_age = max(0, now - stamp)
        settle = 0 if source in ("Codex", PANDA_SOURCE) else REPLY_SETTLE_SECONDS
        if reply_age < settle:
            state = "think"
        elif reply_age < settle + DONE_DISPLAY_SECONDS:
            state = "done"
        else:
            state = "idle"
    if confirmed_active and state in ("idle", "done"):
        state = "think"
    elif not confirmed_active and age > LOG_ACTIVITY_TIMEOUT_SECONDS:
        state = "idle"
    return Snapshot(source, state, last_tool.split("__")[-1][:20], calls, age)


def snapshot() -> Snapshot | None:
    now = time.time()
    desktop = _desktop_activity.read(now)
    local_tasks = {task.thread_id: task for task in desktop.threads
                   if task.source == "Codex" and task.host_id == "local"}
    candidates = []
    covered = set()
    for session in _session_logs.sessions():
        # Rollout names end in the conversation UUID, including resumed logs.
        ident = session.path.stem[-36:] if session.source == "Codex" else ""
        task = local_tasks.get(ident)
        current = session_snapshot(session, now, confirmed_active=task is not None and task.state == "think")
        if task is not None:
            covered.add(ident)
            if task.state == "idle" and session.activity <= desktop.observed_at and current.state != "done":
                current.state = "idle"
        candidates.append(current)
    for task in desktop.threads:
        if task.source == "Codex" and task.host_id == "local" and task.thread_id in covered:
            continue
        candidates.append(Snapshot(task.source, task.state, "", 0, max(0, now - task.updated_at)))
    return select_snapshot(candidates)


def select_snapshot(candidates: list[Snapshot]) -> Snapshot | None:
    def priority(item: Snapshot) -> tuple[int, float]:
        working = item.state not in ("idle", "done", "error", "ask")
        return (2 if working else 1 if item.state == "ask" else 0, -item.age)
    return max(candidates, key=priority, default=None)


def main() -> int:
    from pet_runner import main as run
    return run(snapshot, STATES)


if __name__ == "__main__":
    raise SystemExit(main())
