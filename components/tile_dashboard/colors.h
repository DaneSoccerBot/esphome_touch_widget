// colors.h
#ifndef COLORS_H
#define COLORS_H

#include "esphome.h"
#include "esphome/components/display/display.h"

namespace Colors {
  static esphome::Color GREEN(155, 197, 61);       // #9bc53d
  static esphome::Color YELLOW(253, 231, 76);      // #fde74c
  static esphome::Color RED(195, 66, 63);          // #c3423f
  static esphome::Color ORANGE(255, 128, 0) ;      // #ff8000
  static esphome::Color TILE_BACKGROUND(28, 28, 28);  // #1c1c1c
  static esphome::Color LIGHT_GREY(40, 40, 40);   // #282828
  static esphome::Color SCREEN_BACKGROUND(0, 0, 0); // #000000
  static esphome::Color LIGHT_TEXT(160, 160, 160);  // #c8c8c8  
  static esphome::Color TEXT(220, 220, 220);     // #e6e6e6
  static esphome::Color TILE_BORDER(60, 60, 60);  // #3c3c3c
}

#endif
