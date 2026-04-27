#ifndef SWITCH_ICON_RENDERER_H
#define SWITCH_ICON_RENDERER_H

#include <algorithm>
#include <cstdint>

#include "esphome/components/display/display.h"
#include "colors.h"
#include "pixel_buffer.h"
#include "render_primitives.h"

namespace esphome {
namespace tile_dashboard {

class SwitchIconRenderer {
 public:
  static void draw(esphome::display::Display &it, int16_t x, int16_t y,
                   uint16_t w, uint16_t h, bool on) {
    if (w == 0 || h == 0)
      return;

    CacheEntry &entry = cache_entry_(w, h);
    const int state = on ? 1 : 0;
    if (!entry.valid[state]) {
      if (!render_icon_(entry.buf[state], w, h, on))
        return;
      entry.valid[state] = true;
    }

    entry.buf[state].blit(it, x, y);
  }

 private:
  struct CacheEntry {
    int w{0};
    int h{0};
    bool valid[2]{false, false};
    PixelBuffer buf[2];
  };

  static CacheEntry &cache_entry_(int w, int h) {
    static CacheEntry entries[4];
    static uint8_t next_slot = 0;

    for (auto &entry : entries) {
      if (entry.w == w && entry.h == h)
        return entry;
    }
    for (auto &entry : entries) {
      if (entry.w == 0 || entry.h == 0) {
        entry.w = w;
        entry.h = h;
        entry.valid[0] = false;
        entry.valid[1] = false;
        return entry;
      }
    }

    CacheEntry &entry = entries[next_slot++ % 4];
    entry.buf[0].release();
    entry.buf[1].release();
    entry.w = w;
    entry.h = h;
    entry.valid[0] = false;
    entry.valid[1] = false;
    return entry;
  }

  static bool render_icon_(PixelBuffer &buf, uint16_t w, uint16_t h, bool on) {
    if (!buf.ensure(w, h))
      return false;

    const uint16_t c_bg = PixelBuffer::to_565(Colors::TILE_BACKGROUND);
    const uint16_t c_face = PixelBuffer::to_565(Colors::LIGHT_GREY);
    const uint16_t c_frame = PixelBuffer::to_565(Colors::TILE_BORDER);
    const uint16_t c_text = PixelBuffer::to_565(Colors::NORMAL_TEXT);
    const uint16_t c_green = PixelBuffer::to_565(Colors::GREEN);
    const uint16_t c_red = PixelBuffer::to_565(Colors::RED);
    const uint16_t c_state = on ? c_green : c_red;

    buf.clear(c_bg);

    const int radius = std::max(1, static_cast<int>(w * 0.15f));
    const int border = std::max(1, static_cast<int>(w * 0.05f));
    const int ix = border;
    const int iy = border;
    const int iw = std::max(1, static_cast<int>(w) - 2 * border);
    const int ih = std::max(1, static_cast<int>(h) - 2 * border);
    const int inner_r = std::max(1, static_cast<int>(w * 0.12f));
    const int half = ih / 2;

    using esphome::tile_dashboard::render::BatchPrimitives;

    BatchPrimitives::draw_rounded_rect(buf, 0, 0, w, h, radius, c_frame);
    BatchPrimitives::draw_rounded_rect(buf, ix, iy, iw, ih, inner_r, c_face);

    if (on) {
      BatchPrimitives::draw_rounded_rect(buf, ix, iy + half, iw, ih - half,
                                         inner_r, c_face);
      BatchPrimitives::draw_rounded_rect(buf, ix, iy, iw,
                                         static_cast<int>(half * 1.2f),
                                         inner_r, c_green);
    } else {
      BatchPrimitives::draw_rounded_rect(buf, ix, iy, iw, half, inner_r, c_face);
      BatchPrimitives::draw_rounded_rect(buf, ix,
                                         iy + static_cast<int>(half * 0.8f),
                                         iw,
                                         ih - static_cast<int>(half * 0.8f),
                                         inner_r, c_red);
    }

    const int cx = w / 2;
    const int cy = on ? (iy + static_cast<int>(half * 0.6f))
                      : (iy + static_cast<int>(half * 1.45f));
    const int outer = std::max(1, static_cast<int>(w * 0.26f));
    const int inner = std::max(1, static_cast<int>(w * 0.18f));
    BatchPrimitives::draw_ring(buf, cx, cy, outer, inner, c_text, c_state);

    const int bar_w = std::max(1, static_cast<int>(w * 0.08f));
    const int bar_h = std::max(1, static_cast<int>(w * 0.25f));
    const int bar_outline = std::max(1, static_cast<int>(w * 0.04f));
    const int bar_x = cx - bar_w / 2;
    const int bar_y = cy - static_cast<int>(bar_h * 0.7f) - outer / 2;

    buf.fill_rect(bar_x - bar_outline, bar_y - bar_outline,
                  bar_w + bar_outline * 2, bar_h + bar_outline * 2, c_state);
    buf.fill_rect(bar_x, bar_y, bar_w, bar_h, c_text);
    return true;
  }
};

}  // namespace tile_dashboard
}  // namespace esphome

#endif  // SWITCH_ICON_RENDERER_H
