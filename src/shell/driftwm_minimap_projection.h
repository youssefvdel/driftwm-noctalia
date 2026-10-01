#pragma once

#include <utility>

namespace driftwm_minimap {

  // A Y-up canvas-space rectangle, kept Y-up so accumulation stays free of
  // sign juggling; the projection is the single place the axis flips.
  struct CanvasRect {
    float minX = 0.0F;
    float minY = 0.0F;
    float maxX = 0.0F;
    float maxY = 0.0F;
    bool empty = true;

    // A zero-sized rect contributes nothing, so a window with no geometry yet
    // cannot drag the fitted view out to the origin.
    void include(float centerX, float centerY, float width, float height) noexcept;
    void includePoint(float x, float y) noexcept;
  };

  // Maps Y-up canvas coordinates onto a widget's Y-down pixel space:
  //   px = canvasX * scale + offsetX
  //   py = -canvasY * scale + offsetY
  // The vertical negation is the Y-up to Y-down flip, and it is the only place
  // in the widget where a canvas sign change happens.
  struct Projection {
    float scale = 1.0F;
    float offsetX = 0.0F;
    float offsetY = 0.0F;
  };

  // An axis-aligned pixel rect, ready to hand to a rounded-rect node.
  struct PixelRect {
    float x = 0.0F;
    float y = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
  };

  // Below this canvas span a fit refuses to divide by it, so a lone window at the
  // origin still produces a usable (centered) map instead of a degenerate one.
  inline constexpr float kMinCanvasSpan = 1.0F;
  // The largest widget-px-per-canvas-unit a fit will produce, so a handful of
  // tiny windows spread over a small map does not blow up into unreadable slivers.
  inline constexpr float kMaxScale = 8.0F;

  // Scales `content` to fit a `width` x `height` widget rect, inset by `padding`
  // on every side, and centers it. A degenerate `content` (empty, non-finite, or
  // zero-span in both axes) yields a projection centered on the origin.
  [[nodiscard]] Projection fit(const CanvasRect& content, float width, float height, float padding) noexcept;

  [[nodiscard]] PixelRect project(
    const Projection& projection, float canvasX, float canvasY
  ) noexcept;

  // The exact inverse of project(), which is what makes a click on the map
  // land back on the canvas point the user pointed at.
  [[nodiscard]] std::pair<double, double> unproject(
    const Projection& projection, float pixelX, float pixelY
  ) noexcept;

  // Projects a canvas rect given as center + size (DriftWM's convention) into a
  // pixel rect, never smaller than `minSize` on either axis so a sub-pixel
  // window stays visible as a dot.
  [[nodiscard]] PixelRect projectRect(
    const Projection& projection, float centerX, float centerY, float width, float height,
    float minSize = 1.0F
  ) noexcept;

  // Projects a single point, e.g. a bookmark marker.
  [[nodiscard]] PixelRect projectPoint(
    const Projection& projection, float canvasX, float canvasY, float size
  ) noexcept;

} // namespace driftwm_minimap
