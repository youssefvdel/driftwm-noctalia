#include "compositors/driftwm/driftwm_config_document.h"

#include "core/toml.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

namespace compositors::driftwm {

  namespace {

    [[nodiscard]] bool isSpace(char c) { return c == ' ' || c == '\t'; }

    // Byte range of one line, excluding its line terminator. A file ending in a
    // newline yields no trailing empty line, so a key appended at the end lands
    // after the last real line rather than after a phantom one.
    struct LineRange {
      std::size_t begin = 0;
      std::size_t end = 0;
    };

    [[nodiscard]] std::vector<LineRange> splitLines(const std::string& text) {
      std::vector<LineRange> lines;
      std::size_t begin = 0;
      while (begin < text.size()) {
        const std::size_t newline = text.find('\n', begin);
        if (newline == std::string::npos) {
          lines.push_back({begin, text.size()});
          break;
        }
        std::size_t end = newline;
        if (end > begin && text[end - 1] == '\r') {
          --end;
        }
        lines.push_back({begin, end});
        begin = newline + 1;
      }
      return lines;
    }

    // A cursor over one line. The scan is deliberately line-based: anything that
    // spans lines is consumed by scanRemainder/scanValueToken so its contents can
    // never be mistaken for a table header or a key.
    class LineScanner {
    public:
      LineScanner(std::string_view line, std::size_t offset) : m_line(line), m_offset(offset) {}

      void skipSpaces() {
        while (m_offset < m_line.size() && isSpace(m_line[m_offset])) {
          ++m_offset;
        }
      }

      [[nodiscard]] std::size_t offset() const noexcept { return m_offset; }
      [[nodiscard]] bool atEnd() const noexcept { return m_offset >= m_line.size(); }
      [[nodiscard]] char peek() const noexcept { return m_offset < m_line.size() ? m_line[m_offset] : '\0'; }
      void advance() noexcept { ++m_offset; }

      // One bare or quoted token. Quoting is reported so a rewrite of a string
      // value can keep the file's existing style.
      [[nodiscard]] std::string readToken(bool& singleQuoted, bool& isQuoted) {
        singleQuoted = false;
        isQuoted = false;
        if (atEnd()) {
          return {};
        }
        const char c = m_line[m_offset];
        if (c == '"' || c == '\'') {
          isQuoted = true;
          singleQuoted = c == '\'';
          return readQuoted();
        }
        const std::size_t start = m_offset;
        // Brackets terminate a bare token: a table header's closing bracket is
        // not part of the name, and a name never contains one unquoted.
        while (m_offset < m_line.size() && !isSpace(m_line[m_offset]) && m_line[m_offset] != '#'
               && m_line[m_offset] != '=' && m_line[m_offset] != '.' && m_line[m_offset] != ','
               && m_line[m_offset] != '[' && m_line[m_offset] != ']' && m_line[m_offset] != '{'
               && m_line[m_offset] != '}') {
          ++m_offset;
        }
        return std::string(m_line.substr(start, m_offset - start));
      }

    private:
      [[nodiscard]] std::string readQuoted() {
        const char quote = m_line[m_offset];
        std::string out;
        ++m_offset;
        while (m_offset < m_line.size()) {
          const char c = m_line[m_offset];
          if (c == '\\' && quote == '"' && m_offset + 1 < m_line.size()) {
            out.push_back(c);
            out.push_back(m_line[m_offset + 1]);
            m_offset += 2;
            continue;
          }
          ++m_offset;
          if (c == quote) {
            return out;
          }
          out.push_back(c);
        }
        return out;
      }

      std::string_view m_line;
      std::size_t m_offset;
    };

    // Dotted table or key name: bare and quoted segments joined by dots.
    [[nodiscard]] std::optional<std::string> readDottedName(LineScanner& scanner) {
      std::string name;
      while (true) {
        scanner.skipSpaces();
        if (scanner.atEnd() || scanner.peek() == '#') {
          return name.empty() ? std::nullopt : std::optional<std::string>(name);
        }
        bool segmentSingleQuoted = false;
        bool isQuoted = false;
        const std::string segment = scanner.readToken(segmentSingleQuoted, isQuoted);
        (void)isQuoted;
        (void)segmentSingleQuoted;
        if (segment.empty()) {
          return name.empty() ? std::nullopt : std::optional<std::string>(name);
        }
        if (!name.empty()) {
          name.push_back('.');
        }
        name += segment;
        scanner.skipSpaces();
        if (!scanner.atEnd() && scanner.peek() == '.') {
          scanner.advance();
          continue;
        }
        return name;
      }
    }

    // Walks the rest of a line tracking quote state and bracket depth, so the
    // caller learns whether the construct continues onto the next line.
    void scanRemainder(std::string_view line, std::size_t offset, int& depth, std::string& triple) {
      char quote = '\0';
      while (offset < line.size()) {
        const char c = line[offset];
        if (triple.empty() && (c == '"' || c == '\'') && offset + 2 < line.size() && line[offset + 1] == c
            && line[offset + 2] == c) {
          triple = std::string(3, c);
          offset += 3;
          continue;
        }
        if (!triple.empty()) {
          if (c == triple[0] && offset + 2 < line.size() && line[offset + 1] == triple[0]
              && line[offset + 2] == triple[0]) {
            triple.clear();
            offset += 3;
            continue;
          }
          ++offset;
          continue;
        }
        if (quote != '\0') {
          if (c == '\\' && quote == '"') {
            offset += 2;
            continue;
          }
          if (c == quote) {
            quote = '\0';
          }
          ++offset;
          continue;
        }
        if (c == '"' || c == '\'') {
          quote = c;
          ++offset;
          continue;
        }
        if (c == '#') {
          return;
        }
        if (c == '[' || c == '{') {
          ++depth;
        } else if (c == ']' || c == '}') {
          --depth;
        }
        ++offset;
      }
    }

    // End of the value token that starts at `offset`, as a line-relative index.
    // Only the token is ever replaced, so whatever followed it on the line (a
    // trailing comment, say) is preserved by construction. `singleQuoted` reports
    // the value's own quoting style, not the key's, so a rewrite keeps it.
    [[nodiscard]] std::size_t
    scanValueToken(std::string_view line, std::size_t offset, int& depth, std::string& triple, bool& singleQuoted) {
      while (offset < line.size() && isSpace(line[offset])) {
        ++offset;
      }
      const std::size_t start = offset;
      singleQuoted = false;
      if (offset < line.size() && (line[offset] == '"' || line[offset] == '\'')) {
        const char quote = line[offset];
        if (offset + 2 < line.size() && line[offset + 1] == quote && line[offset + 2] == quote) {
          triple = std::string(3, quote);
          scanRemainder(line, start, depth, triple);
          return line.size();
        }
        singleQuoted = quote == '\'';
        ++offset;
        while (offset < line.size()) {
          const char c = line[offset];
          if (c == '\\' && quote == '"' && offset + 1 < line.size()) {
            offset += 2;
            continue;
          }
          ++offset;
          if (c == quote) {
            break;
          }
        }
        return offset;
      }
      if (offset < line.size() && (line[offset] == '[' || line[offset] == '{')) {
        scanRemainder(line, start, depth, triple);
        return line.size();
      }
      while (offset < line.size() && !isSpace(line[offset]) && line[offset] != '#' && line[offset] != ','
             && line[offset] != ']' && line[offset] != '}') {
        ++offset;
      }
      return offset;
    }

    void appendUtf8(std::string& out, std::uint32_t codePoint) {
      if (codePoint <= 0x7FU) {
        out.push_back(static_cast<char>(codePoint));
      } else if (codePoint <= 0x7FFU) {
        out.push_back(static_cast<char>(0xC0U | (codePoint >> 6U)));
        out.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
      } else if (codePoint <= 0xFFFFU) {
        out.push_back(static_cast<char>(0xE0U | (codePoint >> 12U)));
        out.push_back(static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
      } else {
        out.push_back(static_cast<char>(0xF0U | (codePoint >> 18U)));
        out.push_back(static_cast<char>(0x80U | ((codePoint >> 12U) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
      }
    }

    [[nodiscard]] std::string escapeBasicString(std::string_view value) {
      std::string out;
      out.reserve(value.size() + 2);
      for (const char c : value) {
        switch (c) {
        case '"':
          out += "\\\"";
          break;
        case '\\':
          out += "\\\\";
          break;
        case '\n':
          out += "\\n";
          break;
        case '\r':
          out += "\\r";
          break;
        case '\t':
          out += "\\t";
          break;
        case '\b':
          out += "\\b";
          break;
        case '\f':
          out += "\\f";
          break;
        default: {
          const auto byte = static_cast<unsigned char>(c);
          if (byte < 0x20U) {
            out += std::format("\\u{:04X}", static_cast<unsigned>(byte));
          } else {
            out.push_back(c);
          }
        } break;
        }
      }
      return out;
    }

    // Decodes one backslash escape at `offset` (which points at the backslash).
    void appendDecodedEscape(std::string& out, std::string_view source, std::size_t& offset) {
      if (offset + 1 >= source.size()) {
        out.push_back('\\');
        return;
      }
      const char code = source[offset + 1];
      offset += 2;
      switch (code) {
      case 'n':
        out.push_back('\n');
        return;
      case 't':
        out.push_back('\t');
        return;
      case 'r':
        out.push_back('\r');
        return;
      case 'b':
        out.push_back('\b');
        return;
      case 'f':
        out.push_back('\f');
        return;
      case '"':
        out.push_back('"');
        return;
      case '\\':
        out.push_back('\\');
        return;
      case 'u':
      case 'U':
        break;
      default:
        out.push_back(code);
        return;
      }

      const std::size_t digits = code == 'u' ? 4U : 8U;
      if (offset + digits > source.size()) {
        out.push_back('\\');
        out.push_back(code);
        return;
      }
      std::uint32_t codePoint = 0;
      for (std::size_t i = 0; i < digits; ++i) {
        const char digit = source[offset + i];
        const unsigned value = digit >= '0' && digit <= '9'   ? static_cast<unsigned>(digit - '0')
                               : digit >= 'a' && digit <= 'f' ? static_cast<unsigned>(digit - 'a' + 10)
                               : digit >= 'A' && digit <= 'F' ? static_cast<unsigned>(digit - 'A' + 10)
                                                             : 16U;
        if (value == 16U) {
          out.push_back('\\');
          out.push_back(code);
          return;
        }
        codePoint = codePoint * 16U + value;
      }
      offset += digits;
      appendUtf8(out, codePoint);
    }

    // Decodes a string body with the surrounding quotes already removed.
    [[nodiscard]] std::string decodeQuoted(std::string_view body, bool basic) {
      if (!basic) {
        return std::string(body);
      }
      std::string out;
      out.reserve(body.size());
      std::size_t offset = 0;
      while (offset < body.size()) {
        if (body[offset] == '\\' && offset + 1 < body.size()) {
          appendDecodedEscape(out, body, offset);
          continue;
        }
        out.push_back(body[offset]);
        ++offset;
      }
      return out;
    }

    [[nodiscard]] bool parseInt64(std::string_view literal, std::int64_t& out) {
      std::string cleaned;
      cleaned.reserve(literal.size());
      for (const char c : literal) {
        if (c != '_') {
          cleaned.push_back(c);
        }
      }
      if (cleaned.empty()) {
        return false;
      }
      const auto* first = cleaned.data();
      const auto* last = cleaned.data() + cleaned.size();
      const auto result = std::from_chars(first, last, out);
      return result.ec == std::errc{} && result.ptr == last;
    }

    [[nodiscard]] bool parseDouble(std::string_view literal, double& out) {
      std::string cleaned;
      cleaned.reserve(literal.size());
      for (const char c : literal) {
        if (c != '_') {
          cleaned.push_back(c);
        }
      }
      if (cleaned.empty()) {
        return false;
      }
      const auto* first = cleaned.data();
      const auto* last = cleaned.data() + cleaned.size();
      const auto result = std::from_chars(first, last, out);
      return result.ec == std::errc{} && result.ptr == last;
    }

    [[nodiscard]] bool isBareValueChar(char c) {
      return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '-' || c == '+' || c == '.';
    }

    // Splits a single-line array literal of quoted strings. Arrays holding anything
    // else are not curated fields and parse as nullopt.
    [[nodiscard]] std::optional<std::vector<std::string>> parseStringArray(std::string_view literal) {
      std::size_t offset = 0;
      while (offset < literal.size() && isSpace(literal[offset])) {
        ++offset;
      }
      if (offset >= literal.size() || literal[offset] != '[') {
        return std::nullopt;
      }
      ++offset;
      std::vector<std::string> items;
      while (true) {
        while (offset < literal.size() && (isSpace(literal[offset]) || literal[offset] == ',')) {
          ++offset;
        }
        if (offset >= literal.size()) {
          return std::nullopt;
        }
        if (literal[offset] == ']') {
          return items;
        }
        if (literal[offset] != '"' && literal[offset] != '\'') {
          return std::nullopt;
        }
        const char quote = literal[offset];
        const std::size_t bodyStart = ++offset;
        while (offset < literal.size() && literal[offset] != quote) {
          offset += literal[offset] == '\\' && quote == '"' ? 2U : 1U;
        }
        if (offset >= literal.size()) {
          return std::nullopt;
        }
        items.push_back(decodeQuoted(literal.substr(bodyStart, offset - bodyStart), quote == '"'));
        ++offset;
      }
    }

  } // namespace

  std::string renderScalarLiteral(const DriftwmScalar& value, bool singleQuoted) {
    return std::visit(
        [&](const auto& held) -> std::string {
          using Held = std::decay_t<decltype(held)>;
          if constexpr (std::is_same_v<Held, bool>) {
            return held ? "true" : "false";
          } else if constexpr (std::is_same_v<Held, std::int64_t>) {
            return std::to_string(held);
          } else if constexpr (std::is_same_v<Held, double>) {
            // TOML has no inf/nan literal. driftwm's numeric fields are finite, so
            // an unrepresentable value is rejected by the field bounds rather than
            // written as something serde would refuse.
            if (!std::isfinite(held)) {
              return "0.0";
            }
            char buffer[40];
            const auto result = std::to_chars(buffer, buffer + sizeof(buffer), held);
            std::string out(buffer, result.ptr);
            const bool hasMarker = out.find('.') != std::string::npos || out.find('e') != std::string::npos
              || out.find('E') != std::string::npos;
            // A whole double must still read back as a float: serde rejects an
            // integer literal for an f32 field.
            if (!hasMarker) {
              out += ".0";
            }
            return out;
          } else if constexpr (std::is_same_v<Held, std::string>) {
            // A literal string cannot hold an embedded quote, so fall back to the
            // basic form rather than write something unparseable.
            if (singleQuoted && held.find('\'') == std::string::npos) {
              return "'" + held + "'";
            }
            return "\"" + escapeBasicString(held) + "\"";
          } else {
            std::string out = "[";
            for (std::size_t i = 0; i < held.size(); ++i) {
              if (i > 0) {
                out += ", ";
              }
              out += '"' + escapeBasicString(held[i]) + '"';
            }
            out.push_back(']');
            return out;
          }
        },
        value
    );
  }

  std::optional<DriftwmScalar> parseScalarLiteral(std::string_view literal) {
    while (!literal.empty() && isSpace(literal.front())) {
      literal.remove_prefix(1);
    }
    while (!literal.empty() && isSpace(literal.back())) {
      literal.remove_suffix(1);
    }
    if (literal.empty()) {
      return std::nullopt;
    }

    if (literal == "true") {
      return DriftwmScalar{true};
    }
    if (literal == "false") {
      return DriftwmScalar{false};
    }

    if (literal.front() == '"' || literal.front() == '\'') {
      const char quote = literal.front();
      if (literal.size() < 2U || literal.back() != quote) {
        return std::nullopt;
      }
      return DriftwmScalar{decodeQuoted(literal.substr(1, literal.size() - 2), quote == '"')};
    }

    if (literal.front() == '[') {
      if (auto items = parseStringArray(literal); items.has_value()) {
        return DriftwmScalar{std::move(*items)};
      }
      return std::nullopt;
    }

    for (const char c : literal) {
      if (!isBareValueChar(c)) {
        return std::nullopt;
      }
    }

    if (std::int64_t integer = 0; parseInt64(literal, integer)) {
      return DriftwmScalar{integer};
    }
    if (double number = 0.0; parseDouble(literal, number)) {
      return DriftwmScalar{number};
    }
    return std::nullopt;
  }

  bool validateTomlText(std::string_view text, std::string* error) {
    try {
      // Parsed and discarded: this is the only place toml++ sees the file. Reads
      // and writes go through the line-preserving model, so comments, ordering and
      // unknown keys are never at risk from a re-serialization.
      (void)toml::parse(text);
      return true;
    } catch (const toml::parse_error& e) {
      if (error != nullptr) {
        *error = e.description().empty() ? std::string("invalid TOML") : std::string(e.description());
      }
      return false;
    } catch (const std::exception& e) {
      if (error != nullptr) {
        *error = e.what();
      }
      return false;
    }
  }

  std::string_view DriftwmConfigDocument::tableOf(std::string_view keyPath) {
    const std::size_t dot = keyPath.rfind('.');
    return dot == std::string_view::npos ? std::string_view{} : keyPath.substr(0, dot);
  }

  std::optional<DriftwmConfigDocument> DriftwmConfigDocument::parse(std::string text, std::string* error) {
    if (!validateTomlText(text, error)) {
      return std::nullopt;
    }
    DriftwmConfigDocument document;
    document.m_text = std::move(text);
    document.scan();
    return document;
  }

  void DriftwmConfigDocument::scan() {
    m_keys.clear();
    m_tables.clear();

    const std::vector<LineRange> lines = splitLines(m_text);
    const std::string_view all = m_text;
    std::string table;
    int depth = 0;
    std::string triple;

    for (std::size_t index = 0; index < lines.size(); ++index) {
      const std::string_view line = all.substr(lines[index].begin, lines[index].end - lines[index].begin);
      std::size_t offset = 0;

      if (!triple.empty()) {
        const std::size_t close = line.find(triple);
        if (close == std::string_view::npos) {
          continue;
        }
        triple.clear();
        offset = close + 3;
        if (offset >= line.size()) {
          continue;
        }
      } else if (depth > 0) {
        // Inside a multi-line array or inline table: never a header, never a key.
        scanRemainder(line, offset, depth, triple);
        continue;
      }

      LineScanner scanner(line, offset);
      scanner.skipSpaces();
      if (scanner.atEnd() || scanner.peek() == '#') {
        continue;
      }

      if (scanner.peek() == '[') {
        scanner.advance();
        const bool arrayOfTables = !scanner.atEnd() && scanner.peek() == '[';
        if (arrayOfTables) {
          scanner.advance();
        }
        auto name = readDottedName(scanner);
        if (!name.has_value()) {
          continue;
        }
        table = std::move(*name);
        // A re-opened table keeps its first header: the block it spans runs to the
        // same place either way, and an inserted key goes at the end of the block.
        // The header's own closing bracket is not part of a value, so the bracket
        // depth is deliberately left alone here.
        m_tables.try_emplace(table, TableSpan{.headerLine = index, .endLine = index + 1});
        continue;
      }

      bool keySingleQuoted = false;
      bool isQuoted = false;
      const std::size_t keyStart = scanner.offset();
      const std::string key = scanner.readToken(keySingleQuoted, isQuoted);
      scanner.skipSpaces();
      const bool isAssignment = !scanner.atEnd() && scanner.peek() == '=';
      if (key.empty() || isQuoted || !isAssignment) {
        // Quoted keys are bindings ("mod+d") and their values can be long
        // expressions; track the bracket depth anyway so a multi-line one is still
        // skipped as a whole rather than read as keys.
        scanRemainder(line, isQuoted ? keyStart : scanner.offset(), depth, triple);
        continue;
      }
      scanner.advance();
      // The recorded range must start at the value itself, not at the spacing
      // after '=', or a rewrite would eat that spacing.
      scanner.skipSpaces();

      const int depthBefore = depth;
      triple.clear();
      const std::size_t valueOffset = scanner.offset();
      bool valueSingleQuoted = false;
      const std::size_t valueEnd = scanValueToken(line, valueOffset, depth, triple, valueSingleQuoted);
      const bool singleLine = depth == depthBefore && triple.empty();

      std::string fullPath;
      if (!table.empty()) {
        fullPath = table;
        fullPath.push_back('.');
      }
      fullPath += key;

      m_keys[fullPath] = KeyOccurrence{
          .valueStart = lines[index].begin + valueOffset,
          .valueEnd = lines[index].begin + std::max(valueEnd, valueOffset),
          .singleQuoted = valueSingleQuoted,
          .singleLine = singleLine,
      };
    }

    // A table block runs to the next header, or to the end of the file. The root
    // table is the span before the first header; it always exists and holds every
    // top-level key, which is where driftwm's mod_key / focus_follows_mouse live.
    std::size_t firstHeader = lines.size();
    for (const auto& [path, span] : m_tables) {
      (void)path;
      firstHeader = std::min(firstHeader, span.headerLine);
    }
    m_tables.insert_or_assign("", TableSpan{.headerLine = 0, .endLine = firstHeader});

    std::vector<const std::string*> ordered;
    ordered.reserve(m_tables.size());
    for (const auto& [path, span] : m_tables) {
      (void)span;
      ordered.push_back(&path);
    }
    std::sort(ordered.begin(), ordered.end(), [this](const std::string* a, const std::string* b) {
      return m_tables.at(*a).headerLine < m_tables.at(*b).headerLine;
    });
    for (std::size_t i = 0; i < ordered.size(); ++i) {
      const std::size_t end = i + 1 < ordered.size() ? m_tables.at(*ordered[i + 1]).headerLine : lines.size();
      m_tables.at(*ordered[i]).endLine = end;
    }
  }

  bool DriftwmConfigDocument::has(std::string_view keyPath) const {
    return m_keys.find(std::string(keyPath)) != m_keys.end();
  }

  bool DriftwmConfigDocument::hasTable(std::string_view tablePath) const {
    return m_tables.find(std::string(tablePath)) != m_tables.end();
  }

  bool DriftwmConfigDocument::isEditable(std::string_view keyPath) const {
    const auto it = m_keys.find(std::string(keyPath));
    return it != m_keys.end() && it->second.singleLine;
  }

  std::optional<DriftwmScalar> DriftwmConfigDocument::read(std::string_view keyPath) const {
    const auto it = m_keys.find(std::string(keyPath));
    if (it == m_keys.end() || !it->second.singleLine) {
      return std::nullopt;
    }
    return parseScalarLiteral(
        std::string_view(m_text).substr(it->second.valueStart, it->second.valueEnd - it->second.valueStart)
    );
  }

  bool DriftwmConfigDocument::assign(std::string_view keyPath, const DriftwmScalar& value) {
    const std::string path(keyPath);
    if (const auto it = m_keys.find(path); it != m_keys.end()) {
      if (!it->second.singleLine) {
        // A multi-line value cannot be rewritten one token at a time; leaving it
        // alone beats truncating the user's binding table.
        return false;
      }
      const std::string literal = renderScalarLiteral(value, it->second.singleQuoted);
      m_text.replace(it->second.valueStart, it->second.valueEnd - it->second.valueStart, literal);
      scan();
      return true;
    }

    const std::string_view table = tableOf(keyPath);
    const std::string_view key = table.empty() ? keyPath : keyPath.substr(table.size() + 1);
    const std::string literal = renderScalarLiteral(value, /*singleQuoted=*/false);
    insertKey(table, key, literal);
    scan();
    return true;
  }

  void DriftwmConfigDocument::insertKey(std::string_view tablePath, std::string_view key, std::string_view literal) {
    const auto span = m_tables.find(std::string(tablePath));
    if (span == m_tables.end()) {
      insertTableBlock(tablePath, key, literal);
      return;
    }
    const std::vector<LineRange> lines = splitLines(m_text);
    const std::size_t lineIndex = span->second.endLine;
    const std::size_t insertOffset = lineIndex < lines.size() ? lines[lineIndex].begin : m_text.size();

    // Keep the new key visually separate from the block above it, unless that
    // block is empty (a bare header) or already ends on a blank line.
    bool needsBlankLine = false;
    if (lineIndex > 0) {
      const std::string_view previous = std::string_view(m_text).substr(
          lines[lineIndex - 1].begin, lines[lineIndex - 1].end - lines[lineIndex - 1].begin
      );
      const std::size_t first = previous.find_first_not_of(" \t");
      needsBlankLine = first != std::string_view::npos && previous[first] != '[';
    }

    std::string addition;
    if (needsBlankLine) {
      addition.push_back('\n');
    }
    addition += key;
    addition += " = ";
    addition += literal;
    addition.push_back('\n');
    m_text.insert(insertOffset, addition);
  }

  void DriftwmConfigDocument::insertTableBlock(std::string_view tablePath, std::string_view key, std::string_view literal) {
    std::string block;
    if (!m_text.empty()) {
      if (m_text.back() != '\n') {
        block.push_back('\n');
      }
      block.push_back('\n');
    }
    block += '[';
    block += tablePath;
    block += "]\n";
    block += key;
    block += " = ";
    block += literal;
    block.push_back('\n');
    m_text += block;
  }

} // namespace compositors::driftwm
