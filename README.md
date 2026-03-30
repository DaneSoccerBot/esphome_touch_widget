# esphome_touch_widget

Dieses Repo ist jetzt auf `thin device configs` ausgerichtet:

- Das Repo liefert die wiederverwendbare Dashboard-Komponente.
- Das Repo liefert Hardware-/Basis-Packages.
- Jede Installation behält genau eine lokale Geräte-YAML.
- Grid, Tiles und Home-Assistant-Entity-Mapping bleiben lokal und flexibel.

Der primäre Schnitt ist damit:

- `components/tile_dashboard`
  Die deklarative ESPHome-Komponente mit `tile_dashboard:`.
- `packages/base`
  Gemeinsame Geräte- und Host-Basen.
- `packages/hardware`
  Board-, Display- und Touch-Definitionen.
- `packages/device`
  Dünne Kombinatoren aus Base, Hardware und `external_components`.
- `packages/import`
  Remote-Packages für `github://...`-Einbindung.
- `examples`
  Vollständige lokale Geräte-YAMLs als Referenz.

## Zielbild

Ein Nutzer legt in ESPHome genau eine lokale Geräte-YAML an. Diese YAML:

- importiert die generischen Repo-Pakete
- definiert ihre eigenen Sensoren und Text-Sensoren
- definiert ihr eigenes `tile_dashboard:` mit beliebigem Grid

Ein minimales Muster sieht so aus:

```yaml
substitutions:
  device_name: wohnzimmer-display
  friendly_name: Wohnzimmer Display
  wifi_ssid: YOUR_WIFI_SSID
  wifi_password: YOUR_WIFI_PASSWORD
  api_encryption_key: YOUR_API_KEY
  ota_password: YOUR_OTA_PASSWORD
  dashboard_font_file: gfonts://Roboto Condensed
  tile_dashboard_components_source: github://OWNER/esphome_touch_widget/components@v0.1.0
  dashboard_width: "480"
  dashboard_height: "480"
  touch_rotation: "0"

packages:
  device: github://OWNER/esphome_touch_widget/packages/import/display48_device_base.yaml@v0.1.0

sensor:
  - platform: homeassistant
    id: battery_percent
    entity_id: sensor.victron_system_battery_soc

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

Die Geräte-YAML enthält keine Display- oder Touch-Lambdas mehr. Die Komponente hängt sich selbst an Display und Touchscreen.

## Wichtigste Dateien

- [`components/tile_dashboard/__init__.py`](components/tile_dashboard/__init__.py)
  ESPHome-Schema und Codegen.
- [`components/tile_dashboard/config.py`](components/tile_dashboard/config.py)
  Testbare Helper für Tile- und Font-Konfiguration.
- [`components/tile_dashboard/tile_dashboard_component.h`](components/tile_dashboard/tile_dashboard_component.h)
  Runtime für Rendering, Touch-Routing und Tile-Erzeugung.
- [`packages/device/display48_device_base.yaml`](packages/device/display48_device_base.yaml)
  Dünne Basis für das echte 480x480-ESP32-Display.
- [`packages/device/simulator_device_base.yaml`](packages/device/simulator_device_base.yaml)
  Dünne Basis für den Host/SDL-Simulator.
- [`packages/import/display48_device_base.yaml`](packages/import/display48_device_base.yaml)
  Remote-Package für echte Geräte.
- [`examples/display48norelay.refactored.yaml`](examples/display48norelay.refactored.yaml)
  Dünne lokale ESP32-Geräte-YAML.
- [`examples/display48norelay.package_import.yaml`](examples/display48norelay.package_import.yaml)
  Dünne lokale Geräte-YAML über das Import-Package.
- [`examples/simulator_ha_2x2.yaml`](examples/simulator_ha_2x2.yaml)
  Dünne lokale Simulator-YAML.
- [`examples/simulator_showcase_3x3.yaml`](examples/simulator_showcase_3x3.yaml)
  Größeres 3x3-Simulatorbeispiel mit mehr Tile-Typen und animierten Demo-Daten.
- [`examples/simulator_all_tiles.yaml`](examples/simulator_all_tiles.yaml)
  Simulator-Beispiel, das alle Tile-Typen abdeckt.

## Repo-Einbindung

Für reale Nutzer ist der empfohlene Pfad:

```yaml
packages:
  device: github://OWNER/esphome_touch_widget/packages/import/display48_device_base.yaml@v0.1.0
```

Danach bleibt alles Flexible lokal:

- `sensor:`, `text_sensor:`, `switch:`
- `tile_dashboard:`
- Grid-Größe
- Tile-Anordnung
- Entity-Mapping

Wichtig: ESPHome erzeugt im Dashboard weiterhin eine lokale Geräte-YAML. Das ist hier gewollt, weil genau dort die gerätespezifische Belegung lebt.

## Entwicklung

### Setup

Nach dem Clone ist der empfohlene Einstieg genau ein Kommando:

macOS / Linux:

```bash
./scripts/bootstrap.sh
```

Windows:

```powershell
.\scripts\bootstrap.ps1
```

Das erzeugt eine lokale `.venv`, installiert das gepinnte ESPHome-Tooling aus
[`requirements-dev.txt`](requirements-dev.txt) und prüft den Host-Simulator.

Die Bootstrap-Skripte rufen intern weiterhin [`scripts/dev.py`](scripts/dev.py)
auf. Wenn du den Python-Weg direkt nutzen willst, geht das ebenfalls:

- macOS / Linux: `python3 scripts/dev.py setup`
- Windows: `py -3 scripts/dev.py setup`

Für den lokalen Simulator braucht das Repo zusätzlich Systempakete:

- macOS: `brew install sdl2 pkg-config`
- Ubuntu/Debian: `sudo apt install libsdl2-dev pkg-config`
- Fedora: `sudo dnf install SDL2-devel pkgconf-pkg-config`
- Arch: `sudo pacman -S sdl2 pkgconf`
- Windows: am einfachsten über WSL2 + WSLg; native Host-Builds brauchen SDL2, `pkg-config` und Visual-Studio-C++-Build-Tools

Der neue Dev-Entrypoint kapselt danach alle üblichen Kommandos ohne manuelles
Aktivieren der venv.

### Simulator

Schnelle UI-Iteration:

```bash
python3 scripts/dev.py run-sim-2x2
```

Alle Tile-Typen im Simulator:

```bash
python3 scripts/dev.py run-sim-all
```

Größeres 3x3-Showcase im Simulator:

```bash
python3 scripts/dev.py run-sim-3x3
```

### ESP32

Hardware-Build:

```bash
python3 scripts/dev.py compile-esp32
```

Import-Package-Pfad lokal prüfen:

```bash
python3 scripts/dev.py config
```

## Tests

Die Tests liegen unter `tests/` und decken drei Ebenen ab:

- Helper-Unit-Tests für Tile- und Font-Logik
- Schema-Tests für `tile_dashboard:`
- e2e-Tests für dünne Geräte-YAMLs, Import-Packages und Simulator-Codegen

Schnelllauf:

```bash
python3 scripts/dev.py test
```

Dev-Helper:

```bash
python3 scripts/dev.py doctor
python3 scripts/dev.py unit
python3 scripts/dev.py config
python3 scripts/dev.py compile-sim
python3 scripts/dev.py compile-esp32
```

Standardmäßig geprüft werden:

- dünne ESP32- und Simulator-Geräte-YAMLs
- das generische Import-Package
- alle Tile-Typen im Simulator
- generierter C++-Code per `esphome compile --only-generate` im Host-Pfad

Die vollen Compile-Tests bleiben separat über die vorhandenen `compile-*`-Targets.

## ESPHome 2025.3.3 Hinweise

Mit lokalem ESPHome `2025.3.3` gibt es hier zwei relevante Eigenheiten:

- Die Beispiele arbeiten bewusst mit expliziten `substitutions` statt auf Package-Defaults zu vertrauen.
- Programmgenerierte Fonts brauchen weiterhin ein minimales Bootstrap-Font. Dieses steckt jetzt unsichtbar in den generischen `packages/device/*`-Basen, nicht mehr in der Geräte-YAML.

## Repo-Schnitt

Dieses Repo enthält bewusst nur den aktiv gepflegten Pfad:

- `components/tile_dashboard`
- `packages/base`, `packages/device`, `packages/hardware`, `packages/import`
- `examples`, `tests`, `scripts`, `assets/fonts`

Ältere Experimente und alternative Kombinatoren bleiben lokal möglich, sind aber absichtlich nicht Teil des versionierten Kern-Repos.

## Nächste sinnvolle Ausbaustufen

- Zusätzliche Hardware-Basen für andere Display-Boards ergänzen.
- Mehr Beispiel-Geräte-YAMLs mit 3x2- und 4x4-Grids hinzufügen.
- Die Beispielwerte in [`packages/import/display48_device_base.yaml`](packages/import/display48_device_base.yaml) auf das echte öffentliche Repo und Release-Tags setzen.

## Lizenz

Der Quellcode dieses Repos steht unter der MIT-Lizenz. Siehe [`LICENSE`](LICENSE).

Das mitgelieferte Font-Asset unter [`assets/fonts/RobotoCondensed-Regular.ttf`](assets/fonts/RobotoCondensed-Regular.ttf)
ist ein Drittanbieter-Asset und bleibt unter seiner eigenen Lizenz. Siehe
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).
