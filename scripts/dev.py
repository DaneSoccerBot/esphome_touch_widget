#!/usr/bin/env python3
from __future__ import annotations

import argparse
import filecmp
import json
import os
import re
import subprocess
import sys
import tempfile
import time
import uuid
import venv
from pathlib import Path

from tooling import (
    PLATFORMIO_CORE_DIR,
    ROOT,
    check_host_simulator_support,
    detect_windows_msys2_root,
    require_venv,
    run,
    simulator_substitution_args,
    stage_windows_runtime_dlls,
    tooling_env,
    venv_esphome,
    venv_python,
)


REQUIREMENTS_FILE = ROOT / "requirements-dev.txt"
BUILD_SETTINGS_FILE = ROOT / "build_settings.yaml"
PINNED_ESPHOME_VERSION = "2026.3.0"


SIMULATOR_CONFIGS = (
    "examples/simulator_ha_2x2.yaml",
    "examples/simulator_showcase_3x3.yaml",
    "examples/simulator_all_tiles.yaml",
)

# Map each simulator config to its PlatformIO build output directory.
SIMULATOR_BUILD_DIRS = {
    "examples/simulator_ha_2x2.yaml": ROOT / "examples/.esphome/build/dashboard-simulator/.pioenvs/dashboard-simulator",
    "examples/simulator_showcase_3x3.yaml": ROOT / "examples/.esphome/build/dashboard-3x3-showcase/.pioenvs/dashboard-3x3-showcase",
    "examples/simulator_all_tiles.yaml": ROOT / "examples/.esphome/build/dashboard-all-tiles/.pioenvs/dashboard-all-tiles",
}

SIMULATOR_SNAPSHOT_DIR = ROOT / "screenshots/simulator"
SIMULATOR_GOLDEN_DIR = ROOT / "tests/golden/simulator"
ESP32_USB_TEST_CONFIG = "dev-local/esp32_usb_test.yaml"
ESP32_SNAPSHOT_DIR = ROOT / "screenshots/esp32"
ESP32_GOLDEN_DIR = ROOT / "tests/golden/esp32"
ESP32_DEFAULT_BAUD = 115200
ESP32_SERIAL_ENV = "ESP32_PORT"
ESP32_SCENE_SHOT_NAMES = ("grid", "fullscreen", "grid_after")
SCREENSHOT_TOOL = ROOT / "dev-local/screenshot.py"
ESP32_DEFAULT_IMAGE = ESP32_SNAPSHOT_DIR / "current.png"
ESP32_BRIDGE_SCRIPT = ROOT / "dev-local/esp32_bridge.py"
ESP32_BRIDGE_DIR = ROOT / ".cache/esp32_bridge"
ESP32_BRIDGE_PENDING_DIR = ESP32_BRIDGE_DIR / "pending"
ESP32_BRIDGE_RESULT_DIR = ESP32_BRIDGE_DIR / "results"
PANEL_CAPTURE_TOOL = ROOT / "dev-local/panel_capture.py"
PANEL_SNAPSHOT_DIR = ROOT / "screenshots/panel"
PANEL_DEFAULT_IMAGE = PANEL_SNAPSHOT_DIR / "current.png"


def _require_local_tool(path: Path, purpose: str) -> None:
    if path.exists():
        return
    try:
        display_path = path.relative_to(ROOT)
    except ValueError:
        display_path = path
    raise SystemExit(
        f"{purpose} is local-only and not present in this checkout: {display_path}\n"
        "Local hardware capture helpers live under dev-local/ and are intentionally not versioned."
    )


def esphome_command(subcommand: str, config_path: str) -> list[str]:
    args = [venv_esphome()]
    if config_path in SIMULATOR_CONFIGS:
        args.extend(simulator_substitution_args())
    args.extend([subcommand, config_path])
    return args


def create_venv() -> None:
    if not venv_python().exists():
        print(f"+ create virtualenv at {ROOT / '.venv'}")
        builder = venv.EnvBuilder(with_pip=True)
        builder.create(ROOT / ".venv")


def _venv_package_version(package: str) -> str | None:
    if not venv_python().exists():
        return None
    result = subprocess.run(
        [
            str(venv_python()),
            "-c",
            "import importlib.metadata as m, sys\n"
            "try:\n"
            "    print(m.version(sys.argv[1]))\n"
            "except m.PackageNotFoundError:\n"
            "    raise SystemExit(1)\n",
            package,
        ],
        cwd=ROOT,
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        return None
    return result.stdout.strip()


def install_python_dependencies() -> None:
    required = {
        "esphome": PINNED_ESPHOME_VERSION,
        "Pillow": None,
    }
    missing_or_stale = []
    for package, expected in required.items():
        installed_version = _venv_package_version(package)
        if installed_version is None:
            missing_or_stale.append(package)
            continue
        if expected is not None and installed_version != expected:
            missing_or_stale.append(package)

    if not missing_or_stale:
        print(f"ESPHome {PINNED_ESPHOME_VERSION} and dev tooling deps already installed in local virtualenv.")
        return

    env = dict(os.environ)
    env.setdefault("PIP_DISABLE_PIP_VERSION_CHECK", "1")
    run([venv_python(), "-m", "pip", "install", "-r", REQUIREMENTS_FILE], env=env)


def cmd_setup(_: argparse.Namespace) -> None:
    create_venv()
    install_python_dependencies()
    cmd_doctor(argparse.Namespace())


def cmd_doctor(_: argparse.Namespace) -> None:
    create_venv()
    if not venv_esphome().exists():
        print("ESPHome is not installed yet. Run `python3 scripts/dev.py setup` first.")
        raise SystemExit(1)

    version = subprocess.run(
        [str(venv_esphome()), "version"],
        cwd=ROOT,
        capture_output=True,
        text=True,
        check=False,
    )
    print(f"Repository root: {ROOT}")
    print(f"Virtualenv: {venv_python()}")
    print(f"PlatformIO core dir: {PLATFORMIO_CORE_DIR}")
    if os.name == "nt":
        msys2_root = detect_windows_msys2_root()
        print(f"MSYS2 root: {msys2_root if msys2_root else 'not detected'}")
    if version.returncode == 0:
        print(version.stdout.strip())
    else:
        print("Unable to read ESPHome version from local virtualenv.")

    sim_ok, sim_message = check_host_simulator_support()
    print(f"Host simulator: {'ready' if sim_ok else 'needs system deps'}")
    print(sim_message)


def cmd_unit(_: argparse.Namespace) -> None:
    require_venv()
    run([venv_python(), "-m", "unittest", "discover", "-s", "tests", "-v"])


def cmd_config(_: argparse.Namespace) -> None:
    require_venv()
    for config_path in (
        *SIMULATOR_CONFIGS,
        "examples/esp32_showcase.yaml",
        "examples/display48norelay.refactored.yaml",
        "examples/display48norelay.package_import.yaml",
    ):
        run(esphome_command("config", config_path))


def cmd_compile_sim(_: argparse.Namespace) -> None:
    require_venv()
    for config_path in SIMULATOR_CONFIGS:
        run(esphome_command("compile", config_path))


def cmd_compile_esp32(_: argparse.Namespace) -> None:
    require_venv()
    for config_path in (
        "examples/esp32_showcase.yaml",
        "examples/display48norelay.refactored.yaml",
        "examples/display48norelay.package_import.yaml",
    ):
        run([venv_esphome(), "compile", config_path])


def _run_simulator(config_path: str) -> None:
    require_venv()
    # Compile first so the build directory exists before we stage DLLs.
    # ESPHome's 'run' would compile again, but the build is already cached.
    run(esphome_command("compile", config_path))
    build_dir = SIMULATOR_BUILD_DIRS.get(config_path)
    if build_dir:
        stage_windows_runtime_dlls(build_dir)
    run(esphome_command("run", config_path), ignore_exit_code=True)


def _simulator_program(config_path: str) -> Path:
    build_dir = SIMULATOR_BUILD_DIRS[config_path]
    name = "program.exe" if os.name == "nt" else "program"
    return build_dir / name


def _snapshot_name(config_path: str) -> str:
    return Path(config_path).stem + ".ppm"


def _capture_one(
    config_path: str,
    output_dir: Path,
    after_ms: int,
    timeout: int,
    verbose: bool = False,
    compile_first: bool = True,
) -> Path:
    if compile_first:
        run(esphome_command("compile", config_path))
    build_dir = SIMULATOR_BUILD_DIRS[config_path]
    stage_windows_runtime_dlls(build_dir)

    program = _simulator_program(config_path)
    if not program.exists():
        raise SystemExit(f"Simulator binary not found: {program}")

    output_dir.mkdir(parents=True, exist_ok=True)
    snapshot = output_dir / _snapshot_name(config_path)
    host_home = ROOT / ".cache/host-home"
    host_home.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ)
    env["HOME"] = str(host_home)
    env["TILE_DASHBOARD_SDL_SNAPSHOT"] = str(snapshot)
    env["TILE_DASHBOARD_SDL_CAPTURE_AFTER_MS"] = str(after_ms)
    env["TILE_DASHBOARD_SDL_EXIT_AFTER_SNAPSHOT"] = "1"
    env["TILE_DASHBOARD_SIM_FREEZE_TICK"] = "1"
    env.setdefault("SDL_VIDEODRIVER", "dummy")
    if os.name == "nt":
        env["USERPROFILE"] = str(host_home)
        env["PATH"] = f"{build_dir}{os.pathsep}{env.get('PATH', '')}"

    print(f"+ capture {config_path} -> {snapshot.relative_to(ROOT) if snapshot.is_relative_to(ROOT) else snapshot}")
    completed = subprocess.run(
        [str(program)],
        cwd=build_dir,
        env=env,
        text=True,
        stdout=None if verbose else subprocess.PIPE,
        stderr=None if verbose else subprocess.STDOUT,
        timeout=timeout,
        check=False,
    )
    if completed.returncode != 0 and env.get("SDL_VIDEODRIVER") == "dummy":
        print("  dummy SDL video driver failed; retrying with default video driver")
        env.pop("SDL_VIDEODRIVER", None)
        completed = subprocess.run(
            [str(program)],
            cwd=build_dir,
            env=env,
            text=True,
            stdout=None if verbose else subprocess.PIPE,
            stderr=None if verbose else subprocess.STDOUT,
            timeout=timeout,
            check=False,
        )
    if completed.returncode != 0:
        if not verbose and completed.stdout:
            print(completed.stdout)
        raise SystemExit(f"Simulator capture failed for {config_path} with exit code {completed.returncode}")
    if not snapshot.exists():
        raise SystemExit(f"Simulator did not write snapshot: {snapshot}")
    return snapshot


def _selected_simulator_configs(config: str | None) -> tuple[str, ...]:
    if config:
        return (config,)
    return SIMULATOR_CONFIGS


def cmd_capture_sim(args: argparse.Namespace) -> None:
    require_venv()
    output_dir = Path(args.output_dir)
    if not output_dir.is_absolute():
        output_dir = ROOT / output_dir
    for config_path in _selected_simulator_configs(args.config):
        _capture_one(
            config_path,
            output_dir,
            args.after_ms,
            args.timeout,
            args.verbose,
            compile_first=not args.skip_compile,
        )


def cmd_verify_sim(args: argparse.Namespace) -> None:
    require_venv()
    if args.update:
        SIMULATOR_GOLDEN_DIR.mkdir(parents=True, exist_ok=True)
        for config_path in _selected_simulator_configs(args.config):
            _capture_one(
                config_path,
                SIMULATOR_GOLDEN_DIR,
                args.after_ms,
                args.timeout,
                args.verbose,
                compile_first=not args.skip_compile,
            )
        print(f"Updated simulator golden snapshots in {SIMULATOR_GOLDEN_DIR.relative_to(ROOT)}")
        return

    with tempfile.TemporaryDirectory(prefix="tile-dashboard-sim-") as tmp:
        tmp_dir = Path(tmp)
        failures = []
        for config_path in _selected_simulator_configs(args.config):
            current = _capture_one(
                config_path,
                tmp_dir,
                args.after_ms,
                args.timeout,
                args.verbose,
                compile_first=not args.skip_compile,
            )
            golden = SIMULATOR_GOLDEN_DIR / _snapshot_name(config_path)
            if not golden.exists():
                failures.append(f"missing golden: {golden.relative_to(ROOT)}")
                continue
            if not filecmp.cmp(current, golden, shallow=False):
                failures.append(f"snapshot differs: {_snapshot_name(config_path)}")
        if failures:
            for failure in failures:
                print(f"  {failure}")
            raise SystemExit("Simulator visual verification failed. Re-run with `verify-sim --update` if the change is intentional.")
    print("Simulator visual verification passed.")


def _serial_port_score(port: str) -> int:
    lower = port.lower()
    if "usbserial" in lower:
        return 0
    if "usbmodem" in lower:
        return 1
    if "wchusbserial" in lower or "slab_usbtouart" in lower:
        return 2
    if "ttyusb" in lower or "ttyacm" in lower:
        return 3
    return 10


def _available_serial_ports() -> list[str]:
    if not SCREENSHOT_TOOL.exists():
        return []
    completed = subprocess.run(
        [str(venv_python()), str(SCREENSHOT_TOOL), "--list-ports"],
        cwd=ROOT,
        env=tooling_env(),
        capture_output=True,
        text=True,
        check=False,
    )
    if completed.returncode != 0:
        message = completed.stderr.strip() or completed.stdout.strip()
        if message:
            print(message)
        return []
    ports = [line.strip() for line in completed.stdout.splitlines() if line.strip()]
    return sorted(ports, key=lambda port: (_serial_port_score(port), port))


def _resolve_serial_port(port: str | None) -> str:
    if port:
        return port

    env_port = os.environ.get(ESP32_SERIAL_ENV)
    if env_port:
        return env_port

    ports = _available_serial_ports()
    if not ports:
        raise SystemExit(
            "No serial port found. Connect the ESP32 or pass `--port /dev/cu.usbserial-...`."
        )
    preferred_ports = [port for port in ports if _serial_port_score(port) < 10]
    if not preferred_ports:
        formatted = "\n".join(f"  {port}" for port in ports)
        raise SystemExit(
            "No ESP32-like USB serial port found. Pass `--port` explicitly if this is intentional:\n"
            f"{formatted}"
        )
    if len(preferred_ports) == 1:
        return preferred_ports[0]

    best_score = _serial_port_score(preferred_ports[0])
    best = [port for port in preferred_ports if _serial_port_score(port) == best_score]
    if len(best) == 1:
        print(f"Auto-selected serial port: {best[0]}")
        return best[0]

    formatted = "\n".join(f"  {port}" for port in preferred_ports)
    raise SystemExit(f"Multiple serial ports found. Pass `--port` explicitly:\n{formatted}")


def _esp32_prefix(config_path: str) -> str:
    return Path(config_path).stem


def _esp32_scene_files(directory: Path, prefix: str) -> list[Path]:
    return [directory / f"{prefix}_{name}.png" for name in ESP32_SCENE_SHOT_NAMES]


def _capture_esp32_scene(
    port: str,
    baud: int,
    output_dir: Path,
    prefix: str,
    timeout: float,
) -> list[Path]:
    _require_local_tool(SCREENSHOT_TOOL, "ESP32 framebuffer capture")
    output_dir.mkdir(parents=True, exist_ok=True)
    run(
        [
            venv_python(),
            SCREENSHOT_TOOL,
            "--port",
            port,
            "--baud",
            str(baud),
            "--watch-dir",
            str(output_dir),
            "--scene-prefix",
            prefix,
            "--timeout",
            str(timeout),
            "--scene-run",
        ]
    )
    return [path for path in _esp32_scene_files(output_dir, prefix) if path.exists()]


def _capture_esp32_single(
    port: str,
    baud: int,
    output: Path,
    timeout: float,
) -> None:
    _require_local_tool(SCREENSHOT_TOOL, "ESP32 framebuffer capture")
    output.parent.mkdir(parents=True, exist_ok=True)
    run(
        [
            venv_python(),
            SCREENSHOT_TOOL,
            "--port",
            port,
            "--baud",
            str(baud),
            "--output",
            str(output),
            "--timeout",
            str(timeout),
        ]
    )


def _write_json_atomic(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(json.dumps(payload, indent=2, sort_keys=True), encoding="utf-8")
    tmp.replace(path)


def _submit_bridge_job(action: str, args: dict, wait_timeout: float) -> dict:
    require_venv()
    job_id = f"{int(time.time())}-{uuid.uuid4().hex[:10]}"
    job = {
        "id": job_id,
        "action": action,
        "args": args,
        "created_at": time.time(),
    }
    result_path = ESP32_BRIDGE_RESULT_DIR / f"{job_id}.json"
    _write_json_atomic(ESP32_BRIDGE_PENDING_DIR / f"{job_id}.json", job)
    print(f"Queued ESP32 bridge job {job_id}: {action}")

    deadline = time.time() + wait_timeout
    while time.time() < deadline:
        if result_path.exists():
            result = json.loads(result_path.read_text(encoding="utf-8"))
            output = result.get("output") or ""
            if output:
                print(output, end="" if output.endswith("\n") else "\n")
            if result.get("error"):
                print(result["error"])
            if not result.get("success"):
                log = result.get("log")
                raise SystemExit(f"ESP32 bridge job failed. Log: {log}")
            return result
        time.sleep(0.25)

    raise SystemExit(
        f"ESP32 bridge job timed out after {wait_timeout:.0f}s.\n"
        "Make sure the bridge is running in a normal Terminal:\n"
        "  python3 scripts/dev.py esp32-bridge"
    )


def cmd_esp32_bridge(_: argparse.Namespace) -> None:
    require_venv()
    _require_local_tool(ESP32_BRIDGE_SCRIPT, "ESP32 bridge")
    run([venv_python(), ESP32_BRIDGE_SCRIPT, "run"])


def cmd_esp32_bridge_ports(args: argparse.Namespace) -> None:
    _submit_bridge_job("ports", {}, args.wait_timeout)


def cmd_flash_esp32_bridge(args: argparse.Namespace) -> None:
    port = _resolve_serial_port(args.port)
    payload = {
        "port": port,
        "config": args.config,
        "skip_compile": args.skip_compile,
    }
    if args.upload_speed:
        payload["upload_speed"] = args.upload_speed
    _submit_bridge_job("flash", payload, args.wait_timeout)


def cmd_capture_esp32_bridge(args: argparse.Namespace) -> None:
    port = _resolve_serial_port(args.port)
    output_dir = Path(args.output_dir)
    if not output_dir.is_absolute():
        output_dir = ROOT / output_dir
    output_dir.mkdir(parents=True, exist_ok=True)
    prefix = args.prefix or "current"
    if args.scene:
        _submit_bridge_job(
            "scene_capture",
            {
                "port": port,
                "baud": args.baud,
                "output_dir": str(output_dir),
                "prefix": prefix,
                "timeout": args.timeout,
                "settle_s": args.settle_s,
            },
            args.wait_timeout,
        )
        return

    _submit_bridge_job(
        "capture",
        {
            "port": port,
            "baud": args.baud,
            "output": str(output_dir / f"{prefix}.png"),
            "timeout": args.timeout,
            "settle_s": args.settle_s,
        },
        args.wait_timeout,
    )


def cmd_esp32_info_bridge(args: argparse.Namespace) -> None:
    port = _resolve_serial_port(args.port)
    _submit_bridge_job(
        "info",
        {"port": port, "baud": args.baud, "timeout": args.timeout, "settle_s": args.settle_s},
        args.wait_timeout,
    )


def cmd_esp32_bench_bridge(args: argparse.Namespace) -> None:
    port = _resolve_serial_port(args.port)
    _submit_bridge_job(
        "bench",
        {"port": port, "baud": args.baud, "timeout": args.timeout, "settle_s": args.settle_s},
        args.wait_timeout,
    )


def _resolve_panel_output(output: str | None, output_dir: str, prefix: str) -> Path:
    if output:
        path = Path(output)
    else:
        path = Path(output_dir) / f"{prefix}.png"
    if not path.is_absolute():
        path = ROOT / path
    return path


def _panel_capture_command(args: argparse.Namespace, output: Path) -> list:
    cmd: list = [
        venv_python(),
        PANEL_CAPTURE_TOOL,
        "--camera",
        str(args.camera),
        "--output",
        str(output),
        "--warmup-s",
        str(args.warmup_s),
        "--frames",
        str(args.frames),
        "--rotate",
        str(args.rotate),
    ]
    optional_args = (
        ("--request-size", args.request_size),
        ("--crop", args.crop),
        ("--quad", args.quad),
        ("--output-size", args.output_size),
    )
    for flag, value in optional_args:
        if value:
            cmd.extend([flag, str(value)])
    if args.raw_output:
        raw_output = Path(args.raw_output)
        if not raw_output.is_absolute():
            raw_output = ROOT / raw_output
        cmd.extend(["--raw-output", str(raw_output)])
    return cmd


def cmd_panel_cameras(args: argparse.Namespace) -> None:
    _require_local_tool(PANEL_CAPTURE_TOOL, "Panel camera capture")
    require_venv()
    cmd = [venv_python(), PANEL_CAPTURE_TOOL, "--list-cameras", "--max-index", str(args.max_index)]
    if args.no_probe:
        cmd.append("--no-probe")
    run(cmd)


def cmd_capture_panel(args: argparse.Namespace) -> None:
    _require_local_tool(PANEL_CAPTURE_TOOL, "Panel camera capture")
    require_venv()
    output = _resolve_panel_output(args.output, args.output_dir, args.prefix)
    output.parent.mkdir(parents=True, exist_ok=True)
    run(_panel_capture_command(args, output))


def _panel_bridge_payload(args: argparse.Namespace, output: Path | None = None) -> dict:
    payload = {
        "camera": args.camera,
        "warmup_s": args.warmup_s,
        "frames": args.frames,
        "rotate": args.rotate,
    }
    if output is not None:
        payload["output"] = str(output)
    for key in ("request_size", "crop", "quad", "output_size", "raw_output"):
        value = getattr(args, key, None)
        if value:
            if key == "raw_output":
                raw_output = Path(value)
                if not raw_output.is_absolute():
                    raw_output = ROOT / raw_output
                value = str(raw_output)
            payload[key] = value
    return payload


def cmd_panel_cameras_bridge(args: argparse.Namespace) -> None:
    _submit_bridge_job(
        "panel_cameras",
        {"max_index": args.max_index, "no_probe": args.no_probe},
        args.wait_timeout,
    )


def cmd_capture_panel_bridge(args: argparse.Namespace) -> None:
    output = _resolve_panel_output(args.output, args.output_dir, args.prefix)
    output.parent.mkdir(parents=True, exist_ok=True)
    _submit_bridge_job("panel_capture", _panel_bridge_payload(args, output), args.wait_timeout)


def cmd_git_commit_bridge(args: argparse.Namespace) -> None:
    _submit_bridge_job(
        "git_commit",
        {
            "message": args.message,
            "exclude": [
                "screenshots",
                "tests/golden",
                "dev-local",
            ],
        },
        args.wait_timeout,
    )


def _pil_modules():
    try:
        from PIL import Image, ImageDraw
    except ModuleNotFoundError as err:
        raise SystemExit("Pillow missing. Run `python3 scripts/dev.py setup` first.") from err
    return Image, ImageDraw


def _row_artifact_scores(image_path: Path, bottom_percent: float) -> tuple[int, int, list[tuple[int, float]]]:
    Image, _ = _pil_modules()
    img = Image.open(image_path).convert("RGB")
    width, height = img.size
    start_y = max(1, min(height - 2, int(height * (1.0 - bottom_percent / 100.0))))
    pix = img.load()
    scores: list[tuple[int, float]] = []

    # A thin scanline artifact has high contrast against the local vertical neighborhood.
    for y in range(start_y, height - 1):
        total = 0
        for x in range(width):
            r, g, b = pix[x, y]
            pr, pg, pb = pix[x, y - 1]
            nr, ng, nb = pix[x, y + 1]
            total += abs(r - ((pr + nr) // 2))
            total += abs(g - ((pg + ng) // 2))
            total += abs(b - ((pb + nb) // 2))
        scores.append((y, total / (width * 3)))
    return start_y, height, scores


def _write_artifact_diagnostics(image_path: Path, bottom_percent: float, top_k: int) -> tuple[Path, Path, list[tuple[int, float]]]:
    Image, ImageDraw = _pil_modules()
    img = Image.open(image_path).convert("RGB")
    width, height = img.size
    start_y, _, scores = _row_artifact_scores(image_path, bottom_percent)
    ranked = sorted(scores, key=lambda item: item[1], reverse=True)[:top_k]

    stem = image_path.with_suffix("")
    crop_path = stem.with_name(f"{stem.name}_bottom_{int(bottom_percent)}pct.png").with_suffix(".png")
    annotated_path = stem.with_name(f"{stem.name}_diagnostic.png").with_suffix(".png")

    img.crop((0, start_y, width, height)).save(crop_path)

    annotated = img.copy()
    draw = ImageDraw.Draw(annotated)
    for y, _score in ranked:
        draw.line((0, y, width - 1, y), fill=(255, 0, 0))
    draw.rectangle((0, start_y, width - 1, height - 1), outline=(255, 200, 0), width=2)
    annotated.save(annotated_path)
    return crop_path, annotated_path, ranked


def cmd_analyze_esp32_image(args: argparse.Namespace) -> None:
    image_path = Path(args.image)
    if not image_path.is_absolute():
        image_path = ROOT / image_path
    if not image_path.exists():
        raise SystemExit(
            f"Image not found: {image_path}\n"
            "Capture it first, for example:\n"
            "  python3 scripts/dev.py capture-esp32 --port /dev/cu.usbserial-XXXX --single --prefix current"
        )

    crop_path, annotated_path, ranked = _write_artifact_diagnostics(image_path, args.bottom_percent, args.top_k)
    Image, _ = _pil_modules()
    width, height = Image.open(image_path).size

    print(f"Analyzed {image_path.relative_to(ROOT)} ({width}x{height})")
    print(f"Bottom crop: {crop_path.relative_to(ROOT)}")
    print(f"Diagnostic overlay: {annotated_path.relative_to(ROOT)}")
    if not ranked:
        print("No rows available for analysis.")
        return

    print("Most suspicious bottom rows:")
    for y, score in ranked:
        print(f"  y={y:4d}  score={score:.2f}")

    strongest = ranked[0][1]
    if strongest < args.warn_score:
        print(
            "No strong horizontal stripe detected in this image. If this image is a framebuffer dump "
            "but the physical panel still shows lines, suspect RGB panel timing, init sequence, "
            "power/reset, or scanout rather than tile rendering."
        )
    else:
        print(
            "Strong row discontinuities detected in this image. For framebuffer dumps this points "
            "to rendering/clipping/framebuffer writes; for panel-camera captures this points to the "
            "physical LCD signal, timing, power, or optical capture path."
        )


def cmd_esp32_ports(_: argparse.Namespace) -> None:
    require_venv()
    ports = _available_serial_ports()
    if not ports:
        print("No serial ports detected.")
        return
    for port in ports:
        print(port)


def cmd_flash_esp32(args: argparse.Namespace) -> None:
    require_venv()
    port = _resolve_serial_port(args.port)
    config_path = args.config
    if not args.skip_compile:
        run([venv_esphome(), "compile", config_path])
    cmd = [venv_esphome(), "upload", "--device", port]
    if args.upload_speed:
        cmd.extend(["--upload_speed", str(args.upload_speed)])
    cmd.append(config_path)
    run(cmd)


def cmd_capture_esp32(args: argparse.Namespace) -> None:
    require_venv()
    _require_local_tool(SCREENSHOT_TOOL, "ESP32 framebuffer capture")
    port = _resolve_serial_port(args.port)
    output_dir = Path(args.output_dir)
    if not output_dir.is_absolute():
        output_dir = ROOT / output_dir
    output_dir.mkdir(parents=True, exist_ok=True)
    prefix = args.prefix or _esp32_prefix(args.config)
    if args.settle_s > 0:
        print(f"+ settle {args.settle_s:.1f}s before capture")
        time.sleep(args.settle_s)

    if args.single:
        output = output_dir / f"{prefix}.png"
        _capture_esp32_single(port, args.baud, output, args.timeout)
        return

    paths = _capture_esp32_scene(port, args.baud, output_dir, prefix, args.timeout)
    expected = len(ESP32_SCENE_SHOT_NAMES)
    if len(paths) != expected:
        raise SystemExit(f"ESP32 scene capture incomplete: {len(paths)}/{expected} screenshots")


def cmd_verify_esp32(args: argparse.Namespace) -> None:
    require_venv()
    _require_local_tool(SCREENSHOT_TOOL, "ESP32 framebuffer capture")
    port = _resolve_serial_port(args.port)
    prefix = args.prefix or _esp32_prefix(args.config)

    if args.update:
        ESP32_GOLDEN_DIR.mkdir(parents=True, exist_ok=True)
        if args.settle_s > 0:
            print(f"+ settle {args.settle_s:.1f}s before capture")
            time.sleep(args.settle_s)
        paths = _capture_esp32_scene(port, args.baud, ESP32_GOLDEN_DIR, prefix, args.timeout)
        expected = len(ESP32_SCENE_SHOT_NAMES)
        if len(paths) != expected:
            raise SystemExit(f"ESP32 golden update incomplete: {len(paths)}/{expected} screenshots")
        print(f"Updated ESP32 golden screenshots in {ESP32_GOLDEN_DIR.relative_to(ROOT)}")
        return

    with tempfile.TemporaryDirectory(prefix="tile-dashboard-esp32-") as tmp:
        tmp_dir = Path(tmp)
        if args.settle_s > 0:
            print(f"+ settle {args.settle_s:.1f}s before capture")
            time.sleep(args.settle_s)
        paths = _capture_esp32_scene(port, args.baud, tmp_dir, prefix, args.timeout)
        expected = len(ESP32_SCENE_SHOT_NAMES)
        if len(paths) != expected:
            raise SystemExit(f"ESP32 scene capture incomplete: {len(paths)}/{expected} screenshots")

        failures = []
        for current, golden in zip(_esp32_scene_files(tmp_dir, prefix), _esp32_scene_files(ESP32_GOLDEN_DIR, prefix)):
            if not golden.exists():
                failures.append(f"missing golden: {golden.relative_to(ROOT)}")
                continue
            if not filecmp.cmp(current, golden, shallow=False):
                failures.append(f"screenshot differs: {golden.name}")
        if failures:
            for failure in failures:
                print(f"  {failure}")
            raise SystemExit("ESP32 visual verification failed. Re-run with `verify-esp32 --update` if intentional.")
    print("ESP32 visual verification passed.")


def cmd_esp32_info(args: argparse.Namespace) -> None:
    require_venv()
    _require_local_tool(SCREENSHOT_TOOL, "ESP32 framebuffer capture")
    port = _resolve_serial_port(args.port)
    run([venv_python(), SCREENSHOT_TOOL, "--port", port, "--baud", str(args.baud), "--timeout", str(args.timeout), "--info"])


def cmd_esp32_bench(args: argparse.Namespace) -> None:
    require_venv()
    _require_local_tool(SCREENSHOT_TOOL, "ESP32 framebuffer capture")
    port = _resolve_serial_port(args.port)
    run([venv_python(), SCREENSHOT_TOOL, "--port", port, "--baud", str(args.baud), "--timeout", str(args.timeout), "--benchmark"])


def cmd_run_sim_2x2(_: argparse.Namespace) -> None:
    _run_simulator("examples/simulator_ha_2x2.yaml")


def cmd_run_sim_3x3(_: argparse.Namespace) -> None:
    _run_simulator("examples/simulator_showcase_3x3.yaml")


def cmd_run_sim_all(_: argparse.Namespace) -> None:
    _run_simulator("examples/simulator_all_tiles.yaml")


def cmd_test(_: argparse.Namespace) -> None:
    cmd_unit(argparse.Namespace())
    cmd_config(argparse.Namespace())


# GitHub-Import-Dateien die project_name/project_version inline brauchen,
# weil ESPHome !include innerhalb von GitHub-Imports nicht auflöst.
IMPORT_YAML_FILES = (
    ROOT / "packages/import/display48_device_base.yaml",
    ROOT / "packages/import/simulator_device_base.yaml",
    ROOT / "packages/import/common.yaml",
)


def _parse_build_settings() -> dict[str, str]:
    """Parse build_settings.yaml (simple key: value format)."""
    settings = {}
    for line in BUILD_SETTINGS_FILE.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        key, _, value = line.partition(":")
        settings[key.strip()] = value.strip().strip('"')
    return settings


def cmd_sync_version(_: argparse.Namespace) -> None:
    """Sync project_name/project_version from build_settings.yaml into import YAMLs."""
    settings = _parse_build_settings()
    version = settings["project_version"]
    name = settings["project_name"]
    print(f"Source: build_settings.yaml  →  {name} {version}")

    version_re = re.compile(r'(project_version:\s*")[^"]*(")') 
    name_re = re.compile(r'(project_name:\s*)\S+')
    updated = []

    for path in IMPORT_YAML_FILES:
        if not path.exists():
            continue
        text = path.read_text(encoding="utf-8")
        new_text = version_re.sub(rf'\g<1>{version}\2', text)
        new_text = name_re.sub(rf'\g<1>{name}', new_text)
        if new_text != text:
            path.write_text(new_text, encoding="utf-8")
            updated.append(path.relative_to(ROOT))
            print(f"  updated {path.relative_to(ROOT)}")

    if not updated:
        print("  all import files already up to date")


def _add_panel_capture_args(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--camera", type=int, default=0)
    parser.add_argument("--output")
    parser.add_argument("--output-dir", default=str(PANEL_SNAPSHOT_DIR.relative_to(ROOT)))
    parser.add_argument("--prefix", default="current")
    parser.add_argument("--raw-output")
    parser.add_argument("--warmup-s", type=float, default=1.0)
    parser.add_argument("--frames", type=int, default=20)
    parser.add_argument("--request-size")
    parser.add_argument("--rotate", type=int, choices=(0, 90, 180, 270), default=0)
    parser.add_argument("--crop", help="Crop after rotation: X,Y,W,H")
    parser.add_argument("--quad", help="Perspective quad after rotation/crop: x1,y1,...,x4,y4")
    parser.add_argument("--output-size", help="Resize/warp output size, e.g. 480x480")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Cross-platform dev helper for esphome_touch_widget")
    subparsers = parser.add_subparsers(dest="command", required=True)

    commands = {
        "setup": cmd_setup,
        "doctor": cmd_doctor,
        "unit": cmd_unit,
        "config": cmd_config,
        "compile-sim": cmd_compile_sim,
        "compile-esp32": cmd_compile_esp32,
        "esp32-ports": cmd_esp32_ports,
        "esp32-bridge": cmd_esp32_bridge,
        "run-sim-2x2": cmd_run_sim_2x2,
        "run-sim-3x3": cmd_run_sim_3x3,
        "run-sim-all": cmd_run_sim_all,
        "test": cmd_test,
        "sync-version": cmd_sync_version,
    }
    for name, handler in commands.items():
        subparsers.add_parser(name)
        subparsers.choices[name].set_defaults(func=handler)

    capture = subparsers.add_parser("capture-sim")
    capture.add_argument("--config", choices=SIMULATOR_CONFIGS)
    capture.add_argument("--output-dir", default=str(SIMULATOR_SNAPSHOT_DIR.relative_to(ROOT)))
    capture.add_argument("--after-ms", type=int, default=1200)
    capture.add_argument("--timeout", type=int, default=10)
    capture.add_argument("--verbose", action="store_true")
    capture.add_argument("--skip-compile", action="store_true")
    capture.set_defaults(func=cmd_capture_sim)

    verify = subparsers.add_parser("verify-sim")
    verify.add_argument("--config", choices=SIMULATOR_CONFIGS)
    verify.add_argument("--update", action="store_true")
    verify.add_argument("--after-ms", type=int, default=1200)
    verify.add_argument("--timeout", type=int, default=10)
    verify.add_argument("--verbose", action="store_true")
    verify.add_argument("--skip-compile", action="store_true")
    verify.set_defaults(func=cmd_verify_sim)

    flash = subparsers.add_parser("flash-esp32")
    flash.add_argument("--config", default=ESP32_USB_TEST_CONFIG)
    flash.add_argument("--port")
    flash.add_argument("--skip-compile", action="store_true")
    flash.add_argument("--upload-speed", type=int)
    flash.set_defaults(func=cmd_flash_esp32)

    bridge_ports = subparsers.add_parser("esp32-bridge-ports")
    bridge_ports.add_argument("--wait-timeout", type=float, default=15.0)
    bridge_ports.set_defaults(func=cmd_esp32_bridge_ports)

    panel_cameras = subparsers.add_parser("panel-cameras")
    panel_cameras.add_argument("--max-index", type=int, default=6)
    panel_cameras.add_argument("--no-probe", action="store_true")
    panel_cameras.set_defaults(func=cmd_panel_cameras)

    panel_cameras_bridge = subparsers.add_parser("panel-cameras-bridge")
    panel_cameras_bridge.add_argument("--max-index", type=int, default=6)
    panel_cameras_bridge.add_argument("--no-probe", action="store_true")
    panel_cameras_bridge.add_argument("--wait-timeout", type=float, default=45.0)
    panel_cameras_bridge.set_defaults(func=cmd_panel_cameras_bridge)

    panel_capture = subparsers.add_parser("capture-panel")
    _add_panel_capture_args(panel_capture)
    panel_capture.set_defaults(func=cmd_capture_panel)

    panel_capture_bridge = subparsers.add_parser("capture-panel-bridge")
    _add_panel_capture_args(panel_capture_bridge)
    panel_capture_bridge.add_argument("--wait-timeout", type=float, default=60.0)
    panel_capture_bridge.set_defaults(func=cmd_capture_panel_bridge)

    git_commit_bridge = subparsers.add_parser("git-commit-bridge")
    git_commit_bridge.add_argument("--message", required=True)
    git_commit_bridge.add_argument("--wait-timeout", type=float, default=60.0)
    git_commit_bridge.set_defaults(func=cmd_git_commit_bridge)

    bridge_flash = subparsers.add_parser("flash-esp32-bridge")
    bridge_flash.add_argument("--config", default=ESP32_USB_TEST_CONFIG)
    bridge_flash.add_argument("--port")
    bridge_flash.add_argument("--skip-compile", action="store_true")
    bridge_flash.add_argument("--upload-speed", type=int)
    bridge_flash.add_argument("--wait-timeout", type=float, default=300.0)
    bridge_flash.set_defaults(func=cmd_flash_esp32_bridge)

    capture_esp32 = subparsers.add_parser("capture-esp32")
    capture_esp32.add_argument("--config", default=ESP32_USB_TEST_CONFIG)
    capture_esp32.add_argument("--port")
    capture_esp32.add_argument("--baud", type=int, default=ESP32_DEFAULT_BAUD)
    capture_esp32.add_argument("--output-dir", default=str(ESP32_SNAPSHOT_DIR.relative_to(ROOT)))
    capture_esp32.add_argument("--prefix")
    capture_esp32.add_argument("--timeout", type=float, default=30.0)
    capture_esp32.add_argument("--settle-s", type=float, default=0.0)
    capture_esp32.add_argument("--single", action="store_true")
    capture_esp32.set_defaults(func=cmd_capture_esp32)

    bridge_capture = subparsers.add_parser("capture-esp32-bridge")
    bridge_capture.add_argument("--port")
    bridge_capture.add_argument("--baud", type=int, default=ESP32_DEFAULT_BAUD)
    bridge_capture.add_argument("--output-dir", default=str(ESP32_SNAPSHOT_DIR.relative_to(ROOT)))
    bridge_capture.add_argument("--prefix", default="current")
    bridge_capture.add_argument("--timeout", type=float, default=30.0)
    bridge_capture.add_argument("--settle-s", type=float, default=0.0)
    bridge_capture.add_argument("--scene", action="store_true")
    bridge_capture.add_argument("--wait-timeout", type=float, default=60.0)
    bridge_capture.set_defaults(func=cmd_capture_esp32_bridge)

    verify_esp32 = subparsers.add_parser("verify-esp32")
    verify_esp32.add_argument("--config", default=ESP32_USB_TEST_CONFIG)
    verify_esp32.add_argument("--port")
    verify_esp32.add_argument("--baud", type=int, default=ESP32_DEFAULT_BAUD)
    verify_esp32.add_argument("--prefix")
    verify_esp32.add_argument("--timeout", type=float, default=30.0)
    verify_esp32.add_argument("--settle-s", type=float, default=0.0)
    verify_esp32.add_argument("--update", action="store_true")
    verify_esp32.set_defaults(func=cmd_verify_esp32)

    esp32_info = subparsers.add_parser("esp32-info")
    esp32_info.add_argument("--port")
    esp32_info.add_argument("--baud", type=int, default=ESP32_DEFAULT_BAUD)
    esp32_info.add_argument("--timeout", type=float, default=15.0)
    esp32_info.set_defaults(func=cmd_esp32_info)

    esp32_info_bridge = subparsers.add_parser("esp32-info-bridge")
    esp32_info_bridge.add_argument("--port")
    esp32_info_bridge.add_argument("--baud", type=int, default=ESP32_DEFAULT_BAUD)
    esp32_info_bridge.add_argument("--timeout", type=float, default=15.0)
    esp32_info_bridge.add_argument("--settle-s", type=float, default=0.0)
    esp32_info_bridge.add_argument("--wait-timeout", type=float, default=30.0)
    esp32_info_bridge.set_defaults(func=cmd_esp32_info_bridge)

    esp32_bench = subparsers.add_parser("esp32-bench")
    esp32_bench.add_argument("--port")
    esp32_bench.add_argument("--baud", type=int, default=ESP32_DEFAULT_BAUD)
    esp32_bench.add_argument("--timeout", type=float, default=15.0)
    esp32_bench.set_defaults(func=cmd_esp32_bench)

    esp32_bench_bridge = subparsers.add_parser("esp32-bench-bridge")
    esp32_bench_bridge.add_argument("--port")
    esp32_bench_bridge.add_argument("--baud", type=int, default=ESP32_DEFAULT_BAUD)
    esp32_bench_bridge.add_argument("--timeout", type=float, default=15.0)
    esp32_bench_bridge.add_argument("--settle-s", type=float, default=0.0)
    esp32_bench_bridge.add_argument("--wait-timeout", type=float, default=45.0)
    esp32_bench_bridge.set_defaults(func=cmd_esp32_bench_bridge)

    analyze_esp32 = subparsers.add_parser("analyze-esp32-image")
    analyze_esp32.add_argument("--image", default=str(ESP32_DEFAULT_IMAGE.relative_to(ROOT)))
    analyze_esp32.add_argument("--bottom-percent", type=float, default=8.0)
    analyze_esp32.add_argument("--top-k", type=int, default=8)
    analyze_esp32.add_argument("--warn-score", type=float, default=18.0)
    analyze_esp32.set_defaults(func=cmd_analyze_esp32_image)

    return parser


def main() -> None:
    parser = build_parser()
    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
