"""Quiet Windows processes and the public Mosaico CLI transport.

No USB/serial handles are opened here. One owned `iris run` client keeps the
project Gateway alive; stopping it never terminates a shared Gateway.
"""
from __future__ import annotations

import base64
import json
import os
import subprocess
import sys
from pathlib import Path

PROJECT = Path(__file__).resolve().parents[1]
ROOT = PROJECT.parents[1]
RUNTIME = PROJECT / ".gateway" / "agent-pet"
PROJECT_NAME = "Agent_Pet"
SUPPORTED_FIRMWARE_NAMES = (PROJECT_NAME, "my_app")  # Pre-rename boards can still sync.
DEFAULT_DEVICE = "auto"
MOSAICO_USB_ID = (0x303A, 0x4002)


def quiet_process_options() -> dict:
    if os.name != "nt":
        return {}
    # CLI helpers inherit a console that has no window to show.
    return {"creationflags": subprocess.CREATE_NO_WINDOW}


def console_python() -> str:
    executable = Path(sys.executable)
    if executable.name.lower() == "pythonw.exe":
        executable = executable.with_name("python.exe")
    return str(executable)


def cli_environment() -> dict[str, str]:
    env = dict(os.environ)
    env["PYTHONIOENCODING"] = "utf-8"
    env["PYTHONUNBUFFERED"] = "1"
    env["NO_PROXY"] = "localhost,127.0.0.1"
    env.pop("MSYSTEM", None)
    return env


class MosaicoClient:
    def __init__(self, device_id: str | None = DEFAULT_DEVICE):
        self.configured_device = None if device_id == DEFAULT_DEVICE else device_id
        self.device_id = self.configured_device
        self.endpoint = None
        self.lease: subprocess.Popen | None = None
        self.lease_log = None

    def argv(self, *args: str) -> list[str]:
        return [console_python(), "-X", "utf8", str(ROOT / "mosaico.py"), "--json", "iris", *args,
                "--project", str(PROJECT)]

    def call(self, *args: str, select: bool = True) -> dict:
        if select:
            if self.device_id is None:
                raise RuntimeError("No live Mosaico has been selected")
            args += ("--device-id", self.device_id)
        result = subprocess.run(self.argv(*args), cwd=ROOT, stdin=subprocess.DEVNULL,
                                capture_output=True, text=True, encoding="utf-8",
                                errors="replace", env=cli_environment(), timeout=50,
                                **quiet_process_options())
        if result.returncode:
            raise RuntimeError((result.stderr or result.stdout).strip()[-800:])
        for line in reversed(result.stdout.splitlines()):
            try:
                value = json.loads(line)
            except ValueError:
                continue
            if isinstance(value, dict):
                return value
        raise RuntimeError("Mosaico CLI returned no JSON response")

    def present(self) -> bool:
        data = self.call("list", "--details", select=False)
        if self.configured_device is None:
            self.device_id = None
            self.endpoint = None
            candidates = {
                entry["endpoint"] for entry in data.get("endpoints", [])
                if entry.get("present") and entry.get("endpoint", "").startswith("usb:")
                and entry.get("link_role") == "control"
                and (entry.get("vid"), entry.get("pid")) == MOSAICO_USB_ID
            }
            if len(candidates) > 1:
                raise RuntimeError("检测到多块 Mosaico，请只连接要同步的那一块。")
            self.endpoint = next(iter(candidates), None)
            return self.endpoint is not None
        serial = bytes.fromhex(self.configured_device)[-6:].hex().upper()
        return any(endpoint.get("present") and
                   str(endpoint.get("serial_number", "")).upper() == serial
                   for endpoint in data.get("endpoints", []))

    def connect_auto(self) -> dict:
        if self.endpoint is None:
            raise RuntimeError("正在等待 Mosaico USB 连接。")
        claim = self.call("claim", "--endpoint", self.endpoint, select=False)
        device = claim.get("device")
        device_id = device.get("device_id") if isinstance(device, dict) else None
        if not isinstance(device_id, str) or not device_id:
            raise RuntimeError(claim.get("message") or "无法握手确认这块 Mosaico。")
        self.device_id = device_id
        self.start_lease()
        status = self.live_status()
        if status.get("board_id") != "esp-mosaico":
            raise RuntimeError("当前设备不是 ESP-Mosaico。")
        pet = json.loads(self.rpc("0x0002"))
        if not isinstance(pet, dict) or not {"anim", "connected", "updates"} <= pet.keys():
            raise RuntimeError("当前设备没有运行 Agent Pet 桌宠。")
        return status

    def live_status(self) -> dict:
        data = self.call("device-status")
        # The CLI wraps live evidence under `device`; retain support for its
        # status wrapper, never treating discovery/cache as a live handshake.
        for name in ("device", "status", "device_status"):
            if isinstance(data.get(name), dict) and "device_id" in data[name]:
                data = data[name]
                break
        if data.get("device_id") != self.device_id or data.get("stale", True):
            raise RuntimeError("Waiting for live identity from the selected Mosaico")
        if data.get("firmware_mode") != "normal" or data.get("project_name") not in SUPPORTED_FIRMWARE_NAMES:
            raise RuntimeError(f"Selected Mosaico is not running {PROJECT_NAME}")
        return data

    def start_lease(self) -> None:
        if self.lease is not None and self.lease.poll() is None:
            return
        if self.device_id is None:
            raise RuntimeError("No live Mosaico has been selected")
        self.close()
        RUNTIME.mkdir(parents=True, exist_ok=True)
        self.lease_log = (RUNTIME / "gateway-client.log").open("w", encoding="utf-8")
        try:
            self.lease = subprocess.Popen(
                self.argv("run", "--device-id", self.device_id), cwd=ROOT,
                stdin=subprocess.DEVNULL, stdout=self.lease_log,
                stderr=subprocess.STDOUT, env=cli_environment(), **quiet_process_options())
        except BaseException:
            self.lease_log.close()
            self.lease_log = None
            raise

    def rpc(self, method: str, payload: str = "") -> str:
        args = ["rpc", "0x5354", method]
        if payload:
            args += ["--payload", payload]
        data = self.call(*args)
        encoded = data.get("response", {}).get("payload_base64")
        if encoded is None:
            raise RuntimeError("Agent Pet RPC response is missing")
        return base64.b64decode(encoded, validate=True).decode("utf-8")

    def close(self) -> None:
        if self.lease is not None:
            if self.lease.poll() is None:
                # Only our CLI client is stopped. Its short lease expires on
                # the Gateway; other clients and active updates remain intact.
                self.lease.terminate()
                try:
                    self.lease.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    self.lease.kill()
                    self.lease.wait(timeout=5)
            self.lease = None
        if self.lease_log is not None:
            self.lease_log.close()
            self.lease_log = None
