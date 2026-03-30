#ifndef TILE_DASHBOARD_H
#define TILE_DASHBOARD_H
// -----------------------------------------------------------------------------
//  tile_dashboard.h  –  Header‑only Dashboard/Tile Library for ESPHome GT911
// -----------------------------------------------------------------------------
//  Features
//  --------
//  • DisplayContext  – Fonts, Grid, Renderer, Cache  (singleton: get_display_ctx())
//  • Tile base class – Template‑Method‑Pattern, Touch‑Handling, Caching
//  • Finished Tiles  – TextValueTile, BatteryTile, ClimateTile, GaugeTile
//  • Dashboard       – Container + Touch‑Routing  (singleton: get_dashboard())
// -----------------------------------------------------------------------------
//  Usage (short):
//     #include "tile_dashboard.h"
//     auto &ctx  = get_display_ctx();
//     auto &dash = get_dashboard();
//     dash.add_tile<BatteryTile>(1,1,"Batt").set_level(50);
//     dash.draw(it);
// -----------------------------------------------------------------------------
#pragma once

#include "esphome.h"
#include "esphome/components/display/display.h"
#include "esphome/components/font/font.h"

#include "colors.h"

#include <vector>
#include <string>
#include <memory>
#include <map>
#include <functional>
#include <cmath>
#include <utility>
#include "display_context.h"
#include "tile.h"    // Tile base class + DisplayContext

using esphome::display::Display;

//==============================================================================
//  Dashboard – Container aller Tiles + Touch‑Router
//==============================================================================
class Dashboard {
public:
  explicit Dashboard(DisplayContext &ctx):ctx_(ctx){}

  template<class T, class... Args>
  T &add_tile(Args&&...args){
    auto u=std::make_unique<T>(std::forward<Args>(args)...);
    T &ref=*u; tiles_.push_back(std::move(u)); return ref;
  }

  void draw(Display &it){
    if (focused_tile_ != nullptr) {
      focused_tile_->draw(it);
      draw_close_button_(it);
      return;
    }
    for(auto &t:tiles_) t->draw(it);
  }

  void clear(){ tiles_.clear(); focused_tile_ = nullptr; }

  bool is_fullscreen() const { return focused_tile_ != nullptr; }

  void enter_fullscreen(Tile *t) {
    focused_tile_ = t;
    t->set_override_geometry({ctx_.x0, ctx_.y0, ctx_.scr_w, ctx_.scr_h});
  }

  void exit_fullscreen() {
    if (focused_tile_ == nullptr) return;
    focused_tile_->clear_override_geometry();
    focused_tile_ = nullptr;
    // invalidate all tiles so the grid redraws fully
    for (auto &t : tiles_) t->invalidate_cache();
  }

  void dispatch_touch(uint8_t col,uint8_t row,uint16_t local_x,uint16_t local_y){
    if (focused_tile_ != nullptr) {
      if (close_button_hit_(local_x, local_y)) { exit_fullscreen(); return; }
      focused_tile_->dispatch_touch(local_x, local_y);
      return;
    }
    for(auto &t:tiles_){
      if(t->col()==col && t->row()==row){
        if (t->fullscreen_enabled()) { enter_fullscreen(t.get()); return; }
        t->dispatch_touch(local_x, local_y);
        break;
      }
    }
  }

  void dispatch_touch_update(uint8_t col,uint8_t row,uint16_t local_x,uint16_t local_y){
    if (focused_tile_ != nullptr) {
      focused_tile_->dispatch_touch_update(local_x, local_y);
      return;
    }
    for(auto &t:tiles_){ if(t->col()==col && t->row()==row){ t->dispatch_touch_update(local_x, local_y); break; }}
  }

  void dispatch_touch_release(uint8_t col,uint8_t row,uint16_t local_x,uint16_t local_y){
    if (focused_tile_ != nullptr) {
      focused_tile_->dispatch_touch_release(local_x, local_y);
      return;
    }
    for(auto &t:tiles_){ if(t->col()==col && t->row()==row){ t->dispatch_touch_release(local_x, local_y); break; }}
  }
  
private:
  static constexpr int CLOSE_BTN_SIZE = 40;
  static constexpr int CLOSE_BTN_MARGIN = 8;

  bool close_button_hit_(uint16_t x, uint16_t y) const {
    const int bx = ctx_.x0 + ctx_.scr_w - CLOSE_BTN_SIZE - CLOSE_BTN_MARGIN;
    const int by = ctx_.y0 + CLOSE_BTN_MARGIN;
    return x >= bx && x <= bx + CLOSE_BTN_SIZE &&
           y >= by && y <= by + CLOSE_BTN_SIZE;
  }

  void draw_close_button_(Display &it) {
    const int bx = ctx_.x0 + ctx_.scr_w - CLOSE_BTN_SIZE - CLOSE_BTN_MARGIN;
    const int by = ctx_.y0 + CLOSE_BTN_MARGIN;
    const int cx = bx + CLOSE_BTN_SIZE / 2;
    const int cy = by + CLOSE_BTN_SIZE / 2;
    const int r = CLOSE_BTN_SIZE / 2;
    it.filled_circle(cx, cy, r, Colors::TILE_BACKGROUND);
    // draw X
    const int d = r * 5 / 10;
    it.line(cx - d, cy - d, cx + d, cy + d, Colors::LIGHT_TEXT);
    it.line(cx + d, cy - d, cx - d, cy + d, Colors::LIGHT_TEXT);
  }

  DisplayContext &ctx_;
  std::vector<std::unique_ptr<Tile>> tiles_;
  Tile *focused_tile_{nullptr};
};

// Singleton ------------------------------------------------------------------
inline Dashboard &get_dashboard(){ static Dashboard d(get_display_ctx()); return d; }

// -----------------------------------------------------------------------------
//  Ende Header
// -----------------------------------------------------------------------------
#endif
