#ifndef GAUGE_TILE_H
#define GAUGE_TILE_H

#include <tuple>
#include <vector>
#include <utility>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include "tile.h"
#include "colors.h" // Farbkonstanten
#include "pixel_buffer.h"
#include "esphome/components/sensor/sensor.h"

using esphome::display::Display;
using esphome::font::Font;

//==============================================================================
//  GaugeTile  –  Eingebettetes Rendering (ohne separaten Renderer)
//==============================================================================
class GaugeTile : public Tile
{
public:
  struct Thresholds
  {
    float red{0}, yellow{0};
  };

  struct Cfg
  {
    esphome::sensor::Sensor *value{nullptr};
    float min_v{0.0f};
    float max_v{100.0f};
    Thresholds thresholds{};
    std::string unit{"%"};
    std::string format{"%.1f"};
  };

  GaugeTile(DisplayContext &ctx, uint8_t col, uint8_t row,
            std::string label, const Cfg &cfg)
      : Tile(ctx, col, row, (uint8_t)1, std::move(label)),
        cfg_(cfg), min_(cfg.min_v), max_(cfg.max_v), thr_(cfg.thresholds),
        unit_(cfg.unit), fmt_(cfg.format)
  {
    this->bind_sensor_();
  }

  GaugeTile(uint8_t col, uint8_t row, std::string label,
            float min_v, float max_v, Thresholds thr,
            std::string unit = "%", const char *fmt = "%.1f")
      : GaugeTile(get_display_ctx(), col, row, std::move(label),
                  Cfg{nullptr, min_v, max_v, thr, std::move(unit), fmt})
  {
  }

  void set_value(float v) { val_ = v; request_redraw(); }

  const char *tile_type_name() const override { return "Gauge"; }

protected:
  void render_update(Display &it) override
  {
    draw_content(it);
  }

  void draw_content(Display &it) override
  {
    this->ensure_font_();

    const int x0 = abs_x();
    const int y0 = abs_y();
    const int w = tile_w();
    const int h = tile_h();
    int cx = x0 + w / 2;
    int cy = y0 + h / 2;
    float value = std::isnan(val_) ? 0.0f : val_;

    if (significant_change(prev_gauge_val_, val_,0.05))
    {
      auto [cx0, cy0, cx1, cy1] = gauge_value_clip();
      it.start_clipping(cx0, cy0, cx1, cy1);
      clear_area_fast(it, cx0, cy0, cx1 - cx0, cy1 - cy0);
      // Geometrie
      float radius = w * 0.42f;
      float thickness = radius * 0.4f;
      float inner_radius = radius - thickness;

      float angle_range = 180.0f;
      float start_angle = 180.0f;

      float frac = (value - min_) / (max_ - min_);
      frac = std::clamp(frac, 0.0f, 1.0f);
      float filled_angle = start_angle + angle_range * frac;

      // Füllfarbe bestimmen
      esphome::Color fill_color = Colors::GREEN;
      if (value < thr_.red)
        fill_color = Colors::RED;
      else if (value < thr_.yellow)
        fill_color = Colors::YELLOW;

      // Arc in PixelBuffer rendern (1 DMA-Transfer statt ~120 Dreiecke)
      // Gauge-Arc geht von 180°-360° (oberer Halbkreis), reicht nur bis cy nach unten
      const int arc_margin = static_cast<int>(std::ceil(radius)) + 2;
      const int buf_x = cx - arc_margin;
      const int buf_y = cy - arc_margin;
      const int buf_w = 2 * arc_margin + 1;
      // Buffer-Höhe: nur bis cy + inner_radius (unterer Rand des Innenkreises)
      const int buf_h = arc_margin + static_cast<int>(std::ceil(inner_radius)) + 2;
      const int bcx = cx - buf_x;
      const int bcy = cy - buf_y;

      // Auf ESP32 den PixelBuffer auf max ~64KB begrenzen
      static constexpr int MAX_BUF_PIXELS = 256 * 128;
      const bool fits_in_buffer = (buf_w * buf_h <= MAX_BUF_PIXELS);

      if (fits_in_buffer && arc_buf_.ensure(buf_w, buf_h)) {
        arc_buf_.clear(PixelBuffer::to_565(Colors::TILE_BACKGROUND));

        // Kombinierter Single-Pass: Track + Fill + Inner Circle
        arc_buf_.draw_gauge_arc(bcx, bcy, radius, inner_radius,
                                start_angle, start_angle + angle_range,
                                filled_angle,
                                PixelBuffer::to_565(Colors::SCREEN_BACKGROUND),
                                PixelBuffer::to_565(fill_color),
                                PixelBuffer::to_565(Colors::TILE_BACKGROUND));

        arc_buf_.blit(it, buf_x, buf_y);
      } else {
        // Strip-basierter Fallback: kombinierter Single-Pass pro Strip
        const uint16_t bg565 = PixelBuffer::to_565(Colors::TILE_BACKGROUND);
        const uint16_t track565 = PixelBuffer::to_565(Colors::SCREEN_BACKGROUND);
        const uint16_t fill565 = PixelBuffer::to_565(fill_color);
        const float sa = start_angle, ea_full = start_angle + angle_range, ea_fill = filled_angle;
        const float ir = inner_radius, or_ = radius;
        const int bcx_l = bcx, bcy_l = bcy;

        arc_buf_.blit_strips(it, buf_x, buf_y, buf_w, buf_h, MAX_BUF_PIXELS, bg565,
          [&](PixelBuffer &strip, int y_off) {
            const int shifted_cy = bcy_l - y_off;
            strip.draw_gauge_arc(bcx_l, shifted_cy, or_, ir,
                                 sa, ea_full, ea_fill,
                                 track565, fill565, bg565);
          });
      }

      it.end_clipping();
      prev_gauge_val_ = val_;
    }
    if (!formatted_equals(prev_val_, val_, fmt_.c_str(), unit_))
    {
      auto [cx0, cy0, cx1, cy1] = number_value_clip();
      it.start_clipping(cx0, cy0, cx1, cy1);
      clear_area_fast(it, cx0, cy0, cx1 - cx0, cy1 - cy0);
      // Wert-Text
      char buf_val[16], buf_all[16];
      std::snprintf(buf_val, sizeof(buf_val), fmt_.c_str(), value);
      std::snprintf(buf_all, sizeof(buf_all), "%s%s", buf_val, unit_.c_str());
      it.print(cx, cy + h * 0.18f,
               gauge_font_, Colors::NORMAL_TEXT,
               esphome::display::TextAlign::CENTER,
               buf_all);
      it.end_clipping();
    }

    prev_val_    = val_;
  }

  std::string make_cache_key() const override
  {
    char b[32];
    std::snprintf(b, sizeof(b), fmt_.c_str(), val_);
    return std::string(b);
  }

private:
  void bind_sensor_()
  {
    if (cfg_.value == nullptr)
      return;
    val_ = cfg_.value->state;
    cfg_.value->add_on_state_callback([this](float value)
                                      {
      this->val_ = value;
      this->request_redraw(); });
  }

  void ensure_font_()
  {
    if (gauge_font_ != nullptr)
      return;
    const float h = float(ctx_.scr_h) / float(std::max(ctx_.rows, 1));
    gauge_font_ = ctx_.get_font_for_size(h * 0.25f);
  }

  Cfg cfg_;
  float val_{NAN}, min_, max_;
  Thresholds thr_;
  std::string unit_;
  std::string fmt_;
  Font *gauge_font_{nullptr};
  void reset_prev_values() override { prev_val_ = NAN; prev_gauge_val_ = NAN; }

  float prev_val_{NAN};
  float prev_gauge_val_{NAN};
  PixelBuffer arc_buf_;

  std::tuple<int, int, int, int> gauge_value_clip() const
  {
    const int top = abs_y() + tile_h() * 0.05f;
    const int bot = abs_y() + tile_h() * 0.5f;
    const int left = abs_x() + tile_w() * 0.05f;
    const int right = abs_x() + tile_w() * 0.95f;
    return {left, top, right, bot};
  }
  std::tuple<int, int, int, int> number_value_clip() const
  {
    const int top = abs_y() + tile_h() * 0.55f;
    const int bot = abs_y() + tile_h() * 0.8f;
    const int left = abs_x() + tile_w() * 0.05f;
    const int right = abs_x() + tile_w() * 0.95f;
    return {left, top, right, bot};
  }

  // Arc-Segment helper
  void draw_arc_segment(Display &it,
                        int cx, int cy,
                        float outer_r, float inner_r,
                        float start_deg, float end_deg,
                        esphome::Color color) const
  {
    const int segs = 60;
    if (end_deg <= start_deg)
      return;
    float start_rad = start_deg * M_PI / 180.0f;
    float end_rad = end_deg * M_PI / 180.0f;
    std::vector<std::pair<int, int>> pts;

    // outer arc
    for (int i = 0; i <= segs; ++i)
    {
      float ang = start_rad + (end_rad - start_rad) * (i / (float)segs);
      float rx = cx + cosf(ang) * outer_r;
      float ry = cy + sinf(ang) * outer_r;
      if (std::isfinite(rx) && std::isfinite(ry))
        pts.emplace_back(int(rx), int(ry));
    }
    // inner arc backwards
    for (int i = segs; i >= 0; --i)
    {
      float ang = start_rad + (end_rad - start_rad) * (i / (float)segs);
      float rx = cx + cosf(ang) * inner_r;
      float ry = cy + sinf(ang) * inner_r;
      if (std::isfinite(rx) && std::isfinite(ry))
        pts.emplace_back(int(rx), int(ry));
    }
    if (pts.size() >= 3)
    {
      for (size_t i = 1; i + 1 < pts.size(); ++i)
      {
        it.filled_triangle(pts[0].first, pts[0].second,
                           pts[i].first, pts[i].second,
                           pts[i + 1].first, pts[i + 1].second,
                           color);
      }
    }
  }
};

#endif // GAUGE_TILE_H
