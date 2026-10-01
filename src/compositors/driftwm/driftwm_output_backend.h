#pragma once

#include "compositors/driftwm/driftwm_event_handler.h"

#include <nlohmann/json.hpp>
#include <optional>
#include <string>

namespace compositors::driftwm {
  class DriftwmRuntime;
} // namespace compositors::driftwm

// Caches the DriftWM active output from the state stream so a query is a cache
// read rather than a blocking IPC round-trip.
class DriftwmOutputBackend final : public compositors::driftwm::DriftwmEventHandler {
public:
  explicit DriftwmOutputBackend(compositors::driftwm::DriftwmRuntime& runtime);

  [[nodiscard]] std::optional<std::string> focusedOutputName() const;

  void handleState(const nlohmann::json& state) override;
  void handleStreamReset() override;

private:
  std::optional<std::string> m_focusedOutput;
};
