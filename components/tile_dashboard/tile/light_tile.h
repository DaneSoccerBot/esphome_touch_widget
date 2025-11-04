#ifndef LIGHT_TILE_H
#define LIGHT_TILE_H

#include <string>
#include "tile/tile.h"
#include "colors.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/api/api_server.h"
#include "esphome/components/api/homeassistant_service.h"
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

  LightTile(uint8_t col, uint8_t row,
            std::string label,
            const Cfg &cfg)
      : Tile(get_display_ctx(), col, row, 1, std::move(label)), cfg_(cfg)
  {
    // Touch‑Handler → toggeln
    add_touch_handler(TouchArea::ANY, [this]
                      { this->toggle(); });
  }

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
    if (on)
      cfg_.state->state = "off";
    else
      cfg_.state->state = "on";
    ESP_LOGD("Switch", "Setting switch state to %s", on ? "OFF" : "ON");
    request_redraw();

    api::HomeAssistantServiceCallAction<> svc(api::global_api_server, false);
    if (!on)
    { // → Einschalten
      ESP_LOGD("Light", "Toggling lights ON");
      svc.set_service("light.turn_on");
      svc.add_data("entity_id", cfg_.entity_id);
      //svc.add_data("brightness", "255");          // 255 = 100 %
      //svc.add_data("rgb_color", "[255,255,255]");      
    }
    else
    { // → Ausschalten
      ESP_LOGD("Light", "Toggling lights OFF");
      svc.set_service("light.turn_off");
      svc.add_data("entity_id", cfg_.entity_id);
    }
    svc.play();
  }

  Cfg cfg_;
};
#endif // LIGHT_TILE_H