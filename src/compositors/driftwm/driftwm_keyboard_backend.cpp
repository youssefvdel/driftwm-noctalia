#include "compositors/driftwm/driftwm_keyboard_backend.h"

#include "compositors/driftwm/driftwm_runtime.h"
#include "util/string_utils.h"

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <utility>

namespace {

  [[nodiscard]] std::optional<std::string> parseLayoutName(const nlohmann::json& response) {
    if (!response.is_object()) {
      return std::nullopt;
    }
    const auto okIt = response.find("Ok");
    if (okIt == response.end() || !okIt->is_object()) {
      return std::nullopt;
    }
    const auto layoutIt = okIt->find("Layout");
    if (layoutIt == okIt->end() || !layoutIt->is_string()) {
      return std::nullopt;
    }
    const auto layout = StringUtils::trim(layoutIt->get<std::string>());
    return layout.empty() ? std::nullopt : std::optional<std::string>{layout};
  }

} // namespace

DriftwmKeyboardBackend::DriftwmKeyboardBackend(compositors::driftwm::DriftwmRuntime& runtime)
    : compositors::driftwm::DriftwmEventHandler(runtime) {}

bool DriftwmKeyboardBackend::isAvailable() const noexcept { return m_runtime.available(); }

bool DriftwmKeyboardBackend::cycleLayout() const {
  if (!isAvailable()) {
    return false;
  }
  return m_runtime.requestAction("switch-layout next");
}

std::optional<KeyboardLayoutState> DriftwmKeyboardBackend::layoutState() const {
  // DriftWM's IPC exposes the active layout only, so the configured layout list
  // stays a wayland-level concern and this reports the single current name.
  const auto current = currentLayoutName();
  if (!current.has_value()) {
    return std::nullopt;
  }
  return KeyboardLayoutState{{*current}, 0};
}

std::optional<std::string> DriftwmKeyboardBackend::currentLayoutName() const {
  if (!isAvailable()) {
    return std::nullopt;
  }

  const auto response = m_runtime.requestJson("{\"Layout\":{\"short\":false}}\n");
  if (!response.has_value()) {
    return std::nullopt;
  }
  return parseLayoutName(*response);
}

void DriftwmKeyboardBackend::setChangeCallback(ChangeCallback callback) { m_changeCallback = std::move(callback); }

void DriftwmKeyboardBackend::handleState(const nlohmann::json& state) {
  const auto layoutIt = state.find("layout");
  if (layoutIt == state.end() || !layoutIt->is_string()) {
    return;
  }

  const auto layout = StringUtils::trim(layoutIt->get<std::string>());
  if (layout == m_streamLayout) {
    return;
  }
  m_streamLayout = layout;
  if (m_changeCallback) {
    m_changeCallback();
  }
}
