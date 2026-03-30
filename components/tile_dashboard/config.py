from copy import deepcopy

import esphome.codegen as cg
from esphome.components import font
from esphome.core import ID

FONT_SIZES = (12, 14, 16, 18, 20, 25, 30, 35, 40, 45, 50, 60, 70, 80, 90)
DEFAULT_GLYPHS = [
    "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZÄÖÜabcdefghijklmnopqrstuvwxyzäöü"
    "!@#$%^&*()_+-=[]{}|;:',.<>?/\\\"€° "
]


def component_base_id(component_id):
    return getattr(component_id, "id", str(component_id))


def tile_position(tile):
    return tile["col"], tile["row"]


def validate_tile_bounds(tile, cols, rows):
    pos = tile_position(tile)
    if tile["col"] > cols or tile["row"] > rows:
        raise ValueError(
            f"Tile {tile['type']} at {pos} is outside the configured grid {cols}x{rows}"
        )
    return pos


def validate_tiles(config):
    positions = set()
    cols = config["cols"]
    rows = config["rows"]

    for tile in config["tiles"]:
        pos = validate_tile_bounds(tile, cols, rows)
        if pos in positions:
            raise ValueError(f"Duplicate tile position detected at {pos}")
        positions.add(pos)

    return config


def font_id_name(base_id, size):
    return f"{base_id}_font_{size}"


def raw_data_id_name(base_id, size):
    return f"{base_id}_font_data_{size}"


def raw_glyph_id_name(base_id, size):
    return f"{base_id}_font_glyphs_{size}"


def build_font_config(component_id, font_file, glyphs, size):
    base_id = component_base_id(component_id)
    return font.CONFIG_SCHEMA(
        {
            "id": ID(
                font_id_name(base_id, size),
                is_declaration=True,
                type=font.Font,
            ),
            "file": deepcopy(font_file),
            "size": size,
            "glyphs": list(glyphs),
            "raw_data_id": ID(
                raw_data_id_name(base_id, size),
                is_declaration=True,
                type=cg.uint8,
            ),
            "raw_glyph_id": ID(
                raw_glyph_id_name(base_id, size),
                is_declaration=True,
                type=font.GlyphData,
            ),
        }
    )


def build_font_configs(component_id, font_file, glyphs):
    return [
        build_font_config(component_id, font_file, glyphs, size)
        for size in FONT_SIZES
    ]
