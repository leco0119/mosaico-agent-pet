"""Concurrent sessions must not hide one another when a turn completes."""
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import agent_pet
from desktop_activity import DesktopActivity


BASE = 1_790_000_000


def codex_event(kind, offset):
    return {"type": "event_msg", "timestamp": BASE + offset,
            "payload": {"type": kind}}


def write_log(path, *records):
    path.write_text("\n".join(map(json.dumps, records)) + "\n", encoding="utf-8")


class ParallelActivityTests(unittest.TestCase):
    def setUp(self):
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        self.root = Path(folder.name)
        self.sources = patch.object(agent_pet, "SOURCES", {"Codex": [self.root]})
        self.sources.start()
        self.addCleanup(self.sources.stop)
        cache = patch.object(agent_pet, "_session_logs", agent_pet.SessionLogs())
        cache.start()
        self.addCleanup(cache.stop)
        desktop = patch.object(agent_pet, "_desktop_activity",
                               DesktopActivity(SimpleNamespace(fetch=lambda: [])), create=True)
        desktop.start()
        self.addCleanup(desktop.stop)

    def snapshot(self, offset):
        with patch.object(agent_pet.time, "time", return_value=BASE + offset):
            return agent_pet.snapshot()

    def test_finishing_one_turn_keeps_another_turn_working(self):
        working = self.root / "working.jsonl"
        finished = self.root / "finished.jsonl"
        write_log(working, codex_event("task_started", 0))
        write_log(finished, codex_event("task_started", 1), codex_event("task_complete", 2))
        self.assertEqual(self.snapshot(3).state, "think")
        self.assertEqual(self.snapshot(20).state, "think")
        write_log(working, codex_event("task_started", 0), codex_event("task_complete", 21))
        self.assertEqual(self.snapshot(22).state, "done")
        self.assertEqual(self.snapshot(27).state, "idle")

    def test_newer_completed_source_does_not_hide_running_codex(self):
        claude = self.root / "claude"
        claude.mkdir()
        codex = self.root / "codex"
        codex.mkdir()
        write_log(codex / "working.jsonl", codex_event("task_started", 0))
        write_log(claude / "finished.jsonl", {
            "type": "assistant", "timestamp": BASE + 10,
            "message": {"content": [{"type": "text", "text": "done"}]}})
        with patch.object(agent_pet, "SOURCES", {"Codex": [codex], "Claude": [claude]}):
            result = self.snapshot(30)
        self.assertEqual((result.source, result.state), ("Codex", "think"))

    def test_deleted_working_log_does_not_leave_a_busy_cache_entry(self):
        working = self.root / "working.jsonl"
        write_log(working, codex_event("task_started", 0))
        write_log(self.root / "finished.jsonl", codex_event("task_complete", 10))
        self.assertEqual(self.snapshot(20).state, "think")
        working.unlink()
        self.assertEqual(self.snapshot(21).state, "idle")

    def test_abandoned_old_logs_do_not_keep_the_pet_busy(self):
        write_log(self.root / "abandoned.jsonl", codex_event("task_started", 0))
        write_log(self.root / "finished.jsonl", codex_event("task_complete", 1_000))
        self.assertEqual(self.snapshot(1_010).state, "idle")

    def test_malformed_other_session_cannot_stop_the_aggregate(self):
        write_log(self.root / "working.jsonl", codex_event("task_started", 10))
        write_log(self.root / "broken.jsonl", {
            "type": "response_item", "timestamp": BASE,
            "payload": {"type": "function_call", "name": None}})
        self.assertEqual(self.snapshot(20).state, "think")

    def test_chatgpt_working_after_codex_finishes_then_returns_to_idle(self):
        write_log(self.root / "finished.jsonl", codex_event("task_complete", 10))
        rows = [{"id": "chatgpt-task", "kind": "chatgpt", "status": "active", "updatedAt": BASE}]
        reader = DesktopActivity(SimpleNamespace(fetch=lambda: rows))
        with patch.object(agent_pet, "_desktop_activity", reader):
            current = self.snapshot(20)
            self.assertEqual((current.source, current.state), ("ChatGPT", "think"))
            self.assertEqual(self.snapshot(1_000).state, "think")
            rows[0]["status"] = "idle"
            self.assertEqual(self.snapshot(1_010).state, "idle")

    def test_live_codex_state_extends_quiet_turn_and_overrides_incomplete_log(self):
        ident = "01a1954f-d36a-7e23-a112-2bae164048cd"
        write_log(self.root / (ident + ".jsonl"), codex_event("task_started", 0))
        rows = [{"id": ident, "kind": "codex", "hostId": "local", "status": "active", "updatedAt": BASE}]
        reader = DesktopActivity(SimpleNamespace(fetch=lambda: rows))
        with patch.object(agent_pet, "_desktop_activity", reader):
            self.assertEqual(self.snapshot(1_000).state, "think")
            rows[0]["status"] = "idle"
            self.assertEqual(self.snapshot(1_005).state, "idle")

    def test_live_codex_does_not_replace_a_known_waiting_state_with_generic_busy(self):
        ident = "01a1954f-d36a-7e23-a112-2bae164048cd"
        write_log(self.root / (ident + ".jsonl"), {
            "type": "response_item", "timestamp": BASE,
            "payload": {"type": "function_call", "name": "request_user_input"}})
        rows = [{"id": ident, "kind": "codex", "hostId": "local", "status": "active", "updatedAt": BASE}]
        with patch.object(agent_pet, "_desktop_activity", DesktopActivity(SimpleNamespace(fetch=lambda: rows))):
            self.assertEqual(self.snapshot(1_000).state, "ask")

    def test_live_codex_remains_busy_when_log_tail_contains_only_metadata(self):
        ident = "01a1954f-d36a-7e23-a112-2bae164048cd"
        write_log(self.root / (ident + ".jsonl"), {"type": "token_usage_record", "timestamp": BASE})
        rows = [{"id": ident, "kind": "codex", "hostId": "local", "status": "active", "updatedAt": BASE}]
        with patch.object(agent_pet, "_desktop_activity", DesktopActivity(SimpleNamespace(fetch=lambda: rows))):
            self.assertEqual(self.snapshot(1_000).state, "think")


if __name__ == "__main__":
    unittest.main()
