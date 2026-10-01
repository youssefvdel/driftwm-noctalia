#include "compositors/driftwm/driftwm_event_handler.h"

#include "compositors/driftwm/driftwm_runtime.h"

namespace compositors::driftwm {

  DriftwmEventHandler::DriftwmEventHandler(DriftwmRuntime& runtime) : m_runtime(runtime) {
    m_runtime.registerEventHandler(this);
  }

  DriftwmEventHandler::~DriftwmEventHandler() { m_runtime.unregisterEventHandler(this); }

} // namespace compositors::driftwm
