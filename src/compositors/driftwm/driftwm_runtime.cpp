#include "compositors/driftwm/driftwm_runtime.h"

#include "compositors/driftwm/driftwm_event_handler.h"
#include "compositors/driftwm/driftwm_socket_path.h"
#include "core/log.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iterator>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace compositors::driftwm {

  namespace {

    constexpr Logger kLog("driftwm_runtime");
    constexpr auto kReconnectInitial = std::chrono::seconds(2);
    constexpr auto kReconnectMax = std::chrono::seconds(30);
    constexpr std::size_t kReadBufferMaxBytes = 1024U * 1024U;
    constexpr std::string_view kSubscribeRequest = "\"Subscribe\"\n";

    [[nodiscard]] bool writeAll(int fd, std::string_view data) {
      std::size_t offset = 0;
      while (offset < data.size()) {
        const ssize_t written = ::write(fd, data.data() + offset, data.size() - offset);
        if (written <= 0) {
          if (written < 0 && errno == EINTR) {
            continue;
          }
          return false;
        }
        offset += static_cast<std::size_t>(written);
      }
      return true;
    }

  } // namespace

  struct DriftwmRuntime::IpcReply {
    enum class Status {
      Unavailable,
      WriteFailed,
      ReadFailed,
      NoResponse,
      InvalidJson,
      Replied,
    };

    Status status = Status::Unavailable;
    std::optional<nlohmann::json> json;
  };

  bool DriftwmRuntime::available() const {
    ensureResolved();
    return !m_socketPath.empty();
  }

  const std::string& DriftwmRuntime::socketPath() const {
    ensureResolved();
    return m_socketPath;
  }

  std::optional<nlohmann::json> DriftwmRuntime::requestJson(std::string_view request) const {
    return this->request(request).json;
  }

  bool DriftwmRuntime::requestOk(std::string_view request, bool acceptNoResponse) const {
    const auto reply = this->request(request);
    if (reply.status == IpcReply::Status::NoResponse) {
      return acceptNoResponse;
    }
    if (!reply.json.has_value() || !reply.json->is_object()) {
      return false;
    }
    return reply.json->contains("Ok");
  }

  bool DriftwmRuntime::requestAction(std::string_view action, bool acceptNoResponse) const {
    nlohmann::json request = nlohmann::json::object();
    request["Action"] = action;
    auto payload = request.dump();
    payload.push_back('\n');
    return requestOk(payload, acceptNoResponse);
  }

  DriftwmRuntime::IpcReply DriftwmRuntime::request(std::string_view request) const {
    ensureResolved();
    if (m_socketPath.empty() || request.empty()) {
      return {IpcReply::Status::Unavailable, std::nullopt};
    }

    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
      return {IpcReply::Status::Unavailable, std::nullopt};
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (m_socketPath.size() >= sizeof(addr.sun_path)) {
      ::close(fd);
      return {IpcReply::Status::Unavailable, std::nullopt};
    }
    std::memcpy(addr.sun_path, m_socketPath.c_str(), m_socketPath.size() + 1);

    if (::connect(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) < 0) {
      ::close(fd);
      return {IpcReply::Status::Unavailable, std::nullopt};
    }

    if (!writeAll(fd, request)) {
      ::close(fd);
      return {IpcReply::Status::WriteFailed, std::nullopt};
    }

    std::string response;
    char buffer[4096];
    while (true) {
      const ssize_t count = ::read(fd, buffer, sizeof(buffer));
      if (count > 0) {
        response.append(buffer, static_cast<std::size_t>(count));
        if (response.contains('\n')) {
          break;
        }
        continue;
      }
      if (count == 0) {
        break;
      }
      if (errno == EINTR) {
        continue;
      }
      ::close(fd);
      return {IpcReply::Status::ReadFailed, std::nullopt};
    }

    ::close(fd);

    const std::size_t newline = response.find('\n');
    if (newline != std::string::npos) {
      response.resize(newline);
    }
    if (response.empty()) {
      return {IpcReply::Status::NoResponse, std::nullopt};
    }

    try {
      return {IpcReply::Status::Replied, nlohmann::json::parse(response)};
    } catch (const nlohmann::json::exception&) {
      return {IpcReply::Status::InvalidJson, std::nullopt};
    }
  }

  void DriftwmRuntime::refresh() {
    m_socketPath.clear();
    m_resolved = false;
    resolveSocketPath();
  }

  DriftwmRuntime::~DriftwmRuntime() { closeSocket(false); }

  void DriftwmRuntime::cleanup() {
    closeSocket(false);
    m_readBuffer.clear();
    m_pendingState.reset();
    m_reconnectBackoff = kReconnectInitial;
    notifyStreamReset();
  }

  void DriftwmRuntime::registerEventHandler(DriftwmEventHandler* handler) {
    if (handler == nullptr || std::ranges::contains(m_eventHandlers, handler)) {
      return;
    }
    m_eventHandlers.push_back(handler);
    if (available()) {
      connectIfNeeded();
    }
  }

  void DriftwmRuntime::unregisterEventHandler(DriftwmEventHandler* handler) { std::erase(m_eventHandlers, handler); }

  void DriftwmRuntime::notifyStreamReset() const {
    for (auto* handler : m_eventHandlers) {
      if (handler != nullptr) {
        handler->handleStreamReset();
      }
    }
  }

  int DriftwmRuntime::pollTimeoutMs() const noexcept {
    if (m_eventSocketFd >= 0) {
      return -1;
    }
    // Nothing to wake up for: no socket and no armed retry. Voting 0 here would
    // spin the main loop, so stay silent until a reconnect is actually pending.
    if (m_nextReconnectAt.time_since_epoch().count() == 0 || !available()) {
      return -1;
    }
    const auto remaining =
        std::chrono::ceil<std::chrono::milliseconds>(m_nextReconnectAt - std::chrono::steady_clock::now()).count();
    return static_cast<int>(std::max<std::int64_t>(0, remaining));
  }

  void DriftwmRuntime::dispatchPoll(short revents) {
    if (!available()) {
      return;
    }
    if (m_eventSocketFd < 0) {
      connectIfNeeded();
      return;
    }
    if ((revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
      closeSocket(true);
      return;
    }
    if ((revents & POLLIN) != 0) {
      readSocket();
    }
  }

  void DriftwmRuntime::connectIfNeeded() {
    ensureResolved();
    if (m_eventSocketFd >= 0 || m_socketPath.empty()) {
      return;
    }

    const auto now = std::chrono::steady_clock::now();
    if (m_nextReconnectAt.time_since_epoch().count() != 0 && now < m_nextReconnectAt) {
      return;
    }

    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
      scheduleReconnect();
      return;
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (m_socketPath.size() >= sizeof(addr.sun_path)) {
      kLog.warn("driftwm socket path too long");
      ::close(fd);
      scheduleReconnect();
      return;
    }
    std::memcpy(addr.sun_path, m_socketPath.c_str(), m_socketPath.size() + 1);

    if (::connect(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) < 0) {
      ::close(fd);
      scheduleReconnect();
      return;
    }

    if (!writeAll(fd, kSubscribeRequest)) {
      ::close(fd);
      scheduleReconnect();
      return;
    }

    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
      (void)::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }

    m_eventSocketFd = fd;
    m_nextReconnectAt = {};
    m_reconnectBackoff = kReconnectInitial;
    m_readBuffer.clear();
    m_pendingState.reset();
    kLog.debug("connected to driftwm state stream");
  }

  void DriftwmRuntime::closeSocket(bool scheduleReconnectFlag) {
    if (m_eventSocketFd >= 0) {
      ::close(m_eventSocketFd);
      m_eventSocketFd = -1;
    }
    m_readBuffer.clear();
    m_pendingState.reset();

    if (scheduleReconnectFlag) {
      scheduleReconnect();
    } else {
      m_nextReconnectAt = {};
    }
  }

  void DriftwmRuntime::scheduleReconnect() {
    const auto now = std::chrono::steady_clock::now();
    m_nextReconnectAt = now + m_reconnectBackoff;
    const auto doubled = m_reconnectBackoff * 2;
    m_reconnectBackoff = std::min(doubled, kReconnectMax);
  }

  void DriftwmRuntime::readSocket() {
    std::array<char, 4096> buffer{};
    while (true) {
      const ssize_t readBytes = ::read(m_eventSocketFd, buffer.data(), buffer.size());
      if (readBytes > 0) {
        m_readBuffer.insert(m_readBuffer.end(), buffer.begin(), buffer.begin() + readBytes);
        if (m_readBuffer.size() > kReadBufferMaxBytes) {
          kLog.warn("driftwm state stream read buffer exceeded {} bytes; reconnecting", kReadBufferMaxBytes);
          closeSocket(true);
          return;
        }
        continue;
      }

      if (readBytes == 0) {
        closeSocket(true);
        return;
      }

      if (errno == EINTR) {
        continue;
      }
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        break;
      }

      closeSocket(true);
      return;
    }

    parseMessages();
  }

  void DriftwmRuntime::parseMessages() {
    auto lineStart = m_readBuffer.begin();
    for (auto it = m_readBuffer.begin(); it != m_readBuffer.end(); ++it) {
      if (*it != '\n') {
        continue;
      }

      std::string line(lineStart, it);
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }

      if (!line.empty() && !handleMessage(line)) {
        m_readBuffer.clear();
        m_pendingState.reset();
        return;
      }

      lineStart = std::next(it);
    }

    if (lineStart != m_readBuffer.begin()) {
      m_readBuffer.erase(m_readBuffer.begin(), lineStart);
    }

    dispatchPendingState();
  }

  bool DriftwmRuntime::handleMessage(std::string_view line) {
    nlohmann::json json;
    try {
      json = nlohmann::json::parse(line);
    } catch (const nlohmann::json::exception& e) {
      kLog.warn("failed to parse driftwm state stream message: {}", e.what());
      return true;
    }

    if (!json.is_object()) {
      return true;
    }

    // Only this process's own request replies share the stream connection; they
    // are not pushed events.
    if (json.contains("Ok")) {
      return true;
    }
    if (json.contains("Err")) {
      kLog.warn("driftwm state stream returned an error, reconnecting");
      closeSocket(true);
      return false;
    }
    if (json.size() != 1) {
      return true;
    }

    const auto it = json.begin();
    if (it.key() == "State" && it.value().is_object()) {
      m_pendingState = it.value();
    }
    return true;
  }

  void DriftwmRuntime::dispatchPendingState() {
    if (!m_pendingState.has_value()) {
      return;
    }

    // Coalesced: an animating canvas pushes a snapshot per rendered frame, and
    // only the newest one of the burst reaches the handlers.
    const nlohmann::json state = std::move(*m_pendingState);
    m_pendingState.reset();
    for (auto* handler : m_eventHandlers) {
      if (handler != nullptr) {
        handler->handleState(state);
      }
    }
  }

  void DriftwmRuntime::ensureResolved() const {
    if (!m_resolved) {
      resolveSocketPath();
    }
  }

  void DriftwmRuntime::resolveSocketPath() const {
    m_resolved = true;

    if (const char* socketPath = std::getenv("DRIFTWM_SOCKET"); socketPath != nullptr && socketPath[0] != '\0') {
      m_socketPath = socketPath;
      return;
    }

    // Derived session socket: only usable once the compositor has created it.
    const std::string fallback = defaultIpcSocketPath();
    if (ipcSocketExists(fallback)) {
      m_socketPath = fallback;
    }
  }

} // namespace compositors::driftwm
