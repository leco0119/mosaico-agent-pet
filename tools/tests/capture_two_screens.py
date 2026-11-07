"""Exercise the actual native backend with taps/swipes and save visual evidence."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import time
from urllib.parse import urlparse
import uuid

PROJECT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT / "managed_components/espressif__esp-gsp/tools/sim_bridge"))
from rpc_client import connect  # noqa: E402 - use this project's pinned bridge client


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--offline", action="store_true")
    parser.add_argument("--empty", action="store_true")
    args = parser.parse_args()
    evidence = PROJECT / "docs/evidence/codex-usage"
    evidence.mkdir(parents=True, exist_ok=True)
    run = PROJECT / ".codex-runs" / ("two-screens-" + uuid.uuid4().hex[:8])
    run.mkdir(parents=True)
    manifest = json.loads((PROJECT / "build-sim/pet_backend-Release.json").read_text())
    ready = run / "ready.json"
    env = os.environ.copy()
    env.update(MOSAICO_PET_SIM_CYCLE="loop", USAGE_PANEL_CAPTURE="1")
    for key in ("USAGE_PANEL_OFFLINE", "USAGE_PANEL_EMPTY", "USAGE_PANEL_PAYLOAD_FILE"):
        env.pop(key, None)
    if args.offline:
        env["USAGE_PANEL_OFFLINE"] = "1"
    if args.empty:
        env["USAGE_PANEL_EMPTY"] = "1"
    flags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
    sim = backend = None
    actions = []
    with (run / "sim.log").open("wb") as simlog, (run / "backend.log").open("wb") as applog:
        try:
            sim = subprocess.Popen([str(Path.home() / ".cache/esp-mosaico/sim/1.5.1/sim.exe"),
                "--bundle", manifest["bundle"], "--frames", "0", "--backend-enable", "--backend-required",
                "--backend-idle-timeout", "45", "--api-listen", "tcp://127.0.0.1:0", "--headless",
                "--api-json", str(PROJECT / "build-sim/pet_backend_assets/bundle_gsp.api.json"),
                "--ready-file", str(ready)], stdout=simlog, stderr=simlog, creationflags=flags)
            deadline = time.monotonic() + 20
            info = None
            while time.monotonic() < deadline:
                if ready.exists():
                    try:
                        info = json.loads(ready.read_text())
                        break
                    except ValueError:
                        pass
                assert sim.poll() is None, "simulator exited"
                time.sleep(.05)
            assert info and info["pid"] == sim.pid
            backend = subprocess.Popen([manifest["executable"], "--endpoint", info["backend"]],
                env=env, stdout=applog, stderr=applog, creationflags=flags)
            endpoint = urlparse(info["api"])
            with connect(endpoint.hostname, endpoint.port) as api:
                def call(method, params=None):
                    result = api.call(method, params)
                    actions.append({"method": method, "params": params, "response": result})
                    assert "error" not in result, result
                    return result.get("result")

                def applied(clicks):
                    deadline = time.monotonic() + 10
                    marker = f"applied clicks={clicks} "
                    while time.monotonic() < deadline:
                        log = (run / "backend.log").read_text(errors="replace")
                        if marker in log:
                            assert "error=0" in log.split(marker)[-1].splitlines()[0], log
                            return
                        assert backend.poll() is None, log
                        time.sleep(.05)
                    raise TimeoutError(marker)

                def capture(name):
                    time.sleep(.3)
                    call("wait", {"frames": 3})
                    path = evidence / (name + ".png")
                    call("screenshot", {"path": str(path), "format": "png"})
                    print("Captured", path.name, flush=True)

                def tap(x, y):
                    before = (run / "backend.log").read_text(errors="replace").count("pet_pages: event type=0 ")
                    call("tap", {"x": x, "y": y})
                    deadline = time.monotonic() + 6
                    while time.monotonic() < deadline:
                        log = (run / "backend.log").read_text(errors="replace")
                        if log.count("pet_pages: event type=0 ") > before:
                            return
                        time.sleep(.02)
                    raise TimeoutError(f"tap {x},{y} did not complete in the native controller")

                def scene(expected):
                    event = api.notification("scene_changed", timeout=8)
                    assert event["to"] == expected, event
                    time.sleep(.5)
                    assert backend.poll() is None

                call("subscribe", {"events": ["scene_changed", "callback"]})
                if not args.empty:
                    applied(0)
                else:
                    time.sleep(.5)
                capture("pet-home")
                call("drag", {"x1": 410, "y1": 240, "x2": 70, "y2": 240})
                scene(1)
                capture("usage-offline" if args.offline else "usage-empty" if args.empty else "usage-main")
                if args.offline or args.empty:
                    tap(420, 450)
                    capture("offline-updating" if args.offline else "empty-updating")
                else:
                    # Real pet ticks continue while the usage page is visible.
                    time.sleep(2.1)
                    tap(420, 450)
                    capture("usage-updating")
                    applied(1)
                    capture("usage-dual")
                    tap(35, 450)
                    applied(2)
                    capture("usage-failed-retained")
                    tap(240, 450)
                    time.sleep(31)
                    capture("usage-timeout-retained")
                    tap(420, 450)
                    applied(4)
                    capture("usage-recovered")
                call("drag", {"x1": 70, "y1": 240, "x2": 410, "y2": 240})
                scene(0)
                capture("pet-returned")
                # Settings retain their own gestures; swiping there cannot leave the page.
                tap(48, 36)
                time.sleep(.6)
                call("drag", {"x1": 410, "y1": 105, "x2": 70, "y2": 105})
                capture("wifi-settings")
                tap(44, 38)
                tap(430, 36)
                time.sleep(.4)
                capture("volume-settings")
                tap(44, 38)
                tap(364, 36)
                scene(1)
                capture("usage-reentered")
                tap(390, 34)
                scene(0)
                call("quit")
            log = (run / "backend.log").read_text(errors="replace")
            assert "error=261" not in log and "last_error=261" not in log, log
            print("Native interaction checks complete", run, flush=True)
        finally:
            for process in (backend, sim):
                if process is not None:
                    if process.poll() is None:
                        process.terminate()
                    process.wait(timeout=5)
            (run / "actions.json").write_text(json.dumps(actions, ensure_ascii=False, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
