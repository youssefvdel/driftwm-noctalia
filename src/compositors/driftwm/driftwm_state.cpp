#include "compositors/driftwm/driftwm_state.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace compositors::driftwm {

  namespace {

    // A JSON number narrowed to a finite float. `get<float>()` on a non-numeric
    // node throws, and a non-finite value would poison every projection built
    // from it, so both are rejected at the boundary.
    [[nodiscard]] bool readFloat(const nlohmann::json& node, double& out) noexcept {
      if (!node.is_number()) {
        return false;
      }
      const double value = node.get<double>();
      if (!std::isfinite(value)) {
        return false;
      }
      out = value;
      return true;
    }

    [[nodiscard]] float readFloatOr(const nlohmann::json& node, std::string_view key, float fallback) noexcept {
      const auto it = node.find(key);
      double value = 0.0;
      return it != node.end() && readFloat(*it, value) ? static_cast<float>(value) : fallback;
    }

    [[nodiscard]] bool readBool(const nlohmann::json& node, std::string_view key) noexcept {
      const auto it = node.find(key);
      return it != node.end() && it->is_boolean() && it->get<bool>();
    }

    [[nodiscard]] std::string readString(const nlohmann::json& node, std::string_view key) {
      const auto it = node.find(key);
      return it != node.end() && it->is_string() ? it->get<std::string>() : std::string{};
    }

    [[nodiscard]] std::string readStringOr(const nlohmann::json& node, std::string_view key, std::string fallback) {
      const auto it = node.find(key);
      return it != node.end() && it->is_string() ? it->get<std::string>() : std::move(fallback);
    }

    // The wire format encodes every point and size as a fixed two-element array
    // (`(f64, f64)` tuples and `[i32; 2]` arrays both serialize that way).
    [[nodiscard]] std::optional<std::array<double, 2>> readPair(const nlohmann::json& node) {
      if (!node.is_array() || node.size() != 2) {
        return std::nullopt;
      }
      std::array<double, 2> out{};
      if (!readFloat(node[0], out[0]) || !readFloat(node[1], out[1])) {
        return std::nullopt;
      }
      return out;
    }

    [[nodiscard]] std::optional<std::array<double, 2>> readPairField(
      const nlohmann::json& node, std::string_view key
    ) {
      const auto it = node.find(key);
      return it != node.end() ? readPair(*it) : std::nullopt;
    }

    // Accepts either signed or unsigned numbers: the parser yields the unsigned
    // form for a non-negative literal, but a programmatically built payload
    // stores the same value as a signed one.
    [[nodiscard]] std::uint64_t readId(const nlohmann::json& node) noexcept {
      const auto it = node.find("id");
      if (it == node.end()) {
        return 0;
      }
      if (it->is_number_unsigned()) {
        return it->get<std::uint64_t>();
      }
      if (it->is_number_integer()) {
        const auto signedId = it->get<std::int64_t>();
        return signedId > 0 ? static_cast<std::uint64_t>(signedId) : 0;
      }
      return 0;
    }

    // A size may legitimately be missing (a fullscreen window reports no
    // geometry at all), in which case the caller keeps zero.
    void readSize(const nlohmann::json& node, float& width, float& height) noexcept {
      if (const auto size = readPairField(node, "size"); size.has_value()) {
        width = static_cast<float>((*size)[0]);
        height = static_cast<float>((*size)[1]);
      }
    }

    [[nodiscard]] std::optional<DriftwmOutput> decodeOutput(const nlohmann::json& node) {
      if (!node.is_object()) {
        return std::nullopt;
      }
      const auto camera = readPairField(node, "camera");
      const auto size = readPairField(node, "size");
      if (!camera.has_value() || !size.has_value()) {
        return std::nullopt;
      }
      const double zoom = readFloatOr(node, "zoom", 1.0F);
      if (!(zoom > 0.0)) {
        return std::nullopt;
      }

      DriftwmOutput output;
      output.name = readString(node, "name");
      output.cameraX = static_cast<float>((*camera)[0]);
      output.cameraY = static_cast<float>((*camera)[1]);
      output.zoom = static_cast<float>(zoom);
      output.width = static_cast<float>((*size)[0]);
      output.height = static_cast<float>((*size)[1]);
      output.active = readBool(node, "active");
      // An output with no bookmark in view reports null rather than omitting the key.
      output.activeBookmark = readString(node, "active_bookmark");
      return output;
    }

    [[nodiscard]] std::optional<DriftwmWindow> decodeWindow(const nlohmann::json& node) {
      if (!node.is_object()) {
        return std::nullopt;
      }
      const auto position = readPairField(node, "position");
      if (!position.has_value()) {
        return std::nullopt;
      }
      const auto size = readPairField(node, "size");

      DriftwmWindow window;
      window.id = readId(node);
      window.appId = readString(node, "app_id");
      window.title = readString(node, "title");
      window.centerX = static_cast<float>((*position)[0]);
      window.centerY = static_cast<float>((*position)[1]);
      if (size.has_value()) {
        window.width = static_cast<float>((*size)[0]);
        window.height = static_cast<float>((*size)[1]);
      }
      window.focused = readBool(node, "is_focused");
      window.widget = readBool(node, "is_widget");
      window.suspended = readBool(node, "suspended");
      // A compositor older than the `mode` field omits it; treat that as Normal.
      window.mode = readStringOr(node, "mode", "Normal");
      return window;
    }

    [[nodiscard]] std::optional<DriftwmScreenWindow> decodeScreenWindow(const nlohmann::json& node) {
      if (!node.is_object()) {
        return std::nullopt;
      }
      DriftwmScreenWindow window;
      window.id = readId(node);
      window.output = readString(node, "output");
      window.appId = readString(node, "app_id");
      window.title = readString(node, "title");
      // A fullscreen entry carries no geometry, so both stay zero and the
      // consumer falls back to the owning output's frame.
      if (const auto position = readPairField(node, "position"); position.has_value()) {
        window.centerX = static_cast<float>((*position)[0]);
        window.centerY = static_cast<float>((*position)[1]);
      }
      readSize(node, window.width, window.height);
      return window;
    }

    [[nodiscard]] std::optional<DriftwmCanvasLayer> decodeCanvasLayer(const nlohmann::json& node) {
      if (!node.is_object()) {
        return std::nullopt;
      }
      const auto position = readPairField(node, "position");
      const auto size = readPairField(node, "size");
      if (!position.has_value() || !size.has_value()) {
        return std::nullopt;
      }

      DriftwmCanvasLayer layer;
      layer.appId = readString(node, "app_id");
      layer.centerX = static_cast<float>((*position)[0]);
      layer.centerY = static_cast<float>((*position)[1]);
      layer.width = static_cast<float>((*size)[0]);
      layer.height = static_cast<float>((*size)[1]);
      return layer;
    }

    // One malformed array entry drops that entry, not the whole snapshot.
    template <typename T, typename Decoder>
    void decodeArray(const nlohmann::json& node, std::string_view key, std::vector<T>& out, Decoder decode) {
      const auto it = node.find(key);
      if (it == node.end() || !it->is_array()) {
        return;
      }
      for (const auto& entry : *it) {
        if (auto decoded = decode(entry)) {
          out.push_back(std::move(*decoded));
        }
      }
    }

  } // namespace

  std::optional<DriftwmState> decodeDriftwmState(const nlohmann::json& state) {
    if (!state.is_object()) {
      return std::nullopt;
    }

    const auto camera = readPairField(state, "camera");
    if (!camera.has_value()) {
      return std::nullopt;
    }
    double zoom = 0.0;
    const auto zoomIt = state.find("zoom");
    if (zoomIt == state.end() || !readFloat(*zoomIt, zoom) || !(zoom > 0.0)) {
      return std::nullopt;
    }

    DriftwmState decoded;
    decoded.cameraX = static_cast<float>((*camera)[0]);
    decoded.cameraY = static_cast<float>((*camera)[1]);
    decoded.zoom = static_cast<float>(zoom);
    decoded.layout = readString(state, "layout");
    decoded.layoutShort = readString(state, "layout_short");
    decoded.activeBookmark = readString(state, "active_bookmark");

    decodeArray(state, "outputs", decoded.outputs, decodeOutput);
    decodeArray(state, "windows", decoded.windows, decodeWindow);
    decodeArray(state, "fullscreen", decoded.fullscreen, decodeScreenWindow);
    decodeArray(state, "pinned", decoded.pinned, decodeScreenWindow);
    decodeArray(state, "canvas_layers", decoded.canvasLayers, decodeCanvasLayer);

    const auto layersIt = state.find("layers");
    if (layersIt != state.end() && layersIt->is_array()) {
      for (const auto& entry : *layersIt) {
        if (entry.is_string()) {
          decoded.layers.push_back(entry.get<std::string>());
        }
      }
    }

    return decoded;
  }

  bool sameDriftwmState(const DriftwmState& left, const DriftwmState& right) noexcept {
    return left == right;
  }

  std::optional<std::vector<std::pair<std::string, std::pair<double, double>>>>
  decodeDriftwmBookmarks(const nlohmann::json& response) {
    if (!response.is_object()) {
      return std::nullopt;
    }
    const auto okIt = response.find("Ok");
    if (okIt == response.end() || !okIt->is_object()) {
      return std::nullopt;
    }
    const auto bookmarksIt = okIt->find("Bookmarks");
    if (bookmarksIt == okIt->end() || !bookmarksIt->is_object()) {
      return std::nullopt;
    }

    std::vector<std::pair<std::string, std::pair<double, double>>> bookmarks;
    bookmarks.reserve(bookmarksIt->size());
    for (const auto& [name, point] : bookmarksIt->items()) {
      if (const auto pair = readPair(point)) {
        bookmarks.emplace_back(name, std::pair<double, double>{(*pair)[0], (*pair)[1]});
      }
    }
    // The reply arrives sorted by name, but the decode does not rely on that.
    std::ranges::sort(bookmarks, {}, [](const auto& entry) { return entry.first; });
    return bookmarks;
  }

} // namespace compositors::driftwm
