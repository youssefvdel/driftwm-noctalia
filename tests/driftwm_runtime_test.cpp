#include "compositors/driftwm/driftwm_keyboard_backend.h"
#include "compositors/driftwm/driftwm_output_backend.h"
#include "compositors/driftwm/driftwm_runtime.h"
#include "tests/test_check.h"

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

  // Minimal DriftWM IPC stand-in: a listening socket that serves the persistent
  // Subscribe stream and answers one-shot requests on separate connections.
  class FakeDriftwm {
  public:
    explicit FakeDriftwm(std::string path) : m_path(std::move(path)) {
      m_listenFd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
      if (m_listenFd < 0) {
        return;
      }

      sockaddr_un address{};
      address.sun_family = AF_UNIX;
      if (m_path.size() < sizeof(address.sun_path)) {
        std::memcpy(address.sun_path, m_path.c_str(), m_path.size() + 1);
        if (::bind(m_listenFd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0) {
          (void)::listen(m_listenFd, 4);
        }
      }
      m_acceptThread = std::thread([this]() { acceptLoop(); });
    }

    ~FakeDriftwm() {
      {
        const std::lock_guard<std::mutex> lock(m_mutex);
        m_stopped = true;
        m_streamClosed = true;
      }
      m_ready.notify_all();
      if (m_listenFd >= 0) {
        (void)::shutdown(m_listenFd, SHUT_RDWR);
        ::close(m_listenFd);
      }
      if (m_acceptThread.joinable()) {
        m_acceptThread.join();
      }
      for (auto& client : m_clients) {
        (void)::shutdown(client.first, SHUT_RDWR);
      }
      for (auto& client : m_clients) {
        if (client.second.joinable()) {
          client.second.join();
        }
        ::close(client.first);
      }
      ::unlink(m_path.c_str());
    }

    FakeDriftwm(const FakeDriftwm&) = delete;
    FakeDriftwm& operator=(const FakeDriftwm&) = delete;

    void setReply(std::string reply) {
      const std::lock_guard<std::mutex> lock(m_mutex);
      m_reply = std::move(reply);
    }

    [[nodiscard]] std::string lastRequest() const {
      const std::lock_guard<std::mutex> lock(m_mutex);
      return m_lastRequest;
    }

    [[nodiscard]] int subscribeRequests() const {
      const std::lock_guard<std::mutex> lock(m_mutex);
      return m_subscribeRequests;
    }

    // Queues raw payload bytes for the subscribed connection.
    void push(std::string payload) {
      const std::lock_guard<std::mutex> lock(m_mutex);
      m_streamQueue.push_back(std::move(payload));
      m_ready.notify_all();
    }

    // Drops the subscribed connection so the client observes a hangup.
    void closeStream() {
      int streamFd = -1;
      {
        const std::lock_guard<std::mutex> lock(m_mutex);
        m_streamClosed = true;
        streamFd = m_streamFd;
      }
      m_ready.notify_all();
      if (streamFd >= 0) {
        (void)::shutdown(streamFd, SHUT_RDWR);
      }
    }

  private:
    void acceptLoop() {
      while (true) {
        const int client = ::accept4(m_listenFd, nullptr, nullptr, SOCK_CLOEXEC);
        const std::lock_guard<std::mutex> lock(m_mutex);
        if (client < 0 || m_stopped) {
          if (client >= 0) {
            ::close(client);
          }
          return;
        }
        m_clients.emplace_back(client, std::thread([this, client]() { serve(client); }));
      }
    }

    void serve(int fd) {
      const std::string line = readLine(fd);
      if (line.empty()) {
        return;
      }

      if (line.find("Subscribe") == std::string::npos) {
        std::string reply;
        {
          const std::lock_guard<std::mutex> lock(m_mutex);
          m_lastRequest = line;
          reply = m_reply;
        }
        if (!reply.empty()) {
          (void)sendAll(fd, reply + "\n");
        }
        return;
      }

      {
        const std::lock_guard<std::mutex> lock(m_mutex);
        ++m_subscribeRequests;
        m_streamFd = fd;
      }
      while (true) {
        std::string payload;
        {
          std::unique_lock<std::mutex> lock(m_mutex);
          m_ready.wait(lock, [this]() { return m_streamClosed || !m_streamQueue.empty() || m_stopped; });
          if (m_streamClosed || m_stopped) {
            m_streamFd = -1;
            return;
          }
          payload = std::move(m_streamQueue.front());
          m_streamQueue.pop_front();
        }
        if (!sendAll(fd, payload)) {
          return;
        }
      }
    }

    [[nodiscard]] static std::string readLine(int fd) {
      std::string line;
      char buffer[512];
      while (!line.contains('\n')) {
        const ssize_t count = ::read(fd, buffer, sizeof(buffer));
        if (count <= 0) {
          return {};
        }
        line.append(buffer, static_cast<std::size_t>(count));
      }
      return line.substr(0, line.find('\n'));
    }

    [[nodiscard]] static bool sendAll(int fd, const std::string& payload) {
      std::size_t offset = 0;
      while (offset < payload.size()) {
        const ssize_t sent = ::send(fd, payload.data() + offset, payload.size() - offset, MSG_NOSIGNAL);
        if (sent <= 0) {
          return false;
        }
        offset += static_cast<std::size_t>(sent);
      }
      return true;
    }

    std::string m_path;
    int m_listenFd = -1;
    std::thread m_acceptThread;
    std::vector<std::pair<int, std::thread>> m_clients;
    mutable std::mutex m_mutex;
    std::condition_variable m_ready;
    std::string m_reply;
    std::string m_lastRequest;
    std::deque<std::string> m_streamQueue;
    int m_streamFd = -1;
    int m_subscribeRequests = 0;
    bool m_streamClosed = false;
    bool m_stopped = false;
  };

  // Counts state-stream deliveries so coalescing can be asserted exactly.
  class CountingHandler final : public compositors::driftwm::DriftwmEventHandler {
  public:
    using DriftwmEventHandler::DriftwmEventHandler;

    void handleState(const nlohmann::json& state) override {
      ++dispatches;
      lastState = state;
    }

    void handleStreamReset() override { ++resets; }

    int dispatches = 0;
    int resets = 0;
    nlohmann::json lastState;
  };

  [[nodiscard]] std::string stateLine(const std::string& activeOutput, const std::string& layout) {
    return nlohmann::json{
               {"State",
                {
                    {"layout", layout},
                    {"outputs", {{{"name", "DP-1"}, {"active", false}}, {{"name", activeOutput}, {"active", true}}}},
                }}}
        .dump() +
        "\n";
  }

  [[nodiscard]] bool waitFor(const std::function<bool()>& predicate, int timeoutMs) {
    for (int elapsed = 0; elapsed <= timeoutMs; elapsed += 5) {
      if (predicate()) {
        return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return predicate();
  }

  // Waits until the queued bytes are all readable, so one dispatch drains them.
  [[nodiscard]] bool waitForBytes(int fd, std::size_t minBytes, int timeoutMs) {
    return waitFor(
        [fd, minBytes]() {
          int available = 0;
          return ::ioctl(fd, FIONREAD, &available) == 0 && static_cast<std::size_t>(available) >= minBytes;
        },
        timeoutMs
    );
  }

  [[nodiscard]] bool waitForHangup(int fd, int timeoutMs) {
    pollfd entry{.fd = fd, .events = POLLIN, .revents = 0};
    return ::poll(&entry, 1, timeoutMs) > 0 && (entry.revents & (POLLHUP | POLLERR | POLLNVAL)) != 0;
  }

} // namespace

int main() {
  unsetenv("XDG_RUNTIME_DIR");
  unsetenv("WAYLAND_DISPLAY");

  const std::string socketPath = "/tmp/noctalia-driftwm-test-" + std::to_string(getpid()) + ".sock";
  FakeDriftwm server(socketPath);
  TEST_CHECK(setenv("DRIFTWM_SOCKET", socketPath.c_str(), 1) == 0);

  compositors::driftwm::DriftwmRuntime runtime;
  TEST_CHECK(runtime.available());
  TEST_CHECK(runtime.socketPath() == socketPath);
  TEST_CHECK(runtime.pollFd() == -1);
  // No socket and no armed retry: never vote 0, that spins the main loop.
  TEST_CHECK(runtime.pollTimeoutMs() == -1);

  // One-shot request helpers.
  server.setReply("{\"Ok\":{\"Layout\":\"English (US)\"}}");
  const auto layoutReply = runtime.requestJson("{\"Layout\":{\"short\":false}}\n");
  TEST_CHECK(layoutReply.has_value());
  TEST_CHECK(layoutReply->at("Ok").at("Layout") == "English (US)");
  TEST_CHECK(server.lastRequest() == "{\"Layout\":{\"short\":false}}");

  server.setReply("{\"Err\":\"invalid request\"}");
  TEST_CHECK(!runtime.requestOk("\"Bogus\"\n"));

  server.setReply("{\"Ok\":null}");
  TEST_CHECK(runtime.requestAction("switch-layout next"));
  TEST_CHECK(server.lastRequest() == "{\"Action\":\"switch-layout next\"}");

  // Registering a handler opens the Subscribe stream.
  CountingHandler handler(runtime);
  DriftwmOutputBackend outputBackend(runtime);
  DriftwmKeyboardBackend keyboardBackend(runtime);
  TEST_CHECK(runtime.pollFd() >= 0);
  TEST_CHECK(runtime.pollTimeoutMs() == -1);
  TEST_CHECK(waitFor([&server]() { return server.subscribeRequests() == 1; }, 2000));

  // A burst of pushed frames coalesces into a single delivery of the newest one.
  const std::string burst =
      stateLine("DP-1", "English (US)") + stateLine("DP-2", "English (US)") + stateLine("DP-3", "English (US)");
  server.push(burst);
  TEST_CHECK(waitForBytes(runtime.pollFd(), burst.size(), 2000));
  runtime.dispatchPoll(POLLIN);
  TEST_CHECK(handler.dispatches == 1);
  TEST_CHECK(handler.lastState.at("outputs").at(1).at("name") == "DP-3");
  TEST_CHECK(outputBackend.focusedOutputName() == std::optional<std::string>("DP-3"));

  // A frame split across two reads only dispatches once it is complete.
  const std::string frame = stateLine("DP-4", "English (US)");
  const std::size_t split = frame.size() / 2;
  server.push(frame.substr(0, split));
  TEST_CHECK(waitForBytes(runtime.pollFd(), split, 2000));
  runtime.dispatchPoll(POLLIN);
  TEST_CHECK(handler.dispatches == 1);
  server.push(frame.substr(split));
  TEST_CHECK(waitForBytes(runtime.pollFd(), frame.size() - split, 2000));
  runtime.dispatchPoll(POLLIN);
  TEST_CHECK(handler.dispatches == 2);
  TEST_CHECK(outputBackend.focusedOutputName() == std::optional<std::string>("DP-4"));

  // Replies, unknown frames and malformed lines are not state deliveries.
  const std::string noise = "{\"Ok\":null}\nnot json\n[1,2,3]\n";
  server.push(noise);
  TEST_CHECK(waitForBytes(runtime.pollFd(), noise.size(), 2000));
  runtime.dispatchPoll(POLLIN);
  TEST_CHECK(handler.dispatches == 2);

  // Keyboard layout: the value comes from a one-shot request, the change callback
  // only from a real layout change reported by the stream.
  int layoutChanges = 0;
  keyboardBackend.setChangeCallback([&layoutChanges]() { ++layoutChanges; });
  server.setReply("{\"Ok\":{\"Layout\":\"English (US)\"}}");
  TEST_CHECK(keyboardBackend.isAvailable());
  TEST_CHECK(keyboardBackend.currentLayoutName() == std::optional<std::string>("English (US)"));
  const auto layoutState = keyboardBackend.layoutState();
  TEST_CHECK(layoutState.has_value());
  TEST_CHECK(layoutState->names == std::vector<std::string>({"English (US)"}));
  TEST_CHECK(layoutState->currentIndex == 0);

  const std::string layoutChange = stateLine("DP-4", "French (AZERTY)");
  server.push(layoutChange);
  TEST_CHECK(waitForBytes(runtime.pollFd(), layoutChange.size(), 2000));
  runtime.dispatchPoll(POLLIN);
  TEST_CHECK(handler.dispatches == 3);
  TEST_CHECK(layoutChanges == 1);

  const std::string sameLayout = stateLine("DP-4", "French (AZERTY)");
  server.push(sameLayout);
  TEST_CHECK(waitForBytes(runtime.pollFd(), sameLayout.size(), 2000));
  runtime.dispatchPoll(POLLIN);
  TEST_CHECK(handler.dispatches == 4);
  TEST_CHECK(layoutChanges == 1);

  server.setReply("{\"Ok\":null}");
  TEST_CHECK(keyboardBackend.cycleLayout());
  TEST_CHECK(server.lastRequest() == "{\"Action\":\"switch-layout next\"}");

  // Losing the stream schedules a backed-off reconnect that never votes timeout 0.
  server.closeStream();
  TEST_CHECK(waitForHangup(runtime.pollFd(), 2000));
  runtime.dispatchPoll(POLLHUP);
  TEST_CHECK(runtime.pollFd() == -1);
  const int backoffMs = runtime.pollTimeoutMs();
  TEST_CHECK(backoffMs > 0);
  TEST_CHECK(backoffMs <= 2000);

  runtime.dispatchPoll(0);
  TEST_CHECK(runtime.pollFd() == -1);
  TEST_CHECK(runtime.pollTimeoutMs() > 0);

  std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<unsigned>(backoffMs) + 100U));
  runtime.dispatchPoll(0);
  TEST_CHECK(runtime.pollFd() >= 0);
  TEST_CHECK(runtime.pollTimeoutMs() == -1);
  TEST_CHECK(waitFor([&server]() { return server.subscribeRequests() == 2; }, 2000));

  // A torn-down stream is stale for every handler and stops asking for wakeups.
  runtime.cleanup();
  TEST_CHECK(runtime.pollFd() == -1);
  TEST_CHECK(runtime.pollTimeoutMs() == -1);
  TEST_CHECK(handler.resets == 1);
  TEST_CHECK(!outputBackend.focusedOutputName().has_value());
  TEST_CHECK(!keyboardBackend.currentLayoutName().has_value());

  return 0;
}
