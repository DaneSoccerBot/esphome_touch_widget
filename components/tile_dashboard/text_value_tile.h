#ifndef TEXT_VALUE_TILE_H
#define TEXT_VALUE_TILE_H

#include <tuple>
#include <string>
#include <cstring>   // snprintf / strncat
#include <cmath>     // std::isnan
#include "tile.h"
#include "esphome/components/sensor/sensor.h"

//==============================================================================
//  TextValueTile  –  kompakte Zahl + Einheit (früher CompactValueTile)
//==============================================================================
class TextValueTile : public Tile {
public:
  struct Cfg {
    esphome::sensor::Sensor *value{nullptr};
  };

  TextValueTile(DisplayContext &ctx, uint8_t col, uint8_t row,
                std::string label,
                std::string unit,
                std::string fmt,
                const Cfg &cfg)
      : Tile(ctx, col, row, (uint8_t)1, std::move(label)),
        cfg_(cfg), unit_(std::move(unit)), fmt_(std::move(fmt)) {
    this->bind_sensor_();
  }

  TextValueTile(DisplayContext &ctx, uint8_t col, uint8_t row,
                std::string label,
                std::string unit,
                std::string fmt = "%.1f")
      : TextValueTile(ctx, col, row, std::move(label), std::move(unit),
                      std::move(fmt), Cfg{}) {}

  TextValueTile(uint8_t col, uint8_t row,
                std::string label,
                std::string unit,
                const char *fmt = "%.1f")
      : TextValueTile(get_display_ctx(), col, row, std::move(label),
                      std::move(unit), fmt, {}) {}

  void set_value(float v) {
    value_ = v;
    request_redraw();
  }

protected:
  // ----- Inhalt zeichnen -----------------------------------------------------
  void draw_content(Display &it) override {
    char buf[32];
    if (!std::isnan(value_))
      snprintf(buf, sizeof(buf), fmt_.c_str(), value_);
    else
      strcpy(buf, "N/A");
    strncat(buf, unit_.c_str(), sizeof(buf) - strlen(buf) - 1);

    it.print(abs_x() + tile_w() / 2,
             abs_y() + tile_h() * 0.5f,
             ctx_.font_value_compact, Colors::NORMAL_TEXT,
             esphome::display::TextAlign::CENTER, buf);
  }

  // ----- Cache-Key -----------------------------------------------------------
  std::string make_cache_key() const override {
    char b[32];
    if (!std::isnan(value_))
      snprintf(b, sizeof(b), "%s%f", unit_.c_str(), value_);
    else
      strcpy(b, "nan");
    return b;
  }

  // ----- Minimaler Update-Bereich -------------------------------------------
  std::tuple<int,int,int,int> value_clip() const override {
    const int top = abs_y() + tile_h() * 0.35f;
    const int bot = abs_y() + tile_h() * 0.65f;
    const int left = abs_x() + tile_w() * 0.05f;
    const int right = abs_x() + tile_w() * 0.95f;
    return { left, top, right, bot };
  }

private:
  void bind_sensor_() {
    if (cfg_.value == nullptr)
      return;
    value_ = cfg_.value->state;
    cfg_.value->add_on_state_callback([this](float value) {
      this->value_ = value;
      this->request_redraw();
    });
  }

  Cfg cfg_;
  float value_{NAN};
  std::string unit_;
  std::string fmt_;
};

#endif  // TEXT_VALUE_TILE_H
