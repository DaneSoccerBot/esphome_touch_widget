#ifdef USE_HOST
#include "sdl_esphome.h"
#include "esphome/components/display/display_color_utils.h"

#include <vector>

namespace {

inline uint16_t read_rgb565(const uint8_t *data, bool big_endian) {
  if (big_endian)
    return static_cast<uint16_t>((data[0] << 8) | data[1]);
  return static_cast<uint16_t>(data[0] | (data[1] << 8));
}

}  // namespace

namespace esphome {
namespace sdl {

void Sdl::setup() {
  SDL_Init(SDL_INIT_VIDEO);
  this->window_ = SDL_CreateWindow(App.get_name().c_str(), this->pos_x_, this->pos_y_, this->width_, this->height_,
                                   this->window_options_);
  this->renderer_ = SDL_CreateRenderer(this->window_, -1, SDL_RENDERER_SOFTWARE);
  SDL_RenderSetLogicalSize(this->renderer_, this->width_, this->height_);
  this->texture_ =
      SDL_CreateTexture(this->renderer_, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STATIC, this->width_, this->height_);
  SDL_SetTextureBlendMode(this->texture_, SDL_BLENDMODE_BLEND);
  this->x_low_ = this->width_;
  this->y_low_ = this->height_;
  this->x_high_ = 0;
  this->y_high_ = 0;
}

void Sdl::update() {
  this->do_update_();
  if ((this->x_high_ < this->x_low_) || (this->y_high_ < this->y_low_))
    return;
  SDL_Rect rect{this->x_low_, this->y_low_, this->x_high_ + 1 - this->x_low_, this->y_high_ + 1 - this->y_low_};
  this->x_low_ = this->width_;
  this->y_low_ = this->height_;
  this->x_high_ = 0;
  this->y_high_ = 0;
  this->redraw_(rect);
}

void Sdl::redraw_(SDL_Rect &rect) {
  SDL_RenderCopy(this->renderer_, this->texture_, &rect, &rect);
  SDL_RenderPresent(this->renderer_);
}

void Sdl::transform_logical_to_physical_(int &x, int &y) const {
  switch (this->rotation_) {
    case display::DISPLAY_ROTATION_90_DEGREES: {
      const int old_x = x;
      x = this->width_ - y - 1;
      y = old_x;
      break;
    }
    case display::DISPLAY_ROTATION_180_DEGREES:
      x = this->width_ - x - 1;
      y = this->height_ - y - 1;
      break;
    case display::DISPLAY_ROTATION_270_DEGREES: {
      const int old_x = x;
      x = y;
      y = this->height_ - old_x - 1;
      break;
    }
    case display::DISPLAY_ROTATION_0_DEGREES:
    default:
      break;
  }
}

SDL_Rect Sdl::physical_rect_for_(int x, int y, int w, int h) const {
  switch (this->rotation_) {
    case display::DISPLAY_ROTATION_90_DEGREES:
      return SDL_Rect{this->width_ - (y + h), x, h, w};
    case display::DISPLAY_ROTATION_180_DEGREES:
      return SDL_Rect{this->width_ - (x + w), this->height_ - (y + h), w, h};
    case display::DISPLAY_ROTATION_270_DEGREES:
      return SDL_Rect{y, this->height_ - (x + w), h, w};
    case display::DISPLAY_ROTATION_0_DEGREES:
    default:
      return SDL_Rect{x, y, w, h};
  }
}

void Sdl::draw_pixels_at(int x_start, int y_start, int w, int h, const uint8_t *ptr, display::ColorOrder order,
                         display::ColorBitness bitness, bool big_endian, int x_offset, int y_offset, int x_pad) {
  SDL_Rect rect = this->physical_rect_for_(x_start, y_start, w, h);
  if (bitness == display::COLOR_BITNESS_565 && order == display::COLOR_ORDER_RGB) {
    const int stride = x_offset + w + x_pad;
    std::vector<uint16_t> bulk(static_cast<size_t>(rect.w) * rect.h);

    for (int src_y = 0; src_y < h; src_y++) {
      for (int src_x = 0; src_x < w; src_x++) {
        const auto index = static_cast<size_t>((src_y + y_offset) * stride + (src_x + x_offset)) * 2;
        const uint16_t pixel = read_rgb565(ptr + index, big_endian);

        int dst_x = src_x;
        int dst_y = src_y;
        switch (this->rotation_) {
          case display::DISPLAY_ROTATION_90_DEGREES:
            dst_x = h - 1 - src_y;
            dst_y = src_x;
            break;
          case display::DISPLAY_ROTATION_180_DEGREES:
            dst_x = w - 1 - src_x;
            dst_y = h - 1 - src_y;
            break;
          case display::DISPLAY_ROTATION_270_DEGREES:
            dst_x = src_y;
            dst_y = w - 1 - src_x;
            break;
          case display::DISPLAY_ROTATION_0_DEGREES:
          default:
            break;
        }

        bulk[static_cast<size_t>(dst_y) * rect.w + dst_x] = pixel;
      }
    }

    SDL_UpdateTexture(this->texture_, &rect, bulk.data(), rect.w * 2);
  } else {
    Display::draw_pixels_at(x_start, y_start, w, h, ptr, order, bitness, big_endian, x_offset, y_offset, x_pad);
  }
  this->redraw_(rect);
}

void Sdl::draw_pixel_at(int x, int y, Color color) {
  if (!this->get_clipping().inside(x, y))
    return;

  this->transform_logical_to_physical_(x, y);
  SDL_Rect rect{x, y, 1, 1};
  auto data = (display::ColorUtil::color_to_565(color, display::COLOR_ORDER_RGB));
  SDL_UpdateTexture(this->texture_, &rect, &data, 2);
  if (x < this->x_low_)
    this->x_low_ = x;
  if (y < this->y_low_)
    this->y_low_ = y;
  if (x > this->x_high_)
    this->x_high_ = x;
  if (y > this->y_high_)
    this->y_high_ = y;
}

void Sdl::process_key(uint32_t keycode, bool down) {
  auto callback = this->key_callbacks_.find(keycode);
  if (callback != this->key_callbacks_.end())
    callback->second(down);
}

void Sdl::loop() {
  SDL_Event e;
  if (SDL_PollEvent(&e)) {
    switch (e.type) {
      case SDL_QUIT:
        exit(0);

      case SDL_MOUSEBUTTONDOWN:
      case SDL_MOUSEBUTTONUP:
        if (e.button.button == 1) {
          this->mouse_x = e.button.x;
          this->mouse_y = e.button.y;
          this->mouse_down = e.button.state != 0;
        }
        break;

      case SDL_MOUSEMOTION:
        if (e.motion.state & 1) {
          this->mouse_x = e.button.x;
          this->mouse_y = e.button.y;
          this->mouse_down = true;
        } else {
          this->mouse_down = false;
        }
        break;

      case SDL_KEYDOWN:
        ESP_LOGD(TAG, "keydown %d", e.key.keysym.sym);
        this->process_key(e.key.keysym.sym, true);
        break;

      case SDL_KEYUP:
        ESP_LOGD(TAG, "keyup %d", e.key.keysym.sym);
        this->process_key(e.key.keysym.sym, false);
        break;

      case SDL_WINDOWEVENT:
        switch (e.window.event) {
          case SDL_WINDOWEVENT_SIZE_CHANGED:
          case SDL_WINDOWEVENT_EXPOSED:
          case SDL_WINDOWEVENT_RESIZED: {
            SDL_Rect rect{0, 0, this->width_, this->height_};
            this->redraw_(rect);
            break;
          }
          default:
            break;
        }
        break;

      default:
        ESP_LOGV(TAG, "Event %d", e.type);
        break;
    }
  }
}

}  // namespace sdl
}  // namespace esphome
#endif
