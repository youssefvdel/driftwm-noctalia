#include "shell/desktop/widgets/desktop_driftwm_minimap_widget.h"

#include "compositors/driftwm/driftwm_state_source.h"
#include "core/input/key_modifiers.h"
#include "render/core/image_file_loader.h"
#include "render/core/renderer.h"
#include "render/scene/input_area.h"
#include "render/scene/node.h"
#include "system/app_identity.h"
#include "system/desktop_entry.h"
#include "system/internal_app_metadata.h"
#include "ui/builders.h"
#include "ui/controls/box.h"
#include "ui/controls/image.h"
#include "ui/style.h"
#include "util/string_utils.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

  using compositors::driftwm::DriftwmState;
  using driftwm_minimap::CanvasRect;
  using driftwm_minimap::PixelRect;

  // Fraction of the remaining pose gap closed per 60 Hz frame, matching the
  // compositor's own easing so a snapshot gap and a render gap read alike.
  constexpr double kPoseEasingPerFrame = 0.3;
  constexpr double kReferenceFrameMs = 1000.0 / 60.0;
  // A pose closer than this counts as settled and stops the frame loop.
  constexpr float kPoseEpsilonCanvas = 0.5F;
  constexpr float kPoseEpsilonZoom = 0.001F;

  // Inset between the map border and the content it frames.
  constexpr float kMapPadding = 3.0F;

  // Per-window app icons: a capped square fitting the smaller rect dimension
  // with padding, centered in the window rect. Icon-only: no name labels.
  constexpr float kIconMaxSide = 28.0F; // times content scale
  constexpr float kIconMinSide = 8.0F; // times content scale, below this no icon
  constexpr float kIconInnerPadding = 1.0F; // per side, times content scale
  // Past this many icons the map is noise; largest rects win the slots.
  constexpr std::size_t kMaxIcons = 24;

  // Edge sampling: average the outer ring of decoded icon pixels, skipping
  // mostly-transparent texels so rounded icons with clear corners sample the
  // artwork edge rather than the transparent surround.
  constexpr int kEdgeSampleWidth = 2; // pixels per side
  constexpr float kEdgeMinAlpha = 24.0F / 255.0F;

  // Focus glow: the same fill as unfocused windows plus a soft bloated outline
  // drawn by a dedicated node behind the window box. The rect shader pads the
  // quad by borderWidth + softness, so a thick soft border reads as a glow.
  constexpr float kFocusGlowExtraWidth = 2.0F; // times content scale
  constexpr float kFocusGlowSoftness = 4.0F;

  // Outgoing click-to-fit is trusted until the pose converges on it or this
  // long after arming, so a late Subscribe echo cannot pin the map forever.
  constexpr auto kClickTargetTimeout = std::chrono::milliseconds{1500};

  constexpr std::int32_t kZBackdrop = 0;
  constexpr std::int32_t kZOutputs = 10;
  constexpr std::int32_t kZLayers = 20;
  constexpr std::int32_t kZWindows = 30;
  constexpr std::int32_t kZFocusGlow = 25;
  constexpr std::int32_t kZIcons = 35;
  constexpr std::int32_t kZPinned = 40;
  constexpr std::int32_t kZBookmarks = 50;
  constexpr std::int32_t kZViewport = 60;
  constexpr std::int32_t kZFullscreen = 70;

  // Default alphas for the theme role each unset color falls back to.
  constexpr float kBackdropAlpha = 0.35F;
  constexpr float kOutputAlpha = 0.8F;
  constexpr float kWindowAlpha = 0.75F;
  constexpr float kSuspendedAlpha = 0.7F;
  constexpr float kLayerAlpha = 0.4F;
  constexpr float kPinnedAlpha = 0.8F;
  constexpr float kBookmarkAlpha = 0.9F;

  constexpr float kBookmarkMarkerSize = 3.0F;
  constexpr float kActiveBookmarkMarkerSize = 5.0F;
  constexpr float kFullscreenDotSize = 4.0F;
  constexpr float kFullscreenDotInset = 2.0F;

  // Clicking a window centers on it and zooms to fit the whole frame with a
  // small margin. The compositor itself clamps out to fit-all and in to native
  // 1.0 (no magnification), so this stays inside that same sane window.
  constexpr float kWindowZoomMargin = 0.9F;
  constexpr double kMinWindowZoom = 0.05;
  constexpr double kMaxWindowZoom = 1.0;

  // The focused output's viewport, falling back to the top-level camera/zoom.
  [[nodiscard]] float viewportZoom(const DriftwmState& state) noexcept {
    for (const auto& output : state.outputs) {
      if (output.active && output.zoom > 0.0F) {
        return output.zoom;
      }
    }
    return state.zoom > 0.0F ? state.zoom : 1.0F;
  }

  [[nodiscard]] std::pair<float, float> viewportSize(const DriftwmState& state) noexcept {
    for (const auto& output : state.outputs) {
      if (output.active && output.width > 0.0F && output.height > 0.0F) {
        return {output.width, output.height};
      }
    }
    return {};
  }

  // Rule coordinates are output-relative logical pixels, so both the origin and
  // the extent have to be scaled by the output's zoom to land back on the canvas.
  struct CanvasPlacement {
    float centerX = 0.0F;
    float centerY = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
    bool valid = false;
  };

  [[nodiscard]] CanvasPlacement
  placeScreenWindow(const DriftwmState& state, const compositors::driftwm::DriftwmScreenWindow& window) noexcept {
    for (const auto& output : state.outputs) {
      if (output.name != window.output || output.zoom <= 0.0F) {
        continue;
      }
      return {
        .centerX = output.cameraX + window.centerX / output.zoom,
        .centerY = output.cameraY + window.centerY / output.zoom,
        .width = window.width / output.zoom,
        .height = window.height / output.zoom,
        .valid = true,
      };
    }
    // A screen-space window whose output is missing from the snapshot has no
    // canvas position, so it is dropped rather than pinned to the origin.
    return {};
  }

  [[nodiscard]] float rectArea(const PixelRect& rect) noexcept { return rect.width * rect.height; }

} // namespace

DesktopDriftwmMinimapWidget::DesktopDriftwmMinimapWidget(
    compositors::driftwm::DriftwmStateSource& source, Options options
)
    : m_source(source), m_options(std::move(options)) {}

DesktopDriftwmMinimapWidget::~DesktopDriftwmMinimapWidget() = default;

bool DesktopDriftwmMinimapWidget::effectiveClickThrough() const noexcept {
  if (!m_options.clickToMove) {
    return true;
  }
  if (!m_options.clickThrough) {
    return false;
  }
  // See-through by default; holding the click key temporarily makes the map
  // clickable. With modifier "none" there is no key to hold, so see-through
  // stays display-only (pick click_through=false for focus-free clicking).
  const std::string& modifier = m_options.clickModifier;
  if (modifier.empty() || modifier == "none") {
    return true;
  }
  return !m_modifierClickActive;
}

void DesktopDriftwmMinimapWidget::setModifierClickActive(bool active) {
  if (m_modifierClickActive == active) {
    return;
  }
  m_modifierClickActive = active;
  // The host flips the layer-shell input region so the hold-to-click gate takes
  // effect without rebuilding the widget.
  if (m_clickThroughCallback) {
    m_clickThroughCallback(effectiveClickThrough());
  }
}

void DesktopDriftwmMinimapWidget::onModifierState(std::uint32_t mods) {
  if (m_options.clickModifier.empty() || m_options.clickModifier == "none") {
    return;
  }
  setModifierClickActive(isModifierHeld(mods));
}

bool DesktopDriftwmMinimapWidget::isModifierHeld(std::uint32_t mods) const noexcept {
  const std::string& modifier = m_options.clickModifier;
  if (modifier.empty() || modifier == "none") {
    return true;
  }
  if (modifier == "super") {
    return (mods & KeyMod::Super) != 0U;
  }
  if (modifier == "alt") {
    return (mods & KeyMod::Alt) != 0U;
  }
  if (modifier == "ctrl") {
    return (mods & KeyMod::Ctrl) != 0U;
  }
  if (modifier == "shift") {
    return (mods & KeyMod::Shift) != 0U;
  }
  return true;
}

void DesktopDriftwmMinimapWidget::create() {
  // The InputArea always accepts left clicks while clickToMove is on: the
  // layer-shell input region (see effectiveClickThrough) gates delivery, and it
  // flips dynamically as the modifier is held/released. Propagating keeps a
  // bound gesture reachable: the map consumes the click for its own camera pan,
  // and an explicit `actions` binding still fires on top.
  const bool clickable = acceptsClicks();
  auto area = ui::inputArea({
      .acceptedButtons = clickable ? InputArea::buttonMask(BTN_LEFT) : 0U,
      .propagateEvents = clickable,
  });
  m_area = area.get();
  if (clickable) {
    m_area->setOnClick([this](const InputArea::PointerData& data) { handleMapClick(data.localX, data.localY); });
  }

  auto backdrop = ui::box();
  m_backdrop = backdrop.get();
  m_backdrop->setZIndex(kZBackdrop);
  m_backdrop->setClipChildren(true);
  area->addChild(std::move(backdrop));

  m_viewport = addSingletonBox(*m_backdrop, kZViewport);
  m_fullscreen = addSingletonBox(*m_backdrop, kZFullscreen);

  // The stream is pumped on the main loop before the frame is drawn, so the map
  // is rebuilt here rather than waiting for an update tick: a converged pose
  // schedules no frame ticks, and a redraw must not repaint stale nodes.
  m_stateConn = m_source.changed().connect([this]() {
    syncMap();
    requestRedraw();
    if (m_options.smoothingMs > 0) {
      requestFrameTick();
    }
  });

  // Every color is resolved to a fixed Color, so a theme change has to be
  // re-resolved here rather than by the nodes themselves.
  m_paletteConn = paletteChanged().connect([this]() {
    m_paletteDirty = true;
    requestRedraw();
  });

  buildDesktopIconIndex();
  setRoot(std::move(area));
}

void DesktopDriftwmMinimapWidget::setEditorPreview(bool enabled) noexcept {
  if (m_editorPreview == enabled) {
    return;
  }
  m_editorPreview = enabled;
  if (root() == nullptr) {
    return;
  }
  // The editor keeps widgets laid out even when the runtime would idle them, so
  // the map is re-synced and any pending glide is re-armed to stay live.
  syncMap();
  requestRedraw();
  if (enabled && needsFrameTick()) {
    requestFrameTick();
  }
}

bool DesktopDriftwmMinimapWidget::needsFrameTick() const {
  if (!m_source.available()) {
    return false;
  }
  return m_options.smoothingMs > 0 && !poseConverged();
}

void DesktopDriftwmMinimapWidget::onFrameTick(float deltaMs, Renderer& renderer) {
  if (!m_source.available()) {
    return;
  }
  const bool wasConverged = poseConverged();
  stepPose(deltaMs);
  if (wasConverged && poseConverged()) {
    return;
  }
  syncMap(renderer);
  requestRedraw();
}

void DesktopDriftwmMinimapWidget::doRebindRenderer(Renderer& renderer) {
  m_renderer = &renderer;
  // Icon textures are owned by the Image nodes and rebind through the scene
  // tree; nothing cached here outlives the context.
}

void DesktopDriftwmMinimapWidget::doLayout(Renderer& renderer) {
  if (root() == nullptr) {
    return;
  }

  // A boxed tile owns both axes and the map simply fills it; an unboxed tile
  // falls back to the configured natural size, scaled like every other content.
  float width = boxInnerWidth();
  float height = boxInnerHeight();
  if (!(width > 0.0F) || !(height > 0.0F)) {
    width = std::max(1.0F, static_cast<float>(m_options.width) * m_contentScale);
    height = std::max(1.0F, static_cast<float>(m_options.height) * m_contentScale);
  }
  m_mapSizeX = width;
  m_mapSizeY = height;
  root()->setSize(m_mapSizeX, m_mapSizeY);
  if (m_backdrop != nullptr) {
    m_backdrop->setPosition(0.0F, 0.0F);
    m_backdrop->setSize(m_mapSizeX, m_mapSizeY);
  }

  m_renderer = &renderer;
  syncColors();
  syncPose();
  syncMap(renderer);
}

void DesktopDriftwmMinimapWidget::doUpdate(Renderer& renderer) {
  m_renderer = &renderer;
  syncPose();
  syncMap(renderer);
}

void DesktopDriftwmMinimapWidget::onFontFamilyChanged(const std::string&, Renderer&) {
  // Icon-only: no text nodes to re-font.
}

float DesktopDriftwmMinimapWidget::borderWidth() const noexcept { return std::max(1.0F, m_contentScale); }

float DesktopDriftwmMinimapWidget::cornerRadius() const noexcept { return Style::scaledRadiusSm(m_contentScale); }

Color DesktopDriftwmMinimapWidget::resolve(const ColorSpec& spec, ColorRole role, float alpha) const {
  if (spec == clearColorSpec()) {
    return resolveColorSpec(colorSpecFromRole(role, alpha));
  }
  // A user-set color keeps its own alpha; the argument is only the role default.
  return resolveColorSpec(spec);
}

void DesktopDriftwmMinimapWidget::syncColors() {
  m_paletteDirty = false;
  if (m_backdrop == nullptr) {
    return;
  }
  RoundedRectStyle backdropStyle;
  backdropStyle.fill = resolve(m_options.backgroundColor, ColorRole::Surface, kBackdropAlpha);
  backdropStyle.border = resolve(m_options.outputColor, ColorRole::Outline, kOutputAlpha);
  backdropStyle.borderWidth = borderWidth();
  backdropStyle.radius = cornerRadius();
  backdropStyle.softness = 0.5F;
  m_backdrop->setStyle(backdropStyle);
}

void DesktopDriftwmMinimapWidget::syncPose() {
  if (!m_source.available()) {
    // A reset stream empties the state, so the next snapshot has to snap rather
    // than glide from a camera that no longer exists. The pose is also snapped
    // exactly once per stream life, then eased from there.
    m_poseInitialized = false;
    m_clickTarget.armed = false;
    return;
  }
  if (m_options.smoothingMs <= 0) {
    return;
  }
  // An armed click target disarms once the stream has caught up to it or the
  // timeout expires; until then the pose eases toward the outgoing target, not
  // the stale stream echo.
  if (m_clickTarget.armed) {
    const auto& state = m_source.state();
    const bool caughtUp = std::abs(state.cameraX - m_clickTarget.cameraX) <= kPoseEpsilonCanvas
        && std::abs(state.cameraY - m_clickTarget.cameraY) <= kPoseEpsilonCanvas
        && std::abs(state.zoom - m_clickTarget.zoom) <= kPoseEpsilonZoom;
    const bool timedOut =
        std::chrono::steady_clock::now() - m_clickTarget.armedAt >= kClickTargetTimeout;
    if (caughtUp || timedOut) {
      m_clickTarget.armed = false;
    }
  }
  // The pose is only snapped when there is nothing to glide from: the first
  // snapshot after (re)connect, or every snapshot when smoothing is off
  // (handled by stepPose/doLayout paths). Otherwise onFrameTick eases it, so
  // a burst of stream frames glides once instead of teleporting.
  if (!m_poseInitialized) {
    if (m_clickTarget.armed) {
      m_pose = {m_clickTarget.cameraX, m_clickTarget.cameraY, m_clickTarget.zoom};
    } else {
      const auto& state = m_source.state();
      m_pose = {state.cameraX, state.cameraY, state.zoom};
    }
    m_poseInitialized = true;
  }
}

void DesktopDriftwmMinimapWidget::stepPose(float deltaMs) {
  const auto& state = m_source.state();
  float goalX = state.cameraX;
  float goalY = state.cameraY;
  float goalZoom = state.zoom;
  if (m_clickTarget.armed) {
    const bool timedOut =
        std::chrono::steady_clock::now() - m_clickTarget.armedAt >= kClickTargetTimeout;
    if (timedOut) {
      m_clickTarget.armed = false;
    } else {
      goalX = m_clickTarget.cameraX;
      goalY = m_clickTarget.cameraY;
      goalZoom = m_clickTarget.zoom;
    }
  }
  if (m_options.smoothingMs <= 0 || !std::isfinite(deltaMs) || deltaMs <= 0.0F) {
    m_pose = {goalX, goalY, goalZoom};
    return;
  }
  // `smoothing_ms` is the budget for the visible glide, not an easing time
  // constant: closing the same share of the gap every 60 Hz frame lands on the
  // target in roughly that many milliseconds.
  const double share =
      1.0 - std::pow(1.0 - kPoseEasingPerFrame, static_cast<double>(deltaMs) / kReferenceFrameMs);
  m_pose.cameraX += static_cast<float>((goalX - m_pose.cameraX) * share);
  m_pose.cameraY += static_cast<float>((goalY - m_pose.cameraY) * share);
  m_pose.zoom += static_cast<float>((goalZoom - m_pose.zoom) * share);
  if (m_clickTarget.armed) {
    const bool caughtUp = std::abs(state.cameraX - m_clickTarget.cameraX) <= kPoseEpsilonCanvas
        && std::abs(state.cameraY - m_clickTarget.cameraY) <= kPoseEpsilonCanvas
        && std::abs(state.zoom - m_clickTarget.zoom) <= kPoseEpsilonZoom;
    const bool arrived = std::abs(goalX - m_pose.cameraX) <= kPoseEpsilonCanvas
        && std::abs(goalY - m_pose.cameraY) <= kPoseEpsilonCanvas
        && std::abs(goalZoom - m_pose.zoom) <= kPoseEpsilonZoom;
    if (caughtUp && arrived) {
      m_clickTarget.armed = false;
    }
  }
}

bool DesktopDriftwmMinimapWidget::poseConverged() const noexcept {
  if (!m_source.available() || m_options.smoothingMs <= 0) {
    return true;
  }
  if (m_clickTarget.armed) {
    if (std::chrono::steady_clock::now() - m_clickTarget.armedAt >= kClickTargetTimeout) {
      return true;
    }
    return std::abs(m_clickTarget.cameraX - m_pose.cameraX) <= kPoseEpsilonCanvas
        && std::abs(m_clickTarget.cameraY - m_pose.cameraY) <= kPoseEpsilonCanvas
        && std::abs(m_clickTarget.zoom - m_pose.zoom) <= kPoseEpsilonZoom;
  }
  const auto& state = m_source.state();
  return std::abs(state.cameraX - m_pose.cameraX) <= kPoseEpsilonCanvas
      && std::abs(state.cameraY - m_pose.cameraY) <= kPoseEpsilonCanvas
      && std::abs(state.zoom - m_pose.zoom) <= kPoseEpsilonZoom;
}

void DesktopDriftwmMinimapWidget::moveCameraTo(float pixelX, float pixelY) {
  if (!clicksActive() || !m_source.available()) {
    return;
  }
  // A fullscreen window parks the viewport, so writing the camera would drop
  // the user out of a fullscreen video rather than panning behind it.
  const auto& state = m_source.state();
  if (!state.fullscreen.empty()) {
    return;
  }
  const auto [canvasX, canvasY] = driftwm_minimap::unproject(m_projection, pixelX, pixelY);
  if (!m_source.moveCamera(canvasX, canvasY)) {
    return;
  }
  // Trust the outgoing target while the compositor animates: the stream first
  // echoes the pre-move pose, which would otherwise drag the easing backwards.
  // No zoom change here, so the target zoom is the snapshot's own zoom.
  const float zoom = state.zoom > 0.0F ? state.zoom : 1.0F;
  m_clickTarget = {
      .cameraX = static_cast<float>(canvasX),
      .cameraY = static_cast<float>(canvasY),
      .zoom = zoom,
      .armedAt = std::chrono::steady_clock::now(),
      .armed = true,
  };
  // driftwm pans animatedly and only reports the target, so keep the frame loop
  // alive to ease the drawn viewport toward the new camera.
  requestFrameTick();
}

bool DesktopDriftwmMinimapWidget::modifierHeldNow() const noexcept {
  if (!m_modifierProvider) {
    return false;
  }
  return isModifierHeld(m_modifierProvider());
}

bool DesktopDriftwmMinimapWidget::clickModifierSatisfied() const noexcept {
  const std::string& modifier = m_options.clickModifier;
  if (modifier.empty() || modifier == "none") {
    return true;
  }
  // Either source satisfies: the live seat mask sampled at click time, or the
  // host-fed key-event flag. The seat's xkb state is only fresh while a
  // Noctalia surface holds keyboard focus (this surface never does), and key
  // events only reach the host while Noctalia is focused — same underlying
  // constraint, but two independent read paths so whichever observed the hold
  // lets the click act.
  return modifierHeldNow() || m_modifierClickActive;
}

void DesktopDriftwmMinimapWidget::handleMapClick(float pixelX, float pixelY) {
  if (!clicksActive() || !m_source.available()) {
    return;
  }
  if (!clickModifierSatisfied()) {
    // Self-heal a flag stuck by a focus loss: if neither source reports the
    // modifier held, drop the latched state so the surface falls back to
    // see-through instead of staying clickable.
    setModifierClickActive(false);
    return;
  }
  const auto& state = m_source.state();
  // Setting camera or zoom while fullscreen exits fullscreen first, so a click
  // during fullscreen must not touch either.
  if (!state.fullscreen.empty()) {
    return;
  }
  const auto [canvasX, canvasY] = driftwm_minimap::unproject(m_projection, pixelX, pixelY);

  // Smallest containing canvas window wins, so overlapping windows zoom to the
  // most specific one rather than the largest behind it.
  const compositors::driftwm::DriftwmWindow* hit = nullptr;
  float hitArea = 0.0F;
  for (const auto& window : state.windows) {
    if (!(window.width > 0.0F) || !(window.height > 0.0F)) {
      continue;
    }
    const float dx = std::abs(static_cast<float>(canvasX) - window.centerX);
    const float dy = std::abs(static_cast<float>(canvasY) - window.centerY);
    if (dx <= window.width * 0.5F && dy <= window.height * 0.5F) {
      const float area = window.width * window.height;
      if (hit == nullptr || area < hitArea) {
        hit = &window;
        hitArea = area;
      }
    }
  }

  if (hit != nullptr) {
    const auto [outputWidth, outputHeight] = viewportSize(state);
    if (outputWidth > 0.0F && outputHeight > 0.0F && hit->width > 0.0F && hit->height > 0.0F) {
      double target = std::min(
                          static_cast<double>(outputWidth) / static_cast<double>(hit->width),
                          static_cast<double>(outputHeight) / static_cast<double>(hit->height)
                      )
          * static_cast<double>(kWindowZoomMargin);
      target = std::clamp(target, kMinWindowZoom, kMaxWindowZoom);
      if (std::isfinite(target) && target > 0.0) {
        // Zoom FIRST, then camera: the compositor clamps the camera target to
        // the zoomed viewport bounds, so a camera sent at the old (zoomed-out)
        // zoom clamps to the wrong place and the follow-up zoom strands the
        // view a few pixels out. Zooming first moves the clamp window, then the
        // camera lands exactly. The stream echoes the stale pre-zoom camera
        // first, so arm the outgoing target and ease toward it until the stream
        // catches up (or a short timeout).
        const float zoom = static_cast<float>(target);
        if (m_source.setZoom(target)) {
          (void)m_source.moveCamera(hit->centerX, hit->centerY);
          m_clickTarget = {
              .cameraX = hit->centerX,
              .cameraY = hit->centerY,
              .zoom = zoom,
              .armedAt = std::chrono::steady_clock::now(),
              .armed = true,
          };
        } else if (m_source.moveCamera(hit->centerX, hit->centerY)) {
          const float fallbackZoom = state.zoom > 0.0F ? state.zoom : 1.0F;
          m_clickTarget = {
              .cameraX = hit->centerX,
              .cameraY = hit->centerY,
              .zoom = fallbackZoom,
              .armedAt = std::chrono::steady_clock::now(),
              .armed = true,
          };
        }
        requestFrameTick();
        return;
      }
    }
    if (m_source.moveCamera(hit->centerX, hit->centerY)) {
      const float zoom = state.zoom > 0.0F ? state.zoom : 1.0F;
      m_clickTarget = {
          .cameraX = hit->centerX,
          .cameraY = hit->centerY,
          .zoom = zoom,
          .armedAt = std::chrono::steady_clock::now(),
          .armed = true,
      };
      requestFrameTick();
    }
    return;
  }

  moveCameraTo(pixelX, pixelY);
}

void DesktopDriftwmMinimapWidget::applyBox(
    Box* box, const PixelRect& rect, const Color& fill, const Color& border, float stroke, float radius
) {
  if (box == nullptr) {
    return;
  }
  box->setVisible(true);
  box->setPosition(rect.x, rect.y);
  box->setSize(rect.width, rect.height);

  RoundedRectStyle style;
  style.fill = fill;
  style.border = border;
  style.borderWidth = border.a > 0.0F ? stroke : 0.0F;
  style.radius = radius;
  style.softness = 0.5F;
  box->setStyle(style);
}

Box* DesktopDriftwmMinimapWidget::addSingletonBox(Node& parent, std::int32_t zIndex) {
  auto node = ui::box();
  Box* raw = node.get();
  raw->setZIndex(zIndex);
  parent.addChild(std::move(node));
  return raw;
}

void DesktopDriftwmMinimapWidget::reserveBoxes(std::vector<Box*>& pool, std::size_t count, std::int32_t zIndex) {
  while (pool.size() < count) {
    pool.push_back(addSingletonBox(*m_backdrop, zIndex));
  }
}

void DesktopDriftwmMinimapWidget::applyGlow(
    Box* box, const PixelRect& rect, const Color& color, float radius
) {
  if (box == nullptr) {
    return;
  }
  // A dedicated halo node behind the window box: transparent fill, a thick
  // soft border in the focused theme color. The rect shader pads the draw
  // quad by borderWidth + softness, so the blurred ring spills outward as a
  // glow while the window fill itself stays untouched.
  box->setVisible(true);
  const float pad = kFocusGlowExtraWidth * m_contentScale;
  box->setPosition(rect.x - pad, rect.y - pad);
  box->setSize(rect.width + pad * 2.0F, rect.height + pad * 2.0F);

  RoundedRectStyle style;
  style.fill = clearColor();
  style.border = color;
  style.borderWidth = std::max(1.0F, m_contentScale) + kFocusGlowExtraWidth * m_contentScale;
  style.radius = radius + pad;
  style.softness = kFocusGlowSoftness;
  box->setStyle(style);
}

std::optional<Color> DesktopDriftwmMinimapWidget::edgeColorForIcon(const std::string& iconPath) {
  if (iconPath.empty()) {
    return std::nullopt;
  }
  if (const auto cached = m_iconEdgeColors.find(iconPath); cached != m_iconEdgeColors.end()) {
    return cached->second;
  }
  // Decode once at a small probe size through the shared image-file machinery
  // (no new libraries); the cache keeps this off the frame path afterwards.
  auto loaded = loadImageFile(iconPath, 64);
  if (!loaded.has_value() || loaded->width <= 0 || loaded->height <= 0 || loaded->rgba.empty()) {
    return std::nullopt;
  }
  const int width = loaded->width;
  const int height = loaded->height;
  const auto& pixels = loaded->rgba;
  const int ring = std::clamp(std::min(width, height) / 8, 1, kEdgeSampleWidth);
  double sumR = 0.0;
  double sumG = 0.0;
  double sumB = 0.0;
  double weight = 0.0;
  auto accumulate = [&](int x, int y) {
    const std::size_t offset =
        (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4U;
    if (offset + 4U > pixels.size()) {
      return;
    }
    const float alpha = static_cast<float>(pixels[offset + 3U]) / 255.0F;
    if (alpha < kEdgeMinAlpha) {
      return;
    }
    // Weight by alpha so semi-transparent edge texels count proportionally.
    sumR += static_cast<double>(pixels[offset + 0U]) * alpha;
    sumG += static_cast<double>(pixels[offset + 1U]) * alpha;
    sumB += static_cast<double>(pixels[offset + 2U]) * alpha;
    weight += alpha;
  };
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < ring; ++x) {
      accumulate(x, y);
      accumulate(width - 1 - x, y);
    }
  }
  for (int x = ring; x < width - ring; ++x) {
    for (int y = 0; y < ring; ++y) {
      accumulate(x, y);
      accumulate(x, height - 1 - y);
    }
  }
  if (!(weight > 0.0)) {
    return std::nullopt;
  }
  const Color sampled{
      .r = static_cast<float>(sumR / weight) / 255.0F,
      .g = static_cast<float>(sumG / weight) / 255.0F,
      .b = static_cast<float>(sumB / weight) / 255.0F,
      .a = kWindowAlpha,
  };
  m_iconEdgeColors.emplace(iconPath, sampled);
  return sampled;
}

void DesktopDriftwmMinimapWidget::reserveIcons(std::size_t count) {
  while (m_iconNodes.size() < count) {
    auto node = ui::image({.fit = ImageFit::Contain});
    Image* raw = node.get();
    raw->setZIndex(kZIcons);
    raw->setHitTestVisible(false);
    raw->setParticipatesInLayout(false);
    m_backdrop->addChild(std::move(node));
    m_iconNodes.push_back(raw);
    m_iconPaths.emplace_back();
  }
}

void DesktopDriftwmMinimapWidget::hideBoxes(std::vector<Box*>& pool, std::size_t used) {
  for (std::size_t i = used; i < pool.size(); ++i) {
    pool[i]->setVisible(false);
  }
}

void DesktopDriftwmMinimapWidget::hideIcons(std::size_t used, Renderer* renderer) {
  for (std::size_t i = used; i < m_iconNodes.size(); ++i) {
    if (m_iconNodes[i] != nullptr) {
      m_iconNodes[i]->setVisible(false);
      if (renderer != nullptr) {
        m_iconNodes[i]->clear(*renderer);
      }
    }
    if (i < m_iconPaths.size()) {
      m_iconPaths[i].clear();
    }
  }
}

void DesktopDriftwmMinimapWidget::buildDesktopIconIndex() {
  m_appIcons.clear();
  auto addIndexKey = [this](std::string_view key, const std::string& icon) {
    if (key.empty() || icon.empty()) {
      return;
    }
    m_appIcons.try_emplace(std::string{key}, icon);
    m_appIcons.try_emplace(StringUtils::toLower(key), icon);
  };

  const auto& entries = desktopEntries();
  for (const auto& entry : entries) {
    if (entry.id.empty() || entry.icon.empty()) {
      continue;
    }
    addIndexKey(entry.id, entry.icon);
    if (const auto dot = entry.id.rfind('.'); dot != std::string::npos && dot + 1 < entry.id.size()) {
      addIndexKey(entry.id.substr(dot + 1), entry.icon);
    }
    if (const auto dash = entry.id.rfind('-'); dash != std::string::npos && dash + 1 < entry.id.size()) {
      const std::string suffix = entry.id.substr(dash + 1);
      if (suffix == "bin" || suffix == "desktop") {
        addIndexKey(entry.id.substr(0, dash), entry.icon);
      }
    }
    if (!entry.startupWmClass.empty()) {
      addIndexKey(entry.startupWmClass, entry.icon);
    }
  }
  m_desktopEntriesVersion = desktopEntriesVersion();
  m_iconThemeGeneration = IconResolver::themeGeneration();
}

std::string DesktopDriftwmMinimapWidget::resolveIconPath(const std::string& appId, int targetSize) {
  if (appId.empty()) {
    return {};
  }

  if (const auto internal = internal_apps::metadataForAppId(appId); internal.has_value()) {
    return internal->iconPath;
  }

  if (const auto entry = app_identity::findDesktopEntry(appId, desktopEntries());
      entry.has_value() && !entry->icon.empty()) {
    const std::string& resolved = m_iconResolver.resolve(entry->icon, targetSize);
    if (!resolved.empty()) {
      return resolved;
    }
  }

  auto resolveByName = [this, targetSize](const std::string& name) -> std::string {
    if (name.empty()) {
      return {};
    }
    return m_iconResolver.resolve(name, targetSize);
  };

  if (auto it = m_appIcons.find(appId); it != m_appIcons.end()) {
    if (const auto path = resolveByName(it->second); !path.empty()) {
      return path;
    }
  }

  const std::string appIdLower = StringUtils::toLower(appId);
  if (auto it = m_appIcons.find(appIdLower); it != m_appIcons.end()) {
    if (const auto path = resolveByName(it->second); !path.empty()) {
      return path;
    }
  }

  if (const auto slash = appId.find_last_of('/'); slash != std::string::npos && slash + 1 < appId.size()) {
    const std::string tail = appId.substr(slash + 1);
    if (auto it = m_appIcons.find(tail); it != m_appIcons.end()) {
      if (const auto path = resolveByName(it->second); !path.empty()) {
        return path;
      }
    }
  }

  if (const auto direct = resolveByName(appId); !direct.empty()) {
    return direct;
  }
  return resolveByName("application-x-executable");
}

void DesktopDriftwmMinimapWidget::refreshIconsIfStale() {
  if (desktopEntriesVersion() == m_desktopEntriesVersion
      && IconResolver::themeGeneration() == m_iconThemeGeneration) {
    return;
  }
  buildDesktopIconIndex();
  // A theme swap can repaint the same path with new pixels, so drop the
  // sampled edge colors alongside the icon index.
  m_iconEdgeColors.clear();
}

CanvasRect DesktopDriftwmMinimapWidget::collectContent(const DriftwmState& state) const {
  CanvasRect content;

  // The viewport is always framed: it is the one thing the map exists to point at.
  const float zoom = viewportZoom(state);
  const auto [outputWidth, outputHeight] = viewportSize(state);
  if (outputWidth > 0.0F && outputHeight > 0.0F) {
    content.include(state.cameraX, state.cameraY, outputWidth / zoom, outputHeight / zoom);
  } else {
    content.includePoint(state.cameraX, state.cameraY);
  }

  if (m_options.showOutputs) {
    for (const auto& output : state.outputs) {
      const float outputZoom = output.zoom > 0.0F ? output.zoom : 1.0F;
      if (output.width > 0.0F && output.height > 0.0F) {
        content.include(output.cameraX, output.cameraY, output.width / outputZoom, output.height / outputZoom);
      } else {
        content.includePoint(output.cameraX, output.cameraY);
      }
    }
  }

  if (m_options.showWindows) {
    for (const auto& window : state.windows) {
      content.include(window.centerX, window.centerY, window.width, window.height);
    }
  }

  if (m_options.showLayers) {
    for (const auto& layer : state.canvasLayers) {
      content.include(layer.centerX, layer.centerY, layer.width, layer.height);
    }
  }

  if (m_options.showPinned) {
    for (const auto& window : state.pinned) {
      if (const auto placement = placeScreenWindow(state, window); placement.valid) {
        content.include(placement.centerX, placement.centerY, placement.width, placement.height);
      }
    }
  }

  if (m_options.showBookmarks) {
    for (const auto& [name, point] : m_source.bookmarks()) {
      (void)name;
      content.includePoint(static_cast<float>(point.first), static_cast<float>(point.second));
    }
  }

  return content;
}

void DesktopDriftwmMinimapWidget::syncMapContent(const DriftwmState& state, Renderer* renderer) {
  const float stroke = borderWidth();
  const float cardRadius = cornerRadius();
  const float markRadius = Style::scaledRadiusSm(m_contentScale);

  const auto outputCount = m_options.showOutputs ? state.outputs.size() : 0U;
  reserveBoxes(m_outputNodes, outputCount, kZOutputs);
  for (std::size_t i = 0; i < outputCount; ++i) {
    const auto& output = state.outputs[i];
    const float outputZoom = output.zoom > 0.0F ? output.zoom : 1.0F;
    const auto rect = driftwm_minimap::projectRect(
      m_projection, output.cameraX, output.cameraY, output.width / outputZoom, output.height / outputZoom, stroke
    );
    const Color border = output.active
                           ? resolve(m_options.outputActiveColor, ColorRole::Primary)
                           : resolve(m_options.outputColor, ColorRole::Outline, kOutputAlpha);
    applyBox(m_outputNodes[i], rect, clearColor(), border, stroke, cardRadius);
  }
  hideBoxes(m_outputNodes, outputCount);

  const auto layerCount = m_options.showLayers ? state.canvasLayers.size() : 0U;
  reserveBoxes(m_layerNodes, layerCount, kZLayers);
  for (std::size_t i = 0; i < layerCount; ++i) {
    const auto& layer = state.canvasLayers[i];
    const auto rect = driftwm_minimap::projectRect(
      m_projection, layer.centerX, layer.centerY, layer.width, layer.height, stroke
    );
    const Color layerColor = resolve(m_options.layerColor, ColorRole::OnSurfaceVariant, kLayerAlpha);
    applyBox(m_layerNodes[i], rect, clearColor(), layerColor, stroke, markRadius);
  }
  hideBoxes(m_layerNodes, layerCount);

  const auto windowCount = m_options.showWindows ? state.windows.size() : 0U;
  reserveBoxes(m_windowNodes, windowCount, kZWindows);
  // Icon edge colors are resolved here (cheap cache hits on the steady state)
  // so box tints never trigger icon decoding as a side effect.
  std::vector<PixelRect> windowRects(windowCount);
  std::vector<Color> windowFills(windowCount);
  std::vector<Color> windowTints(windowCount);
  const Color genericWindow = resolve(m_options.windowColor, ColorRole::OnSurfaceVariant, kWindowAlpha);
  const Color suspendedFill = resolve(m_options.windowSuspendedColor, ColorRole::Outline, kSuspendedAlpha);
  for (std::size_t i = 0; i < windowCount; ++i) {
    const auto& window = state.windows[i];
    const auto rect =
        driftwm_minimap::projectRect(m_projection, window.centerX, window.centerY, window.width, window.height);
    windowRects[i] = rect;
    // Focused and unfocused windows share the same fill; focus is a glow
    // outline drawn separately below.
    Color fill = window.suspended ? suspendedFill : genericWindow;
    Color tint = fill;
    if (!window.suspended) {
      const int probeSize = std::max(16, static_cast<int>(std::round(std::min(rect.width, rect.height) * 2.0F)));
      const std::string iconPath = resolveIconPath(window.appId, probeSize);
      if (!iconPath.empty()) {
        if (const auto edge = edgeColorForIcon(iconPath); edge.has_value()) {
          tint = *edge;
          fill = withAlpha(*edge, 0.35F);
        }
      }
    }
    windowFills[i] = fill;
    windowTints[i] = tint;
    applyBox(m_windowNodes[i], rect, fill, tint, stroke, markRadius);
  }
  hideBoxes(m_windowNodes, windowCount);

  // Focus glow: same fill as every other window, plus a soft bloated outline
  // in the focused theme color drawn behind the box.
  std::size_t focusedCount = 0;
  for (const auto& window : state.windows) {
    if (window.focused) {
      ++focusedCount;
    }
  }
  const std::size_t glowCount = m_options.showWindows ? focusedCount : 0U;
  reserveBoxes(m_focusGlowNodes, glowCount, kZFocusGlow);
  {
    std::size_t glowUsed = 0;
    if (glowCount > 0) {
      const Color glow = resolve(m_options.windowFocusedColor, ColorRole::Secondary);
      for (std::size_t i = 0; i < windowCount; ++i) {
        if (!state.windows[i].focused) {
          continue;
        }
        applyGlow(m_focusGlowNodes[glowUsed], windowRects[i], glow, markRadius);
        ++glowUsed;
      }
    }
    hideBoxes(m_focusGlowNodes, glowUsed);
  }

  const auto pinnedCount = m_options.showPinned ? state.pinned.size() : 0U;
  reserveBoxes(m_pinnedNodes, pinnedCount, kZPinned);
  for (std::size_t i = 0; i < pinnedCount; ++i) {
    const auto placement = placeScreenWindow(state, state.pinned[i]);
    if (!placement.valid) {
      m_pinnedNodes[i]->setVisible(false);
      continue;
    }
    const auto rect = driftwm_minimap::projectRect(
      m_projection, placement.centerX, placement.centerY, placement.width, placement.height, stroke
    );
    const Color pinned = resolve(m_options.pinnedColor, ColorRole::Error, kPinnedAlpha);
    applyBox(m_pinnedNodes[i], rect, withAlpha(pinned, 0.25F), pinned, stroke, markRadius);
  }
  hideBoxes(m_pinnedNodes, pinnedCount);

  std::vector<std::pair<std::string, std::pair<double, double>>> bookmarkPoints;
  if (m_options.showBookmarks) {
    bookmarkPoints = m_source.bookmarks();
  }
  reserveBoxes(m_bookmarkNodes, bookmarkPoints.size(), kZBookmarks);
  for (std::size_t i = 0; i < bookmarkPoints.size(); ++i) {
    const bool active = bookmarkPoints[i].first == state.activeBookmark;
    const float size = (active ? kActiveBookmarkMarkerSize : kBookmarkMarkerSize) * m_contentScale;
    const auto rect = driftwm_minimap::projectPoint(
      m_projection, static_cast<float>(bookmarkPoints[i].second.first),
      static_cast<float>(bookmarkPoints[i].second.second), size
    );
    const Color mark = resolve(m_options.bookmarkColor, ColorRole::Tertiary, kBookmarkAlpha);
    applyBox(
      m_bookmarkNodes[i], rect, mark, active ? mark : clearColor(), active ? stroke : 0.0F, markRadius
    );
  }
  hideBoxes(m_bookmarkNodes, bookmarkPoints.size());

  const float poseZoom = m_pose.zoom > 0.0F ? m_pose.zoom : 1.0F;
  const auto [frameWidth, frameHeight] = viewportSize(state);
  if (frameWidth > 0.0F && frameHeight > 0.0F) {
    const auto rect = driftwm_minimap::projectRect(
      m_projection, m_pose.cameraX, m_pose.cameraY, frameWidth / poseZoom, frameHeight / poseZoom, stroke
    );
    const Color viewport = resolve(m_options.viewportColor, ColorRole::Primary);
    applyBox(m_viewport, rect, clearColor(), viewport, stroke * 1.5F, markRadius);
  } else {
    const Color viewport = resolve(m_options.viewportColor, ColorRole::Primary);
    const auto dot = driftwm_minimap::projectPoint(m_projection, m_pose.cameraX, m_pose.cameraY, stroke * 8.0F);
    applyBox(m_viewport, dot, clearColor(), viewport, stroke, markRadius);
  }

  if (m_options.showFullscreenIndicator && !state.fullscreen.empty()) {
    const float dot = kFullscreenDotSize * m_contentScale;
    applyBox(
      m_fullscreen,
      PixelRect{
        .x = m_mapSizeX - dot - kFullscreenDotInset * m_contentScale,
        .y = kFullscreenDotInset * m_contentScale,
        .width = dot,
        .height = dot,
      },
      resolve(m_options.fullscreenColor, ColorRole::Error), clearColor(), 0.0F, markRadius
    );
  } else {
    m_fullscreen->setVisible(false);
  }

  syncWindowDecorations(state, renderer);
}

void DesktopDriftwmMinimapWidget::syncMap() {
  if (m_renderer != nullptr) {
    syncMap(*m_renderer);
    return;
  }
  if (root() == nullptr || m_backdrop == nullptr) {
    return;
  }
  if (m_paletteDirty) {
    syncColors();
  }
  if (m_mapSizeX <= 0.0F || m_mapSizeY <= 0.0F) {
    return;
  }
  if (!m_source.available()) {
    hideBoxes(m_outputNodes, 0);
    hideBoxes(m_layerNodes, 0);
    hideBoxes(m_windowNodes, 0);
    hideBoxes(m_focusGlowNodes, 0);
    hideBoxes(m_pinnedNodes, 0);
    hideBoxes(m_bookmarkNodes, 0);
    hideIcons(0, nullptr);
    m_viewport->setVisible(false);
    m_fullscreen->setVisible(false);
    return;
  }
  const auto& state = m_source.state();
  m_projection = driftwm_minimap::fit(collectContent(state), m_mapSizeX, m_mapSizeY, kMapPadding * m_contentScale);
  syncMapContent(state, nullptr);
}

void DesktopDriftwmMinimapWidget::syncMap(Renderer& renderer) {
  m_renderer = &renderer;
  if (root() == nullptr || m_backdrop == nullptr) {
    return;
  }
  if (m_paletteDirty) {
    syncColors();
  }
  if (m_mapSizeX <= 0.0F || m_mapSizeY <= 0.0F) {
    return;
  }

  if (!m_source.available()) {
    // Nothing has arrived yet: an empty framed box reads as "waiting", not broken.
    hideBoxes(m_outputNodes, 0);
    hideBoxes(m_layerNodes, 0);
    hideBoxes(m_windowNodes, 0);
    hideBoxes(m_focusGlowNodes, 0);
    hideBoxes(m_pinnedNodes, 0);
    hideBoxes(m_bookmarkNodes, 0);
    hideIcons(0, &renderer);
    m_viewport->setVisible(false);
    m_fullscreen->setVisible(false);
    return;
  }

  refreshIconsIfStale();
  const auto& state = m_source.state();
  m_projection = driftwm_minimap::fit(collectContent(state), m_mapSizeX, m_mapSizeY, kMapPadding * m_contentScale);
  syncMapContent(state, &renderer);
}

void DesktopDriftwmMinimapWidget::syncWindowDecorations(const DriftwmState& state, Renderer* renderer) {
  // Icon-only: centered app icons, no name labels. Without a GL view there is
  // nothing to decode icon files into; icons reappear on the next renderer pass.
  if (renderer == nullptr) {
    hideIcons(0, nullptr);
    return;
  }
  refreshIconsIfStale();
  if (!m_options.showWindows) {
    hideIcons(0, renderer);
    return;
  }

  // Largest rects first, focused window first, so the icon budget stays readable
  // instead of becoming a pile of overlapping slivers.
  struct Candidate {
    std::size_t index;
    PixelRect rect;
    std::size_t area;
    bool focused;
  };
  std::vector<Candidate> candidates;
  candidates.reserve(state.windows.size());
  for (std::size_t i = 0; i < state.windows.size(); ++i) {
    const auto& window = state.windows[i];
    if (window.appId.empty()) {
      continue;
    }
    const auto rect =
        driftwm_minimap::projectRect(m_projection, window.centerX, window.centerY, window.width, window.height);
    const float minSide = kIconMinSide * m_contentScale;
    if (rect.width < minSide || rect.height < minSide) {
      continue;
    }
    candidates.push_back({i, rect, static_cast<std::size_t>(rectArea(rect)), window.focused});
  }
  std::ranges::sort(candidates, [](const Candidate& left, const Candidate& right) {
    if (left.focused != right.focused) {
      return left.focused;
    }
    return left.area > right.area;
  });
  if (candidates.size() > kMaxIcons) {
    candidates.resize(kMaxIcons);
  }

  const float scale = m_contentScale > 0.0F ? m_contentScale : 1.0F;
  const float padding = kIconInnerPadding * scale;
  const float minIcon = kIconMinSide * scale;
  const float maxIcon = kIconMaxSide * scale;

  const std::size_t used = candidates.size();
  reserveIcons(used);

  for (std::size_t i = 0; i < used; ++i) {
    const auto& candidate = candidates[i];
    const auto& window = state.windows[candidate.index];
    // Icon side: capped square fitting the window rect with padding, centered.
    float iconSide = 0.0F;
    if (!window.suspended) {
      const float side = std::min(
          {candidate.rect.width - 2.0F * padding, candidate.rect.height - 2.0F * padding, maxIcon}
      );
      if (side >= minIcon && std::isfinite(side)) {
        iconSide = side;
      }
    }

    std::string iconPath;
    if (iconSide > 0.0F) {
      const int targetSize = std::max(1, static_cast<int>(std::round(iconSide * 2.0F)));
      iconPath = resolveIconPath(window.appId, targetSize);
      if (iconPath.empty()) {
        iconSide = 0.0F;
      }
    }

    Image* icon = m_iconNodes[i];
    if (iconSide > 0.0F) {
      const float iconX = candidate.rect.x + (candidate.rect.width - iconSide) * 0.5F;
      const float iconY = candidate.rect.y + (candidate.rect.height - iconSide) * 0.5F;
      icon->setVisible(true);
      icon->setPosition(iconX, iconY);
      icon->setSize(iconSide, iconSide);
      if (i >= m_iconPaths.size()) {
        m_iconPaths.resize(i + 1);
      }
      if (m_iconPaths[i] != iconPath) {
        if (iconPath.empty()) {
          icon->clear(*renderer);
        } else {
          const int targetSize = std::max(1, static_cast<int>(std::round(iconSide * 2.0F)));
          if (!icon->setSourceFile(*renderer, iconPath, targetSize, true)) {
            icon->setVisible(false);
          }
        }
        m_iconPaths[i] = iconPath;
      }
    } else {
      icon->setVisible(false);
      icon->clear(*renderer);
      if (i < m_iconPaths.size()) {
        m_iconPaths[i].clear();
      }
    }
  }
  hideIcons(used, renderer);
}
