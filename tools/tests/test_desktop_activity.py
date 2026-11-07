"""Desktop activity is live state, not a guess from the history database."""
import json
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import desktop_activity


def thread(kind, ident, status, updated=100):
    return {"kind": kind, "id": ident, "status": status, "updatedAt": updated,
            **({"hostId": "local"} if kind == "codex" else {})}


class Client:
    def __init__(self, *results):
        self.results = iter(results)
        self.calls = 0

    def fetch(self):
        self.calls += 1
        result = next(self.results)
        if isinstance(result, Exception):
            raise result
        return result


class DesktopActivityTests(unittest.TestCase):
    def test_chatgpt_active_survives_local_codex_completion_and_quiet_period(self):
        rows = [thread("codex", "local", "idle"), thread("chatgpt", "remote", "active")]
        client = Client(rows, rows, [thread("chatgpt", "remote", "idle")])
        reader = desktop_activity.DesktopActivity(client)
        current = reader.read(1_000)
        self.assertEqual([(t.source, t.state) for t in current.threads],
                         [("Codex", "idle"), ("ChatGPT", "think")])
        # An actively reported task must not expire after ten quiet minutes.
        self.assertEqual(reader.read(2_000).threads[1].state, "think")
        self.assertEqual(reader.read(2_010).threads[0].state, "idle")

    def test_short_failure_retains_last_state_but_does_not_latch_busy_forever(self):
        client = Client([thread("chatgpt", "remote", "active")],
                        TimeoutError(), TimeoutError(), [thread("chatgpt", "remote", "idle")])
        reader = desktop_activity.DesktopActivity(client)
        self.assertTrue(reader.read(100).available)
        self.assertEqual(reader.read(105).threads[0].state, "think")
        self.assertEqual(reader.read(120).threads, [])
        self.assertEqual(reader.read(125).threads[0].state, "idle")

    def test_closed_app_clears_active_state_immediately(self):
        client = Client([thread("chatgpt", "remote", "active")], desktop_activity.DesktopClosed())
        reader = desktop_activity.DesktopActivity(client)
        reader.read(100)
        result = reader.read(105)
        self.assertFalse(result.available)
        self.assertEqual(result.threads, [])

    def test_polling_is_throttled_and_unknown_states_do_not_become_active(self):
        client = Client([thread("chatgpt", "old", "notLoaded"),
                         thread("chatgpt", "future", "new-unknown-state")])
        reader = desktop_activity.DesktopActivity(client)
        self.assertEqual(reader.read(100).threads, [])
        self.assertEqual(reader.read(101).threads, [])
        self.assertEqual(client.calls, 1)

    def test_read_only_rpc_response_parses_structured_listing(self):
        listing = {"threads": [thread("chatgpt", "remote", "active")], "pinnedThreads": []}
        wire = {"id": 1, "jsonrpc": "2.0", "result": {"success": True, "contentItems": [
            {"type": "inputText", "text": json.dumps(listing)}]}}
        self.assertEqual(desktop_activity.decode_listing(wire), listing)
        wire["result"]["success"] = False
        with self.assertRaises(ValueError):
            desktop_activity.decode_listing(wire)

    def test_older_chatgpt_history_pages_are_included_and_fresh_idle_wins(self):
        recent = [thread("codex", str(i), "idle") for i in range(50)]
        replies = iter([
            {"threads": recent},
            {"threads": [thread("chatgpt", "older", "active")], "nextCursor": "page-2"},
            {"threads": recent},
            {"threads": [thread("chatgpt", "oldest", "active")], "nextCursor": None},
            {"threads": [*recent[:49], thread("chatgpt", "older", "idle")]},
            {"threads": recent},
        ])

        def respond(path, request, deadline):
            self.assertEqual(request["params"]["tool"], "list_threads")
            return {"id": 1, "result": {"success": True, "contentItems": [
                {"type": "inputText", "text": json.dumps(next(replies))}]}}

        client = desktop_activity.DesktopClient()
        client.anchor = "01a1954f-d36a-7e23-a112-2bae164048cd"
        with patch.object(desktop_activity, "find_pipes", return_value=["test-pipe"]), \
                patch.object(desktop_activity, "pipe_request", side_effect=respond), \
                patch.object(desktop_activity.time, "monotonic", return_value=100):
            reader = desktop_activity.DesktopActivity(client)
            self.assertIn("older", [t.thread_id for t in reader.read(100).threads if t.state == "think"])
            self.assertIn("oldest", [t.thread_id for t in reader.read(105).threads if t.state == "think"])
            self.assertNotIn("older", [t.thread_id for t in reader.read(110).threads if t.state == "think"])
            # Dropping out of the recent page must not revive the old active row.
            self.assertNotIn("older", [t.thread_id for t in reader.read(115).threads if t.state == "think"])


if __name__ == "__main__":
    unittest.main()
