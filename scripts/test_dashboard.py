#!/usr/bin/env python3
import argparse
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PYTHON_BIN = ROOT / ".venv/bin/python"
ESPHOME_BIN = ROOT / ".venv/bin/esphome"


def run(cmd, env=None):
    print("+", " ".join(cmd))
    completed = subprocess.run(cmd, cwd=ROOT, env=env)
    if completed.returncode != 0:
        raise SystemExit(completed.returncode)


def main():
    parser = argparse.ArgumentParser(description="Tile dashboard dev/test helper")
    parser.add_argument(
        "target",
        choices=("unit", "config", "compile-sim", "compile-esp32", "all"),
        help="what to run",
    )
    args = parser.parse_args()

    if args.target in ("unit", "all"):
        run([str(PYTHON_BIN), "-m", "unittest", "discover", "-s", "tests", "-v"])

    if args.target in ("config", "all"):
        run([str(ESPHOME_BIN), "config", "examples/simulator_ha_2x2.yaml"])
        run([str(ESPHOME_BIN), "config", "examples/simulator_showcase_3x3.yaml"])
        run([str(ESPHOME_BIN), "config", "examples/simulator_all_tiles.yaml"])
        run([str(ESPHOME_BIN), "config", "examples/display48norelay.refactored.yaml"])
        run([str(ESPHOME_BIN), "config", "examples/display48norelay.package_import.yaml"])

    if args.target in ("compile-sim", "all"):
        run([str(ESPHOME_BIN), "compile", "examples/simulator_ha_2x2.yaml"])
        run([str(ESPHOME_BIN), "compile", "examples/simulator_showcase_3x3.yaml"])
        run([str(ESPHOME_BIN), "compile", "examples/simulator_all_tiles.yaml"])

    if args.target in ("compile-esp32", "all"):
        run([str(ESPHOME_BIN), "compile", "examples/display48norelay.refactored.yaml"])
        run([str(ESPHOME_BIN), "compile", "examples/display48norelay.package_import.yaml"])


if __name__ == "__main__":
    main()
