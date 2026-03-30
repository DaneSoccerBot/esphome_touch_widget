#!/usr/bin/env python3
from __future__ import annotations

import os
import platform
import shutil
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
VENV_DIR = ROOT / ".venv"


def bin_dir() -> Path:
    return VENV_DIR / ("Scripts" if os.name == "nt" else "bin")


def exe_suffix() -> str:
    return ".exe" if os.name == "nt" else ""


def venv_python() -> Path:
    return bin_dir() / f"python{exe_suffix()}"


def venv_esphome() -> Path:
    return bin_dir() / f"esphome{exe_suffix()}"


def run(cmd: list[str], env: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
    print("+", " ".join(str(part) for part in cmd))
    completed = subprocess.run(
        [str(part) for part in cmd],
        cwd=ROOT,
        env=env,
        text=True,
    )
    if completed.returncode != 0:
        raise SystemExit(completed.returncode)
    return completed


def require_venv() -> None:
    if not venv_python().exists():
        raise SystemExit(
            "Local virtualenv missing. Run `python3 scripts/dev.py setup` "
            "or on Windows `py -3 scripts/dev.py setup` first."
        )


def host_simulator_hint() -> str:
    system = platform.system()
    if system == "Darwin":
        return "Install host simulator deps with `brew install sdl2 pkg-config`."
    if system == "Linux":
        try:
            os_release = platform.freedesktop_os_release()
        except OSError:
            os_release = {}
        distro = os_release.get("ID", "").lower()
        if distro in {"ubuntu", "debian", "linuxmint", "pop"}:
            return "Install host simulator deps with `sudo apt install libsdl2-dev pkg-config`."
        if distro in {"fedora", "rhel", "centos"}:
            return "Install host simulator deps with `sudo dnf install SDL2-devel pkgconf-pkg-config`."
        if distro in {"arch", "manjaro"}:
            return "Install host simulator deps with `sudo pacman -S sdl2 pkgconf`."
        return "Install host simulator deps: SDL2 development headers and `pkg-config`."
    return (
        "For Windows, the easiest host-simulator path is WSL2 + WSLg. "
        "Native Windows host builds need SDL2, pkg-config, and Visual Studio C++ build tools."
    )


def check_host_simulator_support() -> tuple[bool, str]:
    pkg_config = shutil.which("pkg-config")
    if pkg_config is None:
        return False, host_simulator_hint()
    result = subprocess.run(
        [pkg_config, "--exists", "sdl2"],
        cwd=ROOT,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        text=True,
    )
    if result.returncode != 0:
        return False, host_simulator_hint()
    return True, "SDL2 development files detected."
