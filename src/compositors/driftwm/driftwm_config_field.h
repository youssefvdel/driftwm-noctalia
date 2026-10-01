#pragma once

#include "compositors/driftwm/driftwm_config_document.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace compositors::driftwm {

  // One knob of driftwm's own config.toml, described once and read by both the
  // settings tab and the document editor. Nothing here is mirrored into noctalia's
  // config: the values live only in the file driftwm reads, and `keyPath` is the
  // single source of truth for where.
  enum class DriftwmFieldKind : std::uint8_t {
    Boolean,   // driftwm expects a TOML bool
    Number,    // float field: written with a decimal point so serde keeps it a float
    Integer,   // integer field: never written as a float literal
    Choice,    // closed set of string values (`options`, space separated)
    Text,      // free-form string
    StringList,// single-line array of strings (autostart)
  };

  struct DriftwmField {
    // Stable id; also the settings tab's i18n key suffix.
    std::string_view id;
    // Dotted path in config.toml, e.g. "input.keyboard.layout".
    std::string_view keyPath;
    DriftwmFieldKind kind = DriftwmFieldKind::Boolean;
    // Group card the row renders in.
    std::string_view groupId;
    std::string_view label;
    std::string_view description;
    double minValue = 0.0;
    double maxValue = 0.0;
    double step = 0.0;
    // Space-separated string values for Choice fields.
    std::string_view options;
    // driftwm's built-in default, as the literal its reference config documents.
    // Shown when the key is absent; committing the same value is a no-op.
    std::string_view defaultLiteral;
  };

  struct DriftwmFieldGroup {
    std::string_view id;
    std::string_view label;
    std::string_view description;
  };

  [[nodiscard]] std::span<const DriftwmField> driftwmConfigFields();
  [[nodiscard]] std::span<const DriftwmFieldGroup> driftwmConfigFieldGroups();
  // Fields of one group, in declaration order.
  [[nodiscard]] std::vector<const DriftwmField*> driftwmConfigFieldsInGroup(std::string_view groupId);

  [[nodiscard]] const DriftwmField* findDriftwmFieldById(std::string_view id);
  [[nodiscard]] const DriftwmField* findDriftwmFieldByKeyPath(std::string_view keyPath);

  // Choice values of a field, split from `options`.
  [[nodiscard]] std::vector<std::string> driftwmFieldOptions(const DriftwmField& field);

  // Value driftwm falls back to when the key is missing from config.toml, parsed
  // from `defaultLiteral`. Null when the literal does not match the field's kind.
  [[nodiscard]] std::optional<DriftwmScalar> driftwmFieldDefaultValue(const DriftwmField& field);

  // Human-readable name of a field kind, for diagnostics.
  [[nodiscard]] std::string_view driftwmFieldKindName(DriftwmFieldKind kind);

  // Human-readable type errors for a value the field cannot accept. Empty when
  // `value` is compatible with the field's kind and bounds.
  [[nodiscard]] std::string driftwmFieldValueError(const DriftwmField& field, const DriftwmScalar& value);

  // Repoints `value` at the shape `field` renders, for a row that has to display a
  // hand-written value. config.toml is editable by hand, so a key can hold a
  // perfectly valid TOML literal of the wrong shape: an integer where driftwm's
  // field is a float, a whole float where it is an integer, a quoted number.
  // Integers and floats are interchangeable here; every other mismatch is refused
  // and the caller falls back to driftwmFieldDefaultValue, so a hand edit can never
  // leave a row reading a variant it cannot hold.
  [[nodiscard]] bool coerceScalarToField(const DriftwmScalar& value, const DriftwmField& field, DriftwmScalar& out);

} // namespace compositors::driftwm
