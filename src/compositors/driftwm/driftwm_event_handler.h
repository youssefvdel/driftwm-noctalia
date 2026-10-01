#pragma once

#include <nlohmann/json.hpp>

namespace compositors::driftwm {

  class DriftwmRuntime;

  // Base for objects that consume the DriftWM state stream. Handlers register
  // themselves with the runtime on construction and unregister on destruction.
  // The runtime owns the socket and delivers each pushed snapshot to every
  // registered handler.
  class DriftwmEventHandler {
  public:
    explicit DriftwmEventHandler(DriftwmRuntime& runtime);
    virtual ~DriftwmEventHandler();

    DriftwmEventHandler(const DriftwmEventHandler&) = delete;
    DriftwmEventHandler& operator=(const DriftwmEventHandler&) = delete;

    // DriftWM answers a Subscribe request with a complete state snapshot and then
    // pushes another one for every rendered frame, so the runtime coalesces a burst
    // of frames and delivers only the newest snapshot of the poll wakeup.
    virtual void handleState(const nlohmann::json& state) = 0;
    // Called when the runtime tears the stream down (e.g. cleanup): the handler's
    // cached state should be considered stale.
    virtual void handleStreamReset() {}

  protected:
    DriftwmRuntime& m_runtime;
  };

} // namespace compositors::driftwm
