"""Agent Pet lifecycle: single instance, reconnects, heartbeats and disk logs."""
from __future__ import annotations

import argparse
import json
import logging
from logging.handlers import RotatingFileHandler
import os
import subprocess
import time
from types import SimpleNamespace

from host_runtime import DEFAULT_DEVICE, MosaicoClient, RUNTIME


class SingleInstance:
    """The OS releases this project-scoped lock after crashes and reboots."""
    def __init__(self):
        self.file = None

    def acquire(self) -> bool:
        RUNTIME.mkdir(parents=True, exist_ok=True)
        self.file = (RUNTIME / "instance.lock").open("a+b")
        try:
            if not self.file.tell():
                self.file.write(b"0")
                self.file.flush()
            self.file.seek(0)
            if os.name == "nt":
                import msvcrt
                msvcrt.locking(self.file.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(self.file, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError:
            self.close()
            return False
        return True

    def close(self) -> None:
        if self.file is not None:
            self.file.close()
            self.file = None


def publish_status(**values) -> None:
    values.update(pid=os.getpid(), updated_at=time.time())
    path = RUNTIME / "status.json"
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps(values, ensure_ascii=False), encoding="utf-8")
    temporary.replace(path)


def push(current, client: MosaicoClient, states: dict) -> None:
    phrase, animation = states[current.state]
    detail = (f"{current.tool} · {current.calls}次" if current.tool and current.calls
              else current.tool or "--")
    fields = {"T": current.source, "A": animation, "P": phrase, "D": detail}
    payload = "\n".join(f"{key}={' '.join(str(value).split())}" for key, value in fields.items())
    response = json.loads(client.rpc("0x0001", payload))
    if response.get("ok") is not True:
        raise RuntimeError(f"Pet update rejected: {response}")


def run_agent(args, get_snapshot, states, *, reset_stop: bool = True) -> int:
    instance = SingleInstance()
    if not instance.acquire():
        logging.info("Agent Pet is already running")
        return 0
    stop = RUNTIME / "stop"
    if reset_stop:
        stop.unlink(missing_ok=True)
    automatic = args.device_id == DEFAULT_DEVICE
    selected_device = None if automatic else args.device_id
    client = MosaicoClient(selected_device)
    shown = None
    connected = False
    last_push = last_probe = 0.0
    last_error = None
    logging.info("Agent Pet started; waiting for USB %s", args.device_id)
    try:
        publish_status(running=True, connected=False, device_id=args.device_id)
        while not stop.exists():
            now = time.monotonic()
            try:
                if not connected:
                    if now - last_probe < args.reconnect_interval:
                        time.sleep(args.interval)
                        continue
                    last_probe = now
                    if not client.present():
                        if last_error is not None:
                            publish_status(running=True, connected=False, device_id=selected_device)
                            last_error = None
                        time.sleep(args.interval)
                        continue
                    if automatic:
                        device = client.connect_auto()
                        selected_device = device["device_id"]
                    else:
                        client.start_lease()
                        device = client.live_status()
                    connected = True
                    shown = None
                    logging.info("USB connected; boot_id=%s", device.get("boot_id"))
                current = get_snapshot() or SimpleNamespace(
                    source="Agent Pet", state="idle", tool="", calls=0)
                key = (current.source, current.state, current.tool, current.calls)
                if key != shown or now - last_push >= args.heartbeat:
                    push(current, client, states)
                    if key != shown:
                        logging.info("%s %s %s %s", *key)
                    shown, last_push = key, time.monotonic()
                    last_error = None
                    publish_status(running=True, connected=True, device_id=selected_device,
                                   source=current.source, state=current.state,
                                   phrase=states[current.state][0], tool=current.tool, calls=current.calls)
            except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as exc:
                connected = False
                if automatic:
                    client.close()
                error = str(exc)[-800:]
                if error != last_error:
                    logging.warning("Waiting/reconnecting: %s", error)
                    last_error = error
                publish_status(running=True, connected=False, device_id=selected_device, error=error)
            time.sleep(args.interval)
    finally:
        try:
            client.close()
            publish_status(running=False, connected=False, device_id=args.device_id)
            stop.unlink(missing_ok=True)
        finally:
            instance.close()
        logging.info("Agent Pet stopped")
    return 0


def main(get_snapshot, states, *, reset_stop: bool = True) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    action = parser.add_mutually_exclusive_group()
    action.add_argument("--once", action="store_true", help="Inspect logs without contacting a device")
    action.add_argument("--stop", action="store_true", help="Stop only this project's pet client")
    action.add_argument("--status", action="store_true", help="Read client status")
    parser.add_argument("--background", action="store_true", help="Log to disk without console output")
    parser.add_argument("--device-id", default=DEFAULT_DEVICE,
                        help="Full Device ID, or 'auto' for one attached Mosaico")
    parser.add_argument("--interval", type=float, default=1.5)
    parser.add_argument("--reconnect-interval", type=float, default=5)
    parser.add_argument("--heartbeat", type=float, default=10)
    args = parser.parse_args()
    if args.once:
        print(get_snapshot())
        return 0
    RUNTIME.mkdir(parents=True, exist_ok=True)
    if args.stop:
        (RUNTIME / "stop").touch()
        return 0
    if args.status:
        path = RUNTIME / "status.json"
        data = json.loads(path.read_text(encoding="utf-8")) if path.exists() else {}
        probe = SingleInstance()
        if probe.acquire():
            data["running"] = False
            probe.close()
        print(json.dumps(data, ensure_ascii=False))
        return 0
    if args.interval < 0.5 or args.reconnect_interval < 2 or not 2 <= args.heartbeat <= 15:
        parser.error("interval >= 0.5, reconnect-interval >= 2, heartbeat must be 2..15 seconds")
    try:
        if args.device_id != DEFAULT_DEVICE:
            bytes.fromhex(args.device_id)
            if len(args.device_id) < 12:
                raise ValueError()
    except ValueError:
        parser.error("device-id must be the full hexadecimal ESP-Iris Device ID")
    handlers = [RotatingFileHandler(RUNTIME / "agent-pet.log", maxBytes=512_000,
                                   backupCount=2, encoding="utf-8")]
    if not args.background:
        handlers.append(logging.StreamHandler())
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s",
                        handlers=handlers)
    try:
        return run_agent(args, get_snapshot, states, reset_stop=reset_stop)
    except KeyboardInterrupt:
        return 0
    except Exception:
        logging.exception("Agent Pet failed")
        return 1
