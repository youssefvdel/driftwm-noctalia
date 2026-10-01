#pragma once

#include "compositors/driftwm/driftwm_state.h"
#include "shell/desktop/desktop_widget.h"
#include "shell/driftwm_minimap_projection.h"
#include "system/icon_resolver.h"
#include "ui/palette.h"
#include "ui/signal.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

class Box;
class Image;
class InputArea;
class Node;
class Renderer;
namespace compositors::driftwm {
  class DriftwmStateSource;
}

// A DriftWM canvas minimap, drawn entirely from the compositor's own Subscribe
// stream. Every mark on the map is real DriftWM state — output viewports, window
// frames, focused/fullscreen/suspended/layer state, screen-pinned windows,
// bookmarks and the camera viewport. There is deliberately no geometry fallback
// for other compositors, so the factory never builds this widget off DriftWM.
//
// Window content is shown as per-window APP ICONS (resolved from window.appId
// through the shared desktop-entry + icon-theme machinery). Icon-only: no
// name labels are drawn; the icon alone identifies each app. There is no
// screencopy path: icons are cheap, always right-side-up, and need no capture
// helpers or GPU snapshot caches. Each window box is tinted with the average
// color of its icon's outer-edge pixels (cached per icon path), and the
// focused window keeps the same fill plus a soft glow outline.
//
// Click model: the widget defaults to see-through (clicks pass to apps). While
// the configured click modifier (default "super") is held the host flips the
// layer-shell input region to clickable so camera move / click-to-zoom can act.
// Wayland only delivers key events (and hence modifier state) to the focused
// client, and this surface uses LayerShellKeyboard::None and never takes focus,
// so the Super-held signal is best-effort: it is reliable when a Noctalia
// surface holds keyboard focus (settings, launcher, panels) or when
// click_modifier is "none". When focus sits on an app window the compositor
// keeps modifiers to itself and the map stays see-through; use "none" for
// focus-free clicking.
class DesktopDriftwmMinimapWidget : public DesktopWidget {
public:
  struct Options {
    // Logical map size; it is the natural size of a tile with no explicit box.
    int width = 220;
    int height = 120;
    bool showOutputs = true;
    bool showWindows = true;
    bool showTitles = true;
    bool showLayers = false;
    bool showPinned = true;
    bool showBookmarks = true;
    bool clickToMove = true;
    bool showFullscreenIndicator = true;
    // 0 snaps the viewport rect onto every snapshot instead of easing toward it.
    int smoothingMs = 120;
    // When true the host places this instance on the Top layer-shell layer so
    // the map floats above windows; false keeps the host's Bottom default.
    bool alwaysOnTop = false;
    // Which modifier must be held for a map click to act ("none" means always
    // clickable, "super"/"alt"/"ctrl"/"shift" require that key). Plain clicks
    // are ignored while a modifier is required.
    std::string clickModifier = "super";
    // When true the map is see-through by default (clicks pass to apps); holding
    // the click modifier temporarily makes it clickable. When false the surface
    // is always clickable but clicks still require the modifier (unless "none").
    bool clickThrough = true;

    // An unset color falls back to the theme role named in the comment.
    ColorSpec backgroundColor; // Surface
    ColorSpec outputColor; // Outline
    ColorSpec outputActiveColor; // Primary
    ColorSpec windowColor; // OnSurfaceVariant
    ColorSpec windowFocusedColor; // Secondary
    ColorSpec windowSuspendedColor; // Outline
    ColorSpec viewportColor; // Primary
    ColorSpec bookmarkColor; // Tertiary
    ColorSpec layerColor; // OnSurfaceVariant
    ColorSpec pinnedColor; // Error
    ColorSpec fullscreenColor; // Error
    ColorSpec textColor; // OnSurface
  };

  DesktopDriftwmMinimapWidget(compositors::driftwm::DriftwmStateSource& source, Options options);
  ~DesktopDriftwmMinimapWidget() override;

  void create() override;
  void setEditorPreview(bool enabled) noexcept override;
  [[nodiscard]] bool needsFrameTick() const override;
  [[nodiscard]] bool wantsTopLayer() const noexcept override { return m_options.alwaysOnTop; }
  [[nodiscard]] bool wantsClickThrough() const noexcept override { return effectiveClickThrough(); }
  void onFrameTick(float deltaMs, Renderer& renderer) override;

  // Live KeyMod bitmask sampled at click time (WaylandConnection::keyboardModifiers).
  // Pointer events carry no modifier state, so the host provides this. The seat's
  // xkb state only tracks modifiers while a Noctalia surface holds keyboard focus
  // (this widget uses LayerShellKeyboard::None and never does), so a 0 return with
  // a non-"none" modifier is expected when focus sits on an app window. The host
  // also pushes global key-event state via setModifierClickActive(); either source
  // satisfying the requirement lets the click act (dual-source for reliability).
  using ModifierProvider = std::function<std::uint32_t()>;
  void setModifierProvider(ModifierProvider provider) { m_modifierProvider = std::move(provider); }
  // Lets the host toggle the layer-shell input region without rebuilding the
  // widget when the modifier state changes.
  using ClickThroughCallback = std::function<void(bool)>;
  void setClickThroughCallback(ClickThroughCallback callback) { m_clickThroughCallback = std::move(callback); }
  [[nodiscard]] bool effectiveClickThrough() const noexcept;
  [[nodiscard]] bool modifierClickActive() const noexcept { return m_modifierClickActive; }
  void setModifierClickActive(bool active);
  // Host-driven modifier feed: maps a live KeyMod mask to the configured click
  // modifier and updates the click-through state (same path as key events).
  void onModifierState(std::uint32_t mods);
  [[nodiscard]] bool isModifierHeld(std::uint32_t mods) const noexcept;
  [[nodiscard]] bool modifierHeldNow() const noexcept;
  [[nodiscard]] const std::string& clickModifierName() const noexcept { return m_options.clickModifier; }
  [[nodiscard]] bool clickThroughConfigured() const noexcept { return m_options.clickThrough; }

private:
  void doLayout(Renderer& renderer) override;
  void doUpdate(Renderer& renderer) override;
  void onFontFamilyChanged(const std::string& family, Renderer& renderer) override;

  // The rendered viewport pose, eased toward the snapshot's camera.
  struct ViewPose {
    float cameraX = 0.0F;
    float cameraY = 0.0F;
    float zoom = 1.0F;
  };

  void syncColors();
  void syncMap(Renderer& renderer);
  void syncMap();
  void syncMapContent(const compositors::driftwm::DriftwmState& state, Renderer* renderer);
  void syncWindowDecorations(const compositors::driftwm::DriftwmState& state, Renderer* renderer);
  void syncPose();
  void stepPose(float deltaMs);
  [[nodiscard]] bool poseConverged() const noexcept;
  void moveCameraTo(float pixelX, float pixelY);
  void handleMapClick(float pixelX, float pixelY);
  [[nodiscard]] bool clickModifierSatisfied() const noexcept;
  [[nodiscard]] bool clicksActive() const noexcept { return m_options.clickToMove && !effectiveClickThrough(); }
  // Whether the InputArea should accept buttons at all. True whenever a click
  // could act now or after a future modifier flip (i.e. clickToMove and not
  // permanently display-only). The surface input region still gates delivery.
  [[nodiscard]] bool acceptsClicks() const noexcept { return m_options.clickToMove; }

  [[nodiscard]] float borderWidth() const noexcept;
  [[nodiscard]] float cornerRadius() const noexcept;
  [[nodiscard]] driftwm_minimap::CanvasRect
  collectContent(const compositors::driftwm::DriftwmState& state) const;
  [[nodiscard]] Color resolve(const ColorSpec& spec, ColorRole role, float alpha = 1.0F) const;
  void applyBox(Box* box, const driftwm_minimap::PixelRect& rect, const Color& fill, const Color& border, float stroke,
                float radius);
  void applyGlow(
      Box* box, const driftwm_minimap::PixelRect& rect, const Color& color, float radius
  );
  // The backdrop owns every mark; the pools are non-owning indices into it.
  [[nodiscard]] Box* addSingletonBox(Node& parent, std::int32_t zIndex);
  void reserveBoxes(std::vector<Box*>& pool, std::size_t count, std::int32_t zIndex);
  void reserveIcons(std::size_t count);
  static void hideBoxes(std::vector<Box*>& pool, std::size_t used);
  void hideIcons(std::size_t used, Renderer* renderer);
  void doRebindRenderer(Renderer& renderer) override;

  // Per-window app icons: pooled Image nodes centered in their window rects,
  // resolved through desktop entries + the shared icon theme (no new libraries).
  void buildDesktopIconIndex();
  [[nodiscard]] std::string resolveIconPath(const std::string& appId, int targetSize);
  void refreshIconsIfStale();
  // Average color of the outer-edge pixels of a decoded icon file, cached per
  // path so each file is sampled once. Used to tint the window box.
  [[nodiscard]] std::optional<Color> edgeColorForIcon(const std::string& iconPath);

  compositors::driftwm::DriftwmStateSource& m_source;
  Options m_options;
  ModifierProvider m_modifierProvider;
  ClickThroughCallback m_clickThroughCallback;
  bool m_modifierClickActive = false;
  Signal<>::ScopedConnection m_stateConn;
  Signal<>::ScopedConnection m_paletteConn;

  InputArea* m_area = nullptr;
  Box* m_backdrop = nullptr;
  Box* m_viewport = nullptr;
  Box* m_fullscreen = nullptr;

  std::vector<Box*> m_outputNodes;
  std::vector<Box*> m_layerNodes;
  std::vector<Box*> m_windowNodes;
  std::vector<Box*> m_pinnedNodes;
  std::vector<Box*> m_bookmarkNodes;
  std::vector<Box*> m_focusGlowNodes;
  std::vector<Image*> m_iconNodes;
  // Last icon path bound per pooled slot, so unchanged windows skip re-decode.
  std::vector<std::string> m_iconPaths;

  IconResolver m_iconResolver;
  std::unordered_map<std::string, std::string> m_appIcons;
  std::uint64_t m_desktopEntriesVersion = 0;
  std::uint64_t m_iconThemeGeneration = 0;

  // Outgoing click-to-fit target. While armed, incoming Subscribe snapshots are
  // not trusted for the pose: the compositor echoes the pre-zoom clamped camera
  // first (zoom arrives a frame later), so the pose would snap back to stale
  // values and freeze a few pixels out. The target disarms on convergence or a
  // short timeout.
  struct ClickTarget {
    float cameraX = 0.0F;
    float cameraY = 0.0F;
    float zoom = 1.0F;
    std::chrono::steady_clock::time_point armedAt{};
    bool armed = false;
  };

  ViewPose m_pose;
  ClickTarget m_clickTarget;
  std::unordered_map<std::string, Color> m_iconEdgeColors;
  driftwm_minimap::Projection m_projection;
  // The map's own pixel size, resolved from the tile box or the natural size.
  float m_mapSizeX = 0.0F;
  float m_mapSizeY = 0.0F;
  // Whether the pose has ever been seeded. The pose is snapped exactly once per
  // stream life, then eased from there.
  bool m_poseInitialized = false;
  bool m_editorPreview = false;
  bool m_paletteDirty = true;

  Renderer* m_renderer = nullptr;
};
