import sys
import unittest
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from esphome.core import CORE  # noqa: E402

from components.tile_dashboard.config import (  # noqa: E402
    DEFAULT_GLYPHS,
    FONT_SIZES,
    build_font_config,
    build_font_configs,
    component_base_id,
    font_id_name,
    get_page_grid,
    tile_position,
    validate_tile_bounds,
    validate_tiles,
)


FONT_FILE = {"path": "../assets/fonts/RobotoCondensed-Regular.ttf", "type": "local"}
PIXEL_BUFFER_HEADER = ROOT / "components/tile_dashboard/pixel_buffer.h"


def rgb565(red, green, blue):
    return ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3)


def pixelbuffer_esp32_bytes(color565):
    swapped = ((color565 >> 8) | ((color565 & 0xFF) << 8)) & 0xFFFF
    return bytes((swapped & 0xFF, swapped >> 8))


def decode_display_565(raw_bytes, *, big_endian):
    if big_endian:
        return (raw_bytes[0] << 8) | raw_bytes[1]
    return raw_bytes[0] | (raw_bytes[1] << 8)


def clip_blit_geometry(dst_x, dst_y, width, height, clip_rect=None):
    src_x_offset = 0
    src_y_offset = 0
    src_x_pad = 0
    draw_x = dst_x
    draw_y = dst_y
    draw_w = width
    draw_h = height

    if clip_rect is not None:
        clip_x, clip_y, clip_w, clip_h = clip_rect
        left = max(draw_x, clip_x)
        top = max(draw_y, clip_y)
        right = min(draw_x + draw_w, clip_x + clip_w)
        bottom = min(draw_y + draw_h, clip_y + clip_h)
        if left >= right or top >= bottom:
            return None

        src_x_offset = left - draw_x
        src_y_offset = top - draw_y
        draw_w = right - left
        draw_h = bottom - top
        draw_x = left
        draw_y = top
        src_x_pad = width - draw_w - src_x_offset

    return {
        "dst_x": draw_x,
        "dst_y": draw_y,
        "draw_w": draw_w,
        "draw_h": draw_h,
        "src_x_offset": src_x_offset,
        "src_y_offset": src_y_offset,
        "src_x_pad": src_x_pad,
    }


class TileDashboardUnitTests(unittest.TestCase):
    def setUp(self):
        self.old_config_path = CORE.config_path
        CORE.config_path = str(ROOT / "examples/simulator_ha_2x2.yaml")
        self.addCleanup(setattr, CORE, "config_path", self.old_config_path)

    def test_component_base_id_uses_object_id_attribute(self):
        obj = type("Obj", (), {"id": "dashboard_ui"})()
        self.assertEqual(component_base_id(obj), "dashboard_ui")

    def test_component_base_id_falls_back_to_string(self):
        self.assertEqual(component_base_id("dashboard_ui"), "dashboard_ui")

    def test_tile_position_returns_grid_tuple(self):
        self.assertEqual(tile_position({"col": 3, "row": 2}), (0, 3, 2))
        self.assertEqual(tile_position({"col": 3, "row": 2, "page": 1}), (1, 3, 2))

    def test_validate_tile_bounds_accepts_in_grid_tile(self):
        tile = {"type": "switch", "col": 2, "row": 3}
        self.assertEqual(validate_tile_bounds(tile, 2, 3), (0, 2, 3))

    def test_validate_tile_bounds_accepts_spanning_tile(self):
        tile = {"type": "climate", "col": 1, "row": 1, "colspan": 2, "rowspan": 2}
        self.assertEqual(validate_tile_bounds(tile, 3, 3), (0, 1, 1))

    def test_validate_tile_bounds_rejects_span_exceeding_grid(self):
        with self.assertRaisesRegex(ValueError, "exceeds grid 2x2"):
            validate_tile_bounds(
                {"type": "climate", "col": 1, "row": 1, "colspan": 3, "rowspan": 1}, 2, 2
            )

    def test_validate_tile_bounds_rejects_out_of_grid_column(self):
        with self.assertRaisesRegex(ValueError, "exceeds grid 2x2"):
            validate_tile_bounds({"type": "switch", "col": 3, "row": 1}, 2, 2)

    def test_validate_tile_bounds_rejects_out_of_grid_row(self):
        with self.assertRaisesRegex(ValueError, "exceeds grid 2x2"):
            validate_tile_bounds({"type": "switch", "col": 1, "row": 3}, 2, 2)

    def test_get_page_grid_returns_default(self):
        config = {"cols": 2, "rows": 2}
        self.assertEqual(get_page_grid(config, 0), (2, 2))
        self.assertEqual(get_page_grid(config, 1), (2, 2))

    def test_get_page_grid_returns_override(self):
        config = {"cols": 2, "rows": 2, "page_configs": [{"page": 1, "cols": 3, "rows": 3}]}
        self.assertEqual(get_page_grid(config, 0), (2, 2))
        self.assertEqual(get_page_grid(config, 1), (3, 3))

    def test_validate_tiles_accepts_unique_positions(self):
        config = {
            "cols": 2,
            "rows": 2,
            "tiles": [
                {"type": "climate", "col": 1, "row": 1},
                {"type": "switch", "col": 2, "row": 1},
            ],
        }
        self.assertIs(validate_tiles(config), config)

    def test_validate_tiles_accepts_full_grid(self):
        config = {
            "cols": 2,
            "rows": 2,
            "tiles": [
                {"type": "climate", "col": 1, "row": 1},
                {"type": "switch", "col": 2, "row": 1},
                {"type": "battery", "col": 1, "row": 2},
                {"type": "gauge", "col": 2, "row": 2},
            ],
        }
        self.assertIs(validate_tiles(config), config)

    def test_validate_tiles_accepts_spanning_tile_with_normal(self):
        config = {
            "cols": 3,
            "rows": 3,
            "tiles": [
                {"type": "climate", "col": 1, "row": 1, "colspan": 2, "rowspan": 2},
                {"type": "switch", "col": 3, "row": 1},
                {"type": "gauge", "col": 3, "row": 2},
                {"type": "battery", "col": 1, "row": 3},
                {"type": "text_value", "col": 2, "row": 3},
            ],
        }
        self.assertIs(validate_tiles(config), config)

    def test_validate_tiles_rejects_overlapping_span(self):
        with self.assertRaisesRegex(ValueError, "Overlapping tiles"):
            validate_tiles(
                {
                    "cols": 3,
                    "rows": 3,
                    "tiles": [
                        {"type": "climate", "col": 1, "row": 1, "colspan": 2, "rowspan": 2},
                        {"type": "switch", "col": 2, "row": 2},
                    ],
                }
            )

    def test_validate_tiles_rejects_duplicate_positions(self):
        with self.assertRaisesRegex(ValueError, "Overlapping tiles"):
            validate_tiles(
                {
                    "cols": 2,
                    "rows": 2,
                    "tiles": [
                        {"type": "climate", "col": 1, "row": 1},
                        {"type": "switch", "col": 1, "row": 1},
                    ],
                }
            )

    def test_validate_tiles_rejects_out_of_grid(self):
        with self.assertRaisesRegex(ValueError, "exceeds grid"):
            validate_tiles(
                {
                    "cols": 2,
                    "rows": 2,
                    "tiles": [{"type": "switch", "col": 3, "row": 1}],
                }
            )

    def test_validate_tiles_uses_page_grid(self):
        config = {
            "cols": 2,
            "rows": 2,
            "page_configs": [{"page": 1, "cols": 3, "rows": 3}],
            "tiles": [
                {"type": "climate", "col": 1, "row": 1, "page": 0},
                {"type": "switch", "col": 3, "row": 3, "page": 1},
            ],
        }
        self.assertIs(validate_tiles(config), config)

    def test_validate_tiles_rejects_tile_exceeding_page_grid(self):
        with self.assertRaisesRegex(ValueError, "exceeds grid 2x2"):
            validate_tiles(
                {
                    "cols": 2,
                    "rows": 2,
                    "tiles": [{"type": "switch", "col": 3, "row": 1, "page": 0}],
                }
            )

    def test_validate_tiles_same_position_different_pages(self):
        config = {
            "cols": 2,
            "rows": 2,
            "tiles": [
                {"type": "climate", "col": 1, "row": 1, "page": 0},
                {"type": "switch", "col": 1, "row": 1, "page": 1},
            ],
        }
        self.assertIs(validate_tiles(config), config)

    def test_font_id_name_uses_expected_convention(self):
        self.assertEqual(font_id_name("dashboard_ui", 45), "dashboard_ui_font_45")

    def test_build_font_config_generates_expected_ids(self):
        config = build_font_config("dashboard_ui", FONT_FILE, DEFAULT_GLYPHS, 35)
        self.assertEqual(str(config["id"]), "dashboard_ui_font_35")
        self.assertEqual(str(config["raw_data_id"]), "dashboard_ui_font_data_35")
        self.assertEqual(str(config["raw_glyph_id"]), "dashboard_ui_font_glyphs_35")

    def test_build_font_config_copies_font_file(self):
        font_file = dict(FONT_FILE)
        config = build_font_config("dashboard_ui", font_file, DEFAULT_GLYPHS, 25)
        font_file["path"] = "mutated"
        self.assertNotEqual(config["file"]["path"], "mutated")

    def test_build_font_config_copies_glyphs(self):
        glyphs = list(DEFAULT_GLYPHS)
        config = build_font_config("dashboard_ui", FONT_FILE, glyphs, 25)
        glyphs.append("XYZ")
        self.assertNotIn("XYZ", config["glyphs"])

    def test_build_font_config_uses_requested_size(self):
        config = build_font_config("dashboard_ui", FONT_FILE, DEFAULT_GLYPHS, 80)
        self.assertEqual(config["size"], 80)

    def test_build_font_configs_generates_all_sizes(self):
        configs = build_font_configs("dashboard_ui", FONT_FILE, DEFAULT_GLYPHS)

        self.assertEqual(len(configs), len(FONT_SIZES))
        self.assertEqual([conf["size"] for conf in configs], list(FONT_SIZES))

    def test_build_font_configs_generate_unique_ids(self):
        configs = build_font_configs("dashboard_ui", FONT_FILE, DEFAULT_GLYPHS)
        self.assertEqual(len({str(conf["id"]) for conf in configs}), len(FONT_SIZES))

    def test_build_font_configs_keep_local_file_reference(self):
        configs = build_font_configs("dashboard_ui", FONT_FILE, DEFAULT_GLYPHS)
        self.assertTrue(
            all(
                conf["file"]["path"].endswith("assets/fonts/RobotoCondensed-Regular.ttf")
                for conf in configs
            )
        )

    def test_pixelbuffer_esp32_bytes_require_big_endian_decode(self):
        color565 = rgb565(0xF2, 0x85, 0x00)
        raw_bytes = pixelbuffer_esp32_bytes(color565)

        self.assertEqual(decode_display_565(raw_bytes, big_endian=True), color565)
        self.assertNotEqual(decode_display_565(raw_bytes, big_endian=False), color565)

    def test_pixelbuffer_blit_marks_rgb565_buffer_big_endian(self):
        header = PIXEL_BUFFER_HEADER.read_text()

        self.assertRegex(
            header,
            re.compile(r"COLOR_BITNESS_565,\s*true\s*,"),
            "PixelBuffer::blit must declare RGB565 data as big-endian to match to_565() on ESP32.",
        )

    def test_clip_blit_geometry_keeps_full_buffer_without_clip(self):
        self.assertEqual(
            clip_blit_geometry(10, 20, 80, 40),
            {
                "dst_x": 10,
                "dst_y": 20,
                "draw_w": 80,
                "draw_h": 40,
                "src_x_offset": 0,
                "src_y_offset": 0,
                "src_x_pad": 0,
            },
        )

    def test_clip_blit_geometry_crops_left_and_top_edges(self):
        self.assertEqual(
            clip_blit_geometry(10, 20, 80, 40, clip_rect=(30, 35, 100, 100)),
            {
                "dst_x": 30,
                "dst_y": 35,
                "draw_w": 60,
                "draw_h": 25,
                "src_x_offset": 20,
                "src_y_offset": 15,
                "src_x_pad": 0,
            },
        )

    def test_clip_blit_geometry_crops_right_edge_and_sets_x_pad(self):
        self.assertEqual(
            clip_blit_geometry(10, 20, 80, 40, clip_rect=(30, 20, 30, 40)),
            {
                "dst_x": 30,
                "dst_y": 20,
                "draw_w": 30,
                "draw_h": 40,
                "src_x_offset": 20,
                "src_y_offset": 0,
                "src_x_pad": 30,
            },
        )

    def test_clip_blit_geometry_returns_none_when_fully_outside_clip(self):
        self.assertIsNone(clip_blit_geometry(10, 20, 80, 40, clip_rect=(200, 200, 20, 20)))

    def test_pixelbuffer_blit_uses_clip_offsets_with_draw_pixels(self):
        header = PIXEL_BUFFER_HEADER.read_text()

        self.assertIn("disp.get_clipping()", header)
        self.assertRegex(header, re.compile(r"draw_pixels_at\([\s\S]*src_x_offset, src_y_offset, src_x_pad\)"))


if __name__ == "__main__":
    unittest.main()
