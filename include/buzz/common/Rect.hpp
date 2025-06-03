#pragma once

namespace buzz
{
  namespace common
  {

    struct Rect
    {
      int x = 0;
      int y = 0;
      int width = 0;
      int height = 0;

      Rect() = default;
      Rect(int x_val, int y_val, int w_val, int h_val)
          : x(x_val), y(y_val), width(w_val), height(h_val) {}

      bool operator==(const Rect& other) const {
          return x == other.x && y == other.y && width == other.width && height == other.height;
      }
      bool operator!=(const Rect& other) const {
          return !(*this == other);
      }
    };

  }
}