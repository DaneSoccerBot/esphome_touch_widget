import os
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCRIPTS = ROOT / "scripts"
if str(SCRIPTS) not in sys.path:
    sys.path.insert(0, str(SCRIPTS))

from tooling import build_windows_msys2_env, detect_windows_msys2_root  # noqa: E402
from tooling import build_platformio_env  # noqa: E402


class ToolingTests(unittest.TestCase):
    def test_build_platformio_env_sets_repo_local_core_dir(self):
        env = build_platformio_env({})
        self.assertTrue(env["PLATFORMIO_CORE_DIR"].endswith(".cache/platformio"))

    def test_build_platformio_env_preserves_explicit_core_dir(self):
        env = build_platformio_env({"PLATFORMIO_CORE_DIR": "X:/custom-platformio"})
        self.assertEqual(env["PLATFORMIO_CORE_DIR"], "X:/custom-platformio")

    def test_detect_windows_msys2_root_uses_explicit_candidate(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            bash_exe = root / "usr/bin/bash.exe"
            bash_exe.parent.mkdir(parents=True)
            bash_exe.write_text("")
            detected = detect_windows_msys2_root([root])
            self.assertEqual(detected, root)

    def test_detect_windows_msys2_root_returns_none_when_missing(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            detected = detect_windows_msys2_root([Path(temp_dir)])
            self.assertIsNone(detected)

    def test_build_windows_msys2_env_injects_required_paths(self):
        root = Path("C:/msys64")
        env = build_windows_msys2_env({"PATH": "C:/Windows/System32"}, root)

        self.assertIn(str(root / "ucrt64/bin"), env["PATH"])
        self.assertIn(str(root / "usr/bin"), env["PATH"])
        self.assertEqual(env["MSYSTEM"], "UCRT64")
        self.assertIn(str(root / "ucrt64/lib/pkgconfig"), env["PKG_CONFIG_PATH"])
        self.assertEqual(
            env["PKG_CONFIG_SYSTEM_INCLUDE_PATH"], str(root / "ucrt64/include")
        )
        self.assertEqual(
            env["PKG_CONFIG_SYSTEM_LIBRARY_PATH"], str(root / "ucrt64/lib")
        )

    def test_build_windows_msys2_env_is_noop_without_root(self):
        base_env = {"PATH": os.environ.get("PATH", "")}
        env = build_windows_msys2_env(base_env, None)
        self.assertEqual(env, base_env)


if __name__ == "__main__":
    unittest.main()
