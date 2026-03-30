#!/usr/bin/env python3
from __future__ import annotations

import os
import platform
import shutil
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
VENV_DIR = ROOT / ".venv"
PLATFORMIO_CORE_DIR = ROOT / ".cache" / "platformio"
TOOLING_BIN_DIR = ROOT / ".cache" / "tooling" / "bin"


def bin_dir() -> Path:
    return VENV_DIR / ("Scripts" if os.name == "nt" else "bin")


def exe_suffix() -> str:
    return ".exe" if os.name == "nt" else ""


def venv_python() -> Path:
    return bin_dir() / f"python{exe_suffix()}"


def venv_esphome() -> Path:
    return bin_dir() / f"esphome{exe_suffix()}"


def build_platformio_env(
    base_env: dict[str, str] | None = None, core_dir: Path = PLATFORMIO_CORE_DIR
) -> dict[str, str]:
    env = dict(base_env or {})
    env.setdefault("PLATFORMIO_CORE_DIR", str(core_dir))
    return env


def ensure_windows_sdl2_config_wrapper(
    root: Path | None, tooling_bin_dir: Path = TOOLING_BIN_DIR
) -> Path | None:
    if root is None:
        return None

    bash_exe = root / "usr/bin/bash.exe"
    sdl2_config = root / "ucrt64/bin/sdl2-config"
    if not bash_exe.exists() or not sdl2_config.exists():
        return None

    tooling_bin_dir.mkdir(parents=True, exist_ok=True)
    wrapper_path = tooling_bin_dir / "sdl2-config.cmd"
    wrapper = (
        "@echo off\n"
        "setlocal\n"
        "set \"MSYSTEM=UCRT64\"\n"
        "set \"CHERE_INVOKING=1\"\n"
        f'"{bash_exe}" -lc "/ucrt64/bin/sdl2-config %*"\n'
    )
    if not wrapper_path.exists() or wrapper_path.read_text() != wrapper:
        wrapper_path.write_text(wrapper)
    return wrapper_path


def get_windows_sdl2_options(root: Path | None = None) -> str | None:
    if os.name != "nt":
        return None

    if root is None:
        root = detect_windows_msys2_root()
    if root is None:
        return None

    bash_exe = root / "usr/bin/bash.exe"
    sdl2_config = root / "ucrt64/bin/sdl2-config"
    if not bash_exe.exists() or not sdl2_config.exists():
        return None

    env = build_windows_msys2_env(build_platformio_env(os.environ), root, TOOLING_BIN_DIR)
    result = subprocess.run(
        [str(bash_exe), "-lc", "/ucrt64/bin/sdl2-config --cflags --libs"],
        cwd=ROOT,
        env=env,
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        return None
    sdl_flags = result.stdout.strip()
    if not sdl_flags:
        return None

    # ESPHome's host main() is C++ with no args.  The -Dmain=SDL_main
    # rename produces a C++-mangled symbol that SDL2main cannot resolve.
    # Drop the SDL_main wrapper flags; the native host entry point works
    # without WinMain.
    drop = {"-Dmain=SDL_main", "-lSDL2main", "-lmingw32", "-mwindows"}
    sdl_flags = " ".join(t for t in sdl_flags.split() if t not in drop)

    # POSIX-to-Winsock2 shim headers for ESPHome host platform on Windows
    compat_dir = (ROOT / "compat" / "win32_posix").as_posix()
    compat_flags = f"-I{compat_dir} -lws2_32"
    return f"{compat_flags} {sdl_flags}"


def simulator_substitution_args() -> list[str]:
    sdl_options = get_windows_sdl2_options()
    if not sdl_options:
        return []
    return ["-s", "simulator_sdl_options", sdl_options]


def detect_windows_msys2_root(candidates: list[Path] | None = None) -> Path | None:
    if candidates is None:
        candidates = []
        explicit = os.environ.get("MSYS2_ROOT")
        if explicit:
            candidates.append(Path(explicit))
        candidates.extend(
            [
                Path("C:/msys64"),
                Path("D:/msys64"),
            ]
        )

    for root in candidates:
        bash_exe = root / "usr/bin/bash.exe"
        if bash_exe.exists():
            return root
    return None


def build_windows_msys2_env(
    base_env: dict[str, str] | None = None,
    root: Path | None = None,
    tooling_bin_dir: Path | None = None,
) -> dict[str, str]:
    env = dict(base_env or {})
    if root is None:
        return env

    ucrt_bin = root / "ucrt64/bin"
    usr_bin = root / "usr/bin"
    path_parts = []
    if tooling_bin_dir is not None:
        path_parts.append(str(tooling_bin_dir))
    path_parts.extend([str(ucrt_bin), str(usr_bin)])
    existing_path = env.get("PATH")
    if existing_path:
        path_parts.append(existing_path)
    env["PATH"] = os.pathsep.join(path_parts)

    pkg_paths = [
        str(root / "ucrt64/lib/pkgconfig"),
        str(root / "ucrt64/share/pkgconfig"),
    ]
    existing_pkg_path = env.get("PKG_CONFIG_PATH")
    if existing_pkg_path:
        pkg_paths.append(existing_pkg_path)
    env["PKG_CONFIG_PATH"] = os.pathsep.join(pkg_paths)
    env.setdefault("PKG_CONFIG_SYSTEM_INCLUDE_PATH", str(root / "ucrt64/include"))
    env.setdefault("PKG_CONFIG_SYSTEM_LIBRARY_PATH", str(root / "ucrt64/lib"))
    env.setdefault("MSYSTEM", "UCRT64")
    env.setdefault("CHERE_INVOKING", "1")
    return env


def tooling_env(env: dict[str, str] | None = None) -> dict[str, str]:
    merged = build_platformio_env(os.environ)
    if env:
        merged.update(env)
    merged = build_platformio_env(merged)
    platformio_core_dir = Path(merged["PLATFORMIO_CORE_DIR"])
    platformio_core_dir.mkdir(parents=True, exist_ok=True)
    if os.name == "nt":
        msys2_root = detect_windows_msys2_root()
        ensure_windows_sdl2_config_wrapper(msys2_root)
        merged = build_windows_msys2_env(merged, msys2_root, TOOLING_BIN_DIR)
    return merged


def run(cmd: list[str], env: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
    print("+", " ".join(str(part) for part in cmd))
    completed = subprocess.run(
        [str(part) for part in cmd],
        cwd=ROOT,
        env=tooling_env(env),
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
        return "Run `./scripts/bootstrap.sh` to install `sdl2` and `pkg-config` automatically."
    if system == "Linux":
        try:
            os_release = platform.freedesktop_os_release()
        except OSError:
            os_release = {}
        distro = os_release.get("ID", "").lower()
        if distro in {"ubuntu", "debian", "linuxmint", "pop"}:
            return "Run `./scripts/bootstrap.sh` to install `libsdl2-dev` and `pkg-config` automatically."
        if distro in {"fedora", "rhel", "centos"}:
            return "Run `./scripts/bootstrap.sh` to install `SDL2-devel` and `pkgconf-pkg-config` automatically."
        if distro in {"arch", "manjaro"}:
            return "Run `./scripts/bootstrap.sh` to install `sdl2` and `pkgconf` automatically."
        return "Install host simulator deps: SDL2 development headers and `pkg-config`."
    return (
        "Run `scripts\\bootstrap.ps1` to install MSYS2 plus the UCRT64 SDL2, pkgconf, and GCC packages."
    )


def check_host_simulator_support() -> tuple[bool, str]:
    if os.name == "nt":
        sdl_options = get_windows_sdl2_options()
        if not sdl_options:
            return False, host_simulator_hint()
        return True, "SDL2 development files detected via MSYS2."

    env = tooling_env()
    sdl2_config = shutil.which("sdl2-config", path=env.get("PATH"))
    if sdl2_config is None:
        return False, host_simulator_hint()
    try:
        result = subprocess.run(
            [sdl2_config, "--cflags", "--libs"],
            cwd=ROOT,
            env=env,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            text=True,
            check=False,
        )
    except OSError:
        return False, host_simulator_hint()
    if result.returncode != 0:
        return False, host_simulator_hint()
    return True, "SDL2 development files detected."
