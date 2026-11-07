"""The portable EXE must include both synchronization paths."""
import io
from pathlib import Path
import sys
import unittest
from zipfile import ZipFile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from windows import build


class WindowsPackageTests(unittest.TestCase):
    def test_usage_dependencies_are_present_in_the_self_contained_payload(self):
        stream = io.BytesIO()
        with ZipFile(stream, "w") as archive:
            build.package_workspace(archive)
        with ZipFile(stream) as archive:
            names = set(archive.namelist())
        prefix = "workspace/" + build.PROJECT.relative_to(build.WORKSPACE).as_posix() + "/tools/"
        expected = {"desktop_activity.py", "usage_sync.py", "usage_agent/__init__.py", "usage_agent/codex_rpc.py",
                    "usage_agent/codex_source.py", "usage_agent/snapshot.py",
                    "usage_agent/payload.py", "usage_agent/textfmt.py"}
        self.assertTrue({prefix + name for name in expected} <= names)
        self.assertFalse(any("claude_quota" in name or "usage_agent/tests" in name for name in names))


if __name__ == "__main__":
    unittest.main()
