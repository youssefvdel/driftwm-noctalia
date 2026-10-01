#pragma once

#include "compositors/driftwm/driftwm_config_document.h"
#include "core/files/file_watcher.h"
#include "ui/signal.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace compositors::driftwm {

  class DriftwmRuntime;

  // Reads and edits driftwm's own config.toml. Nothing is mirrored into noctalia's
  // config: the settings tab and the compositor read the same file, so there is
  // exactly one copy of every value.
  //
  // Every write goes through the same pipeline: re-read the file (so an edit made
  // outside noctalia is never clobbered), rewrite one value token, validate the
  // candidate, write it atomically, then ask the compositor to reload. A write
  // that driftwm would refuse never reaches the live file.
  class DriftwmConfigService {
  public:
    struct ApplyResult {
      enum class Status : std::uint8_t {
        Applied,
        NoConfigFile,
        InvalidValue,
        ValidationFailed,
        WriteFailed,
      };

      Status status = Status::NoConfigFile;
      // False when the file was written but the running compositor could not be
      // asked to reload, e.g. its socket is gone. The edit still takes effect on
      // its next start, so this is reported, not treated as a failure.
      bool reloaded = false;
      // Human-readable detail, always set when status != Applied.
      std::string message;
    };

    // Validates a candidate file the way driftwm itself would. Takes the path of
    // a real file so the implementation can hand it to `driftwm --check-config`.
    using Validator = std::function<bool(const std::filesystem::path& candidate, std::string* error)>;

    DriftwmConfigService(DriftwmRuntime& runtime, FileWatcher& watcher);
    ~DriftwmConfigService();

    DriftwmConfigService(const DriftwmConfigService&) = delete;
    DriftwmConfigService& operator=(const DriftwmConfigService&) = delete;

    // The path driftwm itself reads, in its own order: $DRIFTWM_CONFIG (with a
    // leading ~ expanded), then $XDG_CONFIG_HOME, then $HOME/.config. Kept free of
    // process state so the resolution order can be tested directly.
    [[nodiscard]] static std::filesystem::path resolveConfigPath();
    // resolveConfigPath() with the environment read through `env`, for tests.
    [[nodiscard]] static std::filesystem::path
    resolveConfigPathFrom(const std::function<std::string(const char*)>& env);

    [[nodiscard]] const std::filesystem::path& configPath() const noexcept { return m_configPath; }
    [[nodiscard]] bool available() const noexcept;
    [[nodiscard]] bool validatorAvailable() const;

    [[nodiscard]] std::optional<std::string> readFile(std::string* error = nullptr) const;
    [[nodiscard]] std::optional<DriftwmConfigDocument> loadDocument(std::string* error = nullptr) const;

    // Rewrites one key, validates the result and reloads the compositor. `value`
    // must match the catalogued kind of the field at `keyPath`.
    ApplyResult apply(std::string_view keyPath, const DriftwmScalar& value);

    // Runs a candidate through driftwm's own validator. Exposed so a caller can
    // check a file it did not write.
    [[nodiscard]] bool validateCandidate(const std::filesystem::path& candidate, std::string* error) const;

    // Replaces the validation gate. Tests inject a stub; production leaves it
    // alone so `driftwm --check-config` stays the authority.
    void setValidator(Validator validator);
    void setExternalChangeCallback(std::function<void()> callback);

    // Fires after the on-disk file changes without noctalia writing it, and after
    // a successful apply. Consumers re-read through loadDocument().
    [[nodiscard]] Signal<>& changed() noexcept { return m_changed; }
    [[nodiscard]] const Signal<>& changed() const noexcept { return m_changed; }

  private:
    void watchConfig();
    void onWatchedChange();
    // Runs the default gate: `driftwm --check-config --config <candidate>`.
    [[nodiscard]] static bool runDriftwmCheck(const std::filesystem::path& candidate, std::string* error);
    // Writes `text` to a sibling temp file, validates it, and removes it again.
    [[nodiscard]] bool
    validateCandidateText(std::string_view text, const std::optional<std::filesystem::perms>& mode, std::string* error);

    DriftwmRuntime& m_runtime;
    FileWatcher& m_watcher;
    std::filesystem::path m_configPath;
    Validator m_validator;
    std::function<void()> m_externalChangeCallback;
    Signal<> m_changed;
    FileWatcher::WatchId m_watchId = 0;
    // Exact text of the file as noctalia last knows it. Compared against the file
    // so the atomic write noctalia just made is not reported back as an external
    // edit, while any other writer is.
    std::optional<std::string> m_knownText;
  };

} // namespace compositors::driftwm
