#!/usr/bin/env python3
import argparse
from tooling import run, require_venv, venv_esphome, venv_python


def main():
    parser = argparse.ArgumentParser(description="Tile dashboard dev/test helper")
    parser.add_argument(
        "target",
        choices=("unit", "config", "compile-sim", "compile-esp32", "all"),
        help="what to run",
    )
    args = parser.parse_args()
    require_venv()

    if args.target in ("unit", "all"):
        run([venv_python(), "-m", "unittest", "discover", "-s", "tests", "-v"])

    if args.target in ("config", "all"):
        run([venv_esphome(), "config", "examples/simulator_ha_2x2.yaml"])
        run([venv_esphome(), "config", "examples/simulator_showcase_3x3.yaml"])
        run([venv_esphome(), "config", "examples/simulator_all_tiles.yaml"])
        run([venv_esphome(), "config", "examples/display48norelay.refactored.yaml"])
        run([venv_esphome(), "config", "examples/display48norelay.package_import.yaml"])

    if args.target in ("compile-sim", "all"):
        run([venv_esphome(), "compile", "examples/simulator_ha_2x2.yaml"])
        run([venv_esphome(), "compile", "examples/simulator_showcase_3x3.yaml"])
        run([venv_esphome(), "compile", "examples/simulator_all_tiles.yaml"])

    if args.target in ("compile-esp32", "all"):
        run([venv_esphome(), "compile", "examples/display48norelay.refactored.yaml"])
        run([venv_esphome(), "compile", "examples/display48norelay.package_import.yaml"])


if __name__ == "__main__":
    main()
