"""Build the self-contained Windows x64 Agent Pet launcher."""

from __future__ import annotations

import argparse
import hashlib
from importlib.metadata import distributions
import os
from pathlib import Path
import re
import subprocess
import tempfile
from zipfile import ZIP_DEFLATED, ZipFile

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parents[1]
WORKSPACE = HERE.parents[3]
UTILS = WORKSPACE / "submodule/esp-mosaico-utils"
MOSAICO_TOOLS = UTILS / "mosaico-tools"
IRIS = UTILS / "ESP-Iris/components/esp_iris"
LOCK = IRIS / "tools/requirements.lock"
EMBED_SHA256 = "90905e64a840557d8c1bc4f5b74db3d7ee85e3ffb217611cac1c7813acea4159"
UTF8_STREAMS = (
    "import sys\n"
    "for stream in (sys.stdout, sys.stderr):\n"
    "    if stream is not None:\n"
    "        stream.reconfigure(encoding='utf-8')\n"
)


def sha256(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def package_runtime(archive: ZipFile, embedded_zip: Path, iris_environment: Path) -> None:
    if sha256(embedded_zip) != EMBED_SHA256:
        raise ValueError("Expected the verified Python 3.14.10 x64 embeddable ZIP.")
    with ZipFile(embedded_zip) as source:
        for item in source.namelist():
            if not item.endswith("._pth"):
                archive.writestr(f"runtime/{item}", source.read(item))
    archive.writestr("runtime/python314._pth", "python314.zip\n.\nLib/site-packages\nimport site\n")
    archive.writestr("runtime/sitecustomize.py", UTF8_STREAMS)

    fingerprint = f"python=3.14\nrequirements={sha256(LOCK)}\n"
    if (iris_environment / ".mosaico-requirements").read_text(encoding="utf-8") != fingerprint:
        raise ValueError("The specified Iris environment does not match the project's lock.")
    package_root = (iris_environment / "Lib/site-packages").resolve()
    if not package_root.is_dir():
        # The existing portable launcher seeds dependencies next to python.exe.
        package_root = (iris_environment / "Scripts/Lib/site-packages").resolve()
    if not package_root.is_dir():
        raise ValueError("The Iris environment has no dependency directory.")
    expected: dict[str, set[str]] = {}
    for line in LOCK.read_text(encoding="utf-8").splitlines():
        match = re.match(r"^([a-zA-Z0-9_.-]+)==([^\s;]+)", line)
        if match:
            name = re.sub(r"[-_.]+", "-", match[1]).lower()
            expected.setdefault(name, set()).add(match[2])
    found = set()
    written = set()
    for dist in distributions(path=[str(package_root)]):
        name = re.sub(r"[-_.]+", "-", dist.metadata["Name"]).lower()
        if name not in expected:
            continue
        if dist.version not in expected[name]:
            raise ValueError(f"Unexpected installed dependency {name} {dist.version}")
        found.add(name)
        for item in dist.files or ():
            file = Path(dist.locate_file(item)).resolve()
            if not file.is_relative_to(package_root) or not file.is_file():
                continue
            relative = file.relative_to(package_root)
            if "__pycache__" in relative.parts or file.suffix == ".pyc" or file.name == "direct_url.json":
                continue
            if relative not in written:
                archive.write(file, f"runtime/Lib/site-packages/{relative.as_posix()}")
                written.add(relative)
    # async-timeout is only needed on Python versions older than 3.11.
    if missing := expected.keys() - found - {"async-timeout"}:
        raise ValueError(f"Missing Iris dependencies {sorted(missing)}")


def package_workspace(archive: ZipFile) -> None:
    files = [
        WORKSPACE / "mosaico.py",
        WORKSPACE / ".mosaico.json",
        PROJECT / "CMakeLists.txt",
        UTILS / "LICENSES.md",
        UTILS / "esp-mosaico-vibe-mode/product_contract.json",
        IRIS / "idf_component.yml",
        *[PROJECT / "tools" / name for name in (
            "agent_pet.py", "desktop_activity.py", "pet_runner.py", "host_runtime.py", "usage_sync.py")],
        *sorted((PROJECT / "tools/usage_agent").glob("*.py")),
        *[IRIS / "tools" / name for name in ("esp_iris.py", "requirements.lock", "rpc_catalog.json",
                                            "system_update_bundle.py")],
    ]
    for folder in (
        MOSAICO_TOOLS / "tools/mosaico_cli",
        IRIS / "tools/iris_gateway",
        IRIS / "tools/iris_usb_flash",
        IRIS / "tools/frontend/dist",
    ):
        files.extend(
            path for path in folder.rglob("*")
            if path.is_file() and "__pycache__" not in path.parts and path.suffix != ".pyc"
        )
    for file in files:
        archive.write(file, f"workspace/{file.relative_to(WORKSPACE).as_posix()}")
    project_path = PROJECT.relative_to(WORKSPACE).as_posix()
    archive.write(HERE / "portable_agent.py", f"workspace/{project_path}/tools/portable_agent.py")


def compile_launcher(payload: Path, output: Path) -> None:
    windows = Path(os.environ["WINDIR"])
    csc = windows / "Microsoft.NET/Framework64/v4.0.30319/csc.exe"
    gac = windows / "Microsoft.NET/assembly/GAC_MSIL"
    compression = next((gac / "System.IO.Compression").glob("*/System.IO.Compression.dll"))
    icon = HERE / "PetIdle.ico"
    build_info = payload.with_name("BuildInfo.cs")
    build_info.write_text(
        "internal static class BuildInfo {\n"
        f'    internal const string Id = "{sha256(payload)[:16]}";\n'
        f'    internal const string ProjectName = "{PROJECT.name}";\n'
        "}\n",
        encoding="utf-8",
    )
    subprocess.run(
        [str(csc), "/nologo", "/target:winexe", "/platform:x64", "/optimize+",
         "/codepage:65001", "/r:System.Drawing.dll", "/r:System.Windows.Forms.dll",
         f"/r:{compression}", f"/resource:{payload},AgentPetPayload",
         f"/win32icon:{icon}", f"/resource:{icon},AgentPetIcon",
         f"/out:{output}", str(HERE / "Launcher.cs"), str(build_info)],
        check=True,
    )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--python-embed", type=Path, required=True)
    parser.add_argument("--iris-environment", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="agent-pet-build-", dir=output.parent) as scratch:
        payload = Path(scratch) / "payload.zip"
        with ZipFile(payload, "w", ZIP_DEFLATED, compresslevel=5) as archive:
            package_runtime(archive, args.python_embed, args.iris_environment)
            package_workspace(archive)
        compile_launcher(payload, output)
    print(f"{output} ({output.stat().st_size:,} bytes)")
    print(f"SHA-256 {sha256(output)}")


if __name__ == "__main__":
    main()
