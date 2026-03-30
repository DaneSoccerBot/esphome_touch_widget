#ifndef SWITCH_ICON_RENDERER_H
#define SWITCH_ICON_RENDERER_H

#pragma once

#include "esphome/components/display/display.h"
#include "esphome/core/color.h"
#include "draw_utils.h" // DrawUtils::draw_rounded_rect
#include "colors.h"

namespace esphome
{
  namespace tile_dashboard
  {
    // Local shade function to avoid linker issues
    static inline esphome::Color shade_local(esphome::Color c, int8_t delta)
    {
      auto clamp = [](int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); };
      return esphome::Color(clamp(c.r + delta), clamp(c.g + delta), clamp(c.b + delta));
    }

    class SwitchIconRenderer
    {
    public:
      static void draw(esphome::display::Display &it,
                       int16_t x, int16_t y,
                       uint16_t w, uint16_t h,
                       bool on)
      {
        const uint16_t radius = w * 0.15;
        const uint8_t border = w * 0.05;

        const auto col_face = Colors::LIGHT_GREY;
        const auto col_frame = Colors::TILE_BORDER;
        const auto col_high = shade_local(col_frame, 30);
        const auto col_low = shade_local(col_frame, -40);

        /* ───── Rahmen & Face ───── */
        DrawUtils::draw_rounded_rect(it, x, y, w, h, radius, col_frame);
        // DrawUtils::draw_rounded_rect(it, x, y, w, h, radius, col_high);
        // DrawUtils::draw_rounded_rect(it, x+1, y+1, w-2, h-2, radius, col_low);

        const int16_t ix = x + border;
        const int16_t iy = y + border;
        const uint16_t iw = w - 2 * border;
        const uint16_t ih = h - 2 * border;
        const uint16_t inner_r = w * 0.12;
        DrawUtils::draw_rounded_rect(it, ix, iy, iw, ih, inner_r, col_face);

        /* ───── Farbbalken (respektiert Rundung) ───── */
        const uint16_t half = ih / 2;
        const auto state_col = on ? Colors::GREEN : Colors::RED;

        if (on)
        {
          // Unten: grau
          DrawUtils::draw_rounded_rect(it, ix, iy + half, iw, ih - half, inner_r, col_face);
          // Oben: grün
          DrawUtils::draw_rounded_rect(it, ix, iy, iw, half * 1.2, inner_r, Colors::GREEN);
        }
        else
        {
          // Oben: grau
          DrawUtils::draw_rounded_rect(it, ix, iy, iw, half, inner_r, col_face);
          // Unten: rot
          DrawUtils::draw_rounded_rect(it, ix, iy + half * 0.8, iw, ih - half * 0.8, inner_r, Colors::RED);
        }

        /* ───── Power‑Symbol ───── */
        const int16_t cx = x + w / 2;
        const int16_t cy = on ? (iy + half * 0.6) : (iy + half * 1.45);
        const uint16_t outer = w * 0.26;
        const uint16_t inner = w * 0.18;
        DrawUtils::draw_ring(it, cx, cy, outer, inner, Colors::NORMAL_TEXT, state_col);

        const int16_t barWidth = w * 0.08;
        const int16_t barHeight = w * 0.25;
        const int16_t barOutline = w * 0.04;

        it.filled_rectangle(cx - barWidth / 2 - barOutline, cy - barHeight * 0.7 - outer / 2 - barOutline, barWidth + barOutline * 2, barHeight + barOutline * 2, state_col);
        it.filled_rectangle(cx - barWidth / 2, cy - barHeight * 0.7 - outer / 2, barWidth, barHeight, Colors::NORMAL_TEXT);
      }
    };

  } // namespace tile_dashboard
} // namespace esphome

#endif // SWITCH_ICON_RENDERER_H