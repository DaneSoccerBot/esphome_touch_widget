import os
import subprocess
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
    check_host_simulator_support,
    detect_windows_msys2_root,
    ensure_windows_sdl2_config_wrapper,
    get_windows_sdl2_options,
    simulator_substitution_args,
)
import dev  # noqa: E402


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
        compat_dir = (ROOT / "compat" / "win32_posix").as_posix()
        expected = f"-I{compat_dir} -lws2_32 -IC:/msys64/ucrt64/include/SDL2 -LC:/msys64/ucrt64/lib -lSDL2"
        self.assertEqual(result, expected)
        run_mock.assert_called_once()

    def test_simulator_substitution_args_wraps_windows_sdl_options(self):
        with mock.patch("tooling.get_windows_sdl2_options", return_value="-Ifoo -Lbar -lSDL2"):
            args = simulator_substitution_args()
        self.assertEqual(args, ["-s", "simulator_sdl_options", "-Ifoo -Lbar -lSDL2"])

    def test_check_host_simulator_support_uses_windows_sdl_options(self):
        with mock.patch("tooling.os.name", "nt"):
            with mock.patch("tooling.get_windows_sdl2_options", return_value="-Ifoo -Lbar -lSDL2"):
                ready, message = check_host_simulator_support()
        self.assertTrue(ready)
        self.assertEqual(message, "SDL2 development files detected via MSYS2.")

    def test_check_host_simulator_support_reports_missing_windows_sdl_options(self):
        with mock.patch("tooling.os.name", "nt"):
            with mock.patch("tooling.platform.system", return_value="Windows"):
                with mock.patch("tooling.get_windows_sdl2_options", return_value=None):
                    ready, message = check_host_simulator_support()
        self.assertFalse(ready)
        self.assertIn("bootstrap.ps1", message)

    def test_snapshot_name_uses_config_stem(self):
        self.assertEqual(
            dev._snapshot_name("examples/simulator_all_tiles.yaml"),
            "simulator_all_tiles.ppm",
        )

    def test_selected_simulator_configs_can_select_one_or_all(self):
        self.assertEqual(dev._selected_simulator_configs("one.yaml"), ("one.yaml",))
        self.assertEqual(dev._selected_simulator_configs(None), dev.SIMULATOR_CONFIGS)

    def test_capture_one_runs_program_with_snapshot_environment(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            temp = Path(temp_dir)
            build_dir = temp / "build/.pioenvs/demo"
            build_dir.mkdir(parents=True)
            program = build_dir / ("program.exe" if os.name == "nt" else "program")
            program.write_text("")
            output_dir = temp / "snapshots"

            calls = []

            def fake_subprocess_run(cmd, cwd, env, text, stdout=None, stderr=None, timeout=None, check=False):
                calls.append(
                    {
                        "cmd": cmd,
                        "cwd": cwd,
                        "env": env,
                        "text": text,
                        "stdout": stdout,
                        "stderr": stderr,
                        "timeout": timeout,
                        "check": check,
                    }
                )
                Path(env["TILE_DASHBOARD_SDL_SNAPSHOT"]).write_bytes(b"P6\n1 1\n255\n\0\0\0")
                return mock.Mock(returncode=0)

            with mock.patch.dict(dev.SIMULATOR_BUILD_DIRS, {"examples/demo.yaml": build_dir}, clear=True):
                with mock.patch("dev.run") as run_mock:
                    with mock.patch("dev.stage_windows_runtime_dlls") as stage_mock:
                        with mock.patch("dev.subprocess.run", side_effect=fake_subprocess_run):
                            snapshot = dev._capture_one(
                                "examples/demo.yaml",
                                output_dir,
                                after_ms=234,
                                timeout=7,
                            )

            self.assertEqual(snapshot, output_dir / "demo.ppm")
            self.assertTrue(snapshot.exists())
            run_mock.assert_called_once()
            stage_mock.assert_called_once_with(build_dir)
            self.assertEqual(calls[0]["cwd"], build_dir)
            self.assertEqual(calls[0]["timeout"], 7)
            self.assertEqual(calls[0]["stdout"], subprocess.PIPE)
            self.assertEqual(calls[0]["stderr"], subprocess.STDOUT)
            self.assertEqual(calls[0]["env"]["TILE_DASHBOARD_SDL_CAPTURE_AFTER_MS"], "234")
            self.assertEqual(calls[0]["env"]["TILE_DASHBOARD_SDL_EXIT_AFTER_SNAPSHOT"], "1")
            self.assertEqual(calls[0]["env"]["TILE_DASHBOARD_SIM_FREEZE_TICK"], "1")
            self.assertTrue(calls[0]["env"]["HOME"].endswith(".cache/host-home"))

    def test_capture_one_can_skip_compile_for_fast_cycles(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            temp = Path(temp_dir)
            build_dir = temp / "build/.pioenvs/demo"
            build_dir.mkdir(parents=True)
            program = build_dir / ("program.exe" if os.name == "nt" else "program")
            program.write_text("")
            output_dir = temp / "snapshots"

            def fake_subprocess_run(cmd, cwd, env, text, stdout=None, stderr=None, timeout=None, check=False):
                Path(env["TILE_DASHBOARD_SDL_SNAPSHOT"]).write_bytes(b"P6\n1 1\n255\n\0\0\0")
                return mock.Mock(returncode=0)

            with mock.patch.dict(dev.SIMULATOR_BUILD_DIRS, {"examples/demo.yaml": build_dir}, clear=True):
                with mock.patch("dev.run") as run_mock:
                    with mock.patch("dev.stage_windows_runtime_dlls"):
                        with mock.patch("dev.subprocess.run", side_effect=fake_subprocess_run):
                            dev._capture_one(
                                "examples/demo.yaml",
                                output_dir,
                                after_ms=234,
                                timeout=7,
                                compile_first=False,
                            )

            run_mock.assert_not_called()

    def test_resolve_serial_port_uses_explicit_or_env_port(self):
        self.assertEqual(dev._resolve_serial_port("/dev/cu.manual"), "/dev/cu.manual")
        with mock.patch.dict(os.environ, {"ESP32_PORT": "/dev/cu.env"}, clear=False):
            self.assertEqual(dev._resolve_serial_port(None), "/dev/cu.env")

    def test_resolve_serial_port_auto_selects_single_preferred_port(self):
        with mock.patch.dict(os.environ, {}, clear=True):
            with mock.patch("dev._available_serial_ports", return_value=["/dev/cu.usbserial-1", "/dev/tty.Bluetooth"]):
                self.assertEqual(dev._resolve_serial_port(None), "/dev/cu.usbserial-1")

    def test_resolve_serial_port_requires_disambiguation_for_equal_candidates(self):
        with mock.patch.dict(os.environ, {}, clear=True):
            with mock.patch("dev._available_serial_ports", return_value=["/dev/cu.usbserial-1", "/dev/cu.usbserial-2"]):
                with self.assertRaises(SystemExit):
                    dev._resolve_serial_port(None)

    def test_resolve_serial_port_does_not_auto_select_bluetooth_ports(self):
        with mock.patch.dict(os.environ, {}, clear=True):
            with mock.patch("dev._available_serial_ports", return_value=["/dev/cu.Bluetooth-Incoming-Port"]):
                with self.assertRaises(SystemExit):
                    dev._resolve_serial_port(None)

    def test_esp32_scene_files_use_stable_names(self):
        files = dev._esp32_scene_files(Path("/tmp/out"), "fixture")
        self.assertEqual(
            [path.name for path in files],
            ["fixture_grid.png", "fixture_fullscreen.png", "fixture_grid_after.png"],
        )

    def test_esp32_image_diagnostics_marks_bottom_stripe(self):
        from PIL import Image

        with tempfile.TemporaryDirectory() as temp_dir:
            image_path = Path(temp_dir) / "capture.png"
            img = Image.new("RGB", (10, 10), (0, 0, 0))
            for x in range(10):
                img.putpixel((x, 8), (255, 255, 255))
            img.save(image_path)

            crop_path, annotated_path, ranked = dev._write_artifact_diagnostics(image_path, 30.0, 3)

            self.assertEqual(ranked[0][0], 8)
            self.assertTrue(crop_path.exists())
            self.assertTrue(annotated_path.exists())

    def test_flash_esp32_compiles_then_uploads_without_monitor(self):
        args = mock.Mock(
            config=dev.ESP32_USB_TEST_CONFIG,
            port=None,
            skip_compile=False,
            upload_speed=921600,
        )
        with mock.patch("dev.require_venv"):
            with mock.patch("dev._resolve_serial_port", return_value="/dev/cu.usbserial-1"):
                with mock.patch("dev.run") as run_mock:
                    dev.cmd_flash_esp32(args)

        self.assertEqual(run_mock.call_count, 2)
        self.assertEqual(run_mock.call_args_list[0].args[0][-2:], ["compile", dev.ESP32_USB_TEST_CONFIG])
        upload_cmd = run_mock.call_args_list[1].args[0]
        self.assertIn("upload", [str(part) for part in upload_cmd])
        self.assertIn("--device", upload_cmd)
        self.assertIn("/dev/cu.usbserial-1", upload_cmd)
        self.assertIn("--upload_speed", upload_cmd)

    def test_panel_output_defaults_to_prefix_in_panel_snapshot_dir(self):
        output = dev._resolve_panel_output(None, "screenshots/panel", "glass")
        self.assertEqual(output, dev.ROOT / "screenshots/panel/glass.png")

    def test_panel_bridge_payload_resolves_raw_output(self):
        args = mock.Mock(
            camera=1,
            warmup_s=1.5,
            frames=12,
            rotate=90,
            request_size="1920x1080",
            crop=None,
            quad="1,2,3,4,5,6,7,8",
            output_size="480x480",
            raw_output="screenshots/panel/raw.png",
        )
        payload = dev._panel_bridge_payload(args, dev.ROOT / "screenshots/panel/current.png")

        self.assertEqual(payload["camera"], 1)
        self.assertEqual(payload["rotate"], 90)
        self.assertEqual(payload["request_size"], "1920x1080")
        self.assertEqual(payload["quad"], "1,2,3,4,5,6,7,8")
        self.assertEqual(payload["output_size"], "480x480")
        self.assertEqual(payload["raw_output"], str(dev.ROOT / "screenshots/panel/raw.png"))


if __name__ == "__main__":
    unittest.main()
