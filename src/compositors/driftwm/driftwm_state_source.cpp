#include "compositors/driftwm/driftwm_state_source.h"

#include "compositors/driftwm/driftwm_runtime.h"
#include "core/log.h"

#include <chrono>
#include <cmath>
#include <format>
#include <string>
#include <utility>

namespace compositors::driftwm {

  namespace {
    constexpr Logger kLog("driftwm_state_source");
  } // namespace

  DriftwmStateSource::DriftwmStateSource(DriftwmRuntime& runtime) : DriftwmEventHandler(runtime) {}

  DriftwmStateSource::~DriftwmStateSource() = default;

  void DriftwmStateSource::handleState(const nlohmann::json& state) {
    const auto decoded = decodeDriftwmState(state);
    if (!decoded.has_value()) {
      // A malformed snapshot is not worth tearing the stream down for; the next
      // frame carries the same content and overwrites whatever is cached.
      kLog.warn("discarding an undecodable DriftWM state snapshot");
      return;
    }

    if (m_hasState && sameDriftwmState(m_state, *decoded)) {
      return;
    }
    m_state = std::move(*decoded);
    m_hasState = true;
    ++m_revision;
    m_changed.emit();
  }

  void DriftwmStateSource::handleStreamReset() {
    m_hasState = false;
    m_state = {};
    m_bookmarks.clear();
    m_bookmarksFetched = false;
    m_lastBookmarkRefresh = {};
    ++m_revision;
    m_changed.emit();
  }

  const std::vector<std::pair<std::string, std::pair<double, double>>>& DriftwmStateSource::bookmarks() {
    if (!m_hasState) {
      return m_bookmarks;
    }
    const auto now = std::chrono::steady_clock::now();
    const bool due = !m_bookmarksFetched
                     || (m_lastBookmarkRefresh.time_since_epoch().count() != 0
                         && now - m_lastBookmarkRefresh >= kBookmarkRefreshInterval);
    if (!due) {
      return m_bookmarks;
    }

    m_lastBookmarkRefresh = now;
    m_bookmarksFetched = true;
    refreshBookmarks();
    return m_bookmarks;
  }

  void DriftwmStateSource::refreshBookmarks() {
    const auto response = m_runtime.requestJson("{\"Bookmark\":{}}\n");
    if (!response.has_value()) {
      return;
    }
    const auto decoded = decodeDriftwmBookmarks(*response);
    if (!decoded.has_value()) {
      kLog.warn("unexpected DriftWM bookmark list reply");
      return;
    }
    m_bookmarks = std::move(*decoded);
  }

  bool DriftwmStateSource::moveCamera(double x, double y) const {
    if (!m_hasState || !std::isfinite(x) || !std::isfinite(y)) {
      return false;
    }
    // driftwm pans animatedly, so the reply reports the target, not the pose.
    return m_runtime.requestOk(std::format("{{\"Camera\":[{},{}]}}\n", x, y));
  }

  bool DriftwmStateSource::setZoom(double zoom) const {
    if (!m_hasState || !std::isfinite(zoom) || !(zoom > 0.0)) {
      return false;
    }
    // driftwm zooms animatedly and clamps out to fit-all, in to native 1.0.
    return m_runtime.requestOk(std::format("{{\"Zoom\":{}}}\n", zoom));
  }

} // namespace compositors::driftwm
