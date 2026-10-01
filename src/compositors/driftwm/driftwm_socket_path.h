#pragma once

#include <cstdlib>
#include <string>
#include <string_view>
#include <sys/stat.h>

namespace compositors::driftwm {

  // The session socket DriftWM's own CLI derives: it exports no IPC path, and
  // $DRIFTWM_SOCKET is only a client-side override callers check for first.
  [[nodiscard]] inline std::string defaultIpcSocketPath() {
    const char* runtimeDir = std::getenv("XDG_RUNTIME_DIR");
    const char* waylandDisplay = std::getenv("WAYLAND_DISPLAY");
    if (runtimeDir == nullptr || runtimeDir[0] == '\0' || waylandDisplay == nullptr || waylandDisplay[0] == '\0') {
      return {};
    }
    return std::string(runtimeDir) + "/driftwm/ipc-" + waylandDisplay + ".sock";
  }

  [[nodiscard]] inline bool ipcSocketExists(std::string_view path) {
    const std::string socketPath(path);
    struct stat info{};
    return !socketPath.empty() && ::stat(socketPath.c_str(), &info) == 0 && S_ISSOCK(info.st_mode);
  }

} // namespace compositors::driftwm
