import os
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ESPHOME_BIN = ROOT / ".venv" / ("Scripts" if os.name == "nt" else "bin") / (
    "esphome.exe" if os.name == "nt" else "esphome"
)


class TileDashboardE2ETests(unittest.TestCase):
    maxDiff = None

    def run_esphome(self, *args):
        result = subprocess.run(
            [str(ESPHOME_BIN), *args],
            cwd=ROOT,
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            self.fail(
                f"ESPHome command failed: {' '.join(args)}\n"
                f"stdout:\n{result.stdout}\n\nstderr:\n{result.stderr}"
            )
        return result

    def config_output(self, config_path):
        return self.run_esphome("config", config_path).stdout

    def generate_and_main_cpp(self, config_path, build_name):
        self.run_esphome("compile", "--only-generate", config_path)
        main_cpp = ROOT / "examples/.esphome/build" / build_name / "src/main.cpp"
        return main_cpp.read_text()

    def config_and_main_cpp(self, config_path, build_name):
        result = self.run_esphome("config", config_path)
        generated = self.generate_and_main_cpp(config_path, build_name)
        return result.stdout, generated

    def test_simulator_thin_config_generates_declarative_dashboard(self):
        stdout, generated = self.config_and_main_cpp(
            "examples/simulator_ha_2x2.yaml", "dashboard-simulator"
        )

        self.assertIn("tile_dashboard::TileDashboardComponent *dashboard_ui;", generated)
        self.assertIn("dashboard_ui->add_climate_tile", generated)
        self.assertIn("dashboard_ui->add_gauge_tile", generated)
        self.assertIn("dashboard_ui->add_double_value_tile", generated)
        self.assertIn("dashboard_ui->add_switch_tile", generated)
        self.assertIn("dashboard_ui->add_battery_tile", generated)
        self.assertIn("dashboard_ui->set_display(dashboard_display);", generated)
        self.assertIn("dashboard_ui->set_page_grid(0, 3, 3);", generated)
        self.assertIn("tile_dashboard:", stdout)
        self.assertNotIn("dashboard_font_bootstrap", stdout)

    def test_esp32_thin_config_validates_thin_device_shape(self):
        stdout = self.config_output("examples/display48norelay.refactored.yaml")

        self.assertIn("tile_dashboard:", stdout)
        self.assertIn("cols: 2", stdout)
        self.assertIn("rows: 2", stdout)
        self.assertIn("project_name: esphome.touch_widget", stdout)
        self.assertIn("tile_dashboard_components_source: ../components", stdout)
        self.assertNotIn("packages/layouts", stdout)
        self.assertNotIn("packages/profiles", stdout)

    def test_import_package_config_resolves_repo_relative_paths(self):
        stdout = self.config_output("examples/display48norelay.package_import.yaml")

        self.assertIn("tile_dashboard:", stdout)
        self.assertIn("project_name: esphome.touch_widget", stdout)
        self.assertIn("tile_dashboard_components_source: ../components", stdout)
        self.assertNotIn("packages/layouts", stdout)
        self.assertNotIn("packages/apps", stdout)

    def test_all_tiles_simulator_generates_all_tile_factories(self):
        stdout, generated = self.config_and_main_cpp(
            "examples/simulator_all_tiles.yaml", "dashboard-all-tiles"
        )

        self.assertIn("dashboard_ui->set_layout(800, 480, 4, 2, 0, 0);", generated)
        self.assertIn("dashboard_ui->add_battery_tile", generated)
        self.assertIn("dashboard_ui->add_text_value_tile", generated)
        self.assertIn("dashboard_ui->add_double_value_tile", generated)
        self.assertIn("dashboard_ui->add_gauge_tile", generated)
        self.assertIn("dashboard_ui->add_climate_tile", generated)
        self.assertIn("dashboard_ui->add_switch_tile", generated)
        self.assertIn("dashboard_ui->add_light_tile", generated)
        self.assertIn("tile_dashboard:", stdout)

    def test_showcase_3x3_generates_full_feature_grid(self):
        stdout, generated = self.config_and_main_cpp(
            "examples/simulator_showcase_3x3.yaml", "dashboard-3x3-showcase"
        )

        self.assertIn("dashboard_ui->set_layout(720, 720, 3, 3, 0, 0);", generated)
        self.assertIn("dashboard_ui->add_battery_tile", generated)
        self.assertIn("dashboard_ui->add_text_value_tile", generated)
        self.assertIn("dashboard_ui->add_double_value_tile", generated)
        self.assertIn("dashboard_ui->add_gauge_tile", generated)
        self.assertIn("dashboard_ui->add_climate_tile", generated)
        self.assertIn("dashboard_ui->add_switch_tile", generated)
        self.assertIn("dashboard_ui->add_light_tile", generated)
        self.assertIn("tile_dashboard:", stdout)

    def test_simulator_all_tiles_config_keeps_flexible_grid(self):
        stdout = self.config_output("examples/simulator_all_tiles.yaml")
        self.assertIn("cols: 4", stdout)
        self.assertIn("rows: 2", stdout)
        self.assertIn("dashboard_width: '800'", stdout)

    def test_showcase_3x3_config_keeps_flexible_grid(self):
        stdout = self.config_output("examples/simulator_showcase_3x3.yaml")
        self.assertIn("cols: 3", stdout)
        self.assertIn("rows: 3", stdout)
        self.assertIn("dashboard_width: '720'", stdout)

    def test_thin_examples_validate_without_profiles(self):
        for config_path in (
            "examples/simulator_ha_2x2.yaml",
            "examples/simulator_showcase_3x3.yaml",
            "examples/simulator_all_tiles.yaml",
            "examples/display48norelay.refactored.yaml",
            "examples/display48norelay.package_import.yaml",
        ):
            with self.subTest(config_path=config_path):
                self.run_esphome("config", config_path)

    def test_simulator_compile_succeeds(self):
        if os.environ.get("ESPHOME_RUN_SIM_COMPILE") != "1":
            self.skipTest("enable with ESPHOME_RUN_SIM_COMPILE=1")
        self.run_esphome("compile", "examples/simulator_ha_2x2.yaml")

    def test_simulator_showcase_compile_succeeds(self):
        if os.environ.get("ESPHOME_RUN_SIM_COMPILE") != "1":
            self.skipTest("enable with ESPHOME_RUN_SIM_COMPILE=1")
        self.run_esphome("compile", "examples/simulator_showcase_3x3.yaml")

    def test_simulator_all_tiles_compile_succeeds(self):
        if os.environ.get("ESPHOME_RUN_SIM_COMPILE") != "1":
            self.skipTest("enable with ESPHOME_RUN_SIM_COMPILE=1")
        self.run_esphome("compile", "examples/simulator_all_tiles.yaml")

    @unittest.skipUnless(
        os.environ.get("ESPHOME_RUN_ESP32_COMPILE") == "1",
        "slow hardware compile; enable with ESPHOME_RUN_ESP32_COMPILE=1",
    )
    def test_esp32_thin_compile_succeeds(self):
        self.run_esphome("compile", "examples/display48norelay.refactored.yaml")

    @unittest.skipUnless(
        os.environ.get("ESPHOME_RUN_ESP32_COMPILE") == "1",
        "slow hardware compile; enable with ESPHOME_RUN_ESP32_COMPILE=1",
    )
    def test_esp32_import_package_compile_succeeds(self):
        self.run_esphome("compile", "examples/display48norelay.package_import.yaml")


if __name__ == "__main__":
    unittest.main()
