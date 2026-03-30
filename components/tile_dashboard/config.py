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
    return tile.get("page", 0), tile["col"], tile["row"]


def get_page_grid(config, page):
    """Return (cols, rows) for a given page number."""
    for pc in config.get("page_configs", []):
        if pc["page"] == page:
            return pc["cols"], pc["rows"]
    return config["cols"], config["rows"]


def validate_tile_bounds(tile, cols, rows):
    pos = tile_position(tile)
    colspan = tile.get("colspan", 1)
    rowspan = tile.get("rowspan", 1)
    if tile["col"] + colspan - 1 > cols or tile["row"] + rowspan - 1 > rows:
        raise ValueError(
            f"Tile {tile['type']} at col={tile['col']}, row={tile['row']} "
            f"(span {colspan}x{rowspan}) exceeds grid {cols}x{rows}"
        )
    return pos


def validate_tiles(config):
    occupied = {}
    for tile in config["tiles"]:
        page = tile.get("page", 0)
        cols, rows = get_page_grid(config, page)
        colspan = tile.get("colspan", 1)
        rowspan = tile.get("rowspan", 1)
        col = tile["col"]
        row = tile["row"]

        if col + colspan - 1 > cols or row + rowspan - 1 > rows:
            raise ValueError(
                f"Tile {tile['type']} at col={col}, row={row} "
                f"(span {colspan}x{rowspan}) exceeds grid {cols}x{rows}"
            )

        for c in range(col, col + colspan):
            for r in range(row, row + rowspan):
                cell = (page, c, r)
                if cell in occupied:
                    raise ValueError(
                        f"Overlapping tiles at page={page}, col={c}, row={r}"
                    )
                occupied[cell] = tile

    return config


def font_id_name(base_id, size):
    return f"{base_id}_font_{size}"


def build_font_config(component_id, font_file, glyphs, size):
    base_id = component_base_id(component_id)
    glyph_type = getattr(font, "GlyphData", cg.uint8)
    cfg = {
        "id": ID(
            font_id_name(base_id, size),
            is_declaration=True,
            type=font.Font,
        ),
        "file": deepcopy(font_file),
        "size": size,
        "glyphs": list(glyphs),
        "raw_data_id": ID(
            f"{base_id}_font_data_{size}",
            is_declaration=True,
            type=cg.uint8,
        ),
        "raw_glyph_id": ID(
            f"{base_id}_font_glyphs_{size}",
            is_declaration=True,
            type=glyph_type,
        ),
    }
    return font.CONFIG_SCHEMA(cfg)


def build_font_configs(component_id, font_file, glyphs):
    return [
        build_font_config(component_id, font_file, glyphs, size)
        for size in FONT_SIZES
    ]
