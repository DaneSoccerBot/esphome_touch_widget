#ifndef LIGHT_TILE_H
#define LIGHT_TILE_H

#include <string>
#include "tile.h"
#include "colors.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/api/api_server.h"
#include "ha_action.h"
#include "switch_icon_renderer.h"

using esphome::font::Font;

namespace sens = esphome::sensor;
namespace api = esphome::api;

class LightTile : public Tile
{
public:
  struct Cfg
  {
    esphome::text_sensor::TextSensor *state; // String "on"/"off"
    std::string entity_id;                   // light.led_strip_kuche
  };

  LightTile(DisplayContext &ctx, uint8_t col, uint8_t row,
            std::string label,
            const Cfg &cfg)
      : Tile(ctx, col, row, 1, std::move(label)), cfg_(cfg)
  {
    // Touch‑Handler → toggeln
    add_touch_handler(TouchArea::ANY, [this]
                      { this->toggle(); });
    if (cfg_.state != nullptr)
    {
      cfg_.state->add_on_state_callback([this](const std::string &)
                                        { this->request_redraw(); });
    }
  }

  LightTile(uint8_t col, uint8_t row,
            std::string label,
            const Cfg &cfg)
      : LightTile(get_display_ctx(), col, row, std::move(label), cfg) {}

  const char *tile_type_name() const override { return "Light"; }

protected:
  bool is_on() const
  {
    return cfg_.state && cfg_.state->state == "on";
  }
  void draw_content(Display &it) override
  {
    esphome::tile_dashboard::SwitchIconRenderer::draw(
        it, abs_x() + tile_w() * 0.3, abs_y() + tile_h() * 0.15,
        tile_w() * 0.4, tile_h() * 0.6, is_on());
  }
  std::string make_cache_key() const override
  {
    // wenn TextSensor fehlt ⇒  "OFF"  (damit wird zur Not nichts gezeichnet)
    if (!cfg_.state)
      return "OFF";

    // ESPHome meldet den Light‑State exakt in Kleinbuchstaben
    return (cfg_.state->state == "on") ? "ON" : "OFF";
  }
    // ----- Minimaler Update-Bereich -------------------------------------------
    std::tuple<int,int,int,int> value_clip() const override {
      const int top = abs_y() + tile_h() * 0.14f;
      const int bot = abs_y() + tile_h() * 0.76f;
      const int left = abs_x() + tile_w() * 0.29f;
      const int right = abs_x() + tile_w() * 0.71f;
      return { left, top, right, bot };
    }

private:
  void toggle()
  {
    bool on = is_on();
    if (cfg_.state != nullptr)
      cfg_.state->publish_state(on ? "off" : "on");
    ESP_LOGD("Switch", "Setting switch state to %s", on ? "OFF" : "ON");
    request_redraw();

    ESP_LOGD("Light", "Toggling lights %s", !on ? "ON" : "OFF");
    tile_dashboard_ha::call_action(!on ? "light.turn_on" : "light.turn_off",
                                    {{"entity_id", cfg_.entity_id}});
  }

  Cfg cfg_;
};
#endif // LIGHT_TILE_H
