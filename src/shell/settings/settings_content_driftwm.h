#pragma once

#include "compositors/driftwm/driftwm_config_document.h"
#include "compositors/driftwm/driftwm_config_service.h"

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

class Flex;
class Node;

namespace settings {

  // Data + actions for the DriftWM settings section. Fully custom content: the
  // values live in driftwm's own config.toml, so there is no registry entry and
  // nothing is mirrored into noctalia's config.
  struct SettingsDriftwmContext {
    float scale = 1.0F;
    std::string_view selectedSection;
    compositors::driftwm::DriftwmConfigService* config = nullptr;
    // Parsed config.toml. Null when it could not be read or is not valid TOML, in
    // which case the section shows why and offers no rows.
    const compositors::driftwm::DriftwmConfigDocument* document = nullptr;
    // Reason the document is unavailable, or the outcome of the last rejected
    // write. Empty when there is nothing to report.
    std::string statusMessage;
    bool statusIsError = false;

    Flex* pageTitleRow = nullptr;
    Flex* groupJumpRow = nullptr;
    std::function<void(const Node&)> scrollContentToTop;
    std::unordered_map<std::string, std::unordered_set<std::string>>& expandedGroupsByPage;
    // Re-reads the file and rebuilds, so a committed or externally changed value
    // is what the rows show.
    std::function<void()> requestContentRebuild;
    // Clears `statusMessage` and rebuilds, for the status banner's dismiss.
    std::function<void()> clearStatus;
    // Called with driftwm's own rejection text when a write is refused, so the
    // next render can show it.
    std::function<void(std::string)> onApplyFailed;
  };

  // Renders the DriftWM section. Returns the number of rows shown.
  std::size_t addSettingsDriftwm(Flex& content, SettingsDriftwmContext ctx);

} // namespace settings
