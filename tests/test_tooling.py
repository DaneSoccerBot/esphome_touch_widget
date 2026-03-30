import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
SCRIPTS = ROOT / "scripts"
if str(SCRIPTS) not in sys.path:
    sys.path.insert(0, str(SCRIPTS))

from tooling import (  # noqa: E402
    build_platformio_env,
    build_windows_msys2_env,
    detect_windows_msys2_root,
    ensure_windows_sdl2_config_wrapper,
    get_windows_sdl2_options,
    simulator_substitution_args,
)


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
        env = build_windows_msys2_env(
            {"PATH": "C:/Windows/System32"}, root, Path("C:/repo/.cache/tooling/bin")
        )

        self.assertIn("C:/repo/.cache/tooling/bin", env["PATH"])
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

    def test_ensure_windows_sdl2_config_wrapper_creates_cmd_wrapper(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir) / "msys64"
            bash_exe = root / "usr/bin/bash.exe"
            sdl2_config = root / "ucrt64/bin/sdl2-config"
            bash_exe.parent.mkdir(parents=True)
            sdl2_config.parent.mkdir(parents=True)
            bash_exe.write_text("")
            sdl2_config.write_text("")

            tooling_bin = Path(temp_dir) / "tooling/bin"
            wrapper = ensure_windows_sdl2_config_wrapper(root, tooling_bin)

            self.assertEqual(wrapper, tooling_bin / "sdl2-config.cmd")
            self.assertTrue(wrapper.exists())
            wrapper_text = wrapper.read_text()
            self.assertIn(str(bash_exe), wrapper_text)
            self.assertIn("/ucrt64/bin/sdl2-config", wrapper_text)
            self.assertIn("MSYSTEM=UCRT64", wrapper_text)
            self.assertIn("CHERE_INVOKING=1", wrapper_text)

    def test_ensure_windows_sdl2_config_wrapper_returns_none_without_inputs(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir) / "msys64"
            wrapper = ensure_windows_sdl2_config_wrapper(root, Path(temp_dir) / "bin")
            self.assertIsNone(wrapper)

    def test_get_windows_sdl2_options_returns_stdout_when_available(self):
        root = Path("C:/msys64")
        completed = mock.Mock(returncode=0, stdout="-IC:/msys64/ucrt64/include/SDL2 -LC:/msys64/ucrt64/lib -lSDL2\n")
        with mock.patch("tooling.os.name", "nt"):
            with mock.patch("tooling.subprocess.run", return_value=completed) as run_mock:
                with mock.patch.object(Path, "exists", return_value=True):
                    result = get_windows_sdl2_options(root)
        self.assertEqual(result, "-IC:/msys64/ucrt64/include/SDL2 -LC:/msys64/ucrt64/lib -lSDL2")
        run_mock.assert_called_once()

    def test_simulator_substitution_args_wraps_windows_sdl_options(self):
        with mock.patch("tooling.get_windows_sdl2_options", return_value="-Ifoo -Lbar -lSDL2"):
            args = simulator_substitution_args()
        self.assertEqual(args, ["-s", "simulator_sdl_options", "-Ifoo -Lbar -lSDL2"])


if __name__ == "__main__":
    unittest.main()
