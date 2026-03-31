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
  struct PageGrid { uint8_t cols, rows; };

  explicit Dashboard(DisplayContext &ctx):ctx_(ctx){}

  template<class T, class... Args>
  T &add_tile(Args&&...args){
    auto u=std::make_unique<T>(std::forward<Args>(args)...);
    T &ref=*u;
    ref.set_tile_idx(static_cast<int>(tiles_.size()));
    tiles_.push_back(std::move(u));
    return ref;
  }

  // --- Page grid configuration ---
  void set_default_grid(uint8_t cols, uint8_t rows) {
    default_cols_ = std::max<uint8_t>(cols, 1);
    default_rows_ = std::max<uint8_t>(rows, 1);
  }
  void set_page_grid(uint8_t page, uint8_t cols, uint8_t rows) {
    page_grids_[page] = {std::max<uint8_t>(cols, 1), std::max<uint8_t>(rows, 1)};
  }
  std::pair<uint8_t, uint8_t> page_grid(uint8_t page) const {
    auto it = page_grids_.find(page);
    if (it != page_grids_.end()) return {it->second.cols, it->second.rows};
    return {default_cols_, default_rows_};
  }

  void draw(Display &it){
    if (focused_tile_ != nullptr) {
      focused_tile_->draw(it);
      draw_close_button_(it);
      return;
    }
    apply_page_grid_(active_page_);
    for(auto &t:tiles_){
      if (t->page() == active_page_) t->draw(it);
    }
    if (num_pages() > 1) draw_page_indicator_(it);
  }

  void clear(){ tiles_.clear(); focused_tile_ = nullptr; active_page_ = 0; }

  // --- Fullscreen ---
  bool is_fullscreen() const { return focused_tile_ != nullptr; }

  void enter_fullscreen(Tile *t) {
    focused_tile_ = t;
    // Fullscreen nutzt die gesamte physische Display-Fläche
    t->set_override_geometry({ctx_.x0, ctx_.y0 - ctx_.status_bar_h,
                              ctx_.scr_w, ctx_.display_h});
  }

  void exit_fullscreen() {
    if (focused_tile_ == nullptr) return;
    focused_tile_->clear_override_geometry();
    focused_tile_ = nullptr;
    invalidate_all_();
  }

  // --- Paging ---
  uint8_t active_page() const { return active_page_; }
  uint8_t num_pages() const {
    uint8_t max_page = 0;
    for (auto &t : tiles_) max_page = std::max(max_page, t->page());
    for (auto &kv : page_grids_) max_page = std::max(max_page, kv.first);
    return max_page + 1;
  }

  bool set_page(uint8_t page) {
    if (page >= num_pages() || page == active_page_) return false;
    active_page_ = page;
    page_changed_ = true;
    invalidate_all_();
    return true;
  }

  bool next_page() {
    const uint8_t np = num_pages();
    if (np <= 1) return false;
    return set_page((active_page_ + 1) % np);
  }

  bool prev_page() {
    const uint8_t np = num_pages();
    if (np <= 1) return false;
    return set_page(active_page_ == 0 ? np - 1 : active_page_ - 1);
  }

  bool consume_page_changed() {
    bool v = page_changed_;
    page_changed_ = false;
    return v;
  }

  // --- Touch dispatch ---
  void dispatch_touch(uint8_t col,uint8_t row,uint16_t local_x,uint16_t local_y){
    if (focused_tile_ != nullptr) {
      if (close_button_hit_(local_x, local_y)) { exit_fullscreen(); return; }
      focused_tile_->dispatch_touch(local_x, local_y);
      return;
    }
    for(auto &t:tiles_){
      if(t->page() == active_page_ && t->contains_cell(col, row)){
        const auto [tlx, tly] = span_local_(t.get(), col, row, local_x, local_y);
        if (t->fullscreen_enabled()) { enter_fullscreen(t.get()); return; }
        t->dispatch_touch(tlx, tly);
        break;
      }
    }
  }

  void dispatch_touch_update(uint8_t col,uint8_t row,uint16_t local_x,uint16_t local_y){
    if (focused_tile_ != nullptr) {
      focused_tile_->dispatch_touch_update(local_x, local_y);
      return;
    }
    for(auto &t:tiles_){
      if(t->page() == active_page_ && t->contains_cell(col, row)){
        const auto [tlx, tly] = span_local_(t.get(), col, row, local_x, local_y);
        t->dispatch_touch_update(tlx, tly);
        break;
      }
    }
  }

  void dispatch_touch_release(uint8_t col,uint8_t row,uint16_t local_x,uint16_t local_y){
    if (focused_tile_ != nullptr) {
      focused_tile_->dispatch_touch_release(local_x, local_y);
      return;
    }
    for(auto &t:tiles_){
      if(t->page() == active_page_ && t->contains_cell(col, row)){
        const auto [tlx, tly] = span_local_(t.get(), col, row, local_x, local_y);
        t->dispatch_touch_release(tlx, tly);
        break;
      }
    }
  }
  
private:
  static constexpr int CLOSE_BTN_SIZE = 40;
  static constexpr int CLOSE_BTN_MARGIN = 8;
  static constexpr int DOT_RADIUS = 4;
  static constexpr int DOT_SPACING = 14;

  uint8_t default_cols_{1}, default_rows_{1};
  std::map<uint8_t, PageGrid> page_grids_;

  void apply_page_grid_(uint8_t page) {
    auto [c, r] = page_grid(page);
    ctx_.apply_page_grid(c, r);
  }

  /** Convert cell-local coordinates to tile-local coordinates for spanning tiles. */
  std::pair<uint16_t, uint16_t> span_local_(const Tile *t, uint8_t col, uint8_t row,
                                             uint16_t local_x, uint16_t local_y) const {
    const int cell_w = ctx_.tile_w();
    const int cell_h = ctx_.tile_h();
    return {
      static_cast<uint16_t>((col - t->col()) * cell_w + local_x),
      static_cast<uint16_t>((row - t->row()) * cell_h + local_y)
    };
  }

  void invalidate_all_() {
    for (auto &t : tiles_) t->invalidate_cache();
  }

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
    const int d = r * 5 / 10;
    it.line(cx - d, cy - d, cx + d, cy + d, Colors::LIGHT_TEXT);
    it.line(cx + d, cy - d, cx - d, cy + d, Colors::LIGHT_TEXT);
  }

  void draw_page_indicator_(Display &it) {
    const uint8_t np = num_pages();
    const int total_w = np * DOT_RADIUS * 2 + (np - 1) * (DOT_SPACING - DOT_RADIUS * 2);
    const int start_x = ctx_.x0 + (ctx_.scr_w - total_w) / 2;
    // Dots in der Status-Bar zeichnen (vertikal zentriert)
    const int bar_h = ctx_.status_bar_h > 0 ? ctx_.status_bar_h : 16;
    const int cy = ctx_.y0 - ctx_.status_bar_h + bar_h / 2;
    for (uint8_t i = 0; i < np; i++) {
      const int cx = start_x + i * DOT_SPACING + DOT_RADIUS;
      const auto color = (i == active_page_) ? Colors::NORMAL_TEXT : Colors::TILE_BORDER;
      it.filled_circle(cx, cy, DOT_RADIUS, color);
    }
  }

  DisplayContext &ctx_;
  std::vector<std::unique_ptr<Tile>> tiles_;
  Tile *focused_tile_{nullptr};
  uint8_t active_page_{0};
  bool page_changed_{false};
};

// Singleton ------------------------------------------------------------------
inline Dashboard &get_dashboard(){ static Dashboard d(get_display_ctx()); return d; }

// -----------------------------------------------------------------------------
//  Ende Header
// -----------------------------------------------------------------------------
#endif
