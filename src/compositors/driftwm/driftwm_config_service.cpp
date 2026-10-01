#include "compositors/driftwm/driftwm_config_service.h"

#include "compositors/driftwm/driftwm_runtime.h"
#include "config/atomic_file.h"
#include "core/log.h"
#include "core/process/process.h"
#include "util/file_utils.h"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <system_error>
#include <utility>

namespace compositors::driftwm {

  namespace {

    constexpr Logger kLog("driftwm_config");
    // Bounded because the call runs on the UI thread: the settings tab waits for
    // the verdict before it shows the committed value. The check is normally tens of
    // milliseconds; this only caps a wedged subprocess.
    constexpr auto kValidationTimeout = std::chrono::milliseconds{2000};
    constexpr std::string_view kValidationSuffix = ".noctalia-validate";

    // Removes the candidate file however the validation call returns.
    class ScopedFile {
    public:
      explicit ScopedFile(std::filesystem::path path) : m_path(std::move(path)) {}
      ~ScopedFile() {
        if (!m_path.empty()) {
          std::error_code ec;
          std::filesystem::remove(m_path, ec);
        }
      }

      ScopedFile(const ScopedFile&) = delete;
      ScopedFile& operator=(const ScopedFile&) = delete;

      [[nodiscard]] const std::filesystem::path& path() const noexcept { return m_path; }

    private:
      std::filesystem::path m_path;
    };

    [[nodiscard]] std::string trimMessage(std::string_view text) {
      const auto first = text.find_first_not_of(" \t\r\n");
      if (first == std::string_view::npos) {
        return {};
      }
      const auto last = text.find_last_not_of(" \t\r\n");
      return std::string(text.substr(first, last - first + 1));
    }

  } // namespace

  DriftwmConfigService::DriftwmConfigService(DriftwmRuntime& runtime, FileWatcher& watcher)
      : m_runtime(runtime), m_watcher(watcher), m_configPath(resolveConfigPath()) {
    if (m_configPath.empty()) {
      kLog.warn("could not resolve a driftwm config path");
      return;
    }
    m_knownText = readFile();
    watchConfig();
    kLog.info("editing driftwm config '{}'", m_configPath.string());
  }

  DriftwmConfigService::~DriftwmConfigService() {
    if (m_watchId != 0) {
      m_watcher.unwatch(m_watchId);
      m_watchId = 0;
    }
  }

  std::filesystem::path
  DriftwmConfigService::resolveConfigPathFrom(const std::function<std::string(const char*)>& env) {
    if (const std::string override_path = env("DRIFTWM_CONFIG"); !override_path.empty()) {
      // driftwm expands a leading ~ before opening the file, and so must we or the
      // two would read different files.
      return FileUtils::expandUserPath(override_path);
    }
    if (const std::string configHome = env("XDG_CONFIG_HOME"); !configHome.empty()) {
      return std::filesystem::path(configHome) / "driftwm" / "config.toml";
    }
    if (const std::string home = env("HOME"); !home.empty()) {
      return std::filesystem::path(home) / ".config" / "driftwm" / "config.toml";
    }
    return {};
  }

  std::filesystem::path DriftwmConfigService::resolveConfigPath() {
    return resolveConfigPathFrom([](const char* name) -> std::string {
      const char* value = std::getenv(name);
      return value != nullptr ? std::string(value) : std::string();
    });
  }

  bool DriftwmConfigService::available() const noexcept {
    if (m_configPath.empty()) {
      return false;
    }
    std::error_code ec;
    return std::filesystem::is_regular_file(m_configPath, ec);
  }

  bool DriftwmConfigService::validatorAvailable() const {
    return m_validator != nullptr || process::commandExists("driftwm");
  }

  std::optional<std::string> DriftwmConfigService::readFile(std::string* error) const {
    if (m_configPath.empty()) {
      if (error != nullptr) {
        *error = "no driftwm config path could be resolved";
      }
      return std::nullopt;
    }
    std::ifstream stream(m_configPath, std::ios::binary);
    if (!stream) {
      if (error != nullptr) {
        *error = "cannot read " + m_configPath.string();
      }
      return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
  }

  std::optional<DriftwmConfigDocument> DriftwmConfigService::loadDocument(std::string* error) const {
    auto text = readFile(error);
    if (!text.has_value()) {
      return std::nullopt;
    }
    return DriftwmConfigDocument::parse(std::move(*text), error);
  }

  void DriftwmConfigService::setValidator(Validator validator) { m_validator = std::move(validator); }

  void DriftwmConfigService::setExternalChangeCallback(std::function<void()> callback) {
    m_externalChangeCallback = std::move(callback);
  }

  void DriftwmConfigService::watchConfig() {
    if (!available()) {
      return;
    }
    // WriteCompleted (not Modified) so a half-written file is never read back.
    m_watchId = m_watcher.watch(
        m_configPath, [this]() { onWatchedChange(); }, FileWatcher::WatchTrigger::WriteCompleted
    );
  }

  void DriftwmConfigService::onWatchedChange() {
    auto text = readFile();
    if (!text.has_value()) {
      return;
    }
    if (m_knownText.has_value() && *m_knownText == *text) {
      // noctalia's own atomic write landing. Not an external edit.
      return;
    }
    m_knownText = std::move(text);
    kLog.info("driftwm config changed outside noctalia");
    if (m_externalChangeCallback) {
      m_externalChangeCallback();
    }
    m_changed.emit();
  }

  bool DriftwmConfigService::runDriftwmCheck(const std::filesystem::path& candidate, std::string* error) {
    const std::string candidatePath = candidate.string();
    const process::RunResult result =
        process::runSyncWithTimeout({"driftwm", "--config", candidatePath.c_str(), "--check-config"}, kValidationTimeout);
    if (result) {
      return true;
    }
    if (error != nullptr) {
      *error = trimMessage(result.err.empty() ? result.out : result.err);
      if (error->empty()) {
        *error = result.timedOut ? "driftwm --check-config timed out" : "driftwm --check-config failed";
      }
    }
    return false;
  }

  bool DriftwmConfigService::validateCandidate(const std::filesystem::path& candidate, std::string* error) const {
    if (m_validator) {
      return m_validator(candidate, error);
    }
    if (!process::commandExists("driftwm")) {
      kLog.warn("driftwm is not on PATH; validating the config with the TOML parser only");
      return true;
    }
    return runDriftwmCheck(candidate, error);
  }

  bool DriftwmConfigService::validateCandidateText(
      std::string_view text, const std::optional<std::filesystem::perms>& mode, std::string* error
  ) {
    // Cheap local gate first: a syntax error must not cost a subprocess.
    if (!validateTomlText(text, error)) {
      return false;
    }
    if (m_configPath.empty()) {
      return false;
    }

    const std::filesystem::path candidate =
        m_configPath.parent_path() / (m_configPath.filename().string() + std::string(kValidationSuffix));
    const ScopedFile cleanup(candidate);
    if (!writeTextFileAtomic(candidate, text, mode)) {
      if (error != nullptr) {
        *error = "cannot stage the candidate config next to " + m_configPath.string();
      }
      return false;
    }
    return validateCandidate(candidate, error);
  }

  DriftwmConfigService::ApplyResult DriftwmConfigService::apply(std::string_view keyPath, const DriftwmScalar& value) {
    if (!available()) {
      return {.status = ApplyResult::Status::NoConfigFile, .message = "no driftwm config at " + m_configPath.string()};
    }

    std::string error;
    auto document = loadDocument(&error);
    if (!document.has_value()) {
      return {.status = ApplyResult::Status::InvalidValue, .message = error};
    }

    if (!document->assign(keyPath, value)) {
      return {
          .status = ApplyResult::Status::InvalidValue,
          .message = std::string(keyPath) + " is a multi-line value and cannot be rewritten safely",
      };
    }

    const std::string candidate = document->text();
    if (candidate == m_knownText) {
      // Already the committed value: no write, no reload, nothing to undo.
      return {.status = ApplyResult::Status::Applied, .reloaded = true};
    }

    std::error_code ec;
    const std::optional<std::filesystem::perms> mode = std::filesystem::status(m_configPath, ec).permissions();
    if (!validateCandidateText(candidate, ec ? std::nullopt : mode, &error)) {
      kLog.warn("refusing to write {}: {}", std::string(keyPath), error);
      return {.status = ApplyResult::Status::ValidationFailed, .message = error};
    }

    if (!writeTextFileAtomic(m_configPath, candidate, ec ? std::nullopt : mode)) {
      return {.status = ApplyResult::Status::WriteFailed, .message = "cannot write " + m_configPath.string()};
    }

    m_knownText = candidate;

    // driftwm re-reads the file on this action; without it the edit waits for the
    // next start, which is why a failure here is reported but not fatal.
    const bool reloaded = m_runtime.requestAction("reload-config", /*acceptNoResponse=*/true);
    if (!reloaded) {
      kLog.warn("wrote {} but driftwm did not acknowledge reload-config", std::string(keyPath));
    }
    m_changed.emit();
    return {.status = ApplyResult::Status::Applied, .reloaded = reloaded};
  }

} // namespace compositors::driftwm
