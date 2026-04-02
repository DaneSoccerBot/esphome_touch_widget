#ifdef USE_ESP32_VARIANT_ESP32S3
#include "st7701s.h"

#include <cstring>

#include "esp_heap_caps.h"
#include "esphome/core/application.h"
#include "esphome/core/gpio.h"
#include "esphome/core/log.h"

namespace esphome {
namespace st7701s {

namespace {

inline uint16_t read_pixel_565(const uint8_t *ptr, size_t pixel_index) {
  uint16_t pixel;
  std::memcpy(&pixel, ptr + pixel_index * sizeof(uint16_t), sizeof(uint16_t));
  return pixel;
}

}  // namespace

void ST7701S::setup() {
  this->spi_setup();
  this->write_init_sequence_();

  esp_lcd_rgb_panel_config_t config{};
  config.flags.fb_in_psram = 1;
  config.bounce_buffer_size_px = this->width_ * 10;
  config.num_fbs = 1;
  config.timings.h_res = this->width_;
  config.timings.v_res = this->height_;
  config.timings.hsync_pulse_width = this->hsync_pulse_width_;
  config.timings.hsync_back_porch = this->hsync_back_porch_;
  config.timings.hsync_front_porch = this->hsync_front_porch_;
  config.timings.vsync_pulse_width = this->vsync_pulse_width_;
  config.timings.vsync_back_porch = this->vsync_back_porch_;
  config.timings.vsync_front_porch = this->vsync_front_porch_;
  config.timings.flags.pclk_active_neg = this->pclk_inverted_;
  config.timings.pclk_hz = this->pclk_frequency_;
  config.clk_src = LCD_CLK_SRC_PLL160M;
  size_t data_pin_count = sizeof(this->data_pins_) / sizeof(this->data_pins_[0]);
  for (size_t i = 0; i != data_pin_count; i++) {
    config.data_gpio_nums[i] = this->data_pins_[i]->get_pin();
  }
  config.data_width = data_pin_count;
  config.disp_gpio_num = -1;
  config.hsync_gpio_num = this->hsync_pin_->get_pin();
  config.vsync_gpio_num = this->vsync_pin_->get_pin();
  config.de_gpio_num = this->de_pin_->get_pin();
  config.pclk_gpio_num = this->pclk_pin_->get_pin();
  esp_err_t err = esp_lcd_new_rgb_panel(&config, &this->handle_);
  if (err != ESP_OK) {
    esph_log_e(TAG, "lcd_new_rgb_panel failed: %s", esp_err_to_name(err));
    this->mark_failed();
    return;
  }
  ESP_ERROR_CHECK(esp_lcd_panel_reset(this->handle_));
  ESP_ERROR_CHECK(esp_lcd_panel_init(this->handle_));
}

void ST7701S::loop() {
  if (this->handle_ != nullptr)
    esp_lcd_rgb_panel_restart(this->handle_);
}

void ST7701S::draw_pixels_at(int x_start, int y_start, int w, int h, const uint8_t *ptr, display::ColorOrder order,
                             display::ColorBitness bitness, bool big_endian, int x_offset, int y_offset, int x_pad) {
  if (w <= 0 || h <= 0)
    return;
  if (bitness != display::COLOR_BITNESS_565) {
    return display::Display::draw_pixels_at(x_start, y_start, w, h, ptr, order, bitness, big_endian, x_offset, y_offset,
                                            x_pad);
  }

  x_start += this->offset_x_;
  y_start += this->offset_y_;

  const auto rotation = this->rotation_;
  if (rotation == display::DISPLAY_ROTATION_0_DEGREES) {
    esp_err_t err;
    if (x_offset == 0 && x_pad == 0 && y_offset == 0) {
      err = esp_lcd_panel_draw_bitmap(this->handle_, x_start, y_start, x_start + w, y_start + h, ptr);
    } else {
      const auto stride = x_offset + w + x_pad;
      for (int y = 0; y != h; y++) {
        err = esp_lcd_panel_draw_bitmap(this->handle_, x_start, y + y_start, x_start + w, y + y_start + 1,
                                        ptr + ((y + y_offset) * stride + x_offset) * 2);
        if (err != ESP_OK)
          break;
      }
    }
    if (err != ESP_OK)
      esph_log_e(TAG, "lcd_lcd_panel_draw_bitmap failed: %s", esp_err_to_name(err));
    return;
  }

  const int src_stride = x_offset + w + x_pad;
  const int out_w = (rotation == display::DISPLAY_ROTATION_90_DEGREES ||
                     rotation == display::DISPLAY_ROTATION_270_DEGREES)
                        ? h
                        : w;
  const int out_h = (rotation == display::DISPLAY_ROTATION_90_DEGREES ||
                     rotation == display::DISPLAY_ROTATION_270_DEGREES)
                        ? w
                        : h;

  int phys_x = x_start;
  int phys_y = y_start;
  switch (rotation) {
    case display::DISPLAY_ROTATION_90_DEGREES:
      phys_x = static_cast<int>(this->width_) - (y_start + h);
      phys_y = x_start;
      break;
    case display::DISPLAY_ROTATION_180_DEGREES:
      phys_x = static_cast<int>(this->width_) - (x_start + w);
      phys_y = static_cast<int>(this->height_) - (y_start + h);
      break;
    case display::DISPLAY_ROTATION_270_DEGREES:
      phys_x = y_start;
      phys_y = static_cast<int>(this->height_) - (x_start + w);
      break;
    default:
      break;
  }

  uint16_t *row_buf = static_cast<uint16_t *>(heap_caps_malloc(static_cast<size_t>(out_w) * sizeof(uint16_t),
                                                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (row_buf == nullptr) {
    row_buf = static_cast<uint16_t *>(heap_caps_malloc(static_cast<size_t>(out_w) * sizeof(uint16_t), MALLOC_CAP_8BIT));
  }
  if (row_buf == nullptr) {
    esph_log_e(TAG, "rotation row buffer alloc failed: %d px", out_w);
    return display::Display::draw_pixels_at(x_start - this->offset_x_, y_start - this->offset_y_, w, h, ptr, order,
                                            bitness, big_endian, x_offset, y_offset, x_pad);
  }

  esp_err_t err = ESP_OK;
  for (int out_y = 0; out_y < out_h; out_y++) {
    for (int out_x = 0; out_x < out_w; out_x++) {
      int src_x = out_x;
      int src_y = out_y;
      switch (rotation) {
        case display::DISPLAY_ROTATION_90_DEGREES:
          src_x = out_y;
          src_y = h - 1 - out_x;
          break;
        case display::DISPLAY_ROTATION_180_DEGREES:
          src_x = w - 1 - out_x;
          src_y = h - 1 - out_y;
          break;
        case display::DISPLAY_ROTATION_270_DEGREES:
          src_x = w - 1 - out_y;
          src_y = out_x;
          break;
        default:
          break;
      }
      const size_t src_index = static_cast<size_t>(src_y + y_offset) * src_stride + (src_x + x_offset);
      row_buf[out_x] = read_pixel_565(ptr, src_index);
    }

    err = esp_lcd_panel_draw_bitmap(this->handle_, phys_x, phys_y + out_y, phys_x + out_w, phys_y + out_y + 1,
                                    row_buf);
    if (err != ESP_OK)
      break;
  }

  heap_caps_free(row_buf);

  if (err != ESP_OK)
    esph_log_e(TAG, "lcd_lcd_panel_draw_bitmap failed: %s", esp_err_to_name(err));
}

void ST7701S::draw_pixel_at(int x, int y, Color color) {
  if (!this->get_clipping().inside(x, y))
    return;
  auto pixel = convert_big_endian(display::ColorUtil::color_to_565(color));

  this->draw_pixels_at(x, y, 1, 1, (const uint8_t *) &pixel, display::COLOR_ORDER_RGB, display::COLOR_BITNESS_565, true,
                       0, 0, 0);
  App.feed_wdt();
}

void ST7701S::write_command_(uint8_t value) {
  this->enable();
  if (this->dc_pin_ == nullptr) {
    this->write(value, 9);
  } else {
    this->dc_pin_->digital_write(false);
    this->write_byte(value);
    this->dc_pin_->digital_write(true);
  }
  this->disable();
}

void ST7701S::write_data_(uint8_t value) {
  this->enable();
  if (this->dc_pin_ == nullptr) {
    this->write(value | 0x100, 9);
  } else {
    this->dc_pin_->digital_write(true);
    this->write_byte(value);
  }
  this->disable();
}

void ST7701S::write_sequence_(uint8_t cmd, size_t len, const uint8_t *bytes) {
  this->write_command_(cmd);
  while (len-- != 0)
    this->write_data_(*bytes++);
}

void ST7701S::write_init_sequence_() {
  for (size_t i = 0; i != this->init_sequence_.size();) {
    uint8_t cmd = this->init_sequence_[i++];
    size_t len = this->init_sequence_[i++];
    if (len == ST7701S_DELAY_FLAG) {
      ESP_LOGV(TAG, "Delay %dms", cmd);
      delay(cmd);
    } else {
      this->write_sequence_(cmd, len, &this->init_sequence_[i]);
      i += len;
      ESP_LOGV(TAG, "Command %X, %d bytes", cmd, len);
      if (cmd == SW_RESET_CMD)
        delay(6);
    }
  }
  this->write_sequence_(CMD2_BKSEL, sizeof(CMD2_BK0), CMD2_BK0);
  this->write_command_(SDIR_CMD);
  this->write_data_(this->mirror_x_ ? 0x04 : 0x00);
  uint8_t val = this->color_mode_ == display::COLOR_ORDER_BGR ? 0x08 : 0x00;
  if (this->mirror_y_)
    val |= 0x10;
  this->write_command_(MADCTL_CMD);
  this->write_data_(val);
  ESP_LOGD(TAG, "write MADCTL %X", val);
  this->write_command_(this->invert_colors_ ? INVERT_ON : INVERT_OFF);
  delay(120);
  this->write_command_(SLEEP_OUT);
  this->write_command_(DISPLAY_ON);
  this->spi_teardown();
  delay(10);
}

void ST7701S::dump_config() {
  ESP_LOGCONFIG("", "ST7701S RGB LCD");
  ESP_LOGCONFIG(TAG,
                "  Height: %u\n"
                "  Width: %u",
                this->height_, this->width_);
  LOG_PIN("  CS Pin: ", this->cs_);
  LOG_PIN("  DC Pin: ", this->dc_pin_);
  LOG_PIN("  DE Pin: ", this->de_pin_);
  LOG_PIN("  Reset Pin: ", this->reset_pin_);
  size_t data_pin_count = sizeof(this->data_pins_) / sizeof(this->data_pins_[0]);
  for (size_t i = 0; i != data_pin_count; i++) {
    ESP_LOGCONFIG(TAG, "  Data pin %d: %s", i, this->data_pins_[i]->dump_summary().c_str());
  }
  ESP_LOGCONFIG(TAG, "  SPI Data rate: %dMHz", (unsigned) (this->data_rate_ / 1000000));
}

}  // namespace st7701s
}  // namespace esphome
#endif