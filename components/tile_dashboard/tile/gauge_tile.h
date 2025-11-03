#ifndef GAUGE_TILE_H
#define GAUGE_TILE_H

#include <tuple>
#include <vector>
#include <utility>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include "tile/tile.h"
#include "colors.h" // Farbkonstanten

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

  GaugeTile(uint8_t col, uint8_t row, std::string label,
            float min_v, float max_v, Thresholds thr,
            std::string unit = "%", const char *fmt = "%.1f")
      : Tile(get_display_ctx(), col, row, (uint8_t)1, std::move(label)),
        min_(min_v), max_(max_v), thr_(thr),
        unit_(std::move(unit)), fmt_(fmt)
  {
    // Font für Gauge-Wert berechnen, analog DisplayContext::set_grid
    auto &ctx = get_display_ctx();
    float h = float(ctx.scr_h) / float(ctx.rows);
    gauge_font_ = ctx.get_font_for_size(h * 0.25f);
  }

  void set_value(float v) { val_ = v; }

protected:
  void render_update(Display &it) override
  {
    draw_content(it);
  }

  void draw_content(Display &it) override
  {
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
      ctx_.bg_renderer.drawBgColor(it, cx0, cy0, cx1 - cx0, cy1 - cy0);
      // Geometrie
      float radius = w * 0.42f;
      float thickness = radius * 0.4f;
      float inner_radius = radius - thickness;

      float angle_range = 180.0f;
      float start_angle = 180.0f;

      float frac = (value - min_) / (max_ - min_);
      frac = std::clamp(frac, 0.0f, 1.0f);
      float filled_angle = start_angle + angle_range * frac;

      // Hintergrund-Segment
      draw_arc_segment(it, cx, cy, radius, inner_radius,
                       start_angle, start_angle + angle_range,
                       Colors::SCREEN_BACKGROUND);

      // Füllfarbe bestimmen
      esphome::Color fill_color = Colors::GREEN;
      if (value < thr_.red)
        fill_color = Colors::RED;
      else if (value < thr_.yellow)
        fill_color = Colors::YELLOW;

      // Vordergrund-Segment
      draw_arc_segment(it, cx, cy, radius, inner_radius,
                       start_angle, filled_angle,
                       fill_color);

      // Innenkreis
      it.filled_circle(cx, cy, inner_radius, Colors::TILE_BACKGROUND);
      it.end_clipping();
      prev_gauge_val_ = val_;
    }
    if (!formatted_equals(prev_val_, val_,fmt_, unit_))
    {
      auto [cx0, cy0, cx1, cy1] = number_value_clip();
      it.start_clipping(cx0, cy0, cx1, cy1);
      ctx_.bg_renderer.drawBgColor(it, cx0, cy0, cx1 - cx0, cy1 - cy0);
      // Wert-Text
      char buf_val[16], buf_all[16];
      std::snprintf(buf_val, sizeof(buf_val), fmt_, value);
      std::snprintf(buf_all, sizeof(buf_all), "%s%s", buf_val, unit_.c_str());
      it.print(cx, cy + h * 0.18f,
               gauge_font_, Colors::TEXT,
               esphome::display::TextAlign::CENTER,
               buf_all);
      it.end_clipping();
    }

    prev_val_    = val_;
  }

  std::string make_cache_key() const override
  {
    char b[32];
    std::snprintf(b, sizeof(b), fmt_, val_);
    return std::string(b);
  }

private:
  float val_{NAN}, min_, max_;
  Thresholds thr_;
  std::string unit_;
  const char *fmt_;
  Font *gauge_font_{nullptr};
  float prev_val_{NAN};
  float prev_gauge_val_{NAN};

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
