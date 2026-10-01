#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace compositors::driftwm {

  // DriftWM publishes every position as a CENTER point with Y pointing up — on
  // the canvas, in screen space, and for canvas layers alike — and every size as
  // the window's visual frame (content plus any compositor-drawn chrome). These
  // types keep that convention instead of pre-flipping it, so a consumer decides
  // where the axis flip belongs (the minimap projection does, once, in
  // DriftwmMinimapProjection).
  struct DriftwmWindow {
    std::uint64_t id = 0;
    std::string appId;
    std::string title;
    float centerX = 0.0F;
    float centerY = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
    bool focused = false;
    bool widget = false;
    bool suspended = false;
    // "Normal", "Fit" or "Fill". A compositor older than the field omits it.
    std::string mode = "Normal";

    friend bool operator==(const DriftwmWindow&, const DriftwmWindow&) = default;
  };

  // One output's viewport: `camera` is the viewport center, `zoom` its scale,
  // and `size` the output's logical size.
  struct DriftwmOutput {
    std::string name;
    float cameraX = 0.0F;
    float cameraY = 0.0F;
    float zoom = 1.0F;
    float width = 0.0F;
    float height = 0.0F;
    bool active = false;
    std::string activeBookmark;

    friend bool operator==(const DriftwmOutput&, const DriftwmOutput&) = default;
  };

  // A fullscreen or screen-pinned window. Both live in screen space, not on the
  // canvas: a `fullscreen` entry carries no geometry at all, and a `pinned`
  // entry's position is in rule coordinates (output-center origin, Y-up).
  struct DriftwmScreenWindow {
    std::uint64_t id = 0;
    std::string output;
    std::string appId;
    std::string title;
    float centerX = 0.0F;
    float centerY = 0.0F;
    float width = 0.0F;
    float height = 0.0F;

    friend bool operator==(const DriftwmScreenWindow&, const DriftwmScreenWindow&) = default;
  };

  struct DriftwmCanvasLayer {
    std::string appId;
    float centerX = 0.0F;
    float centerY = 0.0F;
    float width = 0.0F;
    float height = 0.0F;

    friend bool operator==(const DriftwmCanvasLayer&, const DriftwmCanvasLayer&) = default;
  };

  // A complete `State` payload: identical for the `"State"` reply and for each
  // pushed subscription event, so one decode serves both.
  struct DriftwmState {
    // The focused output's viewport, mirrored from the top-level `camera`/`zoom`.
    float cameraX = 0.0F;
    float cameraY = 0.0F;
    float zoom = 1.0F;
    std::string layout;
    std::string layoutShort;
    std::vector<DriftwmOutput> outputs;
    std::vector<DriftwmWindow> windows;
    std::vector<DriftwmScreenWindow> fullscreen;
    std::vector<DriftwmScreenWindow> pinned;
    // Namespaces of screen-space layer-shell surfaces.
    std::vector<std::string> layers;
    std::vector<DriftwmCanvasLayer> canvasLayers;
    std::string activeBookmark;

    friend bool operator==(const DriftwmState&, const DriftwmState&) = default;
  };

  // Decodes one `State` payload. Returns nullopt for a payload that is not an
  // object or whose viewport (`camera` pair, positive finite `zoom`) is
  // unusable, so a caller keeps its previous state instead of drawing a
  // half-decoded frame. Every other field is optional in the wire format and
  // falls back to its documented default.
  [[nodiscard]] std::optional<DriftwmState> decodeDriftwmState(const nlohmann::json& state);

  // True when two consecutive snapshots carry nothing a consumer can observe, so
  // a change signal can skip a redundant notification.
  [[nodiscard]] bool sameDriftwmState(const DriftwmState& left, const DriftwmState& right) noexcept;

  // Decodes the `{"Bookmark":{}}` list reply into name -> canvas point (Y-up).
  // Returns nullopt when the reply is not the expected shape.
  [[nodiscard]] std::optional<std::vector<std::pair<std::string, std::pair<double, double>>>>
  decodeDriftwmBookmarks(const nlohmann::json& response);

} // namespace compositors::driftwm
