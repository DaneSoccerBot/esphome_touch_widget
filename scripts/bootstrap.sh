#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

cd "$ROOT_DIR"

if ! command -v python3 >/dev/null 2>&1; then
  echo "python3 not found. Install Python 3.11+ and rerun this script."
  exit 1
fi

has_sdl2() {
  command -v pkg-config >/dev/null 2>&1 && pkg-config --exists sdl2
}

install_linux_deps() {
  if command -v apt-get >/dev/null 2>&1; then
    sudo apt-get update
    sudo apt-get install -y libsdl2-dev pkg-config
    return
  fi
  if command -v dnf >/dev/null 2>&1; then
    sudo dnf install -y SDL2-devel pkgconf-pkg-config
    return
  fi
  if command -v pacman >/dev/null 2>&1; then
    sudo pacman -S --noconfirm --needed sdl2 pkgconf
    return
  fi
  echo "Unsupported Linux package manager. Install SDL2 development headers and pkg-config manually."
  exit 1
}

if ! has_sdl2; then
  case "$(uname -s)" in
    Darwin)
      if ! command -v brew >/dev/null 2>&1; then
        echo "Homebrew not found. Install Homebrew or install sdl2 and pkg-config manually."
        exit 1
      fi
      brew install sdl2 pkg-config
      ;;
    Linux)
      install_linux_deps
      ;;
  esac
fi

python3 scripts/dev.py setup
