#include "shell/driftwm_minimap_projection.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace driftwm_minimap {

  namespace {
    [[nodiscard]] bool usable(float value) noexcept { return std::isfinite(value); }
  } // namespace

  void CanvasRect::include(float centerX, float centerY, float width, float height) noexcept {
    if (!usable(centerX) || !usable(centerY) || !usable(width) || !usable(height) || width <= 0.0F || height <= 0.0F) {
      return;
    }
    const float halfWidth = width * 0.5F;
    const float halfHeight = height * 0.5F;
    includePoint(centerX - halfWidth, centerY - halfHeight);
    includePoint(centerX + halfWidth, centerY + halfHeight);
  }

  void CanvasRect::includePoint(float x, float y) noexcept {
    if (!usable(x) || !usable(y)) {
      return;
    }
    if (empty) {
      minX = maxX = x;
      minY = maxY = y;
      empty = false;
      return;
    }
    minX = std::min(minX, x);
    maxX = std::max(maxX, x);
    minY = std::min(minY, y);
    maxY = std::max(maxY, y);
  }

  Projection fit(const CanvasRect& content, float width, float height, float padding) noexcept {
    if (!usable(width) || !usable(height) || width <= 0.0F || height <= 0.0F) {
      return {};
    }

    const float availableWidth = std::max(1.0F, width - 2.0F * std::max(0.0F, padding));
    const float availableHeight = std::max(1.0F, height - 2.0F * std::max(0.0F, padding));

    if (content.empty || !usable(content.minX) || !usable(content.maxX) || !usable(content.minY)
        || !usable(content.maxY)) {
      // Nothing to frame: keep 1:1 units and center the origin in the widget.
      return {
        .scale = 1.0F,
        .offsetX = width * 0.5F,
        .offsetY = height * 0.5F,
      };
    }

    const float spanX = std::max(content.maxX - content.minX, kMinCanvasSpan);
    const float spanY = std::max(content.maxY - content.minY, kMinCanvasSpan);
    const float scale = std::clamp(std::min(availableWidth / spanX, availableHeight / spanY), 1.0e-4F, kMaxScale);

    // Center the fitted content, then place its center in the middle of what is
    // left of the widget after the padding inset.
    const float centerX = (content.minX + content.maxX) * 0.5F;
    const float centerY = (content.minY + content.maxY) * 0.5F;
    return {
      .scale = scale,
        .offsetX = width * 0.5F - centerX * scale,
        .offsetY = height * 0.5F + centerY * scale,
    };
  }

  PixelRect project(const Projection& projection, float canvasX, float canvasY) noexcept {
    return {
      .x = canvasX * projection.scale + projection.offsetX,
      .y = -canvasY * projection.scale + projection.offsetY,
      .width = 0.0F,
      .height = 0.0F,
    };
  }

  std::pair<double, double> unproject(const Projection& projection, float pixelX, float pixelY) noexcept {
    if (!(projection.scale > 0.0F) || !usable(projection.scale)) {
      return {0.0, 0.0};
    }
    return {
      static_cast<double>((pixelX - projection.offsetX) / projection.scale),
      static_cast<double>((projection.offsetY - pixelY) / projection.scale),
    };
  }

  PixelRect projectRect(
    const Projection& projection, float centerX, float centerY, float width, float height, float minSize
  ) noexcept {
    if (!usable(centerX) || !usable(centerY) || !usable(width) || !usable(height)) {
      return {};
    }
    if (width < 0.0F) {
      width = 0.0F;
    }
    if (height < 0.0F) {
      height = 0.0F;
    }

    // Y-down pixels: a positive canvas extent maps to a negative pixel extent,
    // so the corners are ordered by sign rather than by min/max.
    const float halfWidth = width * 0.5F * projection.scale;
    const float halfHeight = height * 0.5F * projection.scale;
    const float left = centerX * projection.scale + projection.offsetX - halfWidth;
    const float right = centerX * projection.scale + projection.offsetX + halfWidth;
    const float top = -centerY * projection.scale + projection.offsetY - halfHeight;
    const float bottom = -centerY * projection.scale + projection.offsetY + halfHeight;

    return {
      .x = std::min(left, right),
      .y = std::min(top, bottom),
      .width = std::max(std::abs(right - left), minSize),
      .height = std::max(std::abs(bottom - top), minSize),
    };
  }

  PixelRect projectPoint(const Projection& projection, float canvasX, float canvasY, float size) noexcept {
    if (!usable(canvasX) || !usable(canvasY) || !usable(size)) {
      return {};
    }
    const float extent = std::max(size, 0.0F);
    const auto point = project(projection, canvasX, canvasY);
    return {
      .x = point.x - extent * 0.5F,
      .y = point.y - extent * 0.5F,
      .width = extent,
      .height = extent,
    };
  }

} // namespace driftwm_minimap
