"""Reconnect policy at the real CLI boundary; no USB or shared state access."""
import base64
import copy
import json
from pathlib import Path
import subprocess
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from host_runtime import MosaicoClient

DEVICE = "4553502d495249530100112233445566"
OTHER_DEVICE = "4553502d495249530100aabbccddeeff"
ENDPOINT = "usb:test-control"


class UsbCli:
    """Models stale ownership, successful claims, and the public refusal gate."""

    def __init__(self):
        self.claim = {
            "resource": ENDPOINT, "owner": "old-session", "owner_alive": False,
            "state": "owned", "transfer_id": None, "device_id": DEVICE,
            "generation": 3, "metadata": {},
        }
        self.endpoint = {
            "endpoint": ENDPOINT, "present": True, "link_role": "control",
            "vid": 0x303A, "pid": 0x4002, "serial_number": "112233445566",
            "owner_session_id": "old-session", "busy_reasons": [],
            "ownership": self.claim,
        }
        self.extra_endpoints = []
        self.reconciled = []
        self.claimed = []
        self.refusal = None
        self.rpc_calls = 0
        self.live = {
            "device_id": DEVICE, "stale": False, "firmware_mode": "normal",
            "project_name": "Agent_Pet", "board_id": "esp-mosaico", "boot_id": 42,
        }

    def run(self, argv, **kwargs):
        args = argv[argv.index("iris") + 1:]
        command = args[0]
        error = None
        if command == "list":
            result = {"ok": True, "endpoints": copy.deepcopy([self.endpoint, *self.extra_endpoints])}
        elif command == "reconcile":
            device = args[args.index("--device-id") + 1]
            self.reconciled.append(device)
            error = self.refusal
            if error is None and (self.claim.get("owner_alive") is not False
                                  or self.claim.get("state") != "owned"
                                  or self.claim.get("transfer_id")):
                error = "owner or transfer is still active"
            if error is None:
                self.claim.clear()
                result = {"reconciled": "device:" + device}
        elif command == "claim":
            endpoint = args[args.index("--endpoint") + 1]
            self.claimed.append(endpoint)
            if self.claim:
                error = "USB is not owned by this session"
            else:
                result = {"ok": True, "device": dict(self.live)}
        elif command == "device-status":
            if self.claim:
                error = "USB is not owned by this session"
            else:
                result = {"ok": True, "device": dict(self.live)}
        elif command == "rpc":
            self.rpc_calls += 1
            body = b'{"anim":1,"connected":true,"updates":1}'
            result = {"response": {"payload_base64": base64.b64encode(body).decode()}}
        else:
            raise AssertionError(f"Unexpected device operation: {args}")
        if error:
            return subprocess.CompletedProcess(argv, 4, json.dumps({"ok": False, "message": error}), "")
        return subprocess.CompletedProcess(argv, 0, json.dumps(result), "")


class UsbReconnectTests(unittest.TestCase):
    def setUp(self):
        self.cli = UsbCli()
        transport = patch("host_runtime.subprocess.run", side_effect=self.cli.run)
        transport.start()
        self.addCleanup(transport.stop)

    def client(self, device=None):
        client = MosaicoClient(device)
        # An already live CLI lease avoids launching a host process in these tests.
        client.lease = SimpleNamespace(poll=lambda: None)
        return client

    def test_dead_usb_owner_is_reconciled_before_automatic_connection(self):
        client = self.client()
        self.assertTrue(client.present())
        try:
            result = client.connect_auto()
        except RuntimeError as error:
            self.fail(f"An inactive owner should be recovered automatically: {error}")
        self.assertEqual(result["device_id"], DEVICE)
        self.assertEqual(self.cli.reconciled, [DEVICE])
        self.assertEqual(self.cli.claimed, [ENDPOINT])

    def test_explicit_device_recovers_before_reusing_its_lease(self):
        client = self.client(DEVICE)
        self.assertTrue(client.present())
        client.start_lease()
        try:
            result = client.live_status()
        except RuntimeError as error:
            self.fail(f"An explicitly selected device should recover too: {error}")
        self.assertEqual(result["device_id"], DEVICE)
        self.assertEqual(self.cli.reconciled, [DEVICE])

    def test_incomplete_live_or_transferring_ownership_is_never_reconciled(self):
        changes = [
            {"owner_alive": True}, {"owner_alive": None}, {"owner_alive": 0},
            {"state": "accepting"}, {"transfer_id": "active-transfer"},
            {"owner": ""}, {"device_id": ""}, {"device_id": OTHER_DEVICE},
            {"resource": "usb:other-port"},
        ]
        for change in changes:
            with self.subTest(change=change):
                self.cli = UsbCli()
                self.cli.claim.update(change)
                client = self.client()
                with patch("host_runtime.subprocess.run", side_effect=self.cli.run):
                    self.assertTrue(client.present())
                    with self.assertRaises(RuntimeError):
                        client.connect_auto()
                self.assertEqual(self.cli.reconciled, [])

    def test_busy_endpoint_is_not_reconciled(self):
        self.cli.endpoint["busy_reasons"] = ["firmware.system_update"]
        client = self.client()
        self.assertTrue(client.present())
        with self.assertRaises(RuntimeError):
            client.connect_auto()
        self.assertEqual(self.cli.reconciled, [])

    def test_missing_transfer_evidence_does_not_authorize_cleanup(self):
        del self.cli.claim["transfer_id"]
        client = self.client()
        self.assertTrue(client.present())
        with self.assertRaises(RuntimeError):
            client.connect_auto()
        self.assertEqual(self.cli.reconciled, [])

    def test_a_different_live_board_after_cleanup_is_rejected(self):
        self.cli.live["device_id"] = OTHER_DEVICE
        client = self.client()
        self.assertTrue(client.present())
        with self.assertRaises(RuntimeError):
            client.connect_auto()
        self.assertEqual(self.cli.reconciled, [DEVICE])
        self.assertEqual(self.cli.rpc_calls, 0)

    def test_multiple_present_boards_are_rejected_before_reconciliation(self):
        self.cli.extra_endpoints = [{**self.cli.endpoint, "endpoint": "usb:another-board"}]
        with self.assertRaises(RuntimeError):
            self.client().present()
        self.assertEqual(self.cli.reconciled, [])
        self.assertEqual(self.cli.claimed, [])

    def test_public_reconcile_refusal_prevents_the_claim(self):
        self.cli.refusal = "physical USB lock is still held"
        client = self.client()
        self.assertTrue(client.present())
        with self.assertRaisesRegex(RuntimeError, "physical USB lock"):
            client.connect_auto()
        self.assertEqual(self.cli.reconciled, [DEVICE])
        self.assertEqual(self.cli.claimed, [])

    def test_replug_cannot_switch_to_another_board(self):
        self.cli.claim.clear()
        client = self.client()
        self.assertTrue(client.present())
        self.assertEqual(client.connect_auto()["device_id"], DEVICE)
        self.cli.endpoint["serial_number"] = "AABBCCDDEEFF"
        self.cli.live["device_id"] = OTHER_DEVICE
        self.assertFalse(client.present())
        self.assertEqual(self.cli.reconciled, [])


if __name__ == "__main__":
    unittest.main()
