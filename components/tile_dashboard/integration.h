#ifndef TILE_DASHBOARD_INTEGRATION_H
#define TILE_DASHBOARD_INTEGRATION_H

#include <algorithm>

namespace esphome {
namespace tile_dashboard {

struct TouchMapping {
  int rotated_x{0};
  int rotated_y{0};
  bool inside{true};
  int col{1};
  int row{1};
  int local_x{0};
  int local_y{0};
  int local_x_raw{0};
  int local_y_raw{0};
};

inline TouchMapping map_touch(int x, int y, int width, int height,
                              int cols, int rows, int rotation,
                              int offset_x = 0, int offset_y = 0) {
  TouchMapping mapped;

  switch (rotation) {
    case 90:
      mapped.rotated_x = y;
      mapped.rotated_y = height - x;
      break;
    case 180:
      mapped.rotated_x = width - x;
      mapped.rotated_y = height - y;
      break;
    case 270:
      mapped.rotated_x = width - y;
      mapped.rotated_y = x;
      break;
    case 0:
    default:
      mapped.rotated_x = x;
      mapped.rotated_y = y;
      break;
  }

  cols = std::max(cols, 1);
  rows = std::max(rows, 1);
  width = std::max(width, cols);
  height = std::max(height, rows);

  mapped.rotated_x -= offset_x;
  mapped.rotated_y -= offset_y;
  mapped.inside = mapped.rotated_x >= 0 && mapped.rotated_x < width &&
                  mapped.rotated_y >= 0 && mapped.rotated_y < height;

  mapped.rotated_x = std::clamp(mapped.rotated_x, 0, width - 1);
  mapped.rotated_y = std::clamp(mapped.rotated_y, 0, height - 1);

  const int tile_w = std::max(width / cols, 1);
  const int tile_h = std::max(height / rows, 1);

  mapped.col = std::clamp(mapped.rotated_x / tile_w + 1, 1, cols);
  mapped.row = std::clamp(mapped.rotated_y / tile_h + 1, 1, rows);
  mapped.local_x = std::clamp(mapped.rotated_x % tile_w, 0, tile_w - 1);
  mapped.local_y = std::clamp(mapped.rotated_y % tile_h, 0, tile_h - 1);
  mapped.local_x_raw = std::clamp(mapped.rotated_x, 0, width - 1);
  mapped.local_y_raw = std::clamp(mapped.rotated_y, 0, height - 1);
  return mapped;
}

}  // namespace tile_dashboard
}  // namespace esphome

#endif  // TILE_DASHBOARD_INTEGRATION_H
