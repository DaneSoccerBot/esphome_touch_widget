#ifndef BATTERY_TILE_H
#define BATTERY_TILE_H

#include <tuple>
#include <algorithm> // std::clamp
#include <cmath>     // std::round
#include <cstdio>    // std::snprintf
#include "tile.h"
#include "colors.h"  // Color definitions
#include "render_primitives.h"
#include "esphome/components/sensor/sensor.h"

using esphome::display::Display;

//==============================================================================
//  BatteryTile  –  direkte Einbettung des Akku-Rendercodes in die Tile-Klasse
//==============================================================================
class BatteryTile : public Tile
{
public:
  struct Cfg
  {
    esphome::sensor::Sensor *level{nullptr};
  };

  BatteryTile(DisplayContext &ctx, uint8_t col, uint8_t row,
              std::string label, const Cfg &cfg)
      : Tile(ctx, col, row, (uint8_t)1, std::move(label)), cfg_(cfg)
  {
    this->bind_sensor_();
  }

  BatteryTile(DisplayContext &ctx, uint8_t col, uint8_t row,
              std::string label = "BATTERY")
      : BatteryTile(ctx, col, row, std::move(label), Cfg{}) {}

  BatteryTile(uint8_t col, uint8_t row, std::string label = "BATTERY")
      : BatteryTile(get_display_ctx(), col, row, std::move(label), {}) {}

  void set_level(float pct)
  {
    level_ = pct;
    request_redraw();
  }

  const char *tile_type_name() const override { return "Battery"; }

protected:
  void render_update(Display &it) override
  {
    draw_content(it);
  }
  void draw_content(Display &it) override
  {
    // Werte clampen
    float lvl = std::clamp(level_, 0.0f, 100.0f);

    if (!significant_change(prev_val_, lvl, 0.05) && lvl != 0.0f && lvl != 100.0f)
    {
      return;
    }
    prev_val_ = lvl;

    auto [cx0, cy0, cx1, cy1] = value_clip();
    it.start_clipping(cx0, cy0, cx1, cy1);

    // Farben bestimmen
    esphome::Color fill_color =
        (lvl < 20.0f) ? Colors::RED : (lvl < 50.0f) ? Colors::YELLOW
                                                    : Colors::GREEN;
    const esphome::Color outline = Colors::LIGHT_TEXT;

    // Geometrie
    const int bar_w = tile_w() * 0.6f;
    const int bar_h = tile_h() * 0.3f;
    const int x = abs_x() + tile_w() * 0.2f;
    const int y = abs_y() + tile_h() * 0.35f;

    const int clip_w = cx1 - cx0;
    const int clip_h = cy1 - cy0;
    const uint16_t bg565 = PixelBuffer::to_565(Colors::TILE_BACKGROUND);
    if (battery_buf_.ensure(clip_w, clip_h)) {
      battery_buf_.clear(bg565);
      esphome::tile_dashboard::render::BatchPrimitives::draw_segmented_battery(
          battery_buf_,
          esphome::tile_dashboard::render::SegmentedBatterySpec{
              x - cx0,
              y - cy0,
              bar_w,
              bar_h,
              lvl,
              18,
              PixelBuffer::to_565(fill_color),
              PixelBuffer::to_565(outline),
          });
      battery_buf_.blit(it, cx0, cy0);
    } else {
      clear_area_fast(it, cx0, cy0, clip_w, clip_h);
    }

    it.end_clipping();
  }

  // ----- Cache-Key ---------------------------------------------------------
  std::string make_cache_key() const override
  {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%.0f", level_);
    return std::string(buf);
  }

  // ----- Minimaler Update-Bereich -----------------------------------------
  std::tuple<int, int, int, int> value_clip() const override
  {
    const int top = abs_y() + tile_h() * 0.33f;
    const int bot = abs_y() + tile_h() * 0.67f;
    const int left = abs_x() + tile_w() * 0.15f;
    const int right = abs_x() + tile_w() * 0.85f;
    return {left, top, right, bot};
  }

private:
  void bind_sensor_()
  {
    if (cfg_.level == nullptr)
      return;
    level_ = cfg_.level->state;
    cfg_.level->add_on_state_callback([this](float value)
                                      {
      this->level_ = value;
      this->request_redraw(); });
  }

  void reset_prev_values() override { prev_val_ = NAN; }

  Cfg cfg_;
  float level_{-1.0f};
  float prev_val_{NAN};
  PixelBuffer battery_buf_;
};

#endif // BATTERY_TILE_H
