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

  void dispatch_touch_update(uint16_t local_x, uint16_t local_y) override
  {
    handle_touch(local_x, local_y, false);
  }

  void dispatch_touch(uint16_t local_x, uint16_t local_y) override
  {
    handle_touch(local_x, local_y, false);
  }

  void dispatch_touch_release(uint16_t local_x, uint16_t local_y) override
  {
    handle_touch(local_x, local_y, true);
  }

  void handle_touch(uint16_t local_x, uint16_t local_y, bool is_touch_release)
  {
    ESP_LOGD("Climate", "Touch handler override at local_x=%d and local_y=%d", local_x, local_y);
    const auto modeChangeArea = std::tuple<int, int, int, int>{tile_w() * 0.3f, tile_h() * 0.28f, tile_w() * 0.7f, tile_h() * 0.62f};
    auto [left, top, right, bottom] = modeChangeArea;
    // ESP_LOGD("Climate", "Mode-Area: left=%d, top=%d, right=%d, bottom=%d", left, top, right, bottom);
    //  disp_->filled_rectangle(abs_x()+left, abs_y()+top, right-left, bottom-top, Colors::RED);

    if (is_touch_release && local_x >= left && local_x <= right &&
        local_y >= top && local_y <= bottom)
    {
      ESP_LOGD("Climate", "Mode-Area touched → toggle_mode()");
      this->toggle_mode();
      return;
    }

    // --- Touch relativ zur Tile-Mitte wie gehabt ---
    const float cx = float(tile_w()) * 0.5f;
    const float cy = float(tile_h()) * 0.47f;
    const float dx = float(local_x) - cx;
    const float dy = float(local_y) - cy;

    // Winkel in Grad (0° = rechts, 90° = unten) auf [0,360)
    float angle = std::atan2(dy, dx) * 180.0f / M_PI;
    if (angle < 0)
      angle += 360.0f;

    const float start = 135.0f;
    const float span = 270.0f;
    const float end = std::fmod(start + span, 360.0f); // non-constexpr, aber erlaubt

    // 1) prüfen, ob Winkel wirklich im Bogen liegt
    bool in_arc;
    if (start < end)
    {
      // Normal-Fall: bspw. start=30°, end=300° (hier nicht)
      in_arc = (angle >= start && angle <= end);
    }
    else
    {
      // Wrap-Fall: Bogen von 135…360 und 0…45
      in_arc = (angle >= start || angle <= end);
    }

    // 2) rel-Winkel innerhalb des Bogens berechnen oder clampen
    float rel;
    if (in_arc)
    {
      // Entfernung vom Start entlang des Bogens (auch über 360°)
      rel = angle >= start
                ? angle - start
                : (angle + 360.0f) - start;
      // sicherheitshalber clamp auf [0, span]
      rel = std::clamp(rel, 0.0f, span);
    }
    else
    {
      // Winkel im Komplement (45…135) – an den nächstliegenden Endpunkt clampen
      // Endpunktswinkel in [0,360): start=135, end=45
      float d_start = std::fabs(angle - start);
      float d_end = std::fabs(
          // falls angle > start, nimm angle-360, um 45<–>360+45-Abstand zu messen
          angle > start
              ? (angle - 360.0f) - end
              : angle - end);
      rel = (d_start < d_end) ? 0.0f : span;
    }

    // 3) Prozent 0…1 und rohe Temperatur
    float prog = rel / span;
    float raw_target = cache_.min_t + prog * (cache_.max_t - cache_.min_t);

    // 4) Quantisierung auf step-Raster
    float steps = std::round((raw_target - cache_.min_t) / cache_.step);
    float quantized = cache_.min_t + steps * cache_.step;
    quantized = std::clamp(quantized, cache_.min_t, cache_.max_t);

    ESP_LOGD("Climate",
             "Touch→angle=%.1f°, in_arc=%d, rel=%.1f°, prog=%.2f → quant=%.1f°C, is_touch_release =%d",
             angle, int(in_arc), rel, prog, quantized, is_touch_release);

    // 5) Wert setzen (macht nur API-Call, wenn sich quantized ändert)
    set_target_temp(quantized, is_touch_release);
  }

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
      // nur Hintergrund-Farbe (nicht voller Rahmen!)
      ctx_.bg_renderer.drawBgColor(it,
                                   cx0, cy0, cx1 - cx0, cy1 - cy0);
      draw_current_temp_label(it);
      it.end_clipping();
      return;
    }

    auto [cx0, cy0, cx1, cy1] = value_clip();
    it.start_clipping(cx0, cy0, cx1, cy1);
    // nur Hintergrund-Farbe (nicht voller Rahmen!)
    ctx_.bg_renderer.drawBgColor(it,
                                 cx0, cy0, cx1 - cx0, cy1 - cy0);
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

    // Ring
    float prog = std::clamp((tgt - cache_.min_t) / (cache_.max_t - cache_.min_t), 0.f, 1.f);
    constexpr float span = 270, start = 135;
    float filled = span * prog;

    for (int i = 0; i < 100; i++)
    {
      float deg = start + i * (span / 100);
      float rad = deg * M_PI / 180;
      int px = cx + std::cos(rad) * (radius - thick / 2);
      int py = cy + std::sin(rad) * (radius - thick / 2);
      it.filled_circle(px, py, br,
                       (deg <= start + filled) ? Colors::ORANGE : Colors::LIGHT_GREY);
    }

    if (!std::isnan(tgt))
    {
      // Marker
      float mrad = (start + filled) * M_PI / 180;
      it.filled_circle(cx + std::cos(mrad) * (radius - thick / 2),
                       cy + std::sin(mrad) * (radius - thick / 2),
                       br + 2, esphome::Color::WHITE);
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

    // 3) State updaten, redraw und Service-Call
    cache_.target = new_t;
    ESP_LOGD("Climate", "Setting target to %.1f", cache_.target);
    request_redraw();
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
    request_redraw();
    api::HomeAssistantServiceCallAction<> call(api::global_api_server, false);
    call.set_service("climate.set_hvac_mode");
    call.add_data("entity_id", cfg_.entity_id);
    call.add_data("hvac_mode", next);
    call.play();
  }

  /* ---------- Member ---------- */
  Cfg cfg_;
  mutable Font *font_big_{nullptr}, *font_small_{nullptr};

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
    if (font_big_ != nullptr && font_small_ != nullptr)
      return;
    const float hpx = float(ctx_.scr_h) / float(std::max(ctx_.rows, 1));
    font_big_ = ctx_.get_font_for_size(hpx * 0.22f);
    font_small_ = ctx_.get_font_for_size(hpx * 0.12f);
  }
};

#endif // CLIMATE_TILE_H
