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

  void draw(Display &it){ for(auto &t:tiles_) t->draw(it);}  

  void clear(){ tiles_.clear(); }

  void dispatch_touch(uint8_t col,uint8_t row,uint16_t local_x,uint16_t local_y){
    for(auto &t:tiles_){ if(t->col()==col && t->row()==row){ t->dispatch_touch(local_x, local_y); break; }}
  }

  void dispatch_touch_update(uint8_t col,uint8_t row,uint16_t local_x,uint16_t local_y){
    for(auto &t:tiles_){ if(t->col()==col && t->row()==row){ t->dispatch_touch_update(local_x, local_y); break; }}
  }

  void dispatch_touch_release(uint8_t col,uint8_t row,uint16_t local_x,uint16_t local_y){
    for(auto &t:tiles_){ if(t->col()==col && t->row()==row){ t->dispatch_touch_release(local_x, local_y); break; }}
  }
  
private:
  DisplayContext &ctx_; std::vector<std::unique_ptr<Tile>> tiles_; };

// Singleton ------------------------------------------------------------------
inline Dashboard &get_dashboard(){ static Dashboard d(get_display_ctx()); return d; }

// -----------------------------------------------------------------------------
//  Ende Header
// -----------------------------------------------------------------------------
#endif
