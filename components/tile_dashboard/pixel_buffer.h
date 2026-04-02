#ifndef PIXEL_BUFFER_H
#define PIXEL_BUFFER_H

#include <cmath>
#include <algorithm>
#include <cstdint>
#include <cstdlib>

#include "esphome.h"
#include "esphome/components/display/display.h"
#include "esphome/components/display/display_color_utils.h"
#include "esphome/core/log.h"
#ifdef USE_ESP32
#include "esp_heap_caps.h"
#endif

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
    const size_t bytes = (size_t)w * h * sizeof(uint16_t);
#ifdef USE_ESP32
    // Bevorzuge PSRAM für große Buffer (Framebuffer-nahe Allokation)
    buf_ = static_cast<uint16_t *>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!buf_) // Fallback auf internen Heap für kleine Buffer
      buf_ = static_cast<uint16_t *>(heap_caps_malloc(bytes, MALLOC_CAP_8BIT));
#else
    buf_ = static_cast<uint16_t *>(std::malloc(bytes));
#endif
    if (!buf_) {
      ESP_LOGW("pbuf", "OOM: ensure(%d, %d) = %u bytes", w, h, (unsigned)bytes);
      return false;
    }
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

  /** Gefülltes Rechteck in den Buffer zeichnen (clipped). */
  void fill_rect(int rx, int ry, int rw, int rh, uint16_t color565) {
    const int x0 = std::max(0, rx);
    const int y0 = std::max(0, ry);
    const int x1 = std::min(w_, rx + rw);
    const int y1 = std::min(h_, ry + rh);
    for (int y = y0; y < y1; y++) {
      std::fill(&buf_[y * w_ + x0], &buf_[y * w_ + x1], color565);
    }
  }

  /**
   *  Gefülltes Arc-Segment (Ring-Ausschnitt) in den Buffer zeichnen.
   *  Winkel in Grad, mathematische Konvention (0° = rechts, CCW positiv).
   *  Nutzt Cross-Product statt atan2() für ~20× schnelleren Winkeltest.
   */
  void filled_arc(int cx, int cy, float outer_r, float inner_r,
                  float start_deg, float end_deg, uint16_t color565) {
    const float inner2 = inner_r * inner_r;
    const float outer2 = outer_r * outer_r;
    constexpr float DEG2RAD = static_cast<float>(M_PI) / 180.0f;

    const float s_rad = start_deg * DEG2RAD;
    const float e_rad = end_deg * DEG2RAD;

    // Richtungsvektoren für Start/End-Winkel
    const float sx = std::cos(s_rad), sy = std::sin(s_rad);
    const float ex = std::cos(e_rad), ey = std::sin(e_rad);

    // Cross-Product des Winkelbereichs: positiv wenn <180°, negativ wenn >=180°
    const float range_cross = sx * ey - sy * ex;
    const bool wide = (range_cross <= 0);  // Bogen >= 180°

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

        // Cross-Product-Winkeltest (2 Multiplikationen statt atan2)
        // cs = P×S = sin(θ_S - θ_P): negativ wenn P CCW nach S
        // ce = P×E = sin(θ_E - θ_P): positiv wenn P CCW vor E
        const float cs = dx * sy - dy * sx;  // cross mit Start-Vektor
        const float ce = dx * ey - dy * ex;  // cross mit End-Vektor
        bool in_arc;
        if (wide) {
          in_arc = (cs <= 0 || ce >= 0);  // Bogen >=180°: OR-Logik
        } else {
          in_arc = (cs <= 0 && ce >= 0);  // Bogen <180°: AND-Logik
        }
        if (in_arc) row[x] = color565;
      }
    }
  }

  /**
   *  Kombinierter Gauge-Arc: Track + Fill + Inner Circle in einem Durchgang.
   *  Nutzt Scanline-Clipping: pro Zeile werden äußere/innere Kreisgrenzen
   *  berechnet → innere Kreisfläche per std::fill, Ring-Pixel per Winkeltest.
   */
  void draw_gauge_arc(int cx, int cy, float outer_r, float inner_r,
                      float track_start_deg, float track_end_deg,
                      float fill_end_deg,
                      uint16_t track_color, uint16_t fill_color,
                      uint16_t inner_color) {
    const float inner2 = inner_r * inner_r;
    const float outer2 = outer_r * outer_r;
    constexpr float DEG2RAD = static_cast<float>(M_PI) / 180.0f;

    // Track-Winkel (gesamter Bogen)
    const float ts_rad = track_start_deg * DEG2RAD;
    const float te_rad = track_end_deg * DEG2RAD;
    const float tsx = std::cos(ts_rad), tsy = std::sin(ts_rad);
    const float tex = std::cos(te_rad), tey = std::sin(te_rad);
    const float track_cross = tsx * tey - tsy * tex;
    const bool track_wide = (track_cross <= 0);

    // Fill-Winkel (gefüllter Bereich, gleicher Start wie Track)
    const float fe_rad = fill_end_deg * DEG2RAD;
    const float fex = std::cos(fe_rad), fey = std::sin(fe_rad);
    const float fill_cross = tsx * fey - tsy * fex;
    const bool fill_wide = (fill_cross <= 0);

    const int r = static_cast<int>(std::ceil(outer_r)) + 1;
    const int y0 = std::max(0, cy - r);
    const int y1 = std::min(h_ - 1, cy + r);

    for (int y = y0; y <= y1; y++) {
      const float dy = static_cast<float>(y - cy);
      const float dy2 = dy * dy;

      // Äußere Kreisgrenze: X-Bereich für diese Zeile
      const float outer_dx2 = outer2 - dy2;
      if (outer_dx2 < 0.0f) continue;  // Zeile komplett außerhalb
      const int outer_dx = static_cast<int>(std::sqrt(outer_dx2));
      const int x_lo = std::max(0, cx - outer_dx);
      const int x_hi = std::min(w_ - 1, cx + outer_dx);

      uint16_t *row = &buf_[y * w_];

      // Innere Kreisgrenze
      const float inner_dx2 = inner2 - dy2;
      if (inner_dx2 > 0.0f) {
        // Diese Zeile hat einen inneren Kreisbereich → bulk fill
        const int inner_dx = static_cast<int>(std::sqrt(inner_dx2));
        const int ix_lo = std::max(x_lo, cx - inner_dx);
        const int ix_hi = std::min(x_hi, cx + inner_dx);
        if (ix_lo <= ix_hi)
          std::fill(&row[ix_lo], &row[ix_hi + 1], inner_color);

        // Linker Ring-Bereich [x_lo, ix_lo-1]
        for (int x = x_lo; x < ix_lo; x++) {
          const float dx = static_cast<float>(x - cx);
          const float cs = dx * tsy - dy * tsx;
          const float ce_t = dx * tey - dy * tex;
          bool in_track = track_wide ? (cs <= 0 || ce_t >= 0) : (cs <= 0 && ce_t >= 0);
          if (!in_track) continue;
          const float ce_f = dx * fey - dy * fex;
          bool in_fill = fill_wide ? (cs <= 0 || ce_f >= 0) : (cs <= 0 && ce_f >= 0);
          row[x] = in_fill ? fill_color : track_color;
        }
        // Rechter Ring-Bereich [ix_hi+1, x_hi]
        for (int x = ix_hi + 1; x <= x_hi; x++) {
          const float dx = static_cast<float>(x - cx);
          const float cs = dx * tsy - dy * tsx;
          const float ce_t = dx * tey - dy * tex;
          bool in_track = track_wide ? (cs <= 0 || ce_t >= 0) : (cs <= 0 && ce_t >= 0);
          if (!in_track) continue;
          const float ce_f = dx * fey - dy * fex;
          bool in_fill = fill_wide ? (cs <= 0 || ce_f >= 0) : (cs <= 0 && ce_f >= 0);
          row[x] = in_fill ? fill_color : track_color;
        }
      } else {
        // Kein innerer Kreis in dieser Zeile → alle Pixel sind Ring-Kandidaten
        for (int x = x_lo; x <= x_hi; x++) {
          const float dx = static_cast<float>(x - cx);
          const float d2 = dx * dx + dy2;
          if (d2 > outer2) continue;
          const float cs = dx * tsy - dy * tsx;
          const float ce_t = dx * tey - dy * tex;
          bool in_track = track_wide ? (cs <= 0 || ce_t >= 0) : (cs <= 0 && ce_t >= 0);
          if (!in_track) continue;
          const float ce_f = dx * fey - dy * fex;
          bool in_fill = fill_wide ? (cs <= 0 || ce_f >= 0) : (cs <= 0 && ce_f >= 0);
          row[x] = in_fill ? fill_color : track_color;
        }
      }
    }
  }

  /** Buffer auf das Display blitten (1 DMA-Transfer auf ST7701S). */
  void blit(esphome::display::Display &disp, int x, int y) const {
    if (!buf_ || w_ <= 0 || h_ <= 0) return;

    int dst_x = x;
    int dst_y = y;
    int draw_w = w_;
    int draw_h = h_;
    int src_x_offset = 0;
    int src_y_offset = 0;
    int src_x_pad = 0;

    if (disp.is_clipping()) {
      const auto clip = disp.get_clipping();
      if (clip.is_set()) {
        const int left = std::max(dst_x, static_cast<int>(clip.x));
        const int top = std::max(dst_y, static_cast<int>(clip.y));
        const int right = std::min(dst_x + draw_w, static_cast<int>(clip.x2()));
        const int bottom = std::min(dst_y + draw_h, static_cast<int>(clip.y2()));
        if (left >= right || top >= bottom) return;

        src_x_offset = left - dst_x;
        src_y_offset = top - dst_y;
        draw_w = right - left;
        draw_h = bottom - top;
        dst_x = left;
        dst_y = top;
        src_x_pad = w_ - draw_w - src_x_offset;
      }
    }

    const uint32_t t0 = esphome::millis();
    disp.draw_pixels_at(
        dst_x, dst_y, draw_w, draw_h,
        reinterpret_cast<const uint8_t *>(buf_),
        esphome::display::ColorOrder::COLOR_ORDER_RGB,
        esphome::display::ColorBitness::COLOR_BITNESS_565, true,
        src_x_offset, src_y_offset, src_x_pad);
    const uint32_t dt = esphome::millis() - t0;
    if (dt > 2) {
      ESP_LOGD("pbuf", "blit %dx%d @%d,%d -> %dx%d @%d,%d %ums",
               w_, h_, x, y, draw_w, draw_h, dst_x, dst_y, dt);
    }
  }

  /**
   *  Strip-basiertes Arc-Rendering: Zeichnet den gesamten Arc in
   *  horizontalen Streifen, um das Buffer-Limit einzuhalten.
   *  Der Buffer wird als schmaler Streifen wiederverwendet.
   *
   *  Ruft für jeden Streifen die Zeichenfunktion mit (buf, strip_y_offset)
   *  auf und blittet das Ergebnis danach.
   *
   *  draw_fn(PixelBuffer &buf, int strip_y_offset) — zeichnet in den Buffer,
   *    der den Bereich [strip_y_offset .. strip_y_offset + strip_h) abdeckt.
   */
  template<typename DrawFn>
  bool blit_strips(esphome::display::Display &disp,
                   int dst_x, int dst_y,
                   int total_w, int total_h,
                   int max_pixels,
                   uint16_t bg_color,
                   DrawFn draw_fn) {
    const uint32_t t0 = esphome::millis();
    int strip_h = std::max(1, max_pixels / std::max(total_w, 1));
    strip_h = std::min(strip_h, total_h);
    if (!ensure(total_w, strip_h))
      return false;

    int n_strips = 0;
    for (int y_off = 0; y_off < total_h; y_off += strip_h) {
      const int h = std::min(strip_h, total_h - y_off);
      const int saved_h = h_;
      h_ = h;

      clear(bg_color);
      draw_fn(*this, y_off);
      blit(disp, dst_x, dst_y + y_off);

      h_ = saved_h;
      ++n_strips;
    }
    const uint32_t dt = esphome::millis() - t0;
    ESP_LOGD("pbuf", "blit_strips %dx%d %u strips %ums",
             total_w, total_h, n_strips, dt);
    return true;
  }

  /**
   * Color → RGB565 konvertieren.
   * ESP32 RGB-Panel (ST7701S): Framebuffer erwartet big-endian Byte-Order.
   * ESPHome macht intern convert_big_endian(color_to_565()) vor dem Schreiben.
   * Unser Buffer wird roh an esp_lcd_panel_draw_bitmap übergeben
   * → wir müssen den Byte-Swap selbst machen.
   * SDL-Simulator: Erwartet native (little-endian) Byte-Order.
   */
  static uint16_t to_565(esphome::Color c,
                         esphome::display::ColorOrder order =
                             esphome::display::ColorOrder::COLOR_ORDER_RGB) {
    uint16_t val = esphome::display::ColorUtil::color_to_565(c, order);
#ifdef USE_ESP32
    // Byte-Swap für RGB-Panel Framebuffer (big-endian Speicherlayout)
    val = (val >> 8) | (val << 8);
#endif
    return val;
  }

  bool valid() const { return buf_ != nullptr; }
  int width() const { return w_; }
  int height() const { return h_; }

 private:
  void free_() {
#ifdef USE_ESP32
    heap_caps_free(buf_);
#else
    std::free(buf_);
#endif
    buf_ = nullptr;
    w_ = h_ = 0;
  }

  uint16_t *buf_{nullptr};
  int w_{0}, h_{0};
};

#endif  // PIXEL_BUFFER_H
