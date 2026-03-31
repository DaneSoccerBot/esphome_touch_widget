#ifndef PIXEL_BUFFER_H
#define PIXEL_BUFFER_H

#include <cmath>
#include <algorithm>
#include <cstdint>

#include "esphome.h"
#include "esphome/components/display/display.h"
#include "esphome/components/display/display_color_utils.h"

/**
 *  PixelBuffer — Software-Renderbuffer (RGB565) für Offscreen-Zeichnen.
 *
 *  Motivation: Auf dem ST7701S (RGB-Panel) geht jeder draw_pixel_at()
 *  als separater DMA-Transfer zum Framebuffer. Formen wie Kreise oder
 *  Arc-Segmente erzeugen tausende Einzel-Transfers.
 *
 *  PixelBuffer rendert stattdessen in einen PSRAM-Buffer und blit'tet
 *  das Ergebnis mit einem einzigen draw_pixels_at()-Aufruf (= 1 DMA).
 *
 *  Typischer Speedup: 1000-3000× weniger DMA-Transfers.
 */
class PixelBuffer {
 public:
  ~PixelBuffer() { free_(); }

  /** Buffer allokieren/resizen. Gibt false zurück bei OOM. */
  bool ensure(int w, int h) {
    if (w == w_ && h == h_ && buf_) return true;
    free_();
    buf_ = new (std::nothrow) uint16_t[w * h];
    if (!buf_) return false;
    w_ = w;
    h_ = h;
    return true;
  }

  /** Buffer mit einer Farbe füllen. */
  void clear(uint16_t color565) {
    std::fill(buf_, buf_ + w_ * h_, color565);
  }

  /** Gefüllten Kreis in den Buffer zeichnen. */
  void filled_circle(int cx, int cy, int r, uint16_t color565) {
    const int r2 = r * r;
    const int y0 = std::max(0, cy - r);
    const int y1 = std::min(h_ - 1, cy + r);
    for (int y = y0; y <= y1; y++) {
      const int dy = y - cy;
      const int dx_lim = static_cast<int>(
          std::sqrt(static_cast<float>(r2 - dy * dy)));
      const int x0 = std::max(0, cx - dx_lim);
      const int x1 = std::min(w_ - 1, cx + dx_lim);
      uint16_t *row = &buf_[y * w_];
      std::fill(&row[x0], &row[x1 + 1], color565);
    }
  }

  /**
   *  Gefülltes Arc-Segment (Ring-Ausschnitt) in den Buffer zeichnen.
   *  Winkel in Grad, mathematische Konvention (0° = rechts, CCW positiv).
   */
  void filled_arc(int cx, int cy, float outer_r, float inner_r,
                  float start_deg, float end_deg, uint16_t color565) {
    const float inner2 = inner_r * inner_r;
    const float outer2 = outer_r * outer_r;
    constexpr float TWO_PI = 2.0f * static_cast<float>(M_PI);

    float s = std::fmod(start_deg * (static_cast<float>(M_PI) / 180.0f), TWO_PI);
    float e = std::fmod(end_deg * (static_cast<float>(M_PI) / 180.0f), TWO_PI);
    if (s < 0) s += TWO_PI;
    if (e < 0) e += TWO_PI;
    const bool wraps = s > e;

    const int r = static_cast<int>(std::ceil(outer_r)) + 1;
    const int y0 = std::max(0, cy - r);
    const int y1 = std::min(h_ - 1, cy + r);
    const int x0b = std::max(0, cx - r);
    const int x1b = std::min(w_ - 1, cx + r);

    for (int y = y0; y <= y1; y++) {
      const float dy = static_cast<float>(y - cy);
      uint16_t *row = &buf_[y * w_];
      for (int x = x0b; x <= x1b; x++) {
        const float dx = static_cast<float>(x - cx);
        const float d2 = dx * dx + dy * dy;
        if (d2 < inner2 || d2 > outer2) continue;

        float a = std::atan2(dy, dx);
        if (a < 0) a += TWO_PI;

        bool in = wraps ? (a >= s || a <= e) : (a >= s && a <= e);
        if (in) row[x] = color565;
      }
    }
  }

  /** Buffer auf das Display blitten (1 DMA-Transfer auf ST7701S). */
  void blit(esphome::display::Display &disp, int x, int y) const {
    if (!buf_ || w_ <= 0 || h_ <= 0) return;
    disp.draw_pixels_at(
        x, y, w_, h_,
        reinterpret_cast<const uint8_t *>(buf_),
        esphome::display::ColorOrder::COLOR_ORDER_RGB,
        esphome::display::ColorBitness::COLOR_BITNESS_565, false);
  }

  /** Color → RGB565 konvertieren (muss zum Display-Format passen). */
  static uint16_t to_565(esphome::Color c,
                         esphome::display::ColorOrder order =
                             esphome::display::ColorOrder::COLOR_ORDER_RGB) {
    return esphome::display::ColorUtil::color_to_565(c, order);
  }

  bool valid() const { return buf_ != nullptr; }

 private:
  void free_() {
    delete[] buf_;
    buf_ = nullptr;
    w_ = h_ = 0;
  }

  uint16_t *buf_{nullptr};
  int w_{0}, h_{0};
};

#endif  // PIXEL_BUFFER_H
