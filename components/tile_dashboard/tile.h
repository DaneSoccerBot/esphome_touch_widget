#ifndef TILE_H
#define TILE_H

#include <map>
#include <string>
#include <functional>
#include <tuple>
#include <utility>
#include <cstdint>
#include <optional>

#include "esphome.h"
#include "esphome/components/display/display.h"
#include "colors.h"
#include "display_context.h"
#include "tile_bg_renderer.h"
#include "pixel_buffer.h"

#ifdef USE_ESP32
#include "esp_heap_caps.h"
#endif

using esphome::display::Display;

class Tile
{
public:
  enum class TouchArea
  {
    LEFT,
    CENTER,
    RIGHT,
    ANY
  };

  struct OverrideGeometry {
    int x, y, w, h;
  };

  Tile(DisplayContext &ctx, uint8_t col, uint8_t row, uint8_t numTilesY, std::string label = {})
      : ctx_(ctx), col_(col), row_(row), numTilesY_(numTilesY), label_(std::move(label)) {}
  virtual ~Tile() = default;

  uint8_t col() const { return col_; }
  uint8_t row() const { return row_; }
  uint8_t numTilesY() const { return numTilesY_; }
  uint8_t page() const { return page_; }
  void set_page(uint8_t p) { page_ = p; }
  uint8_t colspan() const { return colspan_; }
  uint8_t rowspan() const { return rowspan_; }
  void set_colspan(uint8_t c) { colspan_ = std::max<uint8_t>(c, 1); }
  void set_rowspan(uint8_t r) { rowspan_ = std::max<uint8_t>(r, 1); }
  void set_tile_idx(int idx) { tile_idx_ = idx; }
  bool fullscreen_enabled() const { return fullscreen_enabled_; }
  void set_fullscreen_enabled(bool v) { fullscreen_enabled_ = v; }
  bool is_fullscreen() const { return override_geo_.has_value(); }
  void bind_display(Display *disp) { disp_ = disp; }

  /** Check whether the grid cell (c, r) falls inside this tile's span. */
  bool contains_cell(uint8_t c, uint8_t r) const {
    return c >= col_ && c < col_ + colspan_ &&
           r >= row_ && r < row_ + rowspan_;
  }

  void set_override_geometry(OverrideGeometry geo) {
    override_geo_ = geo;
    invalidate_cache();
  }
  void clear_override_geometry() {
    override_geo_.reset();
    invalidate_cache();
  }

  void invalidate_cache() {
    const int idx = tile_index();
    if (idx < static_cast<int>(ctx_.cache_value.size()))
      ctx_.cache_value[idx].clear();
    force_full_ = true;
    reset_prev_values();
  }

  virtual void reset_prev_values() {}

  void draw(Display &it)
  {
    if (disp_ == nullptr)        // 1× merken
      disp_ = &it;
    const int idx = tile_index();
    const std::string new_key = make_cache_key();

    // 1) alten Key aus dem Cache (wenn vorhanden) ziehen
    std::string old_key;
    if (idx < ctx_.cache_value.size())
      old_key = ctx_.cache_value[idx];

    // 2) Initial wenn Key leer ODER force_full_ gesetzt (Page-Switch/Zoom-Exit)
    const bool is_initial = old_key.empty() || force_full_;
    if (force_full_) force_full_ = false;
    const bool need_redraw = is_initial || (old_key != new_key);

    if (!need_redraw)
      return;

      ESP_LOGD("tile",
             "draw() tile[%d] @%d,%d -> %s (old='%s', new='%s')",
             idx, col_, row_,
             is_initial    ? "INITIAL"
             : need_redraw ? "UPDATE"
                           : "SKIP",
             old_key.c_str(), new_key.c_str());

    if (is_initial)
    {
#ifdef USE_ESP32
      const size_t psram_before = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
#endif
      const uint32_t t0 = esphome::millis();
      render_full(it);
      const uint32_t dt = esphome::millis() - t0;
#ifdef USE_ESP32
      const long psram_delta = (long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM) - (long)psram_before;
      ESP_LOGI("tile", "FULL %s[%d] @%d,%d %ums fs=%s %dx%d psram=%+ld",
               tile_type_name(), idx, col_, row_, dt,
               is_fullscreen() ? "Y" : "N",
               tile_w(), tile_h(), psram_delta);
#else
      ESP_LOGI("tile", "FULL %s[%d] @%d,%d %ums fs=%s %dx%d",
               tile_type_name(), idx, col_, row_, dt,
               is_fullscreen() ? "Y" : "N",
               tile_w(), tile_h());
#endif
    }
    else
    {
      const uint32_t t0 = esphome::millis();
      render_update(it);
      const uint32_t dt = esphome::millis() - t0;
      ESP_LOGD("tile", "UPD  %s[%d] @%d,%d %ums",
               tile_type_name(), idx, col_, row_, dt);
    }

    // 6) Cache aktualisieren
    if (idx >= ctx_.cache_value.size())
      ctx_.cache_value.resize(idx + 1);
    ctx_.cache_value[idx] = new_key;
  }

  /* -------------------------------------------------------------------- */
  void add_touch_handler(TouchArea a, std::function<void()> cb)
  {
    touch_[a] = std::move(cb);
  }

  virtual void dispatch_touch(uint16_t local_x, uint16_t local_y)
  {
    // Touch-down: keine Actions auslösen — erst bei Release (nach Swipe-Check)
  }

  virtual void dispatch_touch_update(uint16_t local_x, uint16_t local_y)
  {
  }

  virtual void dispatch_touch_release(uint16_t local_x, uint16_t local_y)
  {
    // Touch-Handler erst bei Release auslösen (Swipe wurde bereits geprüft)
    TouchArea a = TouchArea::CENTER;
    if (local_x < tile_w() * 0.33f)
      a = TouchArea::LEFT;
    else if (local_x > tile_w() * 0.66f)
      a = TouchArea::RIGHT;
    auto it = touch_.find(a);
    if (it == touch_.end())
      it = touch_.find(TouchArea::ANY);
    if (it != touch_.end())
      it->second();
  }

protected:
  static bool formatted_equals(float a, float b,
                               const char *fmt, const std::string &unit)
  {
    // beide NaN → gleich
    if (std::isnan(a) && std::isnan(b))
      return true;
    // nur eins NaN → unterschiedlich
    if (std::isnan(a) || std::isnan(b))
      return false;
    // Strings erstellen
    char sa[32], sb[32];
    std::snprintf(sa, sizeof(sa), fmt, a);
    std::strncat(sa, unit.c_str(), sizeof(sa) - std::strlen(sa) - 1);
    std::snprintf(sb, sizeof(sb), fmt, b);
    std::strncat(sb, unit.c_str(), sizeof(sb) - std::strlen(sb) - 1);
    return std::strcmp(sa, sb) == 0;
  }

  static bool significant_change(float a, float b, float threshold = 0.05f)
  {
    // beide NaN → kein Unterschied
    if (std::isnan(a) && std::isnan(b))
      return false;
    // genau einer NaN → Unterschied
    if (std::isnan(a) != std::isnan(b))
      return true;

    // Betrag des größeren der beiden Werte
    const float mag = std::max(std::fabs(a), std::fabs(b));
    // bei (fast) Nullwerten gilt kein signifikanter Unterschied
    if (mag < 1e-6f)
      return false;

    // relative Differenz
    const float rel_diff = std::fabs(a - b) / mag;
    return rel_diff >= threshold;
  }

  /* ---- Hooks ----------------------------------------------------------- */
  virtual void render_full(Display &it)
  {
    int x0 = abs_x(), y0 = abs_y();
    int w = tile_w(), h = tile_h();
    it.start_clipping(x0, y0, x0 + w, y0 + h);
    uint32_t t0 = esphome::millis();

    // Hintergrund via PixelBuffer blitten (1 DMA pro Strip statt per-pixel)
    const uint16_t c_scr = PixelBuffer::to_565(Colors::SCREEN_BACKGROUND);
    const uint16_t c_brd = PixelBuffer::to_565(Colors::TILE_BORDER);
    const uint16_t c_bg  = PixelBuffer::to_565(Colors::TILE_BACKGROUND);
    const int gap = is_fullscreen() ? 0 : 2;
    const int bw  = is_fullscreen() ? 0 : 1;
    const int inset = gap + bw;

    bg_buf().blit_strips(it, x0, y0, w, h, w * 32,
        c_scr,
        [&](PixelBuffer &buf, int strip_y) {
          // Buffer ist bereits mit c_scr gefüllt (margin)
          // Border-Bereich
          const int sh = buf.height();
          for (int sy = 0; sy < sh; sy++) {
            int gy = strip_y + sy;
            if (gy < gap || gy >= h - gap) continue;
            if (gy < inset || gy >= h - inset) {
              // Ganze Border-Zeile
              buf.fill_rect(gap, sy, w - 2 * gap, 1, c_brd);
            } else {
              // Links border + BG + rechts border
              buf.fill_rect(gap, sy, bw, 1, c_brd);
              buf.fill_rect(inset, sy, w - 2 * inset, 1, c_bg);
              buf.fill_rect(w - inset, sy, bw, 1, c_brd);
            }
          }
        });

    uint32_t t1 = esphome::millis();
    draw_labels(it);
    uint32_t t2 = esphome::millis();
    draw_content(it);
    it.end_clipping();
    uint32_t t3 = esphome::millis();
    ESP_LOGI("tile", "  breakdown %s[%d]: bg_blit=%u lbl=%u content=%u ms",
             tile_type_name(), tile_idx_, t1 - t0, t2 - t1, t3 - t2);
  }
  virtual void render_update(Display &it)
  {
    auto [cx0, cy0, cx1, cy1] = value_clip();
    it.start_clipping(cx0, cy0, cx1, cy1);
    clear_area_fast(it, cx0, cy0, cx1 - cx0, cy1 - cy0);
    draw_content(it);
    it.end_clipping();
  }
  virtual void draw_labels(Display &it)
  {
    if (!label_.empty())
      it.print(abs_x() + tile_w() / 2, abs_y() + tile_h() * 0.9f,
               ctx_.font_label_compact, Colors::LIGHT_TEXT,
               esphome::display::TextAlign::CENTER,
               label_.c_str());
  }
  virtual void draw_content(Display &it) = 0;
  virtual std::string make_cache_key() const { return {}; }
  virtual const char *tile_type_name() const { return "Tile"; }

  /**  *** NEU ***  Bereich, der bei Wert-Änderung neu gezeichnet werden soll.
       Default = komplette Kachel, Spezial-Tiles überschreiben es.            */
  virtual std::tuple<int, int, int, int> value_clip() const
  {
    return {abs_x(), abs_y(), abs_x() + tile_w(), abs_y() + tile_h()};
  }

  /* ---- Helper ---------------------------------------------------------- */
  void render_background(Display &it,
                         bool rounded = true,
                         bool border = true,
                         int radius = 8) const
  {
    // In Fullscreen: keine Rounded Corners, kein Border — nur plane Füllung.
    // Rounded-Rects erzeugen 5× filled_rectangle + 4× filled_circle → teuer.
    if (is_fullscreen()) {
      it.filled_rectangle(abs_x(), abs_y(), tile_w(), tile_h(),
                          Colors::TILE_BACKGROUND);
      return;
    }
    ctx_.bg_renderer.drawFullBg(it, abs_x(), abs_y(),
                                tile_w(), tile_h(), numTilesY_,
                                rounded ? radius : 0, border);
  }

  int tile_w() const { return override_geo_ ? override_geo_->w : ctx_.tile_w() * colspan_; }
  int tile_h() const { return override_geo_ ? override_geo_->h : ctx_.tile_h() * rowspan_; }
  int abs_x() const { return override_geo_ ? override_geo_->x : ctx_.x0 + (col_ - 1) * ctx_.tile_w(); }
  int abs_y() const { return override_geo_ ? override_geo_->y : ctx_.y0 + (row_ - 1) * ctx_.tile_h(); }
  int tile_index() const { return tile_idx_; }

protected:
  DisplayContext &ctx_;
  uint8_t col_, row_, numTilesY_;
  uint8_t page_{0};
  uint8_t colspan_{1}, rowspan_{1};
  int tile_idx_{0};
  std::string label_;
  std::string label2_;
  Display *disp_{nullptr};
  bool fullscreen_enabled_{false};
  std::optional<OverrideGeometry> override_geo_;

public:
  /** Shared PixelBuffer für Hintergrund-Rendering (alle Tiles teilen sich einen). */
  static PixelBuffer &bg_buf() {
    static PixelBuffer buf;
    return buf;
  }
protected:

  /**
   * Schnelles Flächen-Clear via blit_strips statt filled_rectangle.
   * filled_rectangle = per-pixel PSRAM-Writes (4.8μs/px).
   * blit_strips = RAM-Fill + 1 DMA pro Strip (~15× schneller).
   */
  void clear_area_fast(Display &it, int x, int y, int w, int h) {
    const uint16_t c_bg = PixelBuffer::to_565(Colors::TILE_BACKGROUND);
    bg_buf().blit_strips(it, x, y, w, h, w * 32,
        c_bg, [](PixelBuffer &, int) {});
  }

  void request_redraw() {
    // Nur Flag setzen — KEIN invalidate_cache()!
    // Der Cache-Key ändert sich durch die State-Änderung (z.B. "OFF"→"ON"),
    // was draw() automatisch als render_update() (statt render_full()) erkennt.
    // invalidate_cache() würde den Key leeren → is_initial=true → render_full()
    // → sichtbarer Background-Blackout + unnötige Arbeit.
    redraw_pending_ = true;
  }

public:
  bool consume_redraw_pending() {
    bool v = redraw_pending_;
    redraw_pending_ = false;
    return v;
  }

private:
  bool redraw_pending_{false};
  bool force_full_{false};
  std::map<TouchArea, std::function<void()>> touch_;
};
#endif
