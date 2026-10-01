#pragma once

#include "compositors/driftwm/driftwm_event_handler.h"
#include "compositors/keyboard_backend.h"

#include <functional>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

namespace compositors::driftwm {
  class DriftwmRuntime;
} // namespace compositors::driftwm

class DriftwmKeyboardBackend final : public compositors::driftwm::DriftwmEventHandler {
public:
  using ChangeCallback = std::function<void()>;

  explicit DriftwmKeyboardBackend(compositors::driftwm::DriftwmRuntime& runtime);

  [[nodiscard]] bool isAvailable() const noexcept;
  [[nodiscard]] bool cycleLayout() const;
  [[nodiscard]] std::optional<KeyboardLayoutState> layoutState() const;
  [[nodiscard]] std::optional<std::string> currentLayoutName() const;

  void setChangeCallback(ChangeCallback callback);
  void handleState(const nlohmann::json& state) override;

private:
  ChangeCallback m_changeCallback;
  // Last layout the state stream reported, so only a real change notifies.
  std::optional<std::string> m_streamLayout;
};
