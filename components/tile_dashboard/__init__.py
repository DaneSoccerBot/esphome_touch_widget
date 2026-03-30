import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import display, font, sensor, switch as switch_, text_sensor, touchscreen
from esphome.const import CONF_DISPLAY, CONF_GLYPHS, CONF_HEIGHT, CONF_ID, CONF_OFFSET_X, CONF_OFFSET_Y, CONF_TYPE, CONF_WIDTH
from esphome.core import CORE

from .config import DEFAULT_GLYPHS, build_font_configs, validate_tiles

DEPENDENCIES = ["display"]

CONF_BOTTOM_LABEL = "bottom_label"
CONF_BOTTOM_SENSOR = "bottom_sensor"
CONF_BOTTOM_UNIT = "bottom_unit"
CONF_COL = "col"
CONF_COLS = "cols"
CONF_ENTITY_ID = "entity_id"
CONF_FONT_FILE = "font_file"
CONF_FORMAT = "format"
CONF_LABEL = "label"
CONF_MAX_VALUE = "max_value"
CONF_MIN_VALUE = "min_value"
CONF_PAYLOAD = "payload"
CONF_RED_THRESHOLD = "red_threshold"
CONF_ROW = "row"
CONF_ROWS = "rows"
CONF_SENSOR = "sensor"
CONF_STATE = "state"
CONF_SWITCH = "switch"
CONF_TILES = "tiles"
CONF_TOP_LABEL = "top_label"
CONF_TOP_SENSOR = "top_sensor"
CONF_TOP_UNIT = "top_unit"
CONF_TOUCH_ROTATION = "touch_rotation"
CONF_TOUCHSCREEN = "touchscreen"
CONF_UNIT = "unit"
CONF_YELLOW_THRESHOLD = "yellow_threshold"

AUTO_INCLUDES = (
    "esphome/components/tile_dashboard/tile_dashboard.h",
    "esphome/components/tile_dashboard/tile_dashboard_component.h",
    "esphome/components/tile_dashboard/display_context.h",
    "esphome/components/tile_dashboard/colors.h",
    "esphome/components/tile_dashboard/integration.h",
    "esphome/components/tile_dashboard/battery_tile.h",
    "esphome/components/tile_dashboard/climate_tile.h",
    "esphome/components/tile_dashboard/double_value_tile.h",
    "esphome/components/tile_dashboard/gauge_tile.h",
    "esphome/components/tile_dashboard/light_tile.h",
    "esphome/components/tile_dashboard/switch_tile.h",
    "esphome/components/tile_dashboard/text_value_tile.h",
)

tile_dashboard_ns = cg.esphome_ns.namespace("tile_dashboard")
TileDashboardComponent = tile_dashboard_ns.class_(
    "TileDashboardComponent", cg.Component, touchscreen.TouchListener
)


def _validate_tiles(config):
    try:
        return validate_tiles(config)
    except ValueError as err:
        raise cv.Invalid(str(err)) from err


TILE_POSITION_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_COL): cv.int_range(min=1),
        cv.Required(CONF_ROW): cv.int_range(min=1),
    }
)

BATTERY_TILE_SCHEMA = TILE_POSITION_SCHEMA.extend(
    {
        cv.Optional(CONF_LABEL, default="BATTERY"): cv.string_strict,
        cv.Required(CONF_SENSOR): cv.use_id(sensor.Sensor),
    }
)

TEXT_VALUE_TILE_SCHEMA = TILE_POSITION_SCHEMA.extend(
    {
        cv.Required(CONF_LABEL): cv.string_strict,
        cv.Required(CONF_SENSOR): cv.use_id(sensor.Sensor),
        cv.Optional(CONF_UNIT, default=""): cv.string_strict,
        cv.Optional(CONF_FORMAT, default="%.1f"): cv.string_strict,
    }
)

DOUBLE_VALUE_TILE_SCHEMA = TILE_POSITION_SCHEMA.extend(
    {
        cv.Required(CONF_TOP_LABEL): cv.string_strict,
        cv.Required(CONF_BOTTOM_LABEL): cv.string_strict,
        cv.Required(CONF_TOP_SENSOR): cv.use_id(sensor.Sensor),
        cv.Required(CONF_BOTTOM_SENSOR): cv.use_id(sensor.Sensor),
        cv.Optional(CONF_TOP_UNIT, default=""): cv.string_strict,
        cv.Optional(CONF_BOTTOM_UNIT, default=""): cv.string_strict,
        cv.Optional("top_format", default="%.1f"): cv.string_strict,
        cv.Optional("bottom_format", default="%.1f"): cv.string_strict,
    }
)

GAUGE_TILE_SCHEMA = TILE_POSITION_SCHEMA.extend(
    {
        cv.Required(CONF_LABEL): cv.string_strict,
        cv.Required(CONF_SENSOR): cv.use_id(sensor.Sensor),
        cv.Optional(CONF_MIN_VALUE, default=0.0): cv.float_,
        cv.Optional(CONF_MAX_VALUE, default=100.0): cv.float_,
        cv.Optional(CONF_RED_THRESHOLD, default=20.0): cv.float_,
        cv.Optional(CONF_YELLOW_THRESHOLD, default=50.0): cv.float_,
        cv.Optional(CONF_UNIT, default="%"): cv.string_strict,
        cv.Optional(CONF_FORMAT, default="%.1f"): cv.string_strict,
    }
)

CLIMATE_TILE_SCHEMA = TILE_POSITION_SCHEMA.extend(
    {
        cv.Required(CONF_LABEL): cv.string_strict,
        cv.Required(CONF_PAYLOAD): cv.use_id(text_sensor.TextSensor),
        cv.Required(CONF_ENTITY_ID): cv.string_strict,
    }
)

SWITCH_TILE_SCHEMA = TILE_POSITION_SCHEMA.extend(
    {
        cv.Required(CONF_LABEL): cv.string_strict,
        cv.Required(CONF_SWITCH): cv.use_id(switch_.Switch),
        cv.Required(CONF_ENTITY_ID): cv.string_strict,
    }
)

LIGHT_TILE_SCHEMA = TILE_POSITION_SCHEMA.extend(
    {
        cv.Required(CONF_LABEL): cv.string_strict,
        cv.Required(CONF_STATE): cv.use_id(text_sensor.TextSensor),
        cv.Required(CONF_ENTITY_ID): cv.string_strict,
    }
)

TILE_SCHEMA = cv.typed_schema(
    {
        "battery": BATTERY_TILE_SCHEMA,
        "text_value": TEXT_VALUE_TILE_SCHEMA,
        "double_value": DOUBLE_VALUE_TILE_SCHEMA,
        "gauge": GAUGE_TILE_SCHEMA,
        "climate": CLIMATE_TILE_SCHEMA,
        "switch": SWITCH_TILE_SCHEMA,
        "light": LIGHT_TILE_SCHEMA,
    }
)

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(TileDashboardComponent),
            cv.Required(CONF_DISPLAY): cv.use_id(display.Display),
            cv.Optional(CONF_TOUCHSCREEN): cv.use_id(touchscreen.Touchscreen),
            cv.Required(CONF_COLS): cv.int_range(min=1),
            cv.Required(CONF_ROWS): cv.int_range(min=1),
            cv.Optional(CONF_WIDTH, default=0): cv.int_range(min=0),
            cv.Optional(CONF_HEIGHT, default=0): cv.int_range(min=0),
            cv.Optional(CONF_OFFSET_X, default=0): cv.int_,
            cv.Optional(CONF_OFFSET_Y, default=0): cv.int_,
            cv.Optional(CONF_TOUCH_ROTATION, default=0): cv.one_of(0, 90, 180, 270, int=True),
            cv.Optional(CONF_FONT_FILE, default="gfonts://Roboto Condensed"): font.font_file_schema,
            cv.Optional(CONF_GLYPHS, default=DEFAULT_GLYPHS): cv.ensure_list(cv.string_strict),
            cv.Required(CONF_TILES): cv.All(cv.ensure_list(TILE_SCHEMA), cv.Length(min=1)),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    _validate_tiles,
)


async def _generate_fonts(config):
    font_vars = []
    for font_config in build_font_configs(
        config[CONF_ID], config[CONF_FONT_FILE], config[CONF_GLYPHS]
    ):
        await font.to_code(font_config)
        font_vars.append(await cg.get_variable(font_config[CONF_ID]))
    return font_vars


async def to_code(config):
    CORE.loaded_integrations.add("font")
    cg.add_define("USE_TILE_DASHBOARD")
    for header in AUTO_INCLUDES:
        cg.add_global(cg.RawStatement(f'#include "{header}"'))

    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    display_var = await cg.get_variable(config[CONF_DISPLAY])
    cg.add(var.set_display(display_var))

    if CONF_TOUCHSCREEN in config:
        touchscreen_var = await cg.get_variable(config[CONF_TOUCHSCREEN])
        cg.add(var.set_touchscreen(touchscreen_var))

    cg.add(
        var.set_layout(
            config[CONF_WIDTH],
            config[CONF_HEIGHT],
            config[CONF_COLS],
            config[CONF_ROWS],
            config[CONF_OFFSET_X],
            config[CONF_OFFSET_Y],
        )
    )
    cg.add(var.set_touch_rotation(config[CONF_TOUCH_ROTATION]))

    fonts = await _generate_fonts(config)
    cg.add(var.set_roboto_fonts(*fonts))

    for tile in config[CONF_TILES]:
        tile_type = tile[CONF_TYPE]
        if tile_type == "battery":
            sensor_var = await cg.get_variable(tile[CONF_SENSOR])
            cg.add(
                var.add_battery_tile(
                    tile[CONF_COL],
                    tile[CONF_ROW],
                    tile[CONF_LABEL],
                    sensor_var,
                )
            )
        elif tile_type == "text_value":
            sensor_var = await cg.get_variable(tile[CONF_SENSOR])
            cg.add(
                var.add_text_value_tile(
                    tile[CONF_COL],
                    tile[CONF_ROW],
                    tile[CONF_LABEL],
                    tile[CONF_UNIT],
                    tile[CONF_FORMAT],
                    sensor_var,
                )
            )
        elif tile_type == "double_value":
            top_sensor = await cg.get_variable(tile[CONF_TOP_SENSOR])
            bottom_sensor = await cg.get_variable(tile[CONF_BOTTOM_SENSOR])
            cg.add(
                var.add_double_value_tile(
                    tile[CONF_COL],
                    tile[CONF_ROW],
                    tile[CONF_TOP_LABEL],
                    tile[CONF_BOTTOM_LABEL],
                    tile[CONF_TOP_UNIT],
                    tile[CONF_BOTTOM_UNIT],
                    tile["top_format"],
                    tile["bottom_format"],
                    top_sensor,
                    bottom_sensor,
                )
            )
        elif tile_type == "gauge":
            sensor_var = await cg.get_variable(tile[CONF_SENSOR])
            cg.add(
                var.add_gauge_tile(
                    tile[CONF_COL],
                    tile[CONF_ROW],
                    tile[CONF_LABEL],
                    sensor_var,
                    tile[CONF_MIN_VALUE],
                    tile[CONF_MAX_VALUE],
                    tile[CONF_RED_THRESHOLD],
                    tile[CONF_YELLOW_THRESHOLD],
                    tile[CONF_UNIT],
                    tile[CONF_FORMAT],
                )
            )
        elif tile_type == "climate":
            payload = await cg.get_variable(tile[CONF_PAYLOAD])
            cg.add(
                var.add_climate_tile(
                    tile[CONF_COL],
                    tile[CONF_ROW],
                    tile[CONF_LABEL],
                    payload,
                    tile[CONF_ENTITY_ID],
                )
            )
        elif tile_type == "switch":
            switch_var = await cg.get_variable(tile[CONF_SWITCH])
            cg.add(
                var.add_switch_tile(
                    tile[CONF_COL],
                    tile[CONF_ROW],
                    tile[CONF_LABEL],
                    switch_var,
                    tile[CONF_ENTITY_ID],
                )
            )
        elif tile_type == "light":
            state = await cg.get_variable(tile[CONF_STATE])
            cg.add(
                var.add_light_tile(
                    tile[CONF_COL],
                    tile[CONF_ROW],
                    tile[CONF_LABEL],
                    state,
                    tile[CONF_ENTITY_ID],
                )
            )
