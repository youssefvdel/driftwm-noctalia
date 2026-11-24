#include "compositors/driftwm/driftwm_runtime.h"
#include "compositors/driftwm/driftwm_workspace_backend.h"
#include "tests/test_check.h"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <initializer_list>
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

  using Bookmark = DriftwmWorkspaceBackend::Bookmark;

  // Minimal DriftWM IPC stand-in: a listening socket that serves the persistent
  // Subscribe stream and answers one-shot requests on separate connections,
  // routing by request kind and recording every one-shot request line.
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

    // The reply a Bookmark list request gets: a JSON line without the newline.
    void setBookmarkReply(std::string reply) {
      const std::lock_guard<std::mutex> lock(m_mutex);
      m_bookmarkReply = std::move(reply);
    }

    // Every one-shot request line so far, in arrival order.
    [[nodiscard]] std::vector<std::string> requests() const {
      const std::lock_guard<std::mutex> lock(m_mutex);
      return m_requests;
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
          m_requests.push_back(line);
          reply = line.find("Bookmark") != std::string::npos ? m_bookmarkReply : std::string("{\"Ok\":\"Ok\"}");
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
    std::string m_bookmarkReply;
    std::vector<std::string> m_requests;
    std::deque<std::string> m_streamQueue;
    int m_streamFd = -1;
    int m_subscribeRequests = 0;
    bool m_streamClosed = false;
    bool m_stopped = false;
  };

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

  [[nodiscard]] std::string stateLine(const nlohmann::json& state) {
    return nlohmann::json{{"State", state}}.dump() + "\n";
  }

  [[nodiscard]] nlohmann::json
  canvasWindow(nlohmann::json id, const std::string& appId, const std::string& title, int x, int y, bool focused) {
    return nlohmann::json{
        {"id", std::move(id)}, {"app_id", appId},     {"title", title},
        {"position", {x, y}},  {"size", {1171, 678}}, {"is_focused", focused},
        {"is_widget", false},  {"suspended", false},  {"mode", "Normal"},
    };
  }

  [[nodiscard]] std::vector<std::string> windowIds(const std::vector<DriftwmWorkspaceBackend::Window>& windows) {
    std::vector<std::string> ids;
    for (const auto& window : windows) {
      ids.push_back(std::to_string(window.id));
    }
    return ids;
  }

  [[nodiscard]] std::vector<std::string> rowIds(const std::vector<WorkspaceWindow>& rows) {
    std::vector<std::string> ids;
    for (const auto& row : rows) {
      ids.push_back(row.windowId);
    }
    return ids;
  }

  // A string list literal. Written as a call so that its commas sit inside
  // parentheses, where the TEST_CHECK macro cannot split on them.
  [[nodiscard]] std::vector<std::string> list(std::initializer_list<const char*> values) {
    return std::vector<std::string>(values.begin(), values.end());
  }

  // "id:key" per row: the projection every grouping decision reads.
  [[nodiscard]] std::vector<std::string> rowKeys(const std::vector<WorkspaceWindow>& rows) {
    std::vector<std::string> keys;
    for (const auto& row : rows) {
      keys.push_back(row.windowId + ":" + row.workspaceKey);
    }
    return keys;
  }

  [[nodiscard]] std::vector<Workspace> threeBookmarks() {
    return std::vector<Workspace>{
        {.id = "home", .name = "home"}, {.id = "work", .name = "work"}, {.id = "far", .name = "far"}
    };
  }

  // The two outputs of the fixture: DP-1 is focused, 2560x1440, on bookmark "work".
  const nlohmann::json kOutputs = nlohmann::json::parse(R"([
    {"name": "eDP-1", "camera": [0.0, 0.0], "zoom": 1.0, "size": [1920, 1080], "active": false, "active_bookmark": null},
    {"name": "DP-1", "camera": [4000.0, 0.0], "zoom": 1.0, "size": [2560, 1440], "active": true, "active_bookmark": "work"}
  ])");

} // namespace

int main() {
  unsetenv("XDG_RUNTIME_DIR");
  unsetenv("WAYLAND_DISPLAY");

  // The bookmark geometry helpers.
  // "home" and "work" sit well within reach of a wide window, "far" is out of it.
  const std::vector<Bookmark> bookmarks{{"work", 4000.0, 0.0}, {"home", 0.0, 0.0}, {"far", 50000.0, 0.0}};

  // Seen from the bookmark whose viewport overlaps the window rect.
  TEST_CHECK(DriftwmWorkspaceBackend::seenFromBookmarks(bookmarks, 150, 100, 1171, 678, 1920, 1080) == list({"home"}));
  TEST_CHECK(DriftwmWorkspaceBackend::seenFromBookmarks(bookmarks, 4100, 50, 1171, 678, 1920, 1080) == list({"work"}));
  TEST_CHECK(DriftwmWorkspaceBackend::seenFromBookmarks(bookmarks, 25000, 0, 800, 600, 1920, 1080).empty());

  // A window wide enough to reach both is seen from both, nearest first.
  TEST_CHECK(
      DriftwmWorkspaceBackend::seenFromBookmarks(bookmarks, 2100, 0, 2400, 600, 1920, 1080) == list({"work", "home"})
  );
  // An exact tie goes to the name that sorts first, whatever the list order.
  TEST_CHECK(
      DriftwmWorkspaceBackend::seenFromBookmarks(bookmarks, 2000, 0, 2400, 600, 1920, 1080) == list({"home", "work"})
  );
  // Touching edges do not overlap: the centers must be strictly closer than half the sum.
  TEST_CHECK(DriftwmWorkspaceBackend::seenFromBookmarks(bookmarks, 960, 0, 0, 0, 1920, 1080).empty());
  TEST_CHECK(DriftwmWorkspaceBackend::seenFromBookmarks(bookmarks, 959, 0, 0, 0, 1920, 1080) == list({"home"}));

  TEST_CHECK(DriftwmWorkspaceBackend::nearestBookmark(bookmarks, 2100, 0) == "work");
  TEST_CHECK(DriftwmWorkspaceBackend::nearestBookmark(bookmarks, 2000, 0) == "home");
  TEST_CHECK(DriftwmWorkspaceBackend::nearestBookmark(bookmarks, 60000, 0) == "far");
  TEST_CHECK(DriftwmWorkspaceBackend::nearestBookmark({}, 10, 10).empty());

  {
    DriftwmWorkspaceBackend::Window canvas;
    canvas.outputName = "DP-1";
    TEST_CHECK(DriftwmWorkspaceBackend::onOutput(canvas, ""));
    TEST_CHECK(DriftwmWorkspaceBackend::onOutput(canvas, "DP-1"));
    TEST_CHECK(!DriftwmWorkspaceBackend::onOutput(canvas, "eDP-1"));
    canvas.outputName.clear();
    TEST_CHECK(DriftwmWorkspaceBackend::onOutput(canvas, "eDP-1"));
    canvas.seenFrom = {"work", "home"};
    TEST_CHECK(canvas.workspaceKey() == "work");
    canvas.seenFrom.clear();
    canvas.fallbackBookmark = "home";
    TEST_CHECK(canvas.workspaceKey() == "home");
  }

  // The state stream, over a stand-in IPC socket.
  const std::string socketPath = "/tmp/noctalia-driftwm-workspace-test-" + std::to_string(getpid()) + ".sock";
  FakeDriftwm server(socketPath);
  TEST_CHECK(setenv("DRIFTWM_SOCKET", socketPath.c_str(), 1) == 0);

  compositors::driftwm::DriftwmRuntime runtime;
  int windowChanges = 0;
  int workspaceChanges = 0;
  {
    DriftwmWorkspaceBackend backend(runtime);
    backend.setWindowChangeCallback([&windowChanges]() { ++windowChanges; });
    backend.setChangeCallback([&workspaceChanges]() { ++workspaceChanges; });

    TEST_CHECK(!backend.available());
    TEST_CHECK(backend.windows().empty());
    TEST_CHECK(!backend.focusedWindowId().has_value());
    TEST_CHECK(backend.focusedWindow() == nullptr);
    TEST_CHECK(backend.workspaceWindows().empty());

    // Registering opened the Subscribe stream.
    TEST_CHECK(waitFor([&server]() { return server.subscribeRequests() == 1; }, 2000));
    TEST_CHECK(runtime.pollFd() >= 0);

    // The first snapshot also fetches the bookmark list.
    server.setBookmarkReply(R"({"Ok":{"Bookmarks":{"home":[0.0,0.0],"work":[4000.0,0.0]}}})");

    nlohmann::json state{
        {"camera", {0.0, 0.0}},
        {"zoom", 1.0},
        {"outputs", kOutputs},
        {"active_bookmark", "work"},
    };
    // Reported in stacking order, which a raise rewrites. A widget and an entry
    // without an id are skipped. Zero is a real id: the first element mapped in
    // a session gets it.
    nlohmann::json widget = canvasWindow(4, "clock", "", 0, 0, false);
    widget["is_widget"] = true;
    state["windows"] = {
        canvasWindow(7, "zen", "Zen", 0, 0, true),
        canvasWindow(3, "kitty", "~", 150, 100, false),
        canvasWindow(9, "kitty", "~", 4100, 50, false),
        widget,
        nlohmann::json{
            {"app_id", "noid"}, {"title", ""}, {"position", {0, 0}}, {"size", {10, 10}}, {"is_widget", false}
        },
        canvasWindow(0, "first", "First", 9000, 9000, false),
    };
    state["windows"][5]["size"] = {100, 100};
    state["fullscreen"] = {{{"id", 5}, {"output", "DP-1"}, {"app_id", "mpv"}, {"title", "movie"}}};
    state["pinned"] = {
        nlohmann::json{
            {"id", 6},
            {"output", "eDP-1"},
            {"app_id", "notes"},
            {"title", "Notes"},
            {"position", {20, 40}},
            {"size", {320, 240}}
        },
        // An output the snapshot no longer lists: dropped rather than guessed.
        nlohmann::json{
            {"id", 8},
            {"output", "HDMI-A-1"},
            {"app_id", "gone"},
            {"title", "Gone"},
            {"position", {20, 40}},
            {"size", {320, 240}}
        },
    };

    const std::string snapshot = stateLine(state);
    server.push(snapshot);
    TEST_CHECK(waitForBytes(runtime.pollFd(), snapshot.size(), 2000));
    runtime.dispatchPoll(POLLIN);
    TEST_CHECK(server.requests() == list({R"({"Bookmark":{}})"}));

    // Id order is creation order: 0 first, then 3, 5, 6, 7, 9.
    TEST_CHECK(backend.available());
    TEST_CHECK(windowIds(backend.windows()) == list({"0", "3", "5", "6", "7", "9"}));
    TEST_CHECK(windowChanges == 1);
    TEST_CHECK(workspaceChanges == 1);

    const auto& windows = backend.windows();
    // A canvas window between bookmarks has no `seenFrom` and falls back to the nearest.
    TEST_CHECK(windows[0].appId == "first");
    TEST_CHECK(windows[0].seenFrom.empty());
    TEST_CHECK(windows[0].fallbackBookmark == "work");
    TEST_CHECK(windows[0].x == 9000 && windows[0].y == -9000);

    // Canvas windows: seen from the bookmark whose viewport shows them.
    TEST_CHECK(windows[1].appId == "kitty" && windows[1].title == "~");
    TEST_CHECK(windows[1].seenFrom == list({"home"}));
    TEST_CHECK(windows[1].fallbackBookmark.empty());
    TEST_CHECK(windows[1].x == 150 && windows[1].y == -100);

    // Screen-space windows name their output and sit under the bookmark that
    // output is on.
    TEST_CHECK(windows[2].id == 5 && windows[2].outputName == "DP-1");
    TEST_CHECK(windows[2].seenFrom.empty() && windows[2].workspaceKey() == "work");
    TEST_CHECK(windows[3].id == 6 && windows[3].outputName == "eDP-1");
    TEST_CHECK(windows[3].workspaceKey() == "home");

    TEST_CHECK(windows[4].focused);
    TEST_CHECK(windows[5].seenFrom == list({"work"}));

    // Focus follows the inventory, with the id the Focus request takes.
    TEST_CHECK(backend.focusedWindowId() == std::optional<std::string>("7"));
    TEST_CHECK(backend.focusedWindow() == &windows[4]);

    // One row per bookmark a window is seen from; a window seen from none keeps
    // one row under its fallback.
    TEST_CHECK(
        rowKeys(backend.workspaceWindows()) == list({"0:work", "3:home", "5:work", "6:home", "7:home", "9:work"})
    );
    // Canvas windows belong to every output; screen-space windows to the one they name.
    TEST_CHECK(rowIds(backend.workspaceWindows("DP-1")) == list({"0", "3", "5", "7", "9"}));
    TEST_CHECK(rowIds(backend.workspaceWindows("eDP-1")) == list({"0", "3", "6", "7", "9"}));
    TEST_CHECK(rowIds(backend.workspaceWindows("HDMI-A-1")) == list({"0", "3", "7", "9"}));

    const auto rows = backend.workspaceWindows();
    TEST_CHECK(rows[1].windowId == "3" && rows[1].appId == "kitty" && rows[1].title == "~");
    TEST_CHECK(rows[1].x == 150 && rows[1].y == -100 && rows[1].outputName.empty());
    TEST_CHECK(rows[2].outputName == "DP-1");

    const auto appIds = backend.appIdsByWorkspace();
    TEST_CHECK(appIds.at("home") == list({"kitty", "notes", "zen"}));
    TEST_CHECK(appIds.at("work") == list({"first", "mpv", "kitty"}));
    const auto dpAppIds = backend.appIdsByWorkspace("DP-1");
    TEST_CHECK(dpAppIds.at("home") == list({"kitty", "zen"}));
    TEST_CHECK(dpAppIds.at("work") == list({"first", "mpv", "kitty"}));
    const auto otherAppIds = backend.appIdsByWorkspace("HDMI-A-1");
    TEST_CHECK(otherAppIds.at("home") == list({"kitty", "zen"}));
    TEST_CHECK(otherAppIds.at("work") == list({"first", "kitty"}));

    // A bookmark is occupied by the windows it shows. The fallback only keeps
    // "first" listed, it does not put it on screen, so an output with no
    // screen-space window leaves "far" empty.
    std::vector<Workspace> workspaces = threeBookmarks();
    backend.apply(workspaces, "HDMI-A-1");
    TEST_CHECK(workspaces[0].occupied);
    TEST_CHECK(workspaces[1].occupied);
    TEST_CHECK(!workspaces[2].occupied);

    // A screen-space window is on screen whatever bookmark its output views, so
    // it occupies every bookmark of that output and of no other.
    workspaces = threeBookmarks();
    backend.apply(workspaces, "DP-1");
    TEST_CHECK(workspaces[0].occupied && workspaces[1].occupied && workspaces[2].occupied);

    // Focus and close by id, zero included. Anything but a decimal id is
    // refused unheard.
    TEST_CHECK(backend.focusWindowById("9"));
    TEST_CHECK(backend.focusWindowById("0", true));
    TEST_CHECK(backend.closeWindowById("5"));
    TEST_CHECK(!backend.focusWindowById(""));
    TEST_CHECK(!backend.focusWindowById("9x"));
    TEST_CHECK(!backend.focusWindowById("-1"));
    TEST_CHECK(!backend.focusWindowById(" 9"));
    TEST_CHECK(!backend.focusWindowById("99999999999999999999999"));
    TEST_CHECK(!backend.closeWindowById("nine"));
    const std::vector<std::string> expectedRequests{
        R"({"Bookmark":{}})", R"({"Focus":9})", R"({"Focus":0})", R"({"Close":5})"
    };
    TEST_CHECK(server.requests() == expectedRequests);

    // A snapshot that only moved the camera changes no window, notifies nobody,
    // and re-reads no bookmark within the throttle.
    nlohmann::json panned = state;
    panned["camera"] = {100.0, 0.0};
    const std::string pan = stateLine(panned);
    server.push(pan);
    TEST_CHECK(waitForBytes(runtime.pollFd(), pan.size(), 2000));
    runtime.dispatchPoll(POLLIN);
    TEST_CHECK(windowChanges == 1);
    TEST_CHECK(workspaceChanges == 1);

    // Focus moves, and the screen-space windows leave.
    nlohmann::json refocused = state;
    refocused["windows"][0]["is_focused"] = false;
    refocused["windows"][1]["is_focused"] = true;
    refocused["fullscreen"] = nlohmann::json::array();
    refocused["pinned"] = nlohmann::json::array();
    const std::string focusMove = stateLine(refocused);
    server.push(focusMove);
    TEST_CHECK(waitForBytes(runtime.pollFd(), focusMove.size(), 2000));
    runtime.dispatchPoll(POLLIN);
    TEST_CHECK(backend.focusedWindowId() == std::optional<std::string>("3"));
    TEST_CHECK(windowIds(backend.windows()) == list({"0", "3", "7", "9"}));
    TEST_CHECK(windowChanges == 2);
    TEST_CHECK(workspaceChanges == 2);
    TEST_CHECK(server.requests().size() == 4);

    // Without screen-space windows only the seen-from rule decides, and an
    // occupancy another source set is kept.
    workspaces = threeBookmarks();
    workspaces[2].occupied = true;
    backend.apply(workspaces, "DP-1");
    TEST_CHECK(workspaces[0].occupied && workspaces[1].occupied && workspaces[2].occupied);
    workspaces = threeBookmarks();
    backend.apply(workspaces, "DP-1");
    TEST_CHECK(workspaces[0].occupied && workspaces[1].occupied && !workspaces[2].occupied);

    // Something that is not a snapshot object is ignored.
    backend.handleState(nlohmann::json::array());
    TEST_CHECK(windowChanges == 2);

    // The bookmark list is throttled: a second read this soon is skipped.
    server.setBookmarkReply(R"({"Ok":{"Bookmarks":{"home":[0.0,0.0]}}})");
    TEST_CHECK(!backend.refreshBookmarks());
    TEST_CHECK(server.requests().size() == 4);

    std::this_thread::sleep_for(std::chrono::milliseconds(2100));

    // "work" is gone: its window is re-listed under the bookmark left, as a
    // fallback only, so it is not on that bookmark's screen. The change is the
    // caller's to report.
    TEST_CHECK(backend.refreshBookmarks());
    TEST_CHECK(server.requests().size() == 5);
    TEST_CHECK(server.requests().back() == R"({"Bookmark":{}})");
    TEST_CHECK(rowKeys(backend.workspaceWindows()) == list({"0:home", "3:home", "7:home", "9:home"}));
    std::vector<Workspace> homeOnly{{.id = "home", .name = "home"}};
    backend.apply(homeOnly, "DP-1");
    TEST_CHECK(homeOnly[0].occupied);
    TEST_CHECK(windowChanges == 2);
    TEST_CHECK(workspaceChanges == 2);
    // The throttle holds.
    TEST_CHECK(!backend.refreshBookmarks());

    // A window that falls back to a bookmark does not occupy it: drop "home"'s
    // seen-from windows and only the fallback is left.
    nlohmann::json strays = refocused;
    strays["windows"] = {canvasWindow(9, "kitty", "~", 4100, 50, false)};
    const std::string stray = stateLine(strays);
    server.push(stray);
    TEST_CHECK(waitForBytes(runtime.pollFd(), stray.size(), 2000));
    runtime.dispatchPoll(POLLIN);
    TEST_CHECK(rowKeys(backend.workspaceWindows()) == list({"9:home"}));
    TEST_CHECK(windowChanges == 3);
    std::vector<Workspace> strayBookmarks = threeBookmarks();
    backend.apply(strayBookmarks, "DP-1");
    TEST_CHECK(!strayBookmarks[0].occupied && !strayBookmarks[1].occupied);

    // A failed bookmark list keeps the last known one.
    server.setBookmarkReply(R"({"Err":"no bookmarks for you"})");
    std::this_thread::sleep_for(std::chrono::milliseconds(2100));
    TEST_CHECK(!backend.refreshBookmarks());
    TEST_CHECK(server.requests().size() == 6);
    TEST_CHECK(rowKeys(backend.workspaceWindows()) == list({"9:home"}));

    // A snapshot restores the full window list against the kept bookmarks
    // without another read.
    server.setBookmarkReply(R"({"Ok":{"Bookmarks":{"home":[0.0,0.0],"work":[4000.0,0.0]}}})");
    const std::string resync = stateLine(refocused);
    server.push(resync);
    TEST_CHECK(waitForBytes(runtime.pollFd(), resync.size(), 2000));
    runtime.dispatchPoll(POLLIN);
    TEST_CHECK(server.requests().size() == 6);
    TEST_CHECK(rowKeys(backend.workspaceWindows()) == list({"0:home", "3:home", "7:home", "9:home"}));
    TEST_CHECK(windowChanges == 4);
    TEST_CHECK(workspaceChanges == 4);

    // A stream reset empties the model and says so, once.
    backend.handleStreamReset();
    TEST_CHECK(!backend.available());
    TEST_CHECK(backend.windows().empty());
    TEST_CHECK(!backend.focusedWindowId().has_value());
    TEST_CHECK(backend.workspaceWindows().empty());
    TEST_CHECK(windowChanges == 5);
    TEST_CHECK(workspaceChanges == 5);
    backend.handleStreamReset();
    TEST_CHECK(windowChanges == 5);

    // The next snapshot is a first one again, and its bookmark fetch starts over.
    const std::string again = stateLine(refocused);
    server.push(again);
    TEST_CHECK(waitForBytes(runtime.pollFd(), again.size(), 2000));
    runtime.dispatchPoll(POLLIN);
    TEST_CHECK(server.requests().size() == 7);
    TEST_CHECK(server.requests().back() == R"({"Bookmark":{}})");
    TEST_CHECK(backend.available());
    TEST_CHECK(rowKeys(backend.workspaceWindows()) == list({"0:work", "3:home", "7:home", "9:work"}));
    TEST_CHECK(windowChanges == 6);
    TEST_CHECK(workspaceChanges == 6);

    // A compositor that predates the window arrays: available and empty.
    const std::string bareState = stateLine(nlohmann::json{{"camera", {0.0, 0.0}}, {"zoom", 1.0}});
    server.push(bareState);
    TEST_CHECK(waitForBytes(runtime.pollFd(), bareState.size(), 2000));
    runtime.dispatchPoll(POLLIN);
    TEST_CHECK(backend.available());
    TEST_CHECK(backend.windows().empty());
    TEST_CHECK(!backend.focusedWindowId().has_value());
    TEST_CHECK(windowChanges == 7);

    // cleanup() forgets everything without notifying: it runs on shutdown.
    backend.cleanup();
    TEST_CHECK(!backend.available());
    TEST_CHECK(backend.windows().empty());
    TEST_CHECK(windowChanges == 7);
    TEST_CHECK(workspaceChanges == 7);
  }

  return 0;
}
