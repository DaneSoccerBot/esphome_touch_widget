#ifndef CLIMATE_TILE_H
#define CLIMATE_TILE_H

#pragma once

#include <tuple>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <cstdio>

#include "tile.h"
#include "colors.h"
#include "pixel_buffer.h"

#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/api/homeassistant_service.h"

using esphome::display::Display;
using esphome::font::Font;
namespace api = esphome::api;

class ClimateTile : public Tile
{
public:
  /* ---------- Konfiguration ---------- */
  struct Cfg
  {
    esphome::text_sensor::TextSensor *payload; // JSON‑String
    std::string entity_id;                     // climate.xxx
  };

  /* ---------- Cache aus JSON ---------- */
  struct Cache
  {
    float target = NAN, current = NAN, last_sent_target = NAN;
    float min_t = 5, max_t = 30, step = 0.5f;
    std::string mode = "off";
    std::vector<std::string> modes = {"off", "heat"};
  } cache_;

  /* ---------- Konstruktor ---------- */
  explicit ClimateTile(DisplayContext &ctx, uint8_t col, uint8_t row,
                       std::string label, const Cfg &cfg)
      : Tile(ctx, col, row, 1, std::move(label)), cfg_(cfg)
  {
    if (cfg_.payload != nullptr)
    {
      if (cfg_.payload->has_state())
        parse_payload_(cfg_.payload->state);
      cfg_.payload->add_on_state_callback([this](const std::string &s)
                                          {
        parse_payload_(s);
        request_redraw(); });
    }
  }

  explicit ClimateTile(uint8_t col, uint8_t row,
                       std::string label, const Cfg &cfg)
      : ClimateTile(get_display_ctx(), col, row, std::move(label), cfg) {}

  void parse_climate_state_from_string(const std::string &s)
  {
    parse_payload_(s);
  }

  void dispatch_touch(uint16_t local_x, uint16_t local_y) override
  {
    // Touch-Down merken für Tap-Erkennung (keine Aktion bei Touch-Down)
    touch_down_x_ = local_x;
    touch_down_y_ = local_y;
  }

  void dispatch_touch_update(uint16_t local_x, uint16_t local_y) override
  {
    // Kein Live-Drag — Temp-Verstellung nur per Tap-Release nah am Arc
  }

  void dispatch_touch_release(uint16_t local_x, uint16_t local_y) override
  {
    // Nur bei echtem Tap (Touch-Down nah an Touch-Up)
    const int tdx = int(local_x) - int(touch_down_x_);
    const int tdy = int(local_y) - int(touch_down_y_);
    if (tdx * tdx + tdy * tdy > TAP_MAX_DIST_SQ) {
      ESP_LOGD("Climate", "Touch rejected: too far from down (%d,%d)->(%d,%d)",
               touch_down_x_, touch_down_y_, local_x, local_y);
      return;
    }
    handle_touch(local_x, local_y);
  }

  void handle_touch(uint16_t local_x, uint16_t local_y)
  {
    // --- Mode-Toggle: Tap in die Mitte ---
    const auto modeChangeArea = std::tuple<int, int, int, int>{tile_w() * 0.3f, tile_h() * 0.28f, tile_w() * 0.7f, tile_h() * 0.62f};
    auto [left, top, right, bottom] = modeChangeArea;

    if (local_x >= left && local_x <= right &&
        local_y >= top && local_y <= bottom)
    {
      ESP_LOGD("Climate", "Mode-Area touched → toggle_mode()");
      this->toggle_mode();
      return;
    }

    // --- Geometrie des Kreisbogens ---
    const float cx = float(tile_w()) * 0.5f;
    const float cy = float(tile_h()) * 0.47f;
    const int size = std::min(tile_w(), tile_h()) * 80 / 100;
    const int radius = size / 2;
    const int thick = std::max(4, int(size * 0.09f));
    const float arc_r = float(radius - thick / 2);

    // Distanz zum Arc-Zentrum
    const float dx = float(local_x) - cx;
    const float dy = float(local_y) - cy;
    const float dist = std::sqrt(dx * dx + dy * dy);

    // Proximity-Check: Touch muss innerhalb ±30% der Bogendicke vom Arc-Radius sein
    const float tolerance = std::max(float(thick) * 2.5f, 25.0f);
    if (std::fabs(dist - arc_r) > tolerance) {
      ESP_LOGD("Climate", "Touch rejected: dist=%.0f arc_r=%.0f tolerance=%.0f",
               dist, arc_r, tolerance);
      return;
    }

    // Winkel berechnen
    float angle = std::atan2(dy, dx) * 180.0f / M_PI;
    if (angle < 0)
      angle += 360.0f;

    const float start = 135.0f;
    const float span = 270.0f;
    const float end = std::fmod(start + span, 360.0f);

    // Prüfen ob Winkel im Bogen liegt
    bool in_arc = (angle >= start || angle <= end);
    if (!in_arc) {
      ESP_LOGD("Climate", "Touch rejected: angle=%.1f° outside arc", angle);
      return;
    }

    // Rel-Winkel berechnen
    float rel = (angle >= start) ? angle - start : (angle + 360.0f) - start;
    rel = std::clamp(rel, 0.0f, span);

    // Temperatur berechnen + quantisieren
    float prog = rel / span;
    float raw_target = cache_.min_t + prog * (cache_.max_t - cache_.min_t);
    float steps = std::round((raw_target - cache_.min_t) / cache_.step);
    float quantized = cache_.min_t + steps * cache_.step;
    quantized = std::clamp(quantized, cache_.min_t, cache_.max_t);

    ESP_LOGD("Climate",
             "Touch→angle=%.1f°, dist=%.0f, prog=%.2f → quant=%.1f°C",
             angle, dist, prog, quantized);

    set_target_temp(quantized, true);
  }

  const char *tile_type_name() const override { return "Climate"; }

protected:
  void render_update(Display &it) override
  {
    const int idx = tile_index();
    const std::string new_key = make_cache_key();

    // 1) alten Key aus dem Cache (wenn vorhanden) ziehen
    std::string old_key;
    if (idx < ctx_.cache_value.size())
      old_key = ctx_.cache_value[idx];
    const bool current_only = only_current_changed(old_key, make_cache_key());
    if (current_only)
    {
      auto [cx0, cy0, cx1, cy1] = current_temp_clip();
      it.start_clipping(cx0, cy0, cx1, cy1);
      clear_area_fast(it, cx0, cy0, cx1 - cx0, cy1 - cy0);
      draw_current_temp_label(it);
      it.end_clipping();
      return;
    }

    auto [cx0, cy0, cx1, cy1] = value_clip();
    it.start_clipping(cx0, cy0, cx1, cy1);
    clear_area_fast(it, cx0, cy0, cx1 - cx0, cy1 - cy0);
    draw_content(it);
    it.end_clipping();
  }
  /* ---------- Zeichenroutine ---------- */
  void draw_content(Display &it) override
  {
    ensure_fonts_();
    const float tgt = cache_.target;

    // Geometrie
    const int w = tile_w(), h = tile_h();
    const int x = abs_x(), y = abs_y();
    const int size = std::min(w, h) * 80 / 100;
    const int cx = x + w / 2;
    const int cy = y + h * 0.47;
    const int radius = size / 2;
    const int thick = std::max(4, int(size * 0.09f));
    const int br = std::max(2, thick / 2);

    // Ring — in PixelBuffer rendern, dann einmal blitten
    float prog = std::clamp((tgt - cache_.min_t) / (cache_.max_t - cache_.min_t), 0.f, 1.f);
    constexpr float span = 270, start = 135;
    float filled = span * prog;

    // Arc-Bounding-Box: eng um den tatsächlichen Inhalt
    // Kreise sitzen bei arc_r = (radius - thick/2) vom Zentrum, Marker-Radius = br+2
    const int arc_r = radius - thick / 2;
    const int marker_r = br + 2;
    const int extent = arc_r + marker_r + 1;
    // Arc 135°..405° → max nach oben: sin(270°)=-1 → volle Ausdehnung
    // Max nach unten: sin(135°)=0.707 → nur ~71% der Ausdehnung
    const int y_bottom = static_cast<int>(std::ceil(0.7072f * arc_r)) + marker_r + 1;
    const int buf_w = 2 * extent + 1;
    const int buf_h = extent + y_bottom + 1;

    // Auf ESP32 den PixelBuffer auf max ~64KB (256x128) begrenzen,
    // damit PSRAM-Allokation + DMA-Transfer schnell bleibt
    static constexpr int MAX_BUF_PIXELS = 256 * 128;
    const bool fits_in_buffer = (buf_w * buf_h <= MAX_BUF_PIXELS);

    if (fits_in_buffer && arc_buf_.ensure(buf_w, buf_h)) {
      // --- Normaler Single-Buffer-Pfad (kleinere Arcs im Grid) ---
      const int buf_x = cx - extent;
      const int buf_y = cy - extent;
      const int bcx = cx - buf_x;
      const int bcy = cy - buf_y;
      arc_buf_.clear(PixelBuffer::to_565(Colors::TILE_BACKGROUND));

      const uint16_t c_orange = PixelBuffer::to_565(Colors::ORANGE);
      const uint16_t c_grey = PixelBuffer::to_565(Colors::LIGHT_GREY);
      for (int i = 0; i < 100; i++) {
        float deg = start + i * (span / 100);
        float rad_f = deg * static_cast<float>(M_PI) / 180.0f;
        int px = bcx + std::cos(rad_f) * (radius - thick / 2);
        int py = bcy + std::sin(rad_f) * (radius - thick / 2);
        arc_buf_.filled_circle(px, py, br,
                               (deg <= start + filled) ? c_orange : c_grey);
      }

      if (!std::isnan(tgt)) {
        float mrad = (start + filled) * static_cast<float>(M_PI) / 180.0f;
        int mx = bcx + std::cos(mrad) * (radius - thick / 2);
        int my = bcy + std::sin(mrad) * (radius - thick / 2);
        arc_buf_.filled_circle(mx, my, br + 2,
                               PixelBuffer::to_565(esphome::Color::WHITE));
      }

      arc_buf_.blit(it, buf_x, buf_y);
    } else {
      // --- Strip-basierter Fallback: Arc in horizontale Streifen aufteilen ---
      const int buf_x = cx - extent;
      const int buf_y = cy - extent;
      const int bcx_base = cx - buf_x;
      const int bcy_base = cy - buf_y;

      const uint16_t bg565 = PixelBuffer::to_565(Colors::TILE_BACKGROUND);
      const uint16_t c_orange = PixelBuffer::to_565(Colors::ORANGE);
      const uint16_t c_grey = PixelBuffer::to_565(Colors::LIGHT_GREY);
      const uint16_t c_white = PixelBuffer::to_565(esphome::Color::WHITE);
      const float filled_val = filled;
      const int arc_radius = radius;
      const int arc_thick = thick;
      const int arc_br = br;

      arc_buf_.blit_strips(it, buf_x, buf_y, buf_w, buf_h, MAX_BUF_PIXELS, bg565,
        [&](PixelBuffer &strip, int y_off) {
          // Zeichne alle Track-Kreise die in diesen Streifen fallen
          for (int i = 0; i < 100; i++) {
            float deg = start + i * (span / 100);
            float rad_f = deg * static_cast<float>(M_PI) / 180.0f;
            int px = bcx_base + std::cos(rad_f) * (arc_radius - arc_thick / 2);
            int py = bcy_base + std::sin(rad_f) * (arc_radius - arc_thick / 2) - y_off;
            // Nur zeichnen wenn der Kreis diesen Streifen berührt
            if (py + arc_br >= 0 && py - arc_br < strip.height()) {
              strip.filled_circle(px, py, arc_br,
                                  (deg <= start + filled_val) ? c_orange : c_grey);
            }
          }
          // Marker
          if (!std::isnan(tgt)) {
            float mrad = (start + filled_val) * static_cast<float>(M_PI) / 180.0f;
            int mx = bcx_base + std::cos(mrad) * (arc_radius - arc_thick / 2);
            int my = bcy_base + std::sin(mrad) * (arc_radius - arc_thick / 2) - y_off;
            if (my + (arc_br + 2) >= 0 && my - (arc_br + 2) < strip.height()) {
              strip.filled_circle(mx, my, arc_br + 2, c_white);
            }
          }
        });
    }
    // Zahlen/Text
    char buf[16];
    // zunächst Mode uppercase vorcomputen
    std::string mode_up = cache_.mode;
    std::transform(mode_up.begin(), mode_up.end(), mode_up.begin(),
                   [](unsigned char c)
                   { return std::toupper(c); });

    // prüfen, ob wir Temperatur oder “OFF” anzeigen wollen
    bool is_off = (mode_up == "OFF");
    if (!std::isnan(tgt) && !is_off)
    {
      // gültige Temperatur und nicht im OFF-Modus → Temperatur ausgeben
      std::snprintf(buf, sizeof(buf), "%.1f°C", tgt);
    }
    else
    {
      // entweder NaN oder Mode == OFF → “OFF” anzeigen
      std::snprintf(buf, sizeof(buf), "%s", mode_up.c_str());
    }

    it.print(cx, cy - h * 0.02, font_big_, Colors::NORMAL_TEXT,
             esphome::display::TextAlign::CENTER, buf);

    draw_current_temp_label(it);
  }

  /* ---------- Cache‑Key ---------- */
  std::string make_cache_key() const override
  {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f/%.1f/%s",
                  cache_.target, cache_.current, cache_.mode.c_str());
    return buf;
  }

  // ----- Minimaler Update-Bereich -----------------------------------------
  std::tuple<int, int, int, int> value_clip() const override
  {
    const int top = abs_y() + tile_h() * 0.05f;
    const int bot = abs_y() + tile_h() * 0.8f;
    const int left = abs_x() + tile_w() * 0.05f;
    const int right = abs_x() + tile_w() * 0.95f;
    return {left, top, right, bot};
  }

private:
  std::tuple<int, int, int, int> current_temp_clip() const
  {
    const int top = abs_y() + tile_h() * 0.58f;
    const int bot = abs_y() + tile_h() * 0.72f;
    const int left = abs_x() + tile_w() * 0.29f;
    const int right = abs_x() + tile_w() * 0.71f;
    return {left, top, right, bot};
  }
  void draw_current_temp_label(Display &it)
  {
    ensure_fonts_();
    char buf[16];
    const int w = tile_w(), h = tile_h();
    const int x = abs_x(), y = abs_y();
    const int cx = x + w / 2;
    const int cy = y + h * 0.47;
    const float cur = cache_.current;

    std::snprintf(buf, sizeof(buf), !std::isnan(cur) ? "%.1f°C" : "--.-", cur);
    it.print(cx, cy + h * 0.18, font_small_, Colors::LIGHT_TEXT,
             esphome::display::TextAlign::CENTER, buf);
  }
  /* ---------- JSON‑Parser ---------- */
  /* ---------- Mini‑Helpers ---------- */
  static float extract_float(const char *str, const char *key, float fallback)
  {
    const char *p = std::strstr(str, key);
    if (!p)
      return fallback;
    p += std::strlen(key);          // hinter "key":
    return std::strtof(p, nullptr); // strtof stoppt bei erstem invaliden Char
  }

  static std::string extract_string(const char *str, const char *key,
                                    const std::string &fallback)
  {
    const char *p = std::strstr(str, key);
    if (!p)
      return fallback;
    p += std::strlen(key); // hinter "
    const char *q = std::strchr(p, '"');
    return q ? std::string(p, q) : fallback;
  }

  static void extract_modes(const char *str, std::vector<std::string> &out)
  {
    const char *p = std::strstr(str, "\"hvac_modes\":[");
    if (!p)
      return;
    p = std::strchr(p, '[') + 1;
    out.clear();
    while (*p && *p != ']')
    {
      if (*p == '"')
      {
        const char *q = std::strchr(++p, '"');
        if (!q)
          break;
        out.emplace_back(p, q);
        p = q + 1;
      }
      else
        p++;
    }
  }

  void parse_payload_(const std::string &payload)
  {
    const char *s = payload.c_str();

    cache_.target = extract_float(s, "\"temperature\":", cache_.target);
    cache_.current = extract_float(s, "\"current_temperature\":", cache_.current);
    cache_.min_t = extract_float(s, "\"min_temp\":", cache_.min_t);
    cache_.max_t = extract_float(s, "\"max_temp\":", cache_.max_t);
    cache_.step = extract_float(s, "\"target_temp_step\":", cache_.step);
    cache_.mode = extract_string(s, "\"mode\":\"", cache_.mode);

    extract_modes(s, cache_.modes);

    ESP_LOGD("Climate", "Parsed: target=%.2f current=%.2f min=%.2f max=%.2f step=%.2f mode=%s",
             cache_.target, cache_.current, cache_.min_t,
             cache_.max_t, cache_.step, cache_.mode.c_str());
    std::string modes_join;
    for (auto &m : cache_.modes)
      modes_join += m + ",";
    ESP_LOGD("Climate", "Parsed modes: [%s]", modes_join.c_str());
  }

  /* ---------- Interaktion ---------- */
  void adjust(int dir)
  {
    if (std::isnan(cache_.target))
      return;
    float newT = std::clamp(cache_.target + dir * cache_.step,
                            cache_.min_t, cache_.max_t);
    set_target_temp(newT, false);
  }

  void set_target_temp(float t, bool is_touch_release)
  {
    // 1) Clamp auf min/max
    float new_t = std::clamp(t, cache_.min_t, cache_.max_t);

    // 3) State updaten + Payload publishen (synced alle Tiles mit gleichem Sensor)
    cache_.target = new_t;
    ESP_LOGD("Climate", "Setting target to %.1f", cache_.target);
    publish_state_to_payload_();
    if (!is_touch_release)
      return;
    // 2) Nur weitermachen, wenn sich der Wert wirklich ändert
    if (std::fabs(cache_.target - cache_.last_sent_target) < 1e-3f)
    {
      // Differenz < 0.001 → praktisch gleich → nichts tun
      ESP_LOGD("Climate", "Target temperature unchanged cache_.last_sent_target= %.1f and cache_.target= %.1f so quit without render update and HA call", cache_.last_sent_target, cache_.target);
      return;
    }
    cache_.last_sent_target = cache_.target;
    ESP_LOGD("Climate", "Sending target %.1f to Home Assistant", cache_.target);
    api::HomeAssistantServiceCallAction<> call(api::global_api_server, false);
    call.set_service("climate.set_temperature");
    call.init_data(2);
    call.add_data("entity_id", cfg_.entity_id);
    call.add_data("temperature", esphome::to_string(cache_.target));
    call.play();
  }

  void toggle_mode()
  {
    if (cache_.modes.empty())
      return;
    auto it = std::find(cache_.modes.begin(), cache_.modes.end(), cache_.mode);
    if (it == cache_.modes.end() || ++it == cache_.modes.end())
      it = cache_.modes.begin();
    const std::string next = *it;
    cache_.mode = next;
    ESP_LOGD("Climate", "Toggling mode to %s", next.c_str());
    publish_state_to_payload_();
    api::HomeAssistantServiceCallAction<> call(api::global_api_server, false);
    call.set_service("climate.set_hvac_mode");
    call.init_data(2);
    call.add_data("entity_id", cfg_.entity_id);
    call.add_data("hvac_mode", next);
    call.play();
  }

  // Max Distanz (px²) zwischen Touch-Down und Touch-Up für einen gültigen Tap
  static constexpr int TAP_MAX_DIST = 25;
  static constexpr int TAP_MAX_DIST_SQ = TAP_MAX_DIST * TAP_MAX_DIST;
  uint16_t touch_down_x_{0}, touch_down_y_{0};

  /* ---------- Payload zurück-publizieren (synced alle Tiles) ---------- */
  void publish_state_to_payload_()
  {
    if (cfg_.payload == nullptr)
      return;
    char buf[256];
    // Modes-Array aufbauen
    std::string modes_json = "[";
    for (size_t i = 0; i < cache_.modes.size(); i++) {
      if (i > 0) modes_json += ",";
      modes_json += "\"" + cache_.modes[i] + "\"";
    }
    modes_json += "]";
    std::snprintf(buf, sizeof(buf),
      "{\"temperature\":%.1f,\"current_temperature\":%.1f,"
      "\"min_temp\":%.0f,\"max_temp\":%.0f,\"target_temp_step\":%.1f,"
      "\"mode\":\"%s\",\"hvac_modes\":%s}",
      cache_.target, cache_.current,
      cache_.min_t, cache_.max_t, cache_.step,
      cache_.mode.c_str(), modes_json.c_str());
    cfg_.payload->publish_state(std::string(buf));
  }

  /* ---------- Member ---------- */
  Cfg cfg_;
  mutable Font *font_big_{nullptr}, *font_small_{nullptr};
  PixelBuffer arc_buf_;

  /* ------------------------------------------------------------------
   *  Hilfsfunktion: zerlegt "target/current/mode" in 3 Teile
   * ----------------------------------------------------------------*/
  static std::array<std::string, 3> split_key_(const std::string &k)
  {
    std::array<std::string, 3> out;
    auto p0 = k.find('/');
    if (p0 == std::string::npos)
      return out;
    auto p1 = k.find('/', p0 + 1);
    out[0] = k.substr(0, p0);               // target
    out[1] = k.substr(p0 + 1, p1 - p0 - 1); // current
    out[2] = k.substr(p1 + 1);              // mode
    return out;
  }

  /* prüft, ob nur current geändert hat */
  static bool only_current_changed(const std::string &old_k,
                                   const std::string &new_k)
  {
    if (old_k.empty())
      return false; // Initialfall → kompletter redraw
    auto old = split_key_(old_k);
    auto neu = split_key_(new_k);
    return (old[0] == neu[0]) && // target gleich
           (old[2] == neu[2]) && // mode   gleich
           (old[1] != neu[1]);   // **nur current anders**
  }

  void ensure_fonts_() const
  {
    const float hpx = static_cast<float>(tile_h());
    Font *want_big = ctx_.get_font_for_size(hpx * 0.22f);
    Font *want_small = ctx_.get_font_for_size(hpx * 0.12f);
    if (font_big_ == want_big && font_small_ == want_small)
      return;
    font_big_ = want_big;
    font_small_ = want_small;
  }
};

#endif // CLIMATE_TILE_H
