# components/tile_dashboard/__init__.py
import esphome.codegen as cg
import esphome.config_validation as cv

# Für Header-Only Komponenten brauchen wir nur ein minimales Schema
DEPENDENCIES = []

CONFIG_SCHEMA = cv.Schema({})


async def to_code(config):
    # Nichts zu tun - die Header werden automatisch inkludiert
    # wenn sie in display lambdas verwendet werden
    pass

