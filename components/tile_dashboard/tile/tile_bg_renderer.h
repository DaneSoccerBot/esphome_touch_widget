#ifndef TILE_BG_RENDERER_H
#define TILE_BG_RENDERER_H

#include "esphome.h"
#include "esphome/components/display/display.h"
#include "colors.h"
#include "tile/draw_utils.h" // DrawUtils::draw_rounded_rect

class TileBackgroundRenderer
{
public:
  void drawFullBg(esphome::display::Display &it, int x, int y, int width, int height,
                  int numTilesY, int radius, bool draw_border = true)
  {

    int outerGap = 2;
    int borderWidth = 1;
    int innerGap = outerGap + borderWidth;

    if (numTilesY == 2)
    {
      height = height / 2;
    }
    // Wenn Border: zuerst großen Rahmen zeichnen
    if (draw_border)
    {
      DrawUtils::draw_rounded_rect(it, x + outerGap, y + outerGap, width - 2 * outerGap, height - 2 * outerGap, radius, Colors::TILE_BORDER);
      // Dann kleineres Rechteck oben drauf
      DrawUtils::draw_rounded_rect(it, x + innerGap, y + innerGap, width - 2 * innerGap, height - 2 * innerGap, radius, Colors::TILE_BACKGROUND);
      if (numTilesY == 2)
      {
        DrawUtils::draw_rounded_rect(it, x + outerGap, y + height + outerGap, width - 2 * outerGap, height - 2 * outerGap, radius, Colors::TILE_BORDER);
        // Dann kleineres Rechteck oben drauf
        DrawUtils::draw_rounded_rect(it, x + innerGap, y + height + innerGap, width - 2 * innerGap, height - 2 * innerGap, radius, Colors::TILE_BACKGROUND);
      }
    }
    else
    {
      DrawUtils::draw_rounded_rect(it, x + outerGap, y + outerGap, width - 2 * outerGap, height - 2 * outerGap, radius, Colors::TILE_BACKGROUND);
      if (numTilesY == 2)
      {
        DrawUtils::draw_rounded_rect(it, x + outerGap, y + height + outerGap, width - 2 * outerGap, height - 2 * outerGap, radius, Colors::TILE_BACKGROUND);
      }
    }
  }
  void drawBgColor(esphome::display::Display &it, int x, int y, int width, int height)
  {
    it.filled_rectangle(x, y, width, height, Colors::TILE_BACKGROUND);
  }
};
#endif // TILE_BG_RENDERER_H