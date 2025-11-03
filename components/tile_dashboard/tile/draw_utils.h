#ifndef DRAW_UTILS_H
#define DRAW_UTILS_H

#pragma once // einfacher Guard

#include "esphome/components/display/display.h"
#include "colors.h"

namespace esphome
{
  namespace tile_dashboard
  {

    // Utility function für Farbschattierung
    inline esphome::Color shade(esphome::Color c, int8_t delta)
    {
      auto clamp = [](int v)
      { return v < 0 ? 0 : (v > 255 ? 255 : v); };
      return esphome::Color(clamp(c.r + delta), clamp(c.g + delta), clamp(c.b + delta));
    }

  } // namespace tile_dashboard
} // namespace esphome

class DrawUtils
{
public:
    static void draw_rounded_rect(esphome::display::Display &it, int x, int y, int w, int h, int r, esphome::Color color)
    {
        // Mitte
        it.filled_rectangle(x + r, y + r, w - 2 * r, h - 2 * r, color);

        // Seiten
        it.filled_rectangle(x + r, y, w - 2 * r, r, color);         // oben
        it.filled_rectangle(x + r, y + h - r, w - 2 * r, r, color); // unten
        it.filled_rectangle(x, y + r, r, h - 2 * r, color);         // links
        it.filled_rectangle(x + w - r, y + r, r, h - 2 * r, color); // rechts

        // Ecken (volle Kreise)
        it.filled_circle(x + r, y + r, r, color);                 // oben links
        it.filled_circle(x + w - r - 1, y + r, r, color);         // oben rechts
        it.filled_circle(x + r, y + h - r - 1, r, color);         // unten links
        it.filled_circle(x + w - r - 1, y + h - r - 1, r, color); // unten rechts
    }
    static void draw_ring(esphome::display::Display &it,
                          int16_t cx, int16_t cy,
                          uint16_t r_outer, uint16_t r_inner,
                          esphome::Color col, esphome::Color back)
    {
        it.filled_circle(cx, cy, r_outer, col);
        it.filled_circle(cx, cy, r_inner, back);
    }
};

#endif // TILE_BG_RENDERER_H