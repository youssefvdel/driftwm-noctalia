#pragma once

#include "compositors/driftwm/driftwm_event_handler.h"
#include "compositors/driftwm/driftwm_state.h"
#include "ui/signal.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace compositors::driftwm {

  class DriftwmRuntime;

  // Decodes the shared Subscribe stream into a DriftwmState every consumer can
  // read without touching JSON. One instance per process serves every minimap
  // instance, so a session with several bars still opens a single IPC stream.
  //
  // `revision()` counts accepted snapshots; `changed()` fires on each one that
  // carries something observably new. A payload that fails to decode is dropped
  // and leaves the previous state in place.
  class DriftwmStateSource final : public DriftwmEventHandler {
  public:
    // A bookmark list round-trip, throttled so a busy stream cannot turn into a
    // request storm. Bookmarks only change through user action.
    static constexpr auto kBookmarkRefreshInterval = std::chrono::seconds{2};

    explicit DriftwmStateSource(DriftwmRuntime& runtime);
    ~DriftwmStateSource() override;

    DriftwmStateSource(const DriftwmStateSource&) = delete;
    DriftwmStateSource& operator=(const DriftwmStateSource&) = delete;

    void handleState(const nlohmann::json& state) override;
    void handleStreamReset() override;

    // False until the first snapshot has been decoded: before that there is no
    // compositor state to draw and no viewport to move.
    [[nodiscard]] bool available() const noexcept { return m_hasState; }
    [[nodiscard]] const DriftwmState& state() const noexcept { return m_state; }
    [[nodiscard]] std::uint64_t revision() const noexcept { return m_revision; }
    // Non-const on purpose: subscribers connect to it.
    [[nodiscard]] Signal<>& changed() noexcept { return m_changed; }
    [[nodiscard]] const Signal<>& changed() const noexcept { return m_changed; }

    // Bookmark name -> canvas point (Y-up), sorted by name. The stream carries
    // only the active bookmark's *name*, so the positions come from the one-shot
    // list request, fetched on first use and throttled afterwards. An empty
    // result means "not fetched yet or none exist", never a failure.
    [[nodiscard]] const std::vector<std::pair<std::string, std::pair<double, double>>>& bookmarks();

    // Pans the compositor viewport to a canvas point (Y-up). False when no state
    // has arrived, so a click on an empty minimap cannot move the camera.
    [[nodiscard]] bool moveCamera(double x, double y) const;
    // Sets the compositor zoom level (animated and clamped by the compositor:
    // out to fit-all, in to native 1.0 with no magnification). False when no
    // state has arrived or the level is not a positive finite number.
    [[nodiscard]] bool setZoom(double zoom) const;

  private:
    void refreshBookmarks();

    DriftwmState m_state;
    std::vector<std::pair<std::string, std::pair<double, double>>> m_bookmarks;
    std::chrono::steady_clock::time_point m_lastBookmarkRefresh;
    bool m_bookmarksFetched = false;
    bool m_hasState = false;
    std::uint64_t m_revision = 0;
    Signal<> m_changed;
  };

} // namespace compositors::driftwm
