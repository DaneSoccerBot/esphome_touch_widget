#ifndef DOUBLE_VALUE_TILE_H
#define DOUBLE_VALUE_TILE_H

#include <tuple>
#include <string>
#include <cstring>  // snprintf, strcpy, strncat
#include <cmath>    // std::isnan
#include "tile.h"
#include "colors.h" // Farbkonstanten
#include "esphome/components/sensor/sensor.h"

//==============================================================================
//  DoubleValueTile – zeigt zwei Werte übereinander in einer Kachel
//==============================================================================
class DoubleValueTile : public Tile
{
public:
    struct Cfg
    {
        esphome::sensor::Sensor *top_value{nullptr};
        esphome::sensor::Sensor *bottom_value{nullptr};
    };

    DoubleValueTile(DisplayContext &ctx, uint8_t col, uint8_t row,
                    std::string top_label,
                    std::string bottom_label,
                    std::string top_unit,
                    std::string bottom_unit,
                    std::string fmt_top,
                    std::string fmt_bottom,
                    const Cfg &cfg)
        : Tile(ctx, col, row, (uint8_t)2),
          cfg_(cfg), top_label_(std::move(top_label)), bottom_label_(std::move(bottom_label)), top_unit_(std::move(top_unit)), bottom_unit_(std::move(bottom_unit)), fmt_top_(std::move(fmt_top)), fmt_bottom_(std::move(fmt_bottom)), prev_top_(NAN), prev_bottom_(NAN)
    {
        this->bind_sensors_();
    }

    DoubleValueTile(DisplayContext &ctx, uint8_t col, uint8_t row,
                    std::string top_label,
                    std::string bottom_label,
                    std::string top_unit,
                    std::string bottom_unit,
                    std::string fmt_top = "%.1f",
                    std::string fmt_bottom = "%.1f")
        : DoubleValueTile(ctx, col, row,
                          std::move(top_label), std::move(bottom_label),
                          std::move(top_unit), std::move(bottom_unit),
                          std::move(fmt_top), std::move(fmt_bottom), Cfg{})
    {
    }

    DoubleValueTile(uint8_t col, uint8_t row,
                    std::string top_label,
                    std::string bottom_label,
                    std::string top_unit,
                    std::string bottom_unit,
                    const char *fmt_top = "%.1f",
                    const char *fmt_bottom = "%.1f")
        : DoubleValueTile(get_display_ctx(), col, row,
                          std::move(top_label), std::move(bottom_label),
                          std::move(top_unit), std::move(bottom_unit),
                          fmt_top, fmt_bottom, {})
    {
    }

    void set_top_value(float v) { top_value_ = v; request_redraw(); }
    void set_bottom_value(float v) { bottom_value_ = v; request_redraw(); }

protected:
    void render_update(Display &it) override
    {
        draw_content(it);
    }

    void draw_labels(Display &it) override
    {
        if (!bottom_label_.empty())
            it.print(abs_x() + tile_w() / 2, abs_y() + tile_h() * 0.9f,
                     ctx_.font_label_compact, Colors::LIGHT_TEXT,
                     esphome::display::TextAlign::CENTER,
                     bottom_label_.c_str());
        // Label
        if (!top_label_.empty())
        {
            it.print(abs_x() + tile_w() / 2,
                     abs_y() + tile_h() * 0.4f,
                     ctx_.font_label_compact, Colors::LIGHT_TEXT,
                     esphome::display::TextAlign::CENTER,
                     top_label_.c_str());
        }
    }
    // ----- Inhalt zeichnen -----------------------------------------------------
    void draw_content(Display &it) override
    {
        const int x0 = abs_x();
        const int y0 = abs_y();
        const int w = tile_w();
        const int h = tile_h();
        if (!formatted_equals(prev_top_, top_value_, fmt_top_.c_str(), top_unit_)) {
            auto [cx0, cy0, cx1, cy1] = top_value_clip();
            it.start_clipping(cx0, cy0, cx1, cy1);
            ctx_.bg_renderer.drawBgColor(it, cx0, cy0, cx1 - cx0, cy1 - cy0);
            render_value(it, h * 0.2f, fmt_top_.c_str(), top_unit_, top_value_);
            it.end_clipping();
        }
        if (!formatted_equals(prev_bottom_, bottom_value_, fmt_bottom_.c_str(), bottom_unit_)) {
            auto [cx0, cy0, cx1, cy1] = bottom_value_clip();
            it.start_clipping(cx0, cy0, cx1, cy1);
            ctx_.bg_renderer.drawBgColor(it, cx0, cy0, cx1 - cx0, cy1 - cy0);
            render_value(it, h * 0.7f, fmt_bottom_.c_str(), bottom_unit_, bottom_value_);
            it.end_clipping();
        }
        prev_top_    = top_value_;
        prev_bottom_ = bottom_value_;
    }

    // ----- Cache-Key -----------------------------------------------------------
    std::string make_cache_key() const override
    {
        char key[64];
        std::snprintf(key, sizeof(key), "%s%f|%s%f",
                      top_unit_.c_str(), top_value_,
                      bottom_unit_.c_str(), bottom_value_);
        return std::string(key);
    }

private:
    void bind_sensors_()
    {
        if (cfg_.top_value != nullptr)
        {
            top_value_ = cfg_.top_value->state;
            cfg_.top_value->add_on_state_callback([this](float value)
                                                  {
                this->top_value_ = value;
                this->request_redraw(); });
        }
        if (cfg_.bottom_value != nullptr)
        {
            bottom_value_ = cfg_.bottom_value->state;
            cfg_.bottom_value->add_on_state_callback([this](float value)
                                                     {
                this->bottom_value_ = value;
                this->request_redraw(); });
        }
    }

    Cfg cfg_;
    float top_value_{NAN}, bottom_value_{NAN};
    std::string top_label_, bottom_label_;
    std::string top_unit_, bottom_unit_;
    std::string fmt_top_, fmt_bottom_;
    float prev_top_,  prev_bottom_;

    std::tuple<int, int, int, int> top_value_clip() const
    {
        const int x0 = abs_x() + tile_w() * 5 / 100;
        const int x1 = abs_x() + tile_w() * 95 / 100;
        const int y0 = abs_y() + tile_h() * 5 / 100;
        const int y1 = abs_y() + tile_h() * 35 / 100;
        return {x0, y0, x1, y1};
    }
    std::tuple<int, int, int, int> bottom_value_clip() const
    {
        const int x0 = abs_x() + tile_w() * 5 / 100;
        const int x1 = abs_x() + tile_w() * 95 / 100;
        const int y0 = abs_y() + tile_h() * 55 / 100;
        const int y1 = abs_y() + tile_h() * 85 / 100;
        return {x0, y0, x1, y1};
    }

    void render_value(Display &it, int ypos, const char *fmt, std::string unit, float value)
    {
        const int x0 = abs_x();
        const int y0 = abs_y();
        const int w = tile_w();
        const int h = tile_h();
        const int textHeight = 0.3*h;

        // --- Obere Hälfte ---
        {
            // Wert + Einheit
            char buf[32];
            if (!std::isnan(value))
                std::snprintf(buf, sizeof(buf), fmt, value);
            else
                std::strcpy(buf, "N/A");
            std::strncat(buf, unit.c_str(), sizeof(buf) - std::strlen(buf) - 1);

            it.print(x0 + w / 2,
                     y0 + ypos,
                     ctx_.font_value_compact, Colors::NORMAL_TEXT,
                     esphome::display::TextAlign::CENTER,
                     buf);
        }
    }
};

#endif // DOUBLE_VALUE_TILE_H
