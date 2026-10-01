#include "compositors/driftwm/driftwm_config_document.h"
#include "compositors/driftwm/driftwm_config_field.h"
#include "compositors/driftwm/driftwm_config_service.h"
#include "compositors/driftwm/driftwm_runtime.h"
#include "core/files/file_watcher.h"
#include "core/process/process.h"
#include "tests/test_check.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <print>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

  using namespace compositors::driftwm;

  std::filesystem::path makeTempDir() {
    const auto* env = std::getenv("TMPDIR");
    std::filesystem::path base = env != nullptr && env[0] != '\0' ? std::string(env) : "/tmp";
    base /= "noctalia-driftwm-config-test-" + std::to_string(::getpid()) + "-" + std::to_string(::rand());
    std::filesystem::create_directories(base);
    return base;
  }

  void writeFile(const std::filesystem::path& path, std::string_view content) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << content;
  }

  // Sets an environment variable and restores it afterwards.
  class ScopedEnv {
  public:
    ScopedEnv(const char* name, const std::string& value) : m_name(name) {
      const char* previous = ::getenv(name);
      if (previous != nullptr) {
        m_hadPrevious = true;
        m_previous = previous;
      }
      ::setenv(name, value.c_str(), 1);
    }
    ~ScopedEnv() {
      if (m_hadPrevious) {
        ::setenv(m_name.c_str(), m_previous.c_str(), 1);
      } else {
        ::unsetenv(m_name.c_str());
      }
    }

    ScopedEnv(const ScopedEnv&) = delete;
    ScopedEnv& operator=(const ScopedEnv&) = delete;

  private:
    std::string m_name;
    bool m_hadPrevious = false;
    std::string m_previous;
  };

  // The service reads driftwm's real config path out of the environment, so each
  // test points $DRIFTWM_CONFIG at its own temp file.
  using ScopedConfigPath = ScopedEnv;

  // Keeps the reload request off the wire: with a socket that cannot be connected
  // to, DriftwmRuntime reports "not reloaded" without touching a live compositor.
  ScopedEnv pointRuntimeAtDeadSocket() {
    return ScopedEnv("DRIFTWM_SOCKET", "/tmp/kilo/noctalia-no-such-driftwm.sock");
  }

  // FileWatcher drops events that arrive within 100ms of the previous fire, so a
  // test that writes twice has to wait that long between them.
  void waitPastWatcherDebounce() { std::this_thread::sleep_for(std::chrono::milliseconds{150}); }

  std::string readFile(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
  }

  // A document with the shapes that break a naive re-serialization: comments in
  // every position, blank lines, quoted binding keys, an array-of-tables section,
  // a multi-line array and a key noctalia does not curate.
  constexpr std::string_view kSampleConfig = R"(# driftwm config
# Second header comment.

autostart = ["noctalia", "driftmap --snap 8"]
focus_follows_mouse = true
window_placement = "auto"

[env]
QT_QPA_PLATFORMTHEME = "qt6ct"

[zoom]
# Multiplier per keypress (1.1 = 10% per press)
step = 1.1
fit_padding = 80.0
interact_min = 0.0

[navigation]
drift = 0.1    # momentum coast
pan_step = 100.0

[input.keyboard]
layout = 'halmak'   # single-quoted on purpose
repeat_rate = 20

[keybindings]
"mod+d" = "spawn noctalia msg panel-open launcher"
"mod+t" = "exec ghostty"

[navigation.bookmarks]
"1" = [-1750, 1750]

[[outputs]]
scale = 1.0

[[outputs]]
scale = 2.0

[navigation.anchors]
origin = [
  [0, 0],
  [-1750, 1750],
]
)";

  // A config driftwm's own parser accepts, used by the tests that run the real
  // `driftwm --check-config` gate. The sample above deliberately contains shapes
  // driftwm rejects, so the two are kept apart.
  constexpr std::string_view kRealisticConfig = R"(# driftwm config

autostart = ["noctalia", "driftmap --snap 8"]
focus_follows_mouse = true
window_placement = "auto"

[zoom]
step = 1.1
fit_padding = 80.0

[navigation]
drift = 0.1    # momentum coast
pan_step = 100.0

[input.keyboard]
layout = 'halmak'   # single-quoted on purpose
repeat_rate = 20

[background]
type = "shader"
path = "/usr/local/share/driftwm/wallpapers/static/dark_sea.glsl"
cache_shader = true
)";

  // ---- document preservation ------------------------------------------------

  void testParseRejectsInvalidToml() {
    std::string error;
    TEST_CHECK(!DriftwmConfigDocument::parse("[broken\n", &error).has_value());
    TEST_CHECK(!error.empty());
  }

  void testReadsScalars() {
    auto document = DriftwmConfigDocument::parse(std::string(kSampleConfig));
    TEST_CHECK(document.has_value());

    const auto drift = document->read("navigation.drift");
    TEST_CHECK(drift.has_value());
    TEST_CHECK(std::get<double>(*drift) == 0.1);

    const auto focus = document->read("focus_follows_mouse");
    TEST_CHECK(focus.has_value());
    TEST_CHECK(std::get<bool>(*focus));

    const auto layout = document->read("input.keyboard.layout");
    TEST_CHECK(layout.has_value());
    TEST_CHECK(std::get<std::string>(*layout) == "halmak");

    const auto rate = document->read("input.keyboard.repeat_rate");
    TEST_CHECK(rate.has_value());
    TEST_CHECK(std::get<std::int64_t>(*rate) == 20);

    const auto autostart = document->read("autostart");
    TEST_CHECK(autostart.has_value());
    TEST_CHECK(std::get<std::vector<std::string>>(*autostart).size() == 2);
    TEST_CHECK(std::get<std::vector<std::string>>(*autostart)[1] == "driftmap --snap 8");

    // A multi-line value is present but never editable: rewriting it would
    // truncate the user's anchors.
    TEST_CHECK(document->has("navigation.anchors.origin"));
    TEST_CHECK(!document->isEditable("navigation.anchors.origin"));
    TEST_CHECK(!document->read("navigation.anchors.origin").has_value());

    // Quoted binding keys and array-of-tables entries are not curated fields.
    TEST_CHECK(!document->has("keybindings.mod+d"));
    TEST_CHECK(document->hasTable("outputs"));
  }

  void testAssignPreservesEverythingElse() {
    auto document = DriftwmConfigDocument::parse(std::string(kSampleConfig));
    TEST_CHECK(document.has_value());

    // Existing float with a trailing comment: only the value token changes.
    TEST_CHECK(document->assign("navigation.drift", DriftwmScalar{0.75}));
    const std::string afterDrift = document->text();
    TEST_CHECK(afterDrift.find("drift = 0.75    # momentum coast") != std::string::npos);
    // The commented-out default line above it is untouched.
    TEST_CHECK(afterDrift.find("# Multiplier per keypress (1.1 = 10% per press)") != std::string::npos);
    // Uncurated sections survive verbatim.
    TEST_CHECK(afterDrift.find("\"mod+d\" = \"spawn noctalia msg panel-open launcher\"") != std::string::npos);
    TEST_CHECK(afterDrift.find("[[outputs]]") != std::string::npos);
    TEST_CHECK(afterDrift.find("  [-1750, 1750],") != std::string::npos);

    // A single-quoted string keeps its quoting style.
    TEST_CHECK(document->assign("input.keyboard.layout", DriftwmScalar{std::string("de")}));
    TEST_CHECK(document->text().find("layout = 'de'   # single-quoted on purpose") != std::string::npos);

    // A whole float keeps its decimal point so serde still reads an f32 field.
    TEST_CHECK(document->assign("zoom.step", DriftwmScalar{2.0}));
    TEST_CHECK(document->text().find("step = 2.0\n") != std::string::npos);

    TEST_CHECK(std::get<double>(*document->read("zoom.step")) == 2.0);
  }

  void testAssignInsertsIntoExistingTable() {
    auto document = DriftwmConfigDocument::parse(std::string(kSampleConfig));
    TEST_CHECK(document.has_value());
    TEST_CHECK(!document->has("navigation.drift_missing"));

    TEST_CHECK(document->assign("navigation.camera_speed", DriftwmScalar{0.4}));
    const std::string text = document->text();
    // The key lands at the end of the [navigation] block, before the next header.
    const std::size_t inserted = text.find("camera_speed = 0.4");
    TEST_CHECK(inserted != std::string::npos);
    const std::size_t nextHeader = text.find("[input.keyboard]");
    TEST_CHECK(inserted < nextHeader);
    // Separated from the block above it.
    TEST_CHECK(text.find("\n\ncamera_speed = 0.4") != std::string::npos);
    // And the block's own keys are all still inside it.
    TEST_CHECK(text.find("pan_step = 100.0") < inserted);
    TEST_CHECK(std::get<double>(*document->read("navigation.camera_speed")) == 0.4);
  }

  void testAssignInsertsTopLevelKeyBeforeFirstHeader() {
    auto document = DriftwmConfigDocument::parse(std::string(kSampleConfig));
    TEST_CHECK(document.has_value());

    TEST_CHECK(document->assign("mod_key", DriftwmScalar{std::string("alt")}));
    const std::string text = document->text();
    const std::size_t modKey = text.find("mod_key = \"alt\"");
    TEST_CHECK(modKey != std::string::npos);
    // Before the first table header, in the root table.
    TEST_CHECK(modKey < text.find("[env]"));
    TEST_CHECK(text.find("autostart = ") < modKey);
    TEST_CHECK(std::get<std::string>(*document->read("mod_key")) == "alt");
  }

  void testAssignCreatesMissingTable() {
    auto document = DriftwmConfigDocument::parse(std::string(kSampleConfig));
    TEST_CHECK(document.has_value());
    TEST_CHECK(!document->hasTable("effects"));

    TEST_CHECK(document->assign("effects.blur_radius", DriftwmScalar{std::int64_t{4}}));
    const std::string text = document->text();
    TEST_CHECK(text.find("[effects]\nblur_radius = 4\n") != std::string::npos);
    TEST_CHECK(std::get<std::int64_t>(*document->read("effects.blur_radius")) == 4);
    // The new block is appended, so nothing above it moved.
    TEST_CHECK(text.find("[[outputs]]") < text.find("[effects]"));
  }

  void testAssignRefusesMultiLineValue() {
    auto document = DriftwmConfigDocument::parse(std::string(kSampleConfig));
    TEST_CHECK(document.has_value());
    const std::string before = document->text();
    TEST_CHECK(!document->assign("navigation.anchors.origin", DriftwmScalar{std::int64_t{1}}));
    TEST_CHECK(document->text() == before);
  }

  void testStringListRoundTrip() {
    auto document = DriftwmConfigDocument::parse(std::string(kSampleConfig));
    TEST_CHECK(document.has_value());
    TEST_CHECK(document->assign("autostart", DriftwmScalar{std::vector<std::string>{"a", "b c", "echo \"hi\""}}));
    TEST_CHECK(document->text().find("autostart = [\"a\", \"b c\", \"echo \\\"hi\\\"\"]") != std::string::npos);
    const auto parsed = document->read("autostart");
    TEST_CHECK(parsed.has_value());
    TEST_CHECK(std::get<std::vector<std::string>>(*parsed).size() == 3);
    TEST_CHECK(std::get<std::vector<std::string>>(*parsed)[2] == "echo \"hi\"");
  }

  void testNoOpAssignLeavesTextUntouched() {
    auto document = DriftwmConfigDocument::parse(std::string(kSampleConfig));
    TEST_CHECK(document.has_value());
    const std::string before = document->text();
    TEST_CHECK(document->assign("navigation.drift", DriftwmScalar{0.1}));
    TEST_CHECK(document->text() == before);
  }

  void testRenderAndParseRoundTrip() {
    TEST_CHECK(renderScalarLiteral(DriftwmScalar{true}) == "true");
    TEST_CHECK(renderScalarLiteral(DriftwmScalar{std::int64_t{-7}}) == "-7");
    TEST_CHECK(renderScalarLiteral(DriftwmScalar{12.0}) == "12.0");
    TEST_CHECK(renderScalarLiteral(DriftwmScalar{0.1}) == "0.1");
    TEST_CHECK(renderScalarLiteral(DriftwmScalar{std::string("de")}) == "\"de\"");
    TEST_CHECK(renderScalarLiteral(DriftwmScalar{std::string("de")}, true) == "'de'");
    // A quote inside a value forces the basic form rather than breaking the line.
    TEST_CHECK(renderScalarLiteral(DriftwmScalar{std::string("it's")}, true) == "\"it's\"");
    TEST_CHECK(renderScalarLiteral(DriftwmScalar{std::vector<std::string>{}}) == "[]");

    const auto parsed = parseScalarLiteral("12.0");
    TEST_CHECK(parsed.has_value() && std::get<double>(*parsed) == 12.0);
    const auto escaped = parseScalarLiteral("\"a\\nb\"");
    TEST_CHECK(escaped.has_value() && std::get<std::string>(*escaped) == "a\nb");
    // Unsupported shapes are reported as absent, never guessed at.
    TEST_CHECK(!parseScalarLiteral("[[0, 0], [1, 1]]").has_value());
    TEST_CHECK(!parseScalarLiteral("{ a = 1 }").has_value());
  }

  // ---- curated field catalog ------------------------------------------------

  void testFieldCatalogIsWellFormed() {
    std::vector<std::string> keyPaths;
    std::vector<std::string> ids;
    for (const auto& field : driftwmConfigFields()) {
      TEST_CHECK(!field.id.empty());
      TEST_CHECK(!field.keyPath.empty());
      TEST_CHECK(!field.label.empty());
      TEST_CHECK(!field.groupId.empty());
      TEST_CHECK(std::ranges::find(driftwmConfigFieldGroups(), field.groupId, &DriftwmFieldGroup::id)
                 != driftwmConfigFieldGroups().end());
      ids.emplace_back(field.id);
      keyPaths.emplace_back(field.keyPath);

      const auto defaults = driftwmFieldDefaultValue(field);
      TEST_CHECK(defaults.has_value());
      // driftwmFieldValueError is the same check the settings tab runs before it
      // writes, so a catalogued default that its own bounds reject would make the
      // row permanently unsavable.
      TEST_CHECK(driftwmFieldValueError(field, *defaults).empty());

      if (field.kind == DriftwmFieldKind::Choice) {
        TEST_CHECK(!driftwmFieldOptions(field).empty());
      }
    }
    std::ranges::sort(ids);
    TEST_CHECK(std::ranges::adjacent_find(ids) == ids.end());
    std::ranges::sort(keyPaths);
    TEST_CHECK(std::ranges::adjacent_find(keyPaths) == keyPaths.end());
  }

  void testFieldValueErrors() {
    const auto* drift = findDriftwmFieldById("navigation.drift");
    TEST_CHECK(drift != nullptr);
    TEST_CHECK(drift->keyPath == "navigation.drift");
    TEST_CHECK(findDriftwmFieldByKeyPath("navigation.drift") == drift);
    TEST_CHECK(driftwmFieldValueError(*drift, DriftwmScalar{0.25}).empty());
    TEST_CHECK(!driftwmFieldValueError(*drift, DriftwmScalar{5.0}).empty());
    // An integer literal must never land in a float field, and the message has to
    // name the kind driftwm expects, not the kind that was offered.
    const std::string kindError = driftwmFieldValueError(*drift, DriftwmScalar{std::int64_t{1}});
    TEST_CHECK(!kindError.empty());
    TEST_CHECK(kindError.find("Float") != std::string::npos);
    TEST_CHECK(kindError.find("navigation.drift") != std::string::npos);

    const auto* mode = findDriftwmFieldById("decorations.default_mode");
    TEST_CHECK(mode != nullptr);
    TEST_CHECK(driftwmFieldValueError(*mode, DriftwmScalar{std::string("minimal")}).empty());
    TEST_CHECK(!driftwmFieldValueError(*mode, DriftwmScalar{std::string("server")}).empty());

    // Every requested group is represented.
    for (const char* group : {"general", "navigation", "zoom", "snap", "decorations", "effects", "background",
                              "input", "cursor", "mouse", "session", "xwayland", "autostart"}) {
      TEST_CHECK(!driftwmConfigFieldsInGroup(group).empty());
    }
  }

  // A config.toml is hand-editable, so a curated key can hold a valid TOML literal
  // of the wrong shape. The document must read it and the tab must be able to
  // render it: a std::get on the raw variant would throw straight out of the render.
  void testCoercesHandEditedValueShapes() {
    constexpr std::string_view kWrongShapes = R"(
[snap]
gap = 12
enabled = 1

[input.keyboard]
repeat_rate = 20.0
layout = 5

[cursor]
theme = true
size = 24.0

[background]
type = ["shader"]
)";
    auto document = DriftwmConfigDocument::parse(std::string(kWrongShapes));
    TEST_CHECK(document.has_value());

    // The document itself reports the literal's own shape, not the field's.
    const auto gap = document->read("snap.gap");
    TEST_CHECK(gap.has_value() && std::holds_alternative<std::int64_t>(*gap));

    // An integer where the field is a float, and the reverse: both are unambiguous
    // and coerce cleanly.
    const auto* gapField = findDriftwmFieldById("snap.gap");
    const auto* rateField = findDriftwmFieldById("keyboard.repeat_rate");
    TEST_CHECK(gapField != nullptr && rateField != nullptr);

    DriftwmScalar coerced;
    TEST_CHECK(coerceScalarToField(*document->read("snap.gap"), *gapField, coerced));
    TEST_CHECK(std::get<double>(coerced) == 12.0);
    TEST_CHECK(document->assign("snap.gap", coerced));
    TEST_CHECK(document->text().find("gap = 12.0") != std::string::npos);

    TEST_CHECK(coerceScalarToField(*document->read("input.keyboard.repeat_rate"), *rateField, coerced));
    TEST_CHECK(std::get<std::int64_t>(coerced) == 20);

    // A fractional float is not an integer: refusing it keeps 20.5 from becoming 20.
    TEST_CHECK(!coerceScalarToField(DriftwmScalar{20.5}, *rateField, coerced));
    // Genuinely wrong kinds are refused, so the caller falls back to the default
    // rather than displaying a number where driftwm wants a string.
    const auto* layoutField = findDriftwmFieldById("keyboard.layout");
    const auto* themeField = findDriftwmFieldById("cursor.theme");
    const auto* typeField = findDriftwmFieldById("background.type");
    TEST_CHECK(!coerceScalarToField(DriftwmScalar{std::int64_t{5}}, *layoutField, coerced));
    TEST_CHECK(!coerceScalarToField(DriftwmScalar{true}, *themeField, coerced));
    TEST_CHECK(!coerceScalarToField(*document->read("background.type"), *typeField, coerced));
    // A bool field will not swallow an integer either.
    const auto* enabledField = findDriftwmFieldById("snap.enabled");
    TEST_CHECK(!coerceScalarToField(*document->read("snap.enabled"), *enabledField, coerced));
    // A whole float in an integer field is fine, as shown above.
    TEST_CHECK(coerceScalarToField(*document->read("cursor.size"), *findDriftwmFieldById("cursor.size"), coerced));
  }

  // ---- path resolution ------------------------------------------------------

  void testPathResolutionOrder() {
    const auto env = [](std::initializer_list<std::pair<const char*, const char*>> vars) {
      return [vars](const char* name) -> std::string {
        for (const auto& [key, value] : vars) {
          if (std::string_view(key) == name) {
            return value == nullptr ? std::string() : std::string(value);
          }
        }
        return {};
      };
    };

    TEST_CHECK(
        DriftwmConfigService::resolveConfigPathFrom(
            env({{"DRIFTWM_CONFIG", "/etc/driftwm/custom.toml"}, {"XDG_CONFIG_HOME", "/xdg"}, {"HOME", "/home/u"}})
        )
        == std::filesystem::path("/etc/driftwm/custom.toml")
    );
    TEST_CHECK(
        DriftwmConfigService::resolveConfigPathFrom(env({{"XDG_CONFIG_HOME", "/xdg"}, {"HOME", "/home/u"}}))
        == std::filesystem::path("/xdg/driftwm/config.toml")
    );
    TEST_CHECK(
        DriftwmConfigService::resolveConfigPathFrom(env({{"HOME", "/home/u"}}))
        == std::filesystem::path("/home/u/.config/driftwm/config.toml")
    );
    TEST_CHECK(DriftwmConfigService::resolveConfigPathFrom(env({})).empty());

    // A leading ~ expands the same way driftwm expands it, so both read one file.
    ::setenv("HOME", "/home/u", 1);
    ::setenv("XDG_CONFIG_HOME", "/xdg", 1);
    ::setenv("DRIFTWM_CONFIG", "~/drift.toml", 1);
    TEST_CHECK(DriftwmConfigService::resolveConfigPath() == std::filesystem::path("/home/u/drift.toml"));
    ::unsetenv("DRIFTWM_CONFIG");
    TEST_CHECK(DriftwmConfigService::resolveConfigPath() == std::filesystem::path("/xdg/driftwm/config.toml"));
    ::unsetenv("XDG_CONFIG_HOME");
    ::unsetenv("HOME");
    TEST_CHECK(DriftwmConfigService::resolveConfigPath().empty());
  }

  // ---- validation gate and apply pipeline -----------------------------------

  void testApplyRejectsWhatTheGateRejects() {
    const auto dir = makeTempDir();
    const auto configPath = dir / "config.toml";
    writeFile(configPath, kSampleConfig);
    const ScopedConfigPath scopedPath("DRIFTWM_CONFIG", configPath.string());
    const auto deadSocket = pointRuntimeAtDeadSocket();

    FileWatcher watcher;
    DriftwmRuntime runtime;
    DriftwmConfigService service(runtime, watcher);
    service.setValidator([](const std::filesystem::path& candidate, std::string* error) {
      // Stands in for `driftwm --check-config`: reads what was staged.
      TEST_CHECK(std::filesystem::exists(candidate));
      if (error != nullptr) {
        *error = "staged candidate rejected";
      }
      return false;
    });

    const auto rejected = service.apply("navigation.drift", DriftwmScalar{0.9});
    TEST_CHECK(rejected.status == DriftwmConfigService::ApplyResult::Status::ValidationFailed);
    TEST_CHECK(rejected.message == "staged candidate rejected");
    // The live file is byte-for-byte what it was: a rejected candidate never lands.
    TEST_CHECK(readFile(configPath) == std::string(kSampleConfig));
    // The candidate was cleaned up.
    TEST_CHECK(!std::filesystem::exists(configPath.string() + ".noctalia-validate"));

    // The same edit passes once the gate accepts it.
    service.setValidator([](const std::filesystem::path&, std::string*) { return true; });
    const auto applied = service.apply("navigation.drift", DriftwmScalar{0.9});
    TEST_CHECK(applied.status == DriftwmConfigService::ApplyResult::Status::Applied);
    // The socket is unreachable, so the write lands but the reload is reported as
    // not acknowledged rather than silently assumed.
    TEST_CHECK(!applied.reloaded);
    const std::string after = readFile(configPath);
    TEST_CHECK(after.find("drift = 0.9    # momentum coast") != std::string::npos);
    TEST_CHECK(after.find("\"mod+t\" = \"exec ghostty\"") != std::string::npos);

    // Writing the value that is already on disk is a no-op, not a rewrite.
    const auto before = readFile(configPath);
    const auto again = service.apply("navigation.drift", DriftwmScalar{0.9});
    TEST_CHECK(again.status == DriftwmConfigService::ApplyResult::Status::Applied);
    TEST_CHECK(readFile(configPath) == before);

    std::filesystem::remove_all(dir);
  }

  void testApplyRejectsUnparseableFile() {
    const auto dir = makeTempDir();
    const auto configPath = dir / "config.toml";
    writeFile(configPath, "[navigation\ndrift = 0.1\n");
    const ScopedConfigPath scopedPath("DRIFTWM_CONFIG", configPath.string());
    const auto deadSocket = pointRuntimeAtDeadSocket();

    FileWatcher watcher;
    DriftwmRuntime runtime;
    DriftwmConfigService service(runtime, watcher);
    service.setValidator([](const std::filesystem::path&, std::string*) { return true; });

    const auto result = service.apply("navigation.drift", DriftwmScalar{0.5});
    TEST_CHECK(result.status == DriftwmConfigService::ApplyResult::Status::InvalidValue);
    TEST_CHECK(readFile(configPath) == "[navigation\ndrift = 0.1\n");

    std::filesystem::remove_all(dir);
  }

  void testApplyReportsMissingConfig() {
    const auto dir = makeTempDir();
    const ScopedConfigPath scopedPath("DRIFTWM_CONFIG", (dir / "absent.toml").string());

    FileWatcher watcher;
    DriftwmRuntime runtime;
    DriftwmConfigService service(runtime, watcher);
    TEST_CHECK(!service.available());

    const auto result = service.apply("navigation.drift", DriftwmScalar{0.5});
    TEST_CHECK(result.status == DriftwmConfigService::ApplyResult::Status::NoConfigFile);

    std::filesystem::remove_all(dir);
  }

  // ---- the real driftwm validator -------------------------------------------

  // Exercises the production gate: a candidate is staged and handed to
  // `driftwm --check-config`, so these only run where the binary is installed.
  void testRealValidatorAcceptsACuratedEdit() {
    if (!process::commandExists("driftwm")) {
      return;
    }
    const auto dir = makeTempDir();
    const auto configPath = dir / "config.toml";
    writeFile(configPath, kRealisticConfig);
    const ScopedConfigPath scopedPath("DRIFTWM_CONFIG", configPath.string());
    const auto deadSocket = pointRuntimeAtDeadSocket();

    FileWatcher watcher;
    DriftwmRuntime runtime;
    // No stub validator: this service runs the real gate.
    DriftwmConfigService service(runtime, watcher);
    TEST_CHECK(service.validatorAvailable());

    const auto result = service.apply("navigation.drift", DriftwmScalar{0.4});
    TEST_CHECK(result.status == DriftwmConfigService::ApplyResult::Status::Applied);
    TEST_CHECK(readFile(configPath).find("drift = 0.4    # momentum coast") != std::string::npos);
    TEST_CHECK(!std::filesystem::exists(configPath.string() + ".noctalia-validate"));

    std::filesystem::remove_all(dir);
  }

  void testRealValidatorRejectsAKeyDriftwmDoesNotKnow() {
    if (!process::commandExists("driftwm")) {
      return;
    }
    const auto dir = makeTempDir();
    const auto configPath = dir / "config.toml";
    writeFile(configPath, kRealisticConfig);
    const ScopedConfigPath scopedPath("DRIFTWM_CONFIG", configPath.string());
    const auto deadSocket = pointRuntimeAtDeadSocket();

    FileWatcher watcher;
    DriftwmRuntime runtime;
    DriftwmConfigService service(runtime, watcher);

    // Well-formed TOML, in the file's own style, and driftwm still refuses it:
    // the candidate never replaces the live config.
    const auto result = service.apply("navigation.definitely_not_a_key", DriftwmScalar{1.0});
    TEST_CHECK(result.status == DriftwmConfigService::ApplyResult::Status::ValidationFailed);
    TEST_CHECK(result.message.find("definitely_not_a_key") != std::string::npos);
    TEST_CHECK(readFile(configPath) == std::string(kRealisticConfig));

    std::filesystem::remove_all(dir);
  }

  // Every catalogued key path and value shape, checked against driftwm's own
  // schema. A typo in a key path passes every unit test above and is rejected the
  // first time a user touches that row, so it is worth one subprocess.
  void testRealValidatorAcceptsEveryCuratedKey() {
    if (!process::commandExists("driftwm")) {
      return;
    }
    const auto dir = makeTempDir();
    const auto configPath = dir / "config.toml";
    const ScopedConfigPath scopedPath("DRIFTWM_CONFIG", configPath.string());
    const auto deadSocket = pointRuntimeAtDeadSocket();

    // A document holding every field at its documented default, i.e. the file a
    // fully populated settings tab would produce. One validator run covers them all.
    std::string text = "# driftwm config\n";
    for (const auto& field : compositors::driftwm::driftwmConfigFields()) {
      const auto value = compositors::driftwm::driftwmFieldDefaultValue(field);
      TEST_CHECK(value.has_value());
      TEST_CHECK(driftwmFieldValueError(field, *value).empty());
      auto document = DriftwmConfigDocument::parse(text);
      TEST_CHECK(document.has_value());
      TEST_CHECK(document->assign(field.keyPath, *value));
      text = document->text();
    }
    writeFile(configPath, text);

    FileWatcher watcher;
    DriftwmRuntime runtime;
    DriftwmConfigService service(runtime, watcher);

    std::string error;
    const bool accepted = service.validateCandidate(configPath, &error);
    if (!accepted) {
      std::println(stderr, "driftwm rejected the curated field set:\n{}\n--- file ---\n{}", error, text);
    }
    TEST_CHECK(accepted);

    std::filesystem::remove_all(dir);
  }

  // ---- external edits -------------------------------------------------------

  void testExternalEditIsReportedAndOwnWriteIsNot() {
    const auto dir = makeTempDir();
    const auto configPath = dir / "config.toml";
    writeFile(configPath, kSampleConfig);
    const ScopedConfigPath scopedPath("DRIFTWM_CONFIG", configPath.string());
    const auto deadSocket = pointRuntimeAtDeadSocket();

    FileWatcher watcher;
    DriftwmRuntime runtime;
    DriftwmConfigService service(runtime, watcher);
    service.setValidator([](const std::filesystem::path&, std::string*) { return true; });

    std::atomic<int> changes{0};
    service.setExternalChangeCallback([&changes]() { ++changes; });

    // noctalia's own write must not come back as an external edit.
    TEST_CHECK(service.apply("navigation.drift", DriftwmScalar{0.6}).status
               == DriftwmConfigService::ApplyResult::Status::Applied);
    watcher.dispatch();
    TEST_CHECK(changes.load() == 0);

    // Someone else editing the file must be reported, so the tab can re-read.
    waitPastWatcherDebounce();
    writeFile(configPath, "# edited by hand\nautostart = []\n");
    bool fired = false;
    for (int attempt = 0; attempt < 200 && !fired; ++attempt) {
      watcher.dispatch();
      fired = changes.load() > 0;
      if (!fired) {
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
      }
    }
    TEST_CHECK(fired);
    // The re-read document reflects the hand edit, not the cached copy.
    const auto document = service.loadDocument();
    TEST_CHECK(document.has_value());
    TEST_CHECK(std::get<std::vector<std::string>>(*document->read("autostart")).empty());
    TEST_CHECK(!document->has("navigation.drift"));

    std::filesystem::remove_all(dir);
  }

} // namespace

int main() {
  testParseRejectsInvalidToml();
  testReadsScalars();
  testAssignPreservesEverythingElse();
  testAssignInsertsIntoExistingTable();
  testAssignInsertsTopLevelKeyBeforeFirstHeader();
  testAssignCreatesMissingTable();
  testAssignRefusesMultiLineValue();
  testStringListRoundTrip();
  testNoOpAssignLeavesTextUntouched();
  testRenderAndParseRoundTrip();
  testFieldCatalogIsWellFormed();
  testFieldValueErrors();
  testCoercesHandEditedValueShapes();
  testPathResolutionOrder();
  testApplyRejectsWhatTheGateRejects();
  testApplyRejectsUnparseableFile();
  testApplyReportsMissingConfig();
  testRealValidatorAcceptsACuratedEdit();
  testRealValidatorRejectsAKeyDriftwmDoesNotKnow();
  testRealValidatorAcceptsEveryCuratedKey();
  testExternalEditIsReportedAndOwnWriteIsNot();
  return 0;
}
