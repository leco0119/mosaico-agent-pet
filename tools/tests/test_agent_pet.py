"""Host behavior checks; no device access, startup changes or external packages."""
import base64
import json
import os
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import agent_pet
import host_runtime
import pet_runner


TEST_DEVICE_ID = "4553502d495249530100112233445566"


class AgentPetTests(unittest.TestCase):
    def setUp(self):
        desktop = patch.object(agent_pet, "_desktop_activity", SimpleNamespace(
            read=lambda now: SimpleNamespace(threads=[], observed_at=now)))
        desktop.start()
        self.addCleanup(desktop.stop)

    def test_open_windows_log_with_old_mtime_is_still_active(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "session.jsonl"
            path.write_text(json.dumps({"type": "response_item", "timestamp": "2026-11-05T04:00:00Z",
                "payload": {"type": "function_call", "name": "exec_command"}}) + "\n")
            os.utime(path, (1, 1))
            now = agent_pet.datetime.fromisoformat("2026-11-05T04:00:01+00:00").timestamp()
            with patch.object(agent_pet, "SOURCES", {"Codex": [Path(folder)]}), \
                    patch.object(agent_pet, "_session_logs", agent_pet.SessionLogs()), \
                    patch.object(agent_pet.time, "time", return_value=now):
                current = agent_pet.snapshot()
            self.assertEqual(current.state, "shell")
            self.assertEqual(current.age, 1)

    def test_resumed_older_session_wins_by_event_time(self):
        with tempfile.TemporaryDirectory() as folder:
            first = Path(folder) / "older-session.jsonl"
            second = Path(folder) / "newer-session.jsonl"
            first.write_text('{"timestamp":"2026-11-05T04:00:02Z"}\n')
            second.write_text('{"timestamp":"2026-11-05T04:00:00Z"}\n')
            os.utime(first, (1, 1))
            os.utime(second, (100, 100))
            logs = agent_pet.SessionLogs()
            with patch.object(agent_pet, "SOURCES", {"Codex": [Path(folder)]}):
                self.assertEqual(logs.newest()[1], first)
                with second.open("a") as file:
                    file.write('{"timestamp":"2026-11-05T04:00:03Z"}\n')
                os.utime(second, (100, 100))
                self.assertEqual(logs.newest()[1], second)

    def test_codex_commentary_does_not_finish_the_turn(self):
        record = {"type": "response_item", "payload": {
            "type": "message", "role": "assistant", "channel": "commentary"}}
        self.assertEqual(agent_pet.classify(record), ("thinking", ""))
        record["payload"]["channel"] = "final"
        self.assertEqual(agent_pet.classify(record), ("reply", ""))
        record["payload"]["role"] = "system"
        self.assertIsNone(agent_pet.classify(record))

    def test_codex_lifecycle_and_namespaced_tools(self):
        self.assertEqual(agent_pet.classify({"type": "event_msg", "payload": {
            "type": "task_started"}}), ("user", ""))
        self.assertEqual(agent_pet.classify({"type": "event_msg", "payload": {
            "type": "task_complete"}}), ("reply", ""))
        self.assertEqual(agent_pet.tool_state("functions.exec"), "shell")
        self.assertEqual(agent_pet.tool_state("functions.request_user_input_async"), "ask")

    def test_codex_completion_returns_to_idle_despite_later_metadata(self):
        completed = {"type": "event_msg", "timestamp": "2026-11-05T04:00:00Z",
                     "payload": {"type": "task_complete"}}
        usage = {"type": "token_usage_record", "timestamp": "2026-11-05T04:00:04Z"}
        finished_at = agent_pet.event_time([completed], 0)
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "session.jsonl"
            path.write_text("\n".join(map(json.dumps, (completed, usage))) + "\n", encoding="utf-8")
            with patch.object(agent_pet, "SOURCES", {"Codex": [path.parent]}), \
                    patch.object(agent_pet, "_session_logs", agent_pet.SessionLogs()), \
                    patch.object(agent_pet.time, "time", return_value=finished_at + 4) as clock:
                self.assertEqual(agent_pet.snapshot().state, "done")
                clock.return_value = finished_at + 5
                self.assertEqual(agent_pet.snapshot().state, "idle")

    def test_pandacode_uses_actual_activity_to_choose_source(self):
        base = 1_790_000_000_000
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            panda = root / "panda" / "project" / "sessions" / "session.jsonl"
            panda.parent.mkdir(parents=True)
            codex = root / "codex" / "session.jsonl"
            codex.parent.mkdir()
            codex.write_text(json.dumps({"timestamp": base + 1_000}) + "\n")
            panda.write_text(json.dumps({
                "type": "session_meta", "field": "turnMetrics", "updatedAt": base + 20_000,
                "value": [{"status": "completed", "completedAt": base}],
            }) + "\n")
            logs = agent_pet.SessionLogs()
            sources = {"Codex": [codex.parent], "PandaCode": [root / "panda"]}
            with patch.object(agent_pet, "SOURCES", sources):
                self.assertEqual(logs.newest(), ("Codex", codex))
                with panda.open("a") as stream:
                    stream.write(json.dumps({
                        "type": "message", "encoding": "dual",
                        "ui": {"role": "user", "timestamp": base + 2_000},
                    }) + "\n")
                self.assertEqual(logs.newest(), ("PandaCode", panda))

    def test_pandacode_turn_uses_messages_then_terminal_metrics(self):
        base = 1_790_000_000

        def dual(role, offset, **fields):
            return {"type": "message", "encoding": "dual",
                    "ui": {"role": role, "timestamp": (base + offset) * 1000},
                    "tch": {"role": role, **fields}}

        def assistant(offset, *tools):
            return {"type": "message", "encoding": "shared",
                    "uiExtras": {"timestamp": (base + offset) * 1000},
                    "sharedBody": {"role": "assistant",
                                   "toolCalls": [{"name": name} for name in tools]}}

        def terminal(status, offset):
            return {"type": "session_meta", "field": "turnMetrics",
                    "value": [{"status": status, "completedAt": (base + offset) * 1000}]}

        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "project" / "sessions" / "session.jsonl"
            path.parent.mkdir(parents=True)
            records = []

            def expect(offset, state, calls, *additions):
                records.extend(additions)
                path.write_text("\n".join(map(json.dumps, records)) + "\n", encoding="utf-8")
                with patch.object(agent_pet.time, "time", return_value=base + offset):
                    result = agent_pet.snapshot()
                self.assertEqual((result.source, result.state, result.calls), ("PandaCode", state, calls))

            with patch.object(agent_pet, "SOURCES", {"PandaCode": [Path(folder)]}), \
                    patch.object(agent_pet, "_session_logs", agent_pet.SessionLogs()):
                expect(1, "think", 0, dual("user", 0))
                expect(2, "shell", 2, assistant(2, "read_file", "run_shell"))
                expect(3, "ask", 3, assistant(3, "ask_question"))
                expect(4, "error", 3, dual("tool", 4, toolResults=[{"isError": True}]))
                expect(20, "think", 3, assistant(5))
                expect(21, "done", 3, terminal("completed", 21))
                expect(26, "idle", 3, {"type": "session_meta", "updatedAt": (base + 26) * 1000})
                expect(27, "think", 0, dual("user", 27), terminal("completed", 21))
                expect(28, "error", 0, terminal("failed", 28))
                expect(30, "idle", 0, dual("user", 29), terminal("cancelled", 30))

    def test_every_state_is_screen_only_and_keeps_utf8(self):
        class Client:
            def rpc(self, method, payload):
                self.payload = payload
                return '{"ok":true}'
        client = Client()
        for state in agent_pet.STATES:
            current = agent_pet.Snapshot("Codex", state, "exec", 3, 0)
            pet_runner.push(current, client, agent_pet.STATES)
            fields = dict(line.split("=", 1) for line in client.payload.splitlines())
            self.assertEqual(set(fields), {"A", "P", "D", "T"})
            self.assertEqual(fields["P"], agent_pet.STATES[state][0])
            self.assertNotIn("?", fields["P"])

    def test_cached_identity_and_other_firmware_are_rejected(self):
        client = host_runtime.MosaicoClient(TEST_DEVICE_ID)
        live = {"device_id": client.device_id, "stale": False,
                "firmware_mode": "normal", "project_name": "Agent_Pet"}
        with patch.object(client, "call", return_value={"device": live}):
            self.assertIs(client.live_status(), live)
            live["project_name"] = "my_app"
            self.assertIs(client.live_status(), live)
            live["stale"] = True
            with self.assertRaises(RuntimeError):
                client.live_status()
            live["stale"] = False
            live["project_name"] = "another_app"
            with self.assertRaises(RuntimeError):
                client.live_status()
            live["project_name"] = "vibe-mode"
            live["firmware_mode"] = "recovery"
            with self.assertRaises(RuntimeError):
                client.live_status()

    def test_usb_presence_uses_selected_serial_not_cached_device(self):
        client = host_runtime.MosaicoClient(TEST_DEVICE_ID)
        with patch.object(client, "call", return_value={"devices": [
                {"device_id": client.device_id, "present": True}], "endpoints": [
                {"serial_number": "ANOTHER-BOARD", "present": True}]}):
            self.assertFalse(client.present())
        with patch.object(client, "call", return_value={"endpoints": [
                {"serial_number": "112233445566", "present": True}]}):
            self.assertTrue(client.present())

    def test_rpc_decodes_the_real_payload_wrapper(self):
        client = host_runtime.MosaicoClient(TEST_DEVICE_ID)
        encoded = base64.b64encode('{"ok":true}'.encode()).decode()
        with patch.object(client, "call", return_value={"response": {"payload_base64": encoded}}):
            self.assertEqual(json.loads(client.rpc("0x0001", "A=1")), {"ok": True})

    def test_auto_select_requires_one_present_usb_control_endpoint(self):
        client = host_runtime.MosaicoClient()
        control = {"endpoint": "usb:one", "present": True, "link_role": "control",
                   "vid": 0x303A, "pid": 0x4002}
        data = {**control, "endpoint": "usb:data", "link_role": "data"}
        with patch.object(client, "call", return_value={"endpoints": [control, data]}):
            self.assertTrue(client.present())
            self.assertEqual(client.endpoint, "usb:one")
            self.assertIsNone(client.device_id)
        other = {**control, "endpoint": "usb:two"}
        with patch.object(client, "call", return_value={"endpoints": [control, other]}):
            with self.assertRaises(RuntimeError):
                client.present()
            self.assertIsNone(client.endpoint)

    def test_auto_connect_checks_live_identity_and_pet_service(self):
        client = host_runtime.MosaicoClient(None)
        client.endpoint = "usb:one"
        device = {"device_id": TEST_DEVICE_ID, "stale": False,
                  "firmware_mode": "normal", "project_name": "Agent_Pet", "board_id": "esp-mosaico"}
        response = {"device": device}
        with patch.object(client, "call", return_value=response) as call, \
                patch.object(client, "start_lease"), patch.object(client, "rpc", return_value="{}") as rpc:
            with self.assertRaises(RuntimeError):
                client.connect_auto()
            call.assert_any_call("claim", "--endpoint", "usb:one", select=False)
            rpc.assert_called_once_with("0x0002")

    def test_two_starts_cannot_create_two_watchers(self):
        with tempfile.TemporaryDirectory() as folder, patch.object(pet_runner, "RUNTIME", Path(folder)):
            first = pet_runner.SingleInstance()
            second = pet_runner.SingleInstance()
            self.assertTrue(first.acquire())
            try:
                self.assertFalse(second.acquire())
            finally:
                first.close()
                second.close()
            self.assertTrue(second.acquire())
            second.close()

    def test_disconnect_and_replug_resend_an_unchanged_state(self):
        with tempfile.TemporaryDirectory() as folder:
            runtime = Path(folder)

            class Clock:
                now = 0.0

                def sleep(self, seconds):
                    self.now += seconds
                    if self.now >= 16:
                        (runtime / "stop").touch()

            class Client:
                def __init__(self, device):
                    self.presence = iter((False, True, False, True))
                    self.payloads = []
                    self.boot = 0
                    self.closed = False

                def present(self):
                    return next(self.presence, True)

                def start_lease(self):
                    pass

                def live_status(self):
                    self.boot += 1
                    return {"boot_id": self.boot}

                def rpc(self, method, payload):
                    self.payloads.append(payload)
                    if len(self.payloads) == 2:
                        raise RuntimeError("USB unplugged")
                    return '{"ok":true}'

                def close(self):
                    self.closed = True

            clock = Clock()
            fake_time = SimpleNamespace(monotonic=lambda: clock.now, time=lambda: clock.now,
                                        sleep=clock.sleep)
            client = Client(TEST_DEVICE_ID)
            args = SimpleNamespace(device_id=TEST_DEVICE_ID, interval=1,
                                   reconnect_interval=2, heartbeat=2)
            current = agent_pet.Snapshot("Codex", "read", "read_file", 1, 0)
            with patch.object(pet_runner, "RUNTIME", runtime), patch.object(pet_runner, "time", fake_time), \
                    patch.object(pet_runner, "MosaicoClient", return_value=client), \
                    patch.object(pet_runner, "UsageSync"):
                self.assertEqual(pet_runner.run_agent(args, lambda: current, agent_pet.STATES), 0)
            self.assertEqual(client.boot, 2)
            self.assertGreaterEqual(len(client.payloads), 4)
            self.assertEqual(len(set(client.payloads)), 1)
            self.assertTrue(client.closed)
            self.assertFalse(json.loads((runtime / "status.json").read_text())["running"])


if __name__ == "__main__":
    unittest.main()
