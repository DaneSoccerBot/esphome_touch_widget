#!/usr/bin/env python3
from __future__ import annotations

import argparse
import importlib.metadata
import os
import re
import subprocess
import sys
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
    venv_esphome,
    venv_python,
)


REQUIREMENTS_FILE = ROOT / "requirements-dev.txt"
BUILD_SETTINGS_FILE = ROOT / "build_settings.yaml"
PINNED_ESPHOME_VERSION = "2025.3.3"


SIMULATOR_CONFIGS = (
    "examples/simulator_ha_2x2.yaml",
    "examples/simulator_showcase_3x3.yaml",
    "examples/simulator_all_tiles.yaml",
)

# Map each simulator config to its PlatformIO build output directory.
SIMULATOR_BUILD_DIRS = {
    "examples/simulator_ha_2x2.yaml": ROOT / "examples/.esphome/build/dashboard-2x2-ha/.pioenvs/dashboard-2x2-ha",
    "examples/simulator_showcase_3x3.yaml": ROOT / "examples/.esphome/build/dashboard-3x3-showcase/.pioenvs/dashboard-3x3-showcase",
    "examples/simulator_all_tiles.yaml": ROOT / "examples/.esphome/build/dashboard-all-tiles/.pioenvs/dashboard-all-tiles",
}


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


def install_python_dependencies() -> None:
    try:
        installed_version = importlib.metadata.version("esphome")
    except importlib.metadata.PackageNotFoundError:
        installed_version = None

    if installed_version == PINNED_ESPHOME_VERSION:
        print(f"ESPHome {PINNED_ESPHOME_VERSION} already installed in local virtualenv.")
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
        "run-sim-2x2": cmd_run_sim_2x2,
        "run-sim-3x3": cmd_run_sim_3x3,
        "run-sim-all": cmd_run_sim_all,
        "test": cmd_test,
        "sync-version": cmd_sync_version,
    }
    for name, handler in commands.items():
        subparsers.add_parser(name)
        subparsers.choices[name].set_defaults(func=handler)

    return parser


def main() -> None:
    parser = build_parser()
    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
