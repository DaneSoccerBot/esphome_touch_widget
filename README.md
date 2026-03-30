# esphome_touch_widget

`esphome_touch_widget` is an extensible touch UI framework for ESPHome. It is
designed to turn inexpensive, low-power ESP32 touch displays into Home
Assistant control panels, with fast iteration in a local simulator and on real
hardware.

The long-term direction is broader than a simple tile dashboard. The current
focus is reusable tiles, declarative layout, and simulator-first development,
but the project is intended to grow toward richer UI widgets, paging, full
screen focus views, and more advanced control workflows for power-sensitive
environments such as camper vans, cabins, and remote solar-backed buildings.

## Architecture

The repository is structured around `thin device configs`:

- The repo provides the reusable dashboard component.
- The repo provides hardware and base packages.
- Each installation keeps one local device YAML.
- Grid layout, tile selection, and Home Assistant entity mapping remain local
  and flexible.

The primary split is:

- `components/tile_dashboard`
  Declarative ESPHome component exposed as `tile_dashboard:`.
- `packages/base`
  Shared device and host base packages.
- `packages/hardware`
  Board, display, and touchscreen definitions.
- `packages/device`
  Thin package combinators that wire together base, hardware, and
  `external_components`.
- `packages/import`
  Remote packages intended for `github://...` imports.
- `examples`
  Complete reference YAML files for simulator and hardware usage.

## Intended Usage

The target user flow is one local ESPHome YAML per device. That YAML:

- imports the generic packages from this repository
- defines its own sensors and text sensors
- defines its own `tile_dashboard:` grid and tile layout

A minimal pattern looks like this:

```yaml
substitutions:
  device_name: living-room-display
  friendly_name: Living Room Display
  wifi_ssid: YOUR_WIFI_SSID
  wifi_password: YOUR_WIFI_PASSWORD
  api_encryption_key: YOUR_API_KEY
  ota_password: YOUR_OTA_PASSWORD
  dashboard_font_file: gfonts://Roboto Condensed
  tile_dashboard_components_source: github://DaneSoccerBot/esphome_touch_widget/components@main
  dashboard_width: "480"
  dashboard_height: "480"
  touch_rotation: "0"

packages:
  device: github://DaneSoccerBot/esphome_touch_widget/packages/import/display48_device_base.yaml@main

sensor:
  - platform: homeassistant
    id: battery_percent
    entity_id: sensor.demo_battery_percent

tile_dashboard:
  display: dashboard_display
  touchscreen: dashboard_touchscreen
  cols: 2
  rows: 2
  width: ${dashboard_width}
  height: ${dashboard_height}
  touch_rotation: ${touch_rotation}
  font_file: ${dashboard_font_file}
  tiles:
    - type: battery
      col: 1
      row: 1
      sensor: battery_percent
```

Device YAML files no longer need display or touchscreen lambdas. The component
attaches itself to the configured display and touchscreen.

## Key Files

- [`components/tile_dashboard/__init__.py`](components/tile_dashboard/__init__.py)
  ESPHome schema and code generation.
- [`components/tile_dashboard/config.py`](components/tile_dashboard/config.py)
  Testable helpers for tile and font configuration.
- [`components/tile_dashboard/tile_dashboard_component.h`](components/tile_dashboard/tile_dashboard_component.h)
  Runtime for rendering, touch routing, and tile instantiation.
- [`packages/device/display48_device_base.yaml`](packages/device/display48_device_base.yaml)
  Thin base package for the physical 480x480 ESP32 display target.
- [`packages/device/simulator_device_base.yaml`](packages/device/simulator_device_base.yaml)
  Thin base package for the host/SDL simulator.
- [`packages/import/display48_device_base.yaml`](packages/import/display48_device_base.yaml)
  Remote package for physical device imports.
- [`examples/display48norelay.refactored.yaml`](examples/display48norelay.refactored.yaml)
  Thin local ESP32 device example.
- [`examples/display48norelay.package_import.yaml`](examples/display48norelay.package_import.yaml)
  Thin local device example built through the import package.
- [`examples/simulator_ha_2x2.yaml`](examples/simulator_ha_2x2.yaml)
  Compact 2x2 simulator example.
- [`examples/simulator_showcase_3x3.yaml`](examples/simulator_showcase_3x3.yaml)
  Larger 3x3 simulator showcase with more tile types and animated demo data.
- [`examples/simulator_all_tiles.yaml`](examples/simulator_all_tiles.yaml)
  Simulator example that exercises every currently supported tile type.

## GitHub Import Path

For public ESPHome users, the recommended starting point is:

```yaml
packages:
  device: github://DaneSoccerBot/esphome_touch_widget/packages/import/display48_device_base.yaml@main
```

After that, all flexible parts stay local:

- `sensor:`, `text_sensor:`, and `switch:`
- `tile_dashboard:`
- grid dimensions
- tile placement
- Home Assistant entity mapping

ESPHome will still generate one local device YAML in the dashboard. That is
intentional here, because the device-specific configuration belongs there.

## Local Development

### Bootstrap

After cloning the repository, the recommended entry point is a single command.

macOS / Linux:

```bash
./scripts/bootstrap.sh
```

Windows:

```powershell
.\scripts\bootstrap.ps1
```

This creates a local `.venv`, installs the pinned ESPHome tooling from
[`requirements-dev.txt`](requirements-dev.txt), and checks whether the host
simulator environment is ready.

The bootstrap scripts call [`scripts/dev.py`](scripts/dev.py) internally. You
can also use the Python entry point directly:

- macOS / Linux: `python3 scripts/dev.py setup`
- Windows: `py -3 scripts/dev.py setup`

For the local simulator you still need system packages:

- macOS: `brew install sdl2 pkg-config`
- Ubuntu / Debian: `sudo apt install libsdl2-dev pkg-config`
- Fedora: `sudo dnf install SDL2-devel pkgconf-pkg-config`
- Arch: `sudo pacman -S sdl2 pkgconf`
- Windows: the easiest path is WSL2 + WSLg; native host builds need SDL2,
  `pkg-config`, and Visual Studio C++ build tools

The new dev entry point wraps the common tasks without requiring manual
activation of the virtual environment.

### Simulator

Quick iteration in the compact simulator:

```bash
python3 scripts/dev.py run-sim-2x2
```

Run the larger 3x3 showcase:

```bash
python3 scripts/dev.py run-sim-3x3
```

Run the full tile coverage simulator:

```bash
python3 scripts/dev.py run-sim-all
```

### ESP32

Validate and compile the hardware examples:

```bash
python3 scripts/dev.py compile-esp32
```

Validate all tracked example configs:

```bash
python3 scripts/dev.py config
```

## Tests

The test suite covers three layers:

- unit tests for tile and font helper logic
- schema tests for `tile_dashboard:`
- end-to-end checks for thin device YAMLs, import packages, and simulator code
  generation

Quick test run:

```bash
python3 scripts/dev.py test
```

Additional development commands:

```bash
python3 scripts/dev.py doctor
python3 scripts/dev.py unit
python3 scripts/dev.py config
python3 scripts/dev.py compile-sim
python3 scripts/dev.py compile-esp32
```

By default, the repo validates:

- thin ESP32 and simulator device YAMLs
- the generic import package
- all supported tile types in simulator examples
- generated C++ output through `esphome compile --only-generate` on the host path

The full compile checks remain separate under the explicit compile commands.

## ESPHome 2025.3.3 Notes

With local ESPHome `2025.3.3`, two practical details matter here:

- the examples intentionally use explicit `substitutions` instead of relying on
  package defaults
- generated fonts still need a minimal bootstrap font, which is hidden inside
  the generic `packages/device/*` base packages rather than repeated in each
  device YAML

## Repository Scope

This repository intentionally tracks only the actively maintained path:

- `components/tile_dashboard`
- `packages/base`, `packages/device`, `packages/hardware`, `packages/import`
- `examples`, `tests`, `scripts`, `assets/fonts`

Older experiments and alternative combinators can still exist locally, but they
are intentionally not part of the versioned core repository.

## Future Work

- add more hardware base packages for other display boards
- add more example device YAMLs with 3x2 and 4x4 grids
- move the public import references from `@main` to tagged releases
- continue expanding the UI model with more widgets, paging, focus views, and
  richer control flows

## License

The repository source code is licensed under MIT. See [`LICENSE`](LICENSE).

The bundled font asset at
[`assets/fonts/RobotoCondensed-Regular.ttf`](assets/fonts/RobotoCondensed-Regular.ttf)
is a third-party asset and remains under its own license. See
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).
