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
  bool fullscreen_enabled() const { return fullscreen_enabled_; }
  void set_fullscreen_enabled(bool v) { fullscreen_enabled_ = v; }
  bool is_fullscreen() const { return override_geo_.has_value(); }
  void bind_display(Display *disp) { disp_ = disp; }

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
  }

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

    // 2) Initial, wenn alter Key leer; sonst nur redraw, wenn Key sich ändert
    const bool is_initial = old_key.empty();
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
      render_full(it);
    }
    else
    {
      render_update(it);
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

  virtual void dispatch_touch_update(uint16_t local_x, uint16_t local_y)
  {

  }

  virtual void dispatch_touch_release(uint16_t local_x, uint16_t local_y)
  {

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
    it.start_clipping(x0, y0, x0 + tile_w(), y0 + tile_h());
    render_background(it);
    draw_labels(it);
    draw_content(it);
    it.end_clipping();
  }
  virtual void render_update(Display &it)
  {
    auto [cx0, cy0, cx1, cy1] = value_clip();
    it.start_clipping(cx0, cy0, cx1, cy1);
    // nur Hintergrund-Farbe (nicht voller Rahmen!)
    ctx_.bg_renderer.drawBgColor(it,
                                 cx0, cy0, cx1 - cx0, cy1 - cy0);
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
    ctx_.bg_renderer.drawFullBg(it, abs_x(), abs_y(),
                                tile_w(), tile_h(), numTilesY_,
                                rounded ? radius : 0, border);
  }

  int tile_w() const { return override_geo_ ? override_geo_->w : ctx_.tile_w(); }
  int tile_h() const { return override_geo_ ? override_geo_->h : ctx_.tile_h(); }
  int abs_x() const { return override_geo_ ? override_geo_->x : ctx_.x0 + (col_ - 1) * ctx_.tile_w(); }
  int abs_y() const { return override_geo_ ? override_geo_->y : ctx_.y0 + (row_ - 1) * ctx_.tile_h(); }
  int tile_index() const { return (row_ - 1) * ctx_.cols + (col_ - 1); }

protected:
  DisplayContext &ctx_;
  uint8_t col_, row_, numTilesY_;
  std::string label_;
  std::string label2_;
  Display *disp_{nullptr};
  bool fullscreen_enabled_{false};
  std::optional<OverrideGeometry> override_geo_; 

  void request_redraw() {
    if (disp_ != nullptr)
      disp_->update();           // Pointer →   ->
  }

private:
  std::map<TouchArea, std::function<void()>> touch_;
};
#endif
