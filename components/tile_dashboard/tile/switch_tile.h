#ifndef SWITCH_TILE_H
#define SWITCH_TILE_H

#include <string>
#include "tile/tile.h"
#include "colors.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/api/api_server.h"
#include "esphome/components/api/homeassistant_service.h"
#include "tile/switch_icon_renderer.h"

using esphome::font::Font;

namespace sens = esphome::sensor;
namespace api  = esphome::api;

class SwitchTile : public Tile {
 public:
  struct Cfg {
    esphome::switch_::Switch *sw;
    std::string               entity_id;
  };

  SwitchTile(uint8_t col, uint8_t row,
             std::string label,
             const Cfg &cfg)
    : Tile(get_display_ctx(), col, row, /* numTilesY=*/1, std::move(label))
    , cfg_(cfg)
  {
    add_touch_handler(TouchArea::ANY, [this]() { this->toggle(); });
  }

 protected:
 void draw_content(Display &it) override {
    // Zustand abfragen
    bool on = (cfg_.sw && cfg_.sw->state);
    esphome::tile_dashboard::SwitchIconRenderer::draw(it, abs_x()+tile_w()*0.3, abs_y()+tile_h()*0.15, tile_w()*0.4, tile_h()*0.6, on);
  }
  

  std::string make_cache_key() const override {
    return (cfg_.sw && cfg_.sw->state) ? "ON" : "OFF";
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
  void toggle() {
    const bool on = cfg_.sw && cfg_.sw->state;
    cfg_.sw->state = !on;
    ESP_LOGD("Switch", "Setting switch state to %s", on ? "OFF" : "ON");
    request_redraw();
    api::HomeAssistantServiceCallAction<> call(api::global_api_server, false);
    call.set_service(!on ? "switch.turn_on" : "switch.turn_off");
    call.add_data("entity_id", cfg_.entity_id);
    call.play();
  }

  Cfg cfg_;
};

#endif  // SWITCH_TILE_H
