#ifndef TILE_DASHBOARD_RENDER_PRIMITIVES_H
#define TILE_DASHBOARD_RENDER_PRIMITIVES_H

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "pixel_buffer.h"

namespace esphome {
namespace tile_dashboard {
namespace render {

struct ProgressArcSpec {
  int cx{0};
  int cy{0};
  float outer_radius{0.0f};
  float inner_radius{0.0f};
  float start_deg{0.0f};
  float span_deg{0.0f};
  float fill_span_deg{0.0f};
  uint16_t track_color{0};
  uint16_t fill_color{0};
  bool marker{false};
  float marker_span_deg{0.0f};
  int marker_radius{0};
  uint16_t marker_color{0};
};

struct SegmentedBatterySpec {
  int x{0};
  int y{0};
  int w{0};
  int h{0};
  float level{0.0f};
  int segments{18};
  uint16_t fill_color{0};
  uint16_t outline_color{0};
};

namespace detail {

struct ArcGate {
  float sx{1.0f};
  float sy{0.0f};
  float ex{1.0f};
  float ey{0.0f};
  float span{0.0f};

  ArcGate(float start_deg, float span_deg) : span(span_deg) {
    constexpr float DEG2RAD = static_cast<float>(M_PI) / 180.0f;
    const float sr = start_deg * DEG2RAD;
    const float er = (start_deg + span_deg) * DEG2RAD;
    sx = std::cos(sr);
    sy = std::sin(sr);
    ex = std::cos(er);
    ey = std::sin(er);
  }

  bool contains(float dx, float dy) const {
    if (span <= 0.0f)
      return false;
    if (span >= 359.9f)
      return true;

    const float cs = dx * sy - dy * sx;
    const float ce = dx * ey - dy * ex;
    if (span >= 180.0f)
      return cs <= 0.0f || ce >= 0.0f;
    return cs <= 0.0f && ce >= 0.0f;
  }
};

}  // namespace detail

class BatchPrimitives {
 public:
  static void draw_rect_outline(PixelBuffer &buf, int x, int y, int w, int h,
                                uint16_t color) {
    if (!buf.valid() || w <= 0 || h <= 0)
      return;
    buf.fill_rect(x, y, w, 1, color);
    buf.fill_rect(x, y + h - 1, w, 1, color);
    buf.fill_rect(x, y, 1, h, color);
    buf.fill_rect(x + w - 1, y, 1, h, color);
  }

  static void draw_rounded_rect(PixelBuffer &buf, int x, int y, int w, int h,
                                int radius, uint16_t color) {
    if (!buf.valid() || w <= 0 || h <= 0)
      return;
    const int r = std::max(0, std::min(radius, std::min(w, h) / 2));
    if (r <= 0) {
      buf.fill_rect(x, y, w, h, color);
      return;
    }

    buf.fill_rect(x + r, y + r, w - 2 * r, h - 2 * r, color);
    buf.fill_rect(x + r, y, w - 2 * r, r, color);
    buf.fill_rect(x + r, y + h - r, w - 2 * r, r, color);
    buf.fill_rect(x, y + r, r, h - 2 * r, color);
    buf.fill_rect(x + w - r, y + r, r, h - 2 * r, color);
    buf.filled_circle(x + r, y + r, r, color);
    buf.filled_circle(x + w - r - 1, y + r, r, color);
    buf.filled_circle(x + r, y + h - r - 1, r, color);
    buf.filled_circle(x + w - r - 1, y + h - r - 1, r, color);
  }

  static void draw_ring(PixelBuffer &buf, int cx, int cy, int outer_radius,
                        int inner_radius, uint16_t color, uint16_t inner_color) {
    if (!buf.valid() || outer_radius <= 0)
      return;
    buf.filled_circle(cx, cy, outer_radius, color);
    if (inner_radius > 0)
      buf.filled_circle(cx, cy, inner_radius, inner_color);
  }

  static void draw_progress_arc(PixelBuffer &buf, const ProgressArcSpec &spec) {
    if (!buf.valid() || spec.outer_radius <= 0.0f)
      return;

    const float outer_r = std::max(spec.outer_radius, spec.inner_radius);
    const float inner_r = std::max(0.0f, std::min(spec.inner_radius, outer_r));
    const float outer2 = outer_r * outer_r;
    const float inner2 = inner_r * inner_r;

    const detail::ArcGate track(spec.start_deg, spec.span_deg);
    const detail::ArcGate fill(
        spec.start_deg,
        std::clamp(spec.fill_span_deg, 0.0f, spec.span_deg));

    const int r = static_cast<int>(std::ceil(outer_r)) + 1;
    const int y0 = std::max(0, spec.cy - r);
    const int y1 = std::min(buf.height() - 1, spec.cy + r);

    auto draw_ring_range = [&](uint16_t *row, int x0, int x1, float dy) {
      for (int x = x0; x <= x1; x++) {
        const float dx = static_cast<float>(x - spec.cx);
        if (!track.contains(dx, dy))
          continue;
        row[x] = fill.contains(dx, dy) ? spec.fill_color : spec.track_color;
      }
    };

    for (int y = y0; y <= y1; y++) {
      const float dy = static_cast<float>(y - spec.cy);
      const float dy2 = dy * dy;
      const float outer_dx2 = outer2 - dy2;
      if (outer_dx2 < 0.0f)
        continue;

      const int outer_dx = static_cast<int>(std::sqrt(outer_dx2));
      const int x_lo = std::max(0, spec.cx - outer_dx);
      const int x_hi = std::min(buf.width() - 1, spec.cx + outer_dx);
      if (x_lo > x_hi)
        continue;

      uint16_t *row = buf.row_data(y);
      const float inner_dx2 = inner2 - dy2;
      if (inner_dx2 > 0.0f) {
        const int inner_dx = static_cast<int>(std::sqrt(inner_dx2));
        const int ix_lo = std::max(x_lo, spec.cx - inner_dx);
        const int ix_hi = std::min(x_hi, spec.cx + inner_dx);
        if (x_lo < ix_lo)
          draw_ring_range(row, x_lo, ix_lo - 1, dy);
        if (ix_hi < x_hi)
          draw_ring_range(row, ix_hi + 1, x_hi, dy);
      } else {
        draw_ring_range(row, x_lo, x_hi, dy);
      }
    }

    if (spec.marker && spec.marker_radius > 0) {
      constexpr float DEG2RAD = static_cast<float>(M_PI) / 180.0f;
      const float marker_deg =
          spec.start_deg + std::clamp(spec.marker_span_deg, 0.0f, spec.span_deg);
      const float rad = marker_deg * DEG2RAD;
      const float marker_r = (outer_r + inner_r) * 0.5f;
      const int mx = spec.cx + static_cast<int>(std::round(std::cos(rad) * marker_r));
      const int my = spec.cy + static_cast<int>(std::round(std::sin(rad) * marker_r));
      buf.filled_circle(mx, my, spec.marker_radius, spec.marker_color);
    }
  }

  static void draw_segmented_battery(PixelBuffer &buf,
                                     const SegmentedBatterySpec &spec) {
    if (!buf.valid() || spec.w <= 0 || spec.h <= 0 || spec.segments <= 0)
      return;

    draw_rect_outline(buf, spec.x - 2, spec.y, spec.w - 1, spec.h,
                      spec.outline_color);

    const int tip_w = std::max(1, static_cast<int>(spec.w * 0.04f));
    const int tip_h = std::max(1, spec.h / 3);
    const int tip_x = spec.x + spec.w;
    const int tip_y = spec.y + spec.h / 3;
    buf.fill_rect(tip_x, tip_y, tip_w, tip_h, spec.outline_color);

    const int gap = 1;
    const int seg_w = std::max(1, (spec.w - (spec.segments + 1) * gap) / spec.segments);
    const int seg_h = std::max(1, spec.h - 4);
    const int seg_y = spec.y + 2;
    const int filled = static_cast<int>(
        std::round((std::clamp(spec.level, 0.0f, 100.0f) / 100.0f) * spec.segments));

    for (int i = 0; i < spec.segments; ++i) {
      const int seg_x = spec.x + gap + i * (seg_w + gap);
      if (i < filled) {
        buf.fill_rect(seg_x, seg_y, seg_w, seg_h, spec.fill_color);
      } else {
        draw_rect_outline(buf, seg_x, seg_y, seg_w, seg_h, spec.outline_color);
      }
    }
  }
};

}  // namespace render
}  // namespace tile_dashboard
}  // namespace esphome

#endif  // TILE_DASHBOARD_RENDER_PRIMITIVES_H
