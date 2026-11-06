"""Prepare the bundled Iris runtime and start the Windows Agent Pet worker."""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile
import threading
import traceback

# The embeddable interpreter uses an isolated module search path.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from host_runtime import ROOT, RUNTIME, MosaicoClient  # noqa: E402


def prepare_iris() -> Path:
    tools = ROOT / "submodule/esp-mosaico-utils/mosaico-tools"
    sys.path.insert(0, str(tools / "tools"))
    from mosaico_cli.gateway import iris_environment_root
    from mosaico_cli.host import virtual_environment_python
    from mosaico_cli.workspace import load_workspace

    workspace = load_workspace(tools, explicit=str(ROOT))
    root = iris_environment_root(workspace.esp_iris_path)
    python = virtual_environment_python(root)
    lock = workspace.esp_iris_path / "components/esp_iris/tools/requirements.lock"
    version = f"{sys.version_info.major}.{sys.version_info.minor}"
    fingerprint = f"python={version}\nrequirements={hashlib.sha256(lock.read_bytes()).hexdigest()}\n"
    marker_name = ".mosaico-requirements"
    marker = root / marker_name
    if python.is_file() and marker.is_file() and marker.read_text(encoding="utf-8") == fingerprint:
        return python
    if root.exists():
        raise RuntimeError(f"Incomplete bundled Iris runtime at {root}")

    # Seed the exact private runtime expected by mosaico.py. Its normal venv
    # bootstrap stays available to development workspaces.
    root.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="agent-pet-", dir=root.parent) as temporary:
        staged = Path(temporary) / "runtime"
        shutil.copytree(Path(sys.executable).parent, staged / "Scripts",
                        ignore=shutil.ignore_patterns("__pycache__", "*.pyc"))
        (staged / marker_name).write_text(fingerprint, encoding="utf-8")
        staged.rename(root)
    return python


def smoke_test(python: Path) -> int:
    client = MosaicoClient(None)
    # list is discovery only. It does not claim a USB port or change the board.
    discovery = client.call("list", "--details", select=False)
    if discovery.get("ok") is not True:
        raise RuntimeError("Bundled Gateway discovery failed")
    report = {
        "ok": True,
        "python": sys.executable,
        "iris_python": str(python),
        "python_version": sys.version.split()[0],
        "discovery": discovery.get("status"),
        "visible_endpoints": len(discovery.get("endpoints", [])),
    }
    (ROOT / "portable-smoke.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    return 0


def stop_when_launcher_closes() -> None:
    try:
        os.read(sys.stdin.fileno(), 1)
    except OSError:
        pass
    (RUNTIME / "stop").touch()


def main() -> int:
    RUNTIME.mkdir(parents=True, exist_ok=True)
    startup_log = RUNTIME / "portable-startup.log"
    try:
        python = prepare_iris()
        if sys.argv[1:] == ["--self-test"]:
            return smoke_test(python)
        from agent_pet import STATES, snapshot
        from pet_runner import main as run_agent

        threading.Thread(target=stop_when_launcher_closes, daemon=True).start()
        sys.argv[1:] = ["--background", "--device-id", "auto"]
        return run_agent(snapshot, STATES, reset_stop=False)
    except Exception:
        startup_log.write_text(traceback.format_exc(), encoding="utf-8")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
