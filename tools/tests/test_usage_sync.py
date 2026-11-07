"""Usage integration contracts; local fixtures only, no account or USB access."""
import importlib
import json
from pathlib import Path
import sys
import tempfile
import threading
import time
import subprocess
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))


def document(percent=42, *, ok=True, tokens="5.32B", stamp="20261107 12:30"):
    return {"updated_text": stamp, "generated_at": stamp, "stale": False, "codex": {
        "ok": ok, "reason": "" if ok else "读取失败", "ring_percent": percent,
        "ring_label": "每周 · 周四 07:25 重置", "bar_percent": -1,
        "bar_label": "", "bar_reset": "", "tokens_text": tokens}}


class UsageSyncTests(unittest.TestCase):
    def setUp(self):
        self.assertIsNotNone(importlib.util.find_spec("usage_sync"), "usage worker is not implemented")
        self.module = importlib.import_module("usage_sync")
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.cache = Path(self.tmp.name) / "stable" / "usage.json"
        self.legacy = Path(self.tmp.name) / "legacy.json"

    def worker(self, collector, **kwargs):
        worker = self.module.UsageSync(cache_path=self.cache, legacy_path=self.legacy,
                                      collector=collector, **kwargs)
        self.addCleanup(worker.close)
        return worker

    def wait_revision(self, worker, minimum):
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            revision, doc = worker.peek()
            if revision >= minimum:
                return doc
            time.sleep(0.01)
        self.fail("worker did not publish its result")

    def test_startup_cache_is_available_while_slow_account_read_runs(self):
        self.cache.parent.mkdir()
        self.cache.write_text(json.dumps(document()), encoding="utf-8")
        entered, release = threading.Event(), threading.Event()
        self.addCleanup(release.set)
        def slow():
            entered.set()
            release.wait(3)
            return document(60)
        worker = self.worker(slow)
        worker.start()
        self.assertTrue(entered.wait(1))
        started = time.monotonic()
        revision, cached = worker.peek()
        self.assertLess(time.monotonic() - started, 0.1)
        self.assertEqual(cached["codex"]["ring_percent"], 42)
        self.assertTrue(cached["stale"])
        # Snapshot consumers cannot mutate shared worker state.
        cached["codex"]["ring_percent"] = 0
        self.assertEqual(worker.peek()[1]["codex"]["ring_percent"], 42)
        self.assertFalse(worker.request())
        release.set()
        self.assertEqual(self.wait_revision(worker, revision + 1)["codex"]["ring_percent"], 60)

    def test_failed_refresh_keeps_numbers_and_original_time(self):
        self.legacy.write_text(json.dumps(document()), encoding="utf-8")
        worker = self.worker(lambda: document(-1, ok=False, tokens="", stamp="later"))
        worker.start()
        result = self.wait_revision(worker, 2)
        self.assertEqual(result["codex"]["ring_percent"], 42)
        self.assertEqual(result["codex"]["tokens_text"], "5.32B")
        self.assertEqual(result["updated_text"], "20261107 12:30")
        self.assertTrue(result["stale"])
        saved = json.loads(self.cache.read_text(encoding="utf-8"))
        self.assertEqual(saved, result)

    def test_migration_strips_legacy_claude_and_preserves_new_cache(self):
        legacy = document(20)
        legacy["claude"] = {"ok": True, "tokens_text": "123M"}
        self.legacy.write_text(json.dumps(legacy), encoding="utf-8")
        worker = self.worker(lambda: document())
        self.assertNotIn("claude", worker.peek()[1])
        self.assertTrue(self.cache.exists())
        self.cache.write_text(json.dumps(document(80)), encoding="utf-8")
        second = self.worker(lambda: document())
        self.assertEqual(second.peek()[1]["codex"]["ring_percent"], 80)

    def test_corrupt_cache_can_recover_from_a_new_result(self):
        self.cache.parent.mkdir()
        self.cache.write_text("broken", encoding="utf-8")
        worker = self.worker(lambda: document(0))
        self.assertIsNone(worker.peek()[1])
        worker.start()
        self.assertEqual(self.wait_revision(worker, 1)["codex"]["ring_percent"], 0)

    def test_cache_write_failure_still_publishes_new_data(self):
        worker = self.worker(lambda: document(90))
        with patch.object(self.module, "save_document", side_effect=OSError("disk unavailable")):
            worker.start()
            self.assertEqual(self.wait_revision(worker, 1)["codex"]["ring_percent"], 90)

    def test_periodic_read_runs_without_ui_or_usb(self):
        count = []
        def collect():
            count.append(1)
            return document(len(count))
        worker = self.worker(collect, interval=0.05)
        worker.start()
        self.assertGreaterEqual(self.wait_revision(worker, 2)["codex"]["ring_percent"], 2)


class DeliveryTests(unittest.TestCase):
    def setUp(self):
        self.assertIsNotNone(importlib.util.find_spec("usage_sync"), "usage delivery is not implemented")
        self.module = importlib.import_module("usage_sync")
        self.requests = 0
        self.worker = self
        self.delivery = self.module.UsageDelivery(self.worker)
        self.calls = []
        self.seq = 0
        self.boot = None
        self.fail_usage = False

    def request(self):
        self.requests += 1
        return True

    def peek(self):
        return 1, document()

    def rpc(self, method, payload="", *, service="0x5354", timeout=8):
        self.calls.append((threading.get_ident(), service, method, payload))
        if service == "0x5550":
            if self.fail_usage:
                raise RuntimeError("usage service unavailable")
            return "ok=1"
        return json.dumps({"usage_protocol": 1, "usage_refresh": self.seq, "boot_id": self.boot})

    def test_old_firmware_keeps_pet_without_sending_usage(self):
        self.delivery.observe({"ok": True})
        self.delivery.tick(self, 1)
        self.assertEqual(self.calls, [])

    def test_shared_client_delivers_once_and_resends_after_reconnect(self):
        self.delivery.observe({"usage_protocol": 1, "usage_refresh": 0})
        self.delivery.tick(self, 1)
        self.delivery.tick(self, 1.5)
        self.assertEqual(len([c for c in self.calls if c[1] == "0x5550"]), 1)
        self.delivery.disconnected()
        self.delivery.observe({"usage_protocol": 1, "usage_refresh": 0})
        self.delivery.tick(self, 2)
        self.assertEqual(len([c for c in self.calls if c[1] == "0x5550"]), 2)
        self.assertTrue(all(c[0] == threading.get_ident() for c in self.calls))

    def test_refresh_counter_coalesces_and_recovers_after_board_reboot(self):
        self.delivery.observe({"usage_protocol": 1, "usage_refresh": 4})
        self.assertEqual(self.requests, 1)
        self.delivery.observe({"usage_protocol": 1, "usage_refresh": 4})
        self.assertEqual(self.requests, 1)
        self.seq = 5
        self.delivery.tick(self, 3)
        self.assertEqual(self.requests, 2)
        self.delivery.disconnected()
        self.delivery.observe({"usage_protocol": 1, "usage_refresh": 0})
        self.assertEqual(self.requests, 3)

    def test_usage_rpc_failure_does_not_escape_to_pet_reconnect_loop(self):
        self.delivery.observe({"usage_protocol": 1, "usage_refresh": 0})
        self.fail_usage = True
        self.delivery.tick(self, 1)
        self.fail_usage = False
        self.delivery.tick(self, 10)
        self.assertEqual(len([c for c in self.calls if c[1] == "0x5550"]), 2)

    def test_board_reboot_between_heartbeats_resends_without_usb_disconnect(self):
        self.boot = "12345678901234567890"
        self.delivery.observe({"usage_protocol": 1, "usage_refresh": 0, "boot_id": self.boot})
        self.delivery.tick(self, 1)
        self.assertEqual(self.requests, 1)
        # USB already recovered before the next successful heartbeat. Sequence
        # remains zero; the collector revision and pet connection do not change.
        self.boot = "22345678901234567890"
        self.delivery.tick(self, 4)
        self.assertEqual(self.requests, 2)
        self.assertEqual(len([c for c in self.calls if c[1] == "0x5550"]), 2)


class TransportTests(unittest.TestCase):
    def test_usage_service_uses_the_existing_selected_client(self):
        import host_runtime
        client = host_runtime.MosaicoClient("4553502d495249530100112233445566")
        with patch.object(client, "call", return_value={"response": {"payload_base64": "b2s9MQ=="}}) as call:
            self.assertEqual(client.rpc("0x0001", "v=1", service="0x5550", timeout=4), "ok=1")
        call.assert_called_once_with("rpc", "0x5550", "0x0001", "--payload", "v=1", timeout=4)

    def test_closing_worker_cancels_and_reaps_owned_codex_process(self):
        from usage_agent import codex_rpc
        stop = threading.Event()
        timer = threading.Timer(0.1, stop.set)
        children = []
        real_popen = subprocess.Popen
        def launch(*args, **kwargs):
            child = real_popen(*args, **kwargs)
            children.append(child)
            return child
        timer.start()
        started = time.monotonic()
        try:
            with patch.object(codex_rpc.subprocess, "Popen", side_effect=launch):
                with self.assertRaises(codex_rpc.CodexRPCError):
                    codex_rpc.read_account([sys.executable, "-c", "import time; time.sleep(20)"], cancel_event=stop)
            self.assertLess(time.monotonic() - started, 3)
            self.assertTrue(children and children[0].poll() is not None)
        finally:
            timer.cancel()


if __name__ == "__main__":
    unittest.main()
