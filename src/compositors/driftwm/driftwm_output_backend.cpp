#include "compositors/driftwm/driftwm_output_backend.h"

#include "compositors/driftwm/driftwm_runtime.h"
#include "util/string_utils.h"

#include <nlohmann/json.hpp>
#include <optional>
#include <string>

DriftwmOutputBackend::DriftwmOutputBackend(compositors::driftwm::DriftwmRuntime& runtime)
    : compositors::driftwm::DriftwmEventHandler(runtime) {}

std::optional<std::string> DriftwmOutputBackend::focusedOutputName() const { return m_focusedOutput; }

void DriftwmOutputBackend::handleState(const nlohmann::json& state) {
  const auto outputsIt = state.find("outputs");
  if (outputsIt == state.end() || !outputsIt->is_array()) {
    return;
  }

  // Every snapshot is a complete picture, so an output that is no longer the
  // active one must not stay focused.
  m_focusedOutput.reset();
  for (const auto& output : *outputsIt) {
    if (!output.is_object()) {
      continue;
    }
    const auto activeIt = output.find("active");
    if (activeIt == output.end() || !activeIt->is_boolean() || !activeIt->get<bool>()) {
      continue;
    }
    const auto nameIt = output.find("name");
    if (nameIt == output.end() || !nameIt->is_string()) {
      continue;
    }
    const auto name = StringUtils::trim(nameIt->get<std::string>());
    if (!name.empty()) {
      m_focusedOutput = name;
    }
    return;
  }
}

void DriftwmOutputBackend::handleStreamReset() { m_focusedOutput.reset(); }
