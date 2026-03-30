#ifndef TILE_DASHBOARD_COMPONENT_H
#define TILE_DASHBOARD_COMPONENT_H

#include <algorithm>
#include <utility>

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/display/display.h"
#include "esphome/components/touchscreen/touchscreen.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/switch/switch.h"

#include "display_context.h"
#include "integration.h"
#include "tile_dashboard.h"
#include "battery_tile.h"
#include "climate_tile.h"
#include "double_value_tile.h"
#include "gauge_tile.h"
#include "light_tile.h"
#include "switch_tile.h"
#include "text_value_tile.h"

namespace esphome {
namespace tile_dashboard {

static const char *const TAG = "tile_dashboard";

class TileDashboardComponent : public Component, public touchscreen::TouchListener {
 public:
  TileDashboardComponent() : dashboard_(ctx_) {}

  float get_setup_priority() const override { return setup_priority::LATE; }

  void setup() override {
    if (this->display_ == nullptr) {
      ESP_LOGE(TAG, "No display configured");
      this->mark_failed();
      return;
    }

    this->configure_context_();
    this->display_->set_writer([this](display::Display &it) { this->render_(it); });

    if (this->touchscreen_ != nullptr) {
      this->touchscreen_->register_listener(this);
    }
  }

  void dump_config() override {
    ESP_LOGCONFIG(TAG, "Tile Dashboard:");
    if (this->display_ != nullptr) {
      ESP_LOGCONFIG(TAG, "  Display: %dx%d", this->display_->get_width(), this->display_->get_height());
    }
    ESP_LOGCONFIG(TAG, "  Grid: %u cols x %u rows", this->cols_, this->rows_);
    ESP_LOGCONFIG(TAG, "  Dashboard area: %u x %u", this->width_, this->height_);
    ESP_LOGCONFIG(TAG, "  Offset: %d,%d", this->offset_x_, this->offset_y_);
    ESP_LOGCONFIG(TAG, "  Touch rotation: %d", this->touch_rotation_);
    ESP_LOGCONFIG(TAG, "  Touchscreen: %s", this->touchscreen_ != nullptr ? "configured" : "not configured");
  }

  void touch(touchscreen::TouchPoint tp) override {
    auto mapped = this->map_touch_(tp.x, tp.y);
    if (!mapped.inside)
      return;
    this->last_touch_ = mapped;
    this->last_touch_valid_ = true;
    if (this->dashboard_.is_fullscreen()) {
      this->dashboard_.dispatch_touch(0, 0, mapped.local_x_raw, mapped.local_y_raw);
    } else {
      this->dashboard_.dispatch_touch(mapped.col, mapped.row, mapped.local_x, mapped.local_y);
    }
  }

  void update(const touchscreen::TouchPoints_t &tpoints) override {
    for (const auto &tp : tpoints) {
      auto mapped = this->map_touch_(tp.x, tp.y);
      if (!mapped.inside)
        continue;
      this->last_touch_ = mapped;
      this->last_touch_valid_ = true;
      if (this->dashboard_.is_fullscreen()) {
        this->dashboard_.dispatch_touch_update(0, 0, mapped.local_x_raw, mapped.local_y_raw);
      } else {
        this->dashboard_.dispatch_touch_update(mapped.col, mapped.row, mapped.local_x, mapped.local_y);
      }
      break;
    }
  }

  void release() override {
    if (!this->last_touch_valid_)
      return;
    if (this->dashboard_.is_fullscreen()) {
      this->dashboard_.dispatch_touch_release(
          0, 0, this->last_touch_.local_x_raw, this->last_touch_.local_y_raw);
    } else {
      this->dashboard_.dispatch_touch_release(
          this->last_touch_.col, this->last_touch_.row,
          this->last_touch_.local_x, this->last_touch_.local_y);
    }
    this->last_touch_valid_ = false;
  }

  void set_display(display::Display *display) { this->display_ = display; }
  void set_touchscreen(touchscreen::Touchscreen *touchscreen) { this->touchscreen_ = touchscreen; }

  void set_layout(uint16_t width, uint16_t height, uint8_t cols, uint8_t rows,
                  int16_t offset_x, int16_t offset_y) {
    this->width_ = width;
    this->height_ = height;
    this->cols_ = std::max<uint8_t>(cols, 1);
    this->rows_ = std::max<uint8_t>(rows, 1);
    this->offset_x_ = offset_x;
    this->offset_y_ = offset_y;
    this->context_ready_ = false;
    this->screen_initialized_ = false;
  }

  void set_touch_rotation(uint16_t rotation) { this->touch_rotation_ = rotation; }

  void set_roboto_fonts(Font *f12, Font *f14, Font *f16, Font *f18,
                        Font *f20, Font *f25, Font *f30, Font *f35,
                        Font *f40, Font *f45, Font *f50, Font *f60,
                        Font *f70, Font *f80, Font *f90) {
    this->ctx_.set_roboto_fonts(
        f12, f14, f16, f18, f20, f25, f30, f35, f40, f45, f50, f60, f70, f80, f90);
  }

  void add_battery_tile(uint8_t col, uint8_t row, std::string label,
                        sensor::Sensor *sensor, bool fullscreen = false) {
    auto &t = this->dashboard_.add_tile<BatteryTile>(
        this->ctx_, col, row, std::move(label), BatteryTile::Cfg{sensor});
    t.set_fullscreen_enabled(fullscreen);
  }

  void add_text_value_tile(uint8_t col, uint8_t row, std::string label,
                           std::string unit, std::string fmt,
                           sensor::Sensor *sensor, bool fullscreen = false) {
    auto &t = this->dashboard_.add_tile<TextValueTile>(
        this->ctx_, col, row, std::move(label), std::move(unit),
        std::move(fmt), TextValueTile::Cfg{sensor});
    t.set_fullscreen_enabled(fullscreen);
  }

  void add_double_value_tile(uint8_t col, uint8_t row,
                             std::string top_label, std::string bottom_label,
                             std::string top_unit, std::string bottom_unit,
                             std::string fmt_top, std::string fmt_bottom,
                             sensor::Sensor *top_sensor,
                             sensor::Sensor *bottom_sensor, bool fullscreen = false) {
    auto &t = this->dashboard_.add_tile<DoubleValueTile>(
        this->ctx_, col, row, std::move(top_label), std::move(bottom_label),
        std::move(top_unit), std::move(bottom_unit), std::move(fmt_top),
        std::move(fmt_bottom), DoubleValueTile::Cfg{top_sensor, bottom_sensor});
    t.set_fullscreen_enabled(fullscreen);
  }

  void add_gauge_tile(uint8_t col, uint8_t row, std::string label,
                      sensor::Sensor *sensor, float min_value, float max_value,
                      float red_threshold, float yellow_threshold,
                      std::string unit, std::string fmt, bool fullscreen = false) {
    auto &t = this->dashboard_.add_tile<GaugeTile>(
        this->ctx_, col, row, std::move(label),
        GaugeTile::Cfg{
            sensor,
            min_value,
            max_value,
            GaugeTile::Thresholds{red_threshold, yellow_threshold},
            std::move(unit),
            std::move(fmt),
        });
    t.set_fullscreen_enabled(fullscreen);
  }

  void add_climate_tile(uint8_t col, uint8_t row, std::string label,
                        text_sensor::TextSensor *payload,
                        std::string entity_id, bool fullscreen = false) {
    auto &t = this->dashboard_.add_tile<ClimateTile>(
        this->ctx_, col, row, std::move(label),
        ClimateTile::Cfg{payload, std::move(entity_id)});
    t.set_fullscreen_enabled(fullscreen);
  }

  void add_switch_tile(uint8_t col, uint8_t row, std::string label,
                       switch_::Switch *sw, std::string entity_id, bool fullscreen = false) {
    auto &t = this->dashboard_.add_tile<SwitchTile>(
        this->ctx_, col, row, std::move(label),
        SwitchTile::Cfg{sw, std::move(entity_id)});
    t.set_fullscreen_enabled(fullscreen);
  }

  void add_light_tile(uint8_t col, uint8_t row, std::string label,
                      text_sensor::TextSensor *state, std::string entity_id, bool fullscreen = false) {
    auto &t = this->dashboard_.add_tile<LightTile>(
        this->ctx_, col, row, std::move(label),
        LightTile::Cfg{state, std::move(entity_id)});
    t.set_fullscreen_enabled(fullscreen);
  }

 protected:
  void render_(display::Display &it) {
    this->configure_context_();
    if (!this->screen_initialized_) {
      it.fill(Colors::SCREEN_BACKGROUND);
      this->screen_initialized_ = true;
    }
    if (this->was_fullscreen_ && !this->dashboard_.is_fullscreen()) {
      // returning from fullscreen → clear and force full grid redraw
      it.fill(Colors::SCREEN_BACKGROUND);
      this->was_fullscreen_ = false;
    }
    if (!this->was_fullscreen_ && this->dashboard_.is_fullscreen()) {
      // entering fullscreen → clear screen for the focused tile
      it.fill(Colors::SCREEN_BACKGROUND);
      this->was_fullscreen_ = true;
    }
    this->dashboard_.draw(it);
  }

  void configure_context_() {
    if (this->display_ == nullptr)
      return;

    const int resolved_width = this->width_ > 0 ? this->width_ : this->display_->get_width();
    const int resolved_height = this->height_ > 0 ? this->height_ : this->display_->get_height();

    if (this->context_ready_ &&
        this->ctx_.scr_w == resolved_width &&
        this->ctx_.scr_h == resolved_height &&
        this->ctx_.cols == this->cols_ &&
        this->ctx_.rows == this->rows_ &&
        this->ctx_.x0 == this->offset_x_ &&
        this->ctx_.y0 == this->offset_y_) {
      return;
    }

    this->ctx_.set_grid(
        resolved_width, resolved_height, this->cols_, this->rows_,
        this->offset_x_, this->offset_y_);
    this->context_ready_ = true;
    this->screen_initialized_ = false;
  }

  TouchMapping map_touch_(int x, int y) {
    this->configure_context_();
    const int width = this->ctx_.scr_w > 0 ? this->ctx_.scr_w : 1;
    const int height = this->ctx_.scr_h > 0 ? this->ctx_.scr_h : 1;
    return map_touch(
        x, y, width, height, this->cols_, this->rows_, this->touch_rotation_,
        this->offset_x_, this->offset_y_);
  }

  display::Display *display_{nullptr};
  touchscreen::Touchscreen *touchscreen_{nullptr};
  DisplayContext ctx_;
  Dashboard dashboard_;

  uint16_t width_{0};
  uint16_t height_{0};
  uint8_t cols_{1};
  uint8_t rows_{1};
  int16_t offset_x_{0};
  int16_t offset_y_{0};
  uint16_t touch_rotation_{0};

  bool context_ready_{false};
  bool screen_initialized_{false};
  bool was_fullscreen_{false};
  bool last_touch_valid_{false};
  TouchMapping last_touch_{};
};

}  // namespace tile_dashboard
}  // namespace esphome

#endif  // TILE_DASHBOARD_COMPONENT_H
