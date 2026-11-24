#include "compositors/driftwm/driftwm_workspace_backend.h"

#include "compositors/driftwm/driftwm_runtime.h"
#include "compositors/driftwm/driftwm_state.h"
#include "core/log.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <format>
#include <limits>
#include <string>
#include <system_error>
#include <unordered_set>
#include <utility>

namespace {

  constexpr Logger kLog("driftwm_workspace");

  // A bookmark list round-trip is throttled so a busy stream cannot turn into
  // a request storm. Bookmarks only change through user action.
  constexpr auto kBookmarkRefreshInterval = std::chrono::seconds{2};

  // The viewport a bookmark is judged against when no output reports a size.
  constexpr double kFallbackViewportWidth = 1920.0;
  constexpr double kFallbackViewportHeight = 1080.0;

  [[nodiscard]] std::string readString(const nlohmann::json& node, std::string_view key) {
    const auto it = node.find(key);
    return it != node.end() && it->is_string() ? it->get<std::string>() : std::string{};
  }

  [[nodiscard]] bool readBool(const nlohmann::json& node, std::string_view key) noexcept {
    const auto it = node.find(key);
    return it != node.end() && it->is_boolean() && it->get<bool>();
  }

  // Zero is a real DriftWM id: the first element mapped in a session gets it.
  // Only a missing or malformed field counts as absent.
  [[nodiscard]] std::optional<std::uint64_t> readId(const nlohmann::json& node) {
    const auto it = node.find("id");
    if (it == node.end()) {
      return std::nullopt;
    }
    if (it->is_number_unsigned()) {
      return it->get<std::uint64_t>();
    }
    if (it->is_number_integer()) {
      const auto signedId = it->get<std::int64_t>();
      if (signedId >= 0) {
        return static_cast<std::uint64_t>(signedId);
      }
    }
    return std::nullopt;
  }

  // A point or size on the wire: a fixed two-element numeric array.
  [[nodiscard]] std::optional<std::array<double, 2>> readPair(const nlohmann::json& node, std::string_view key) {
    const auto it = node.find(key);
    if (it == node.end() || !it->is_array() || it->size() != 2 || !(*it)[0].is_number() || !(*it)[1].is_number()) {
      return std::nullopt;
    }
    const auto x = (*it)[0].get<double>();
    const auto y = (*it)[1].get<double>();
    if (!std::isfinite(x) || !std::isfinite(y)) {
      return std::nullopt;
    }
    return std::array<double, 2>{x, y};
  }

  // A canvas coordinate narrowed to the integer the sorting fields carry.
  [[nodiscard]] std::int32_t toAxis(double value) noexcept {
    if (!std::isfinite(value)) {
      return 0;
    }
    constexpr auto kLimit = static_cast<double>(std::numeric_limits<std::int32_t>::max());
    return static_cast<std::int32_t>(std::clamp(std::round(value), -kLimit, kLimit));
  }

  [[nodiscard]] double squaredDistance(double ax, double ay, double bx, double by) noexcept {
    const double dx = ax - bx;
    const double dy = ay - by;
    return dx * dx + dy * dy;
  }

  // Only a decimal id can reach the wire: the request is built by splicing the
  // number into JSON, so anything else is refused rather than sent.
  [[nodiscard]] std::optional<std::uint64_t> parseWindowId(const std::string& windowId) noexcept {
    if (windowId.empty()) {
      return std::nullopt;
    }
    std::uint64_t value = 0;
    const auto [end, ec] = std::from_chars(windowId.data(), windowId.data() + windowId.size(), value);
    if (ec != std::errc{} || end != windowId.data() + windowId.size()) {
      return std::nullopt;
    }
    return value;
  }

} // namespace

DriftwmWorkspaceBackend::DriftwmWorkspaceBackend(compositors::driftwm::DriftwmRuntime& runtime)
    : compositors::driftwm::DriftwmEventHandler(runtime) {}

DriftwmWorkspaceBackend::~DriftwmWorkspaceBackend() { cleanup(); }

void DriftwmWorkspaceBackend::setChangeCallback(ChangeCallback callback) { m_changeCallback = std::move(callback); }

void DriftwmWorkspaceBackend::setWindowChangeCallback(WindowChangeCallback callback) {
  m_windowChangeCallback = std::move(callback);
}

bool DriftwmWorkspaceBackend::onOutput(const Window& window, std::string_view outputName) noexcept {
  return outputName.empty() || window.outputName.empty() || window.outputName == outputName;
}

const DriftwmWorkspaceBackend::Window* DriftwmWorkspaceBackend::focusedWindow() const noexcept {
  const auto it = std::ranges::find_if(m_windows, [](const Window& window) { return window.focused; });
  return it == m_windows.end() ? nullptr : &*it;
}

std::vector<std::string> DriftwmWorkspaceBackend::seenFromBookmarks(
    const std::vector<Bookmark>& bookmarks, double centerX, double centerY, double width, double height,
    double viewportWidth, double viewportHeight
) {
  // The window rect and the bookmark's viewport rect overlap exactly when the
  // centers are closer than half the sum of their extents on both axes.
  const double reachX = (std::max(width, 0.0) + viewportWidth) / 2.0;
  const double reachY = (std::max(height, 0.0) + viewportHeight) / 2.0;
  std::vector<std::pair<double, std::string>> seen;
  seen.reserve(bookmarks.size());
  for (const auto& bookmark : bookmarks) {
    if (bookmark.name.empty() || !std::isfinite(bookmark.x) || !std::isfinite(bookmark.y)) {
      continue;
    }
    if (std::abs(centerX - bookmark.x) >= reachX || std::abs(centerY - bookmark.y) >= reachY) {
      continue;
    }
    seen.emplace_back(squaredDistance(centerX, centerY, bookmark.x, bookmark.y), bookmark.name);
  }
  // Pairs sort by distance, then by name, so equal distances resolve the same
  // way whatever order the list arrived in.
  std::ranges::sort(seen);

  std::vector<std::string> result;
  result.reserve(seen.size());
  for (auto& [distance, name] : seen) {
    (void)distance;
    result.push_back(std::move(name));
  }
  return result;
}

std::string DriftwmWorkspaceBackend::nearestBookmark(const std::vector<Bookmark>& bookmarks, double x, double y) {
  const Bookmark* best = nullptr;
  double bestDistance = 0.0;
  for (const auto& bookmark : bookmarks) {
    if (bookmark.name.empty() || !std::isfinite(bookmark.x) || !std::isfinite(bookmark.y)) {
      continue;
    }
    const double distance = squaredDistance(x, y, bookmark.x, bookmark.y);
    if (best == nullptr || distance < bestDistance || (distance == bestDistance && bookmark.name < best->name)) {
      best = &bookmark;
      bestDistance = distance;
    }
  }
  return best != nullptr ? best->name : std::string{};
}

void DriftwmWorkspaceBackend::handleState(const nlohmann::json& state) {
  // Anything but a snapshot object leaves the last one in place: it is what a
  // bookmark refresh re-derives the window list from.
  if (!state.is_object()) {
    return;
  }
  m_lastState = state;
  // A bookmark change only reaches this handler through the re-derivation
  // below, so the outcome of the refresh itself is not reported here.
  (void)refreshBookmarks();
  auto next = buildWindows(state);
  if (!next.has_value()) {
    return;
  }
  const bool first = !m_hasState;
  if (!first && *next == m_windows) {
    return;
  }
  m_windows = std::move(*next);
  m_hasState = true;
  notifyWindowsChanged();
  notifyChanged();
}

bool DriftwmWorkspaceBackend::refreshBookmarks() {
  const auto now = std::chrono::steady_clock::now();
  if (m_bookmarksFetched && now - m_lastBookmarkRefresh < kBookmarkRefreshInterval) {
    return false;
  }
  fetchBookmarks();
  if (!m_hasState) {
    return false;
  }
  auto next = buildWindows(m_lastState);
  if (!next.has_value() || *next == m_windows) {
    return false;
  }
  m_windows = std::move(*next);
  return true;
}

void DriftwmWorkspaceBackend::fetchBookmarks() {
  // Stamped before the request: a failed one must not retry in a tight loop.
  m_lastBookmarkRefresh = std::chrono::steady_clock::now();
  m_bookmarksFetched = true;
  const auto response = m_runtime.requestJson("{\"Bookmark\":{}}\n");
  if (!response.has_value()) {
    return;
  }
  const auto decoded = compositors::driftwm::decodeDriftwmBookmarks(*response);
  if (!decoded.has_value()) {
    kLog.warn("unexpected DriftWM bookmark list reply");
    return;
  }
  m_bookmarks.clear();
  m_bookmarks.reserve(decoded->size());
  for (const auto& [name, point] : *decoded) {
    m_bookmarks.push_back(Bookmark{.name = name, .x = point.first, .y = point.second});
  }
}

std::optional<std::vector<DriftwmWorkspaceBackend::Window>>
DriftwmWorkspaceBackend::buildWindows(const nlohmann::json& state) const {
  if (!state.is_object()) {
    return std::nullopt;
  }

  struct OutputFacts {
    std::string name;
    std::array<double, 2> camera{0.0, 0.0};
    std::string activeBookmark;
  };
  std::vector<OutputFacts> outputs;
  std::optional<std::array<double, 2>> firstSized;
  std::optional<std::array<double, 2>> focusedSized;
  if (const auto outputsIt = state.find("outputs"); outputsIt != state.end() && outputsIt->is_array()) {
    for (const auto& entry : *outputsIt) {
      if (!entry.is_object()) {
        continue;
      }
      OutputFacts facts{.name = readString(entry, "name"), .activeBookmark = readString(entry, "active_bookmark")};
      if (const auto camera = readPair(entry, "camera"); camera.has_value()) {
        facts.camera = *camera;
      }
      if (const auto size = readPair(entry, "size"); size.has_value() && (*size)[0] > 0.0 && (*size)[1] > 0.0) {
        if (!firstSized.has_value()) {
          firstSized = *size;
        }
        if (readBool(entry, "active")) {
          focusedSized = *size;
        }
      }
      outputs.push_back(std::move(facts));
    }
  }
  // A bookmark is visited on the focused output, so its size is the viewport
  // a bookmark is judged against; any sized output, then the fallback, stands
  // in without one.
  std::array<double, 2> viewport{kFallbackViewportWidth, kFallbackViewportHeight};
  if (firstSized.has_value()) {
    viewport = *firstSized;
  }
  if (focusedSized.has_value()) {
    viewport = *focusedSized;
  }

  // The fields every inventory array shares. A widget is compositor chrome
  // and is skipped: the wlr manager does not announce widgets either.
  const auto decodeWindow = [](const nlohmann::json& node) -> std::optional<Window> {
    if (!node.is_object()) {
      return std::nullopt;
    }
    const auto id = readId(node);
    if (!id.has_value() || readBool(node, "is_widget")) {
      return std::nullopt;
    }
    Window window;
    window.id = *id;
    window.appId = readString(node, "app_id");
    window.title = readString(node, "title");
    window.focused = readBool(node, "is_focused");
    return window;
  };

  std::vector<Window> windows;
  if (const auto canvasIt = state.find("windows"); canvasIt != state.end() && canvasIt->is_array()) {
    for (const auto& entry : *canvasIt) {
      auto window = decodeWindow(entry);
      if (!window.has_value()) {
        continue;
      }
      // A missing geometry reads as a point at the origin, so the window is
      // still listed and only its spatial ordering is arbitrary.
      const auto position = readPair(entry, "position");
      const auto size = readPair(entry, "size");
      const double centerX = position.has_value() ? (*position)[0] : 0.0;
      const double centerY = position.has_value() ? (*position)[1] : 0.0;
      const double width = size.has_value() ? (*size)[0] : 0.0;
      const double height = size.has_value() ? (*size)[1] : 0.0;
      window->seenFrom = seenFromBookmarks(m_bookmarks, centerX, centerY, width, height, viewport[0], viewport[1]);
      if (window->seenFrom.empty()) {
        window->fallbackBookmark = nearestBookmark(m_bookmarks, centerX, centerY);
      }
      // DriftWM reports Y up; every other backend's sorting fields are screen
      // coordinates, Y down.
      window->x = toAxis(centerX);
      window->y = toAxis(-centerY);
      windows.push_back(std::move(*window));
    }
  }

  // Pinned and fullscreen windows live in screen space, so no bookmark's
  // viewport describes them: they are listed under the bookmark their output
  // is on, which is where the user sees them.
  for (const std::string_view key : {std::string_view("fullscreen"), std::string_view("pinned")}) {
    const auto screenIt = state.find(key);
    if (screenIt == state.end() || !screenIt->is_array()) {
      continue;
    }
    for (const auto& entry : *screenIt) {
      auto window = decodeWindow(entry);
      if (!window.has_value()) {
        continue;
      }
      window->outputName = readString(entry, "output");
      const auto output = std::ranges::find(outputs, window->outputName, &OutputFacts::name);
      // A window whose output the snapshot no longer lists has nowhere to be
      // seen from; like the minimap, drop it rather than guess.
      if (output == outputs.end()) {
        continue;
      }
      if (!output->activeBookmark.empty()) {
        window->fallbackBookmark = output->activeBookmark;
      } else {
        window->fallbackBookmark = nearestBookmark(m_bookmarks, output->camera[0], output->camera[1]);
      }
      windows.push_back(std::move(*window));
    }
  }

  // DriftWM reports in stacking order, which every raise rewrites; the id is
  // the creation order, which is what a taskbar can stay put in.
  std::ranges::sort(windows, {}, &Window::id);
  return windows;
}

void DriftwmWorkspaceBackend::handleStreamReset() {
  const bool hadState = m_hasState;
  cleanup();
  if (hadState) {
    notifyWindowsChanged();
    notifyChanged();
  }
}

void DriftwmWorkspaceBackend::cleanup() {
  m_windows.clear();
  m_bookmarks.clear();
  m_lastState = nullptr;
  m_lastBookmarkRefresh = {};
  m_bookmarksFetched = false;
  m_hasState = false;
}

void DriftwmWorkspaceBackend::apply(std::vector<Workspace>& workspaces, const std::string& outputName) const {
  for (auto& workspace : workspaces) {
    if (workspace.occupied) {
      continue;
    }
    workspace.occupied = std::ranges::any_of(m_windows, [&](const Window& window) {
      if (!window.outputName.empty()) {
        // A screen-space window is on screen whatever bookmark its output
        // is viewing.
        return outputName.empty() || window.outputName == outputName;
      }
      // Only the bookmarks the window is really seen from count: the
      // fallback keeps it listed under a group, not on that group's screen.
      return (!workspace.id.empty() && std::ranges::contains(window.seenFrom, workspace.id))
          || (!workspace.name.empty() && std::ranges::contains(window.seenFrom, workspace.name));
    });
  }
}

std::vector<WorkspaceWindow> DriftwmWorkspaceBackend::workspaceWindows(const std::string& outputName) const {
  std::vector<WorkspaceWindow> result;
  for (const auto& window : m_windows) {
    if (!onOutput(window, outputName)) {
      continue;
    }
    // One row per bookmark the window is seen from, so a taskbar group shows
    // it where it is visible; a window seen from none keeps one row under its
    // fallback so it is still listed.
    const auto append = [&result, &window](const std::string& bookmark) {
      result.push_back(
          WorkspaceWindow{
              .windowId = std::to_string(window.id),
              .workspaceKey = bookmark,
              .appId = window.appId,
              .title = window.title,
              .x = window.x,
              .y = window.y,
              .outputName = window.outputName,
          }
      );
    };
    if (window.seenFrom.empty()) {
      append(window.fallbackBookmark);
    } else {
      for (const auto& bookmark : window.seenFrom) {
        append(bookmark);
      }
    }
  }
  return result;
}

std::unordered_map<std::string, std::vector<std::string>>
DriftwmWorkspaceBackend::appIdsByWorkspace(const std::string& outputName) const {
  std::unordered_map<std::string, std::vector<std::string>> result;
  std::unordered_map<std::string, std::unordered_set<std::string>> seen;
  for (const auto& window : workspaceWindows(outputName)) {
    if (window.appId.empty() || window.workspaceKey.empty()) {
      continue;
    }
    if (seen[window.workspaceKey].insert(window.appId).second) {
      result[window.workspaceKey].push_back(window.appId);
    }
  }
  return result;
}

std::optional<std::string> DriftwmWorkspaceBackend::focusedWindowId() const {
  const auto* window = focusedWindow();
  return window != nullptr ? std::optional<std::string>{std::to_string(window->id)} : std::nullopt;
}

bool DriftwmWorkspaceBackend::focusWindowById(const std::string& windowId, bool warpPointer) {
  // DriftWM's focus request reveals the window itself: raise, focus, and pan
  // the camera to it, so there is no pointer to warp.
  (void)warpPointer;
  const auto id = parseWindowId(windowId);
  return id.has_value() && m_runtime.requestOk(std::format("{{\"Focus\":{}}}\n", *id));
}

bool DriftwmWorkspaceBackend::closeWindowById(const std::string& windowId) {
  const auto id = parseWindowId(windowId);
  return id.has_value() && m_runtime.requestOk(std::format("{{\"Close\":{}}}\n", *id));
}

void DriftwmWorkspaceBackend::notifyChanged() const {
  if (m_changeCallback) {
    m_changeCallback();
  }
}

void DriftwmWorkspaceBackend::notifyWindowsChanged() const {
  if (m_windowChangeCallback) {
    m_windowChangeCallback();
  }
}
