import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from esphome.core import CORE  # noqa: E402

from components.tile_dashboard import (  # noqa: E402
    CONFIG_SCHEMA,
    CONF_BOTTOM_UNIT,
    CONF_FORMAT,
    CONF_GLYPHS,
    CONF_HEIGHT,
    CONF_MAX_VALUE,
    CONF_MIN_VALUE,
    CONF_OFFSET_X,
    CONF_OFFSET_Y,
    CONF_RED_THRESHOLD,
    CONF_TOP_UNIT,
    CONF_TOUCH_ROTATION,
    CONF_WIDTH,
    CONF_YELLOW_THRESHOLD,
)
from components.tile_dashboard.config import DEFAULT_GLYPHS  # noqa: E402


FONT_FILE = {"path": "../assets/fonts/RobotoCondensed-Regular.ttf", "type": "local"}


class TileDashboardSchemaTests(unittest.TestCase):
    def setUp(self):
        self.old_config_path = CORE.config_path
        CORE.config_path = Path(ROOT / "examples/simulator_ha_2x2.yaml")
        self.addCleanup(setattr, CORE, "config_path", self.old_config_path)

    def base_config(self, tiles, **overrides):
        config = {
            "id": "dashboard_ui",
            "display": "dashboard_display",
            "touchscreen": "dashboard_touchscreen",
            "cols": 2,
            "rows": 2,
            "font_file": dict(FONT_FILE),
            "tiles": tiles,
        }
        config.update(overrides)
        return config

    def test_schema_applies_component_defaults(self):
        config = CONFIG_SCHEMA(
            self.base_config(
                [
                    {
                        "type": "switch",
                        "col": 1,
                        "row": 1,
                        "label": "LIGHT",
                        "switch": "dashboard_light_switch",
                        "entity_id": "switch.light",
                    }
                ]
            )
        )

        self.assertEqual(config[CONF_WIDTH], 0)
        self.assertEqual(config[CONF_HEIGHT], 0)
        self.assertEqual(config[CONF_OFFSET_X], 0)
        self.assertEqual(config[CONF_OFFSET_Y], 0)
        self.assertEqual(config[CONF_TOUCH_ROTATION], 0)
        self.assertEqual(config[CONF_GLYPHS], DEFAULT_GLYPHS)

    def test_schema_accepts_optional_touchscreen_omitted(self):
        config = self.base_config(
            [
                {
                    "type": "switch",
                    "col": 1,
                    "row": 1,
                    "label": "LIGHT",
                    "switch": "dashboard_light_switch",
                    "entity_id": "switch.light",
                }
            ]
        )
        config.pop("touchscreen")
        validated = CONFIG_SCHEMA(config)
        self.assertNotIn("touchscreen", validated)

    def test_schema_preserves_explicit_dimensions_and_offsets(self):
        validated = CONFIG_SCHEMA(
            self.base_config(
                [
                    {
                        "type": "switch",
                        "col": 1,
                        "row": 1,
                        "label": "LIGHT",
                        "switch": "dashboard_light_switch",
                        "entity_id": "switch.light",
                    }
                ],
                width=640,
                height=384,
                offset_x=12,
                offset_y=24,
                touch_rotation=180,
            )
        )

        self.assertEqual(validated[CONF_WIDTH], 640)
        self.assertEqual(validated[CONF_HEIGHT], 384)
        self.assertEqual(validated[CONF_OFFSET_X], 12)
        self.assertEqual(validated[CONF_OFFSET_Y], 24)
        self.assertEqual(validated[CONF_TOUCH_ROTATION], 180)

    def test_schema_applies_text_value_defaults(self):
        validated = CONFIG_SCHEMA(
            self.base_config(
                [
                    {
                        "type": "text_value",
                        "col": 1,
                        "row": 1,
                        "label": "TEMP",
                        "sensor": "room_temperature",
                    }
                ]
            )
        )

        tile = validated["tiles"][0]
        self.assertEqual(tile["unit"], "")
        self.assertEqual(tile[CONF_FORMAT], "%.1f")

    def test_schema_applies_double_value_defaults(self):
        validated = CONFIG_SCHEMA(
            self.base_config(
                [
                    {
                        "type": "double_value",
                        "col": 1,
                        "row": 1,
                        "top_label": "TOP",
                        "bottom_label": "BOTTOM",
                        "top_sensor": "sensor_top",
                        "bottom_sensor": "sensor_bottom",
                    }
                ]
            )
        )

        tile = validated["tiles"][0]
        self.assertEqual(tile[CONF_TOP_UNIT], "")
        self.assertEqual(tile[CONF_BOTTOM_UNIT], "")
        self.assertEqual(tile["top_format"], "%.1f")
        self.assertEqual(tile["bottom_format"], "%.1f")

    def test_schema_applies_gauge_defaults(self):
        validated = CONFIG_SCHEMA(
            self.base_config(
                [
                    {
                        "type": "gauge",
                        "col": 1,
                        "row": 1,
                        "label": "SOLAR",
                        "sensor": "solar_power",
                    }
                ]
            )
        )

        tile = validated["tiles"][0]
        self.assertEqual(tile[CONF_MIN_VALUE], 0.0)
        self.assertEqual(tile[CONF_MAX_VALUE], 100.0)
        self.assertEqual(tile[CONF_RED_THRESHOLD], 20.0)
        self.assertEqual(tile[CONF_YELLOW_THRESHOLD], 50.0)
        self.assertEqual(tile["unit"], "%")
        self.assertEqual(tile[CONF_FORMAT], "%.1f")

    def test_schema_accepts_battery_tile(self):
        validated = CONFIG_SCHEMA(
            self.base_config(
                [
                    {
                        "type": "battery",
                        "col": 1,
                        "row": 1,
                        "sensor": "battery_percent",
                    }
                ]
            )
        )
        self.assertEqual(validated["tiles"][0]["label"], "BATTERY")

    def test_schema_accepts_light_tile(self):
        validated = CONFIG_SCHEMA(
            self.base_config(
                [
                    {
                        "type": "light",
                        "col": 1,
                        "row": 1,
                        "label": "LIGHT",
                        "state": "demo_light_state",
                        "entity_id": "light.demo_light",
                    }
                ]
            )
        )
        self.assertEqual(validated["tiles"][0]["entity_id"], "light.demo_light")

    def test_schema_rejects_duplicate_positions(self):
        with self.assertRaisesRegex(Exception, "Overlapping tiles"):
            CONFIG_SCHEMA(
                self.base_config(
                    [
                        {
                            "type": "switch",
                            "col": 1,
                            "row": 1,
                            "label": "ONE",
                            "switch": "sw1",
                            "entity_id": "switch.one",
                        },
                        {
                            "type": "switch",
                            "col": 1,
                            "row": 1,
                            "label": "TWO",
                            "switch": "sw2",
                            "entity_id": "switch.two",
                        },
                    ]
                )
            )

    def test_schema_rejects_tile_outside_grid(self):
        with self.assertRaisesRegex(Exception, "exceeds grid"):
            CONFIG_SCHEMA(
                self.base_config(
                    [
                        {
                            "type": "switch",
                            "col": 3,
                            "row": 1,
                            "label": "LIGHT",
                            "switch": "dashboard_light_switch",
                            "entity_id": "switch.light",
                        }
                    ]
                )
            )

    def test_schema_rejects_invalid_touch_rotation(self):
        with self.assertRaisesRegex(Exception, "valid options are '0', '90', '180', '270'"):
            CONFIG_SCHEMA(
                self.base_config(
                    [
                        {
                            "type": "switch",
                            "col": 1,
                            "row": 1,
                            "label": "LIGHT",
                            "switch": "dashboard_light_switch",
                            "entity_id": "switch.light",
                        }
                    ],
                    touch_rotation=45,
                )
            )

    def test_schema_requires_at_least_one_tile(self):
        with self.assertRaisesRegex(Exception, "length of value must be at least 1"):
            CONFIG_SCHEMA(self.base_config([]))

    def test_schema_rejects_unknown_tile_type(self):
        with self.assertRaisesRegex(Exception, "unknown"):
            CONFIG_SCHEMA(
                self.base_config(
                    [
                        {
                            "type": "unknown",
                            "col": 1,
                            "row": 1,
                        }
                    ]
                )
            )


if __name__ == "__main__":
    unittest.main()
