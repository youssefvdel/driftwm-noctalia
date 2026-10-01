#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace compositors::driftwm {

  // A config.toml value exactly as driftwm's own parser expects it. The variant
  // order is the kind order: Boolean, Integer, Float, Text, StringList.
  using DriftwmScalar = std::variant<bool, std::int64_t, double, std::string, std::vector<std::string>>;

  // Renders a value as a TOML literal. Integral doubles keep a decimal point so
  // serde still reads them as floats. `singleQuoted` only affects Text values.
  [[nodiscard]] std::string renderScalarLiteral(const DriftwmScalar& value, bool singleQuoted = false);

  // Parses a single-line TOML literal back into a value. Returns nullopt for
  // anything the curated fields do not cover (inline tables, multi-line values).
  [[nodiscard]] std::optional<DriftwmScalar> parseScalarLiteral(std::string_view literal);

  // Validates a whole document as TOML. toml++ is used here and nowhere else:
  // reading and writing go through the line-preserving model below.
  [[nodiscard]] bool validateTomlText(std::string_view text, std::string* error = nullptr);

  // Line-preserving view of driftwm's config.toml.
  //
  // driftwm's reference config is comment-heavy and hand-edited, so this never
  // re-serializes: an edit replaces the bytes of a single value token, and a new
  // key is appended to the end of its table block. Comments, blank lines, key
  // order, unknown keys, quoted binding keys and array-of-tables sections all
  // survive untouched.
  class DriftwmConfigDocument {
  public:
    // Parses and validates `text`. Returns nullopt with a human-readable reason
    // when the text is not valid TOML — the file is then never edited, so a
    // hand-made syntax error cannot be compounded.
    [[nodiscard]] static std::optional<DriftwmConfigDocument> parse(std::string text, std::string* error = nullptr);

    [[nodiscard]] const std::string& text() const noexcept { return m_text; }

    // Value of a dotted key path, or nullopt when the key is absent, spans
    // several lines, or holds a value the curated fields do not cover.
    [[nodiscard]] std::optional<DriftwmScalar> read(std::string_view keyPath) const;
    [[nodiscard]] bool has(std::string_view keyPath) const;
    [[nodiscard]] bool hasTable(std::string_view tablePath) const;
    // True when the key exists and is a single-line value the editor can rewrite.
    [[nodiscard]] bool isEditable(std::string_view keyPath) const;

    // Rewrites one value, or adds the key to its table (appending a new table when
    // the file has none). False only when the key exists as a multi-line or
    // unsupported value, which is left alone rather than truncated.
    bool assign(std::string_view keyPath, const DriftwmScalar& value);

  private:
    // Byte range of one value token inside `m_text`, plus how it was quoted so a
    // rewrite keeps the file's existing style.
    struct KeyOccurrence {
      std::size_t valueStart = 0;
      std::size_t valueEnd = 0;
      bool singleQuoted = false;
      bool singleLine = true;
    };

    struct TableSpan {
      std::size_t headerLine = 0;
      // Line index the block ends at: the next header, or the end of the file.
      std::size_t endLine = 0;
    };

    void scan();
    void insertKey(std::string_view tablePath, std::string_view key, std::string_view literal);
    void insertTableBlock(std::string_view tablePath, std::string_view key, std::string_view literal);
    [[nodiscard]] static std::string_view tableOf(std::string_view keyPath);

    std::string m_text;
    std::unordered_map<std::string, KeyOccurrence> m_keys;
    std::unordered_map<std::string, TableSpan> m_tables;
  };

} // namespace compositors::driftwm
