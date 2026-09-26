#pragma once

#include <GfxRenderer.h>

namespace HeaderBackArrow {
inline void draw(const GfxRenderer& renderer, int centerX, int centerY) {
  renderer.drawLine(centerX - 8, centerY, centerX + 8, centerY, 2, true);
  renderer.drawLine(centerX - 8, centerY, centerX - 1, centerY - 7, 2, true);
  renderer.drawLine(centerX - 8, centerY, centerX - 1, centerY + 7, 2, true);
}
}  // namespace HeaderBackArrow
