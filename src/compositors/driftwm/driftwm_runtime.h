#pragma once

#include <chrono>
#include <nlohmann/json.hpp>
#include <optional>
#include <poll.h>
#include <string>
#include <string_view>
#include <vector>

namespace compositors::driftwm {

  class DriftwmEventHandler;

  // Owns the DriftWM IPC: stateless one-shot requests (fresh socket per call) plus a
  // single persistent Subscribe stream. The stream carries one complete state
  // snapshot per rendered frame, so the runtime coalesces a burst of frames and
  // hands only the newest snapshot to every registered DriftwmEventHandler.
  // Backends are handlers and forward their poll hooks here.
  class DriftwmRuntime {
  public:
    DriftwmRuntime() = default;
    ~DriftwmRuntime();

    DriftwmRuntime(const DriftwmRuntime&) = delete;
    DriftwmRuntime& operator=(const DriftwmRuntime&) = delete;

    [[nodiscard]] bool available() const;
    [[nodiscard]] const std::string& socketPath() const;
    [[nodiscard]] std::optional<nlohmann::json> requestJson(std::string_view request) const;
    [[nodiscard]] bool requestOk(std::string_view request, bool acceptNoResponse = false) const;
    [[nodiscard]] bool requestAction(std::string_view action, bool acceptNoResponse = false) const;
    void refresh();
    void cleanup();

    // State-stream dispatch. Handlers register on construction; the runtime owns
    // the socket and delivers the newest snapshot to every registered handler.
    void registerEventHandler(DriftwmEventHandler* handler);
    void unregisterEventHandler(DriftwmEventHandler* handler);
    [[nodiscard]] int pollFd() const noexcept { return m_eventSocketFd; }
    [[nodiscard]] short pollEvents() const noexcept { return POLLIN | POLLHUP | POLLERR; }
    [[nodiscard]] int pollTimeoutMs() const noexcept;
    void dispatchPoll(short revents);

  private:
    struct IpcReply;

    [[nodiscard]] IpcReply request(std::string_view request) const;
    void ensureResolved() const;
    void resolveSocketPath() const;

    void connectIfNeeded();
    void closeSocket(bool scheduleReconnect);
    void scheduleReconnect();
    void readSocket();
    void parseMessages();
    [[nodiscard]] bool handleMessage(std::string_view line);
    void dispatchPendingState();
    void notifyStreamReset() const;

    mutable bool m_resolved = false;
    mutable std::string m_socketPath;
    std::vector<DriftwmEventHandler*> m_eventHandlers;
    int m_eventSocketFd = -1;
    std::vector<char> m_readBuffer;
    std::optional<nlohmann::json> m_pendingState;
    std::chrono::steady_clock::time_point m_nextReconnectAt;
    std::chrono::seconds m_reconnectBackoff{2};
  };

} // namespace compositors::driftwm
