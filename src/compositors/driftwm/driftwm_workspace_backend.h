#pragma once

#include "compositors/driftwm/driftwm_event_handler.h"
#include "compositors/workspace_backend.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace compositors::driftwm {
  class DriftwmRuntime;
} // namespace compositors::driftwm

// Window and workspace metadata from the DriftWM state stream. DriftWM has no
// workspaces of its own: it exports its bookmarks (named canvas points) as
// ext-workspace-v1 workspaces, and this backend tells the taskbar, dock and
// window switcher which windows exist and from which bookmarks each is seen.
//
// The stream's window inventory is the source of truth for the windows it
// lists. Every entry carries a session-stable id that the Focus and Close
// requests accept, so a window is addressed by that id alone: the backend
// joins inventory entries to no wlr handle.
//
// The stream is pumped by the platform's own DriftWM poll hook, so this
// backend advertises no poll fd of its own.
class DriftwmWorkspaceBackend final : public compositors::WorkspaceMetadataBackend,
                                      public compositors::driftwm::DriftwmEventHandler {
public:
  // One window of the inventory, in id (creation) order.
  struct Window {
    std::uint64_t id = 0;
    std::string appId;
    std::string title;
    // A screen-space window (pinned or fullscreen) names its output here. A
    // canvas window leaves it empty: DriftWM shows it on every output.
    std::string outputName;
    // Bookmarks whose viewport shows the window, nearest first.
    std::vector<std::string> seenFrom;
    // The bookmark the window is listed under when it is seen from none, so
    // a window between bookmarks still has a group: the nearest one for a
    // canvas window, the output's active one for a screen-space window.
    std::string fallbackBookmark;
    bool focused = false;
    // Canvas center for spatial ordering, Y flipped down like every other
    // backend's screen coordinates. Zero for a screen-space window.
    std::int32_t x = 0;
    std::int32_t y = 0;

    // The workspace the taskbar groups this window under.
    [[nodiscard]] const std::string& workspaceKey() const noexcept {
      return seenFrom.empty() ? fallbackBookmark : seenFrom.front();
    }

    friend bool operator==(const Window&, const Window&) = default;
  };

  // A named canvas point (Y-up), as the Bookmark list request reports it.
  struct Bookmark {
    std::string name;
    double x = 0.0;
    double y = 0.0;

    friend bool operator==(const Bookmark&, const Bookmark&) = default;
  };

  using WindowChangeCallback = std::function<void()>;

  explicit DriftwmWorkspaceBackend(compositors::driftwm::DriftwmRuntime& runtime);
  ~DriftwmWorkspaceBackend() override;

  DriftwmWorkspaceBackend(const DriftwmWorkspaceBackend&) = delete;
  DriftwmWorkspaceBackend& operator=(const DriftwmWorkspaceBackend&) = delete;

  void setChangeCallback(ChangeCallback callback) override;
  void handleState(const nlohmann::json& state) override;
  void handleStreamReset() override;
  void apply(std::vector<Workspace>& workspaces, const std::string& outputName = {}) const override;
  [[nodiscard]] std::unordered_map<std::string, std::vector<std::string>>
  appIdsByWorkspace(const std::string& outputName = {}) const override;
  [[nodiscard]] std::vector<WorkspaceWindow> workspaceWindows(const std::string& outputName = {}) const override;
  [[nodiscard]] std::optional<std::string> focusedWindowId() const override;
  bool focusWindowById(const std::string& windowId, bool warpPointer = false) override;
  bool closeWindowById(const std::string& windowId) override;
  void cleanup() override;

  // Fires whenever the window list changes: a window opens, closes, is
  // refocused, retitled, moves to other bookmarks, or the stream resets. The
  // platform forwards it as a toplevel change so taskbars and docks resync.
  void setWindowChangeCallback(WindowChangeCallback callback);

  // False until the first snapshot has been decoded. Every query below then
  // answers from the last snapshot.
  [[nodiscard]] bool available() const noexcept { return m_hasState; }
  [[nodiscard]] const std::vector<Window>& windows() const noexcept { return m_windows; }

  // True when `window` belongs to `outputName`: a canvas window belongs to
  // every output, and an empty name selects every output.
  [[nodiscard]] static bool onOutput(const Window& window, std::string_view outputName) noexcept;

  // The focused window of the last snapshot, or null when none is.
  [[nodiscard]] const Window* focusedWindow() const noexcept;

  // Re-reads the bookmark list when the throttle allows and re-derives which
  // bookmarks each window is seen from. Returns true when that moved a
  // window between bookmarks. Nothing is notified: the caller reports the
  // change through whichever channel it is already in. A new bookmark changes
  // no state snapshot, so this is how a bookmark created at runtime reaches
  // the window model without waiting for unrelated compositor activity.
  bool refreshBookmarks();

  // Bookmarks whose nominal viewport (the output size at zoom 1, centered on
  // the bookmark) overlaps the window rect, nearest first. Ties go to the
  // name that sorts first, so the result never depends on list order.
  [[nodiscard]] static std::vector<std::string> seenFromBookmarks(
      const std::vector<Bookmark>& bookmarks, double centerX, double centerY, double width, double height,
      double viewportWidth, double viewportHeight
  );

  // The bookmark nearest to a canvas point, or empty without bookmarks.
  [[nodiscard]] static std::string nearestBookmark(const std::vector<Bookmark>& bookmarks, double x, double y);

private:
  [[nodiscard]] std::optional<std::vector<Window>> buildWindows(const nlohmann::json& state) const;
  void fetchBookmarks();
  void notifyChanged() const;
  void notifyWindowsChanged() const;

  // The runtime reference is the event handler base's m_runtime.
  std::vector<Window> m_windows;
  std::vector<Bookmark> m_bookmarks;
  // The snapshot the window list was derived from, kept so a bookmark list
  // refresh can re-derive it.
  nlohmann::json m_lastState;
  std::chrono::steady_clock::time_point m_lastBookmarkRefresh;
  bool m_bookmarksFetched = false;
  bool m_hasState = false;
  ChangeCallback m_changeCallback;
  WindowChangeCallback m_windowChangeCallback;
};
