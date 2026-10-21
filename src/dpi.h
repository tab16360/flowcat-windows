#pragma once
#include <algorithm>
#include <cmath>
namespace fc {
// Only native window/GDI coordinates are pixels. Direct2D stays in 96-DPI DIPs.
struct DpiScale {
 unsigned dpi=96;
 int pixels(int dips) const { return static_cast<int>(std::lround(static_cast<double>(dips)*dpi/96.0)); }
 float dips(int pixels) const { return static_cast<float>(pixels)*96.0f/dpi; }
};
}
