#ifndef TEXT_VALUE_TILE_H
#define TEXT_VALUE_TILE_H

#include <tuple>
#include <string>
#include <cstring>   // snprintf / strncat
#include <cmath>     // std::isnan
#include "tile/tile.h"

//==============================================================================
//  TextValueTile  –  kompakte Zahl + Einheit (früher CompactValueTile)
//==============================================================================
class TextValueTile : public Tile {
public:
  TextValueTile(uint8_t col, uint8_t row,
                std::string label,
                std::string unit,
                const char *fmt = "%.1f")
      : Tile(get_display_ctx(), col, row, (uint8_t)1, std::move(label)),
        unit_(std::move(unit)), fmt_(fmt) {}

  void set_value(float v) { value_ = v; }

protected:
  // ----- Inhalt zeichnen -----------------------------------------------------
  void draw_content(Display &it) override {
    char buf[32];
    if (!std::isnan(value_))
      snprintf(buf, sizeof(buf), fmt_, value_);
    else
      strcpy(buf, "N/A");
    strncat(buf, unit_.c_str(), sizeof(buf) - strlen(buf) - 1);

    it.print(abs_x() + tile_w() / 2,
             abs_y() + tile_h() * 0.5f,
             ctx_.font_value_compact, Colors::TEXT,
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
  float value_{NAN};
  std::string unit_;
  const char *fmt_;
};

#endif  // TEXT_VALUE_TILE_H
