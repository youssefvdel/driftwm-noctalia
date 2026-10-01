#include "shell/settings/settings_content_driftwm.h"

#include "compositors/driftwm/driftwm_config_field.h"
#include "i18n/i18n.h"
#include "i18n/i18n_service.h"
#include "shell/settings/settings_content.h"
#include "shell/settings/settings_content_common.h"
#include "ui/builders.h"
#include "ui/palette.h"
#include "ui/style.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace settings {

  namespace {

    using compositors::driftwm::DriftwmConfigDocument;
    using compositors::driftwm::DriftwmConfigService;
    using compositors::driftwm::DriftwmField;
    using compositors::driftwm::DriftwmFieldKind;
    using compositors::driftwm::DriftwmScalar;
    using compositors::driftwm::driftwmConfigFieldGroups;
    using compositors::driftwm::driftwmConfigFieldsInGroup;

    // The catalog carries driftwm's own English wording, so an untranslated field
    // still reads correctly instead of showing a raw key.
    [[nodiscard]] std::string translated(std::string_view key, std::string_view fallback) {
      const std::string_view raw = i18n::Service::instance().lookup(key);
      return raw.empty() ? std::string(fallback) : std::string(raw);
    }

    [[nodiscard]] std::string fieldLabel(const DriftwmField& field) {
      return translated("settings.driftwm." + std::string(field.id) + ".label", field.label);
    }

    [[nodiscard]] std::string fieldDescription(const DriftwmField& field) {
      return translated("settings.driftwm." + std::string(field.id) + ".description", field.description);
    }

    [[nodiscard]] std::string groupLabel(std::string_view groupId, std::string_view fallback) {
      return translated("settings.driftwm.group." + std::string(groupId) + ".label", fallback);
    }

    // One command per line; a trailing newline is not an empty command.
    [[nodiscard]] std::vector<std::string> splitLines(std::string_view text) {
      std::vector<std::string> lines;
      std::size_t offset = 0;
      while (offset <= text.size()) {
        const std::size_t newline = text.find('\n', offset);
        if (newline == std::string_view::npos) {
          if (offset < text.size()) {
            lines.emplace_back(text.substr(offset));
          }
          break;
        }
        if (newline > offset) {
          lines.emplace_back(text.substr(offset, newline - offset));
        }
        offset = newline + 1;
      }
      return lines;
    }

    [[nodiscard]] std::string joinLines(const std::vector<std::string>& lines) {
      std::string out;
      for (const auto& line : lines) {
        if (!out.empty()) {
          out.push_back('\n');
        }
        out += line;
      }
      return out;
    }

    // What the row should show: the file's value, or driftwm's documented default
    // when the key is absent or holds a shape this row cannot represent. `present`
    // is false in the latter case, which surfaces driftwm's fallback rather than
    // pretending the user has set something.
    struct FieldState {
      std::optional<DriftwmScalar> value;
      bool present = false;
    };

    [[nodiscard]] FieldState readField(const DriftwmConfigDocument& document, const DriftwmField& field) {
      FieldState state;
      if (const auto value = document.read(field.keyPath); value.has_value()) {
        // Coerced rather than read blind: config.toml is hand-editable, so a key can
        // hold a valid TOML literal of the wrong shape, and std::get on the variant
        // would throw straight out of the render.
        DriftwmScalar coerced;
        if (compositors::driftwm::coerceScalarToField(*value, field, coerced)) {
          state.value = std::move(coerced);
          state.present = true;
          return state;
        }
      }
      state.value = compositors::driftwm::driftwmFieldDefaultValue(field);
      return state;
    }

    // Total accessors: readField already coerced the value into the field's shape,
    // and these fall back to driftwm's default rather than ever throwing on a
    // hand-edited file.
    [[nodiscard]] bool booleanValue(const FieldState& state) {
      return state.value.has_value() && *std::get_if<bool>(&*state.value);
    }

    [[nodiscard]] double numberValue(const FieldState& state) {
      return state.value.has_value() ? *std::get_if<double>(&*state.value) : 0.0;
    }

    [[nodiscard]] std::int64_t integerValue(const FieldState& state) {
      return state.value.has_value() ? *std::get_if<std::int64_t>(&*state.value) : 0;
    }

    [[nodiscard]] std::string textValue(const FieldState& state) {
      return state.value.has_value() ? *std::get_if<std::string>(&*state.value) : std::string();
    }

    [[nodiscard]] std::vector<std::string> listValue(const FieldState& state) {
      if (state.value.has_value()) {
        if (const auto* list = std::get_if<std::vector<std::string>>(&*state.value)) {
          return *list;
        }
      }
      return {};
    }

    void commit(const SettingsDriftwmContext& ctx, const DriftwmField& field, DriftwmScalar value) {
      if (ctx.config == nullptr) {
        return;
      }
      const std::string reason = compositors::driftwm::driftwmFieldValueError(field, value);
      if (!reason.empty()) {
        return;
      }
      const DriftwmConfigService::ApplyResult result = ctx.config->apply(field.keyPath, std::move(value));
      if (result.status != DriftwmConfigService::ApplyResult::Status::Applied && ctx.onApplyFailed) {
        ctx.onApplyFailed(result.message);
      }
      if (ctx.requestContentRebuild) {
        ctx.requestContentRebuild();
      }
    }

    void addRow(Flex& body, const DriftwmField& field, const FieldState& state, const SettingsDriftwmContext& ctx) {
      const float scale = ctx.scale;

      auto title = ui::row({.align = FlexAlign::Center, .gap = Style::spaceSm * scale, .fillWidth = true});
      title->addChild(
          makeLabel(fieldLabel(field), Style::fontSizeBody * scale, colorSpecFromRole(ColorRole::OnSurface), FontWeight::Bold)
      );
      title->addChild(ui::spacer());
      if (!state.present) {
        // driftwm falls back to this value, so the row is showing the default.
        title->addChild(makeLabel(
            i18n::tr("settings.driftwm.default-badge"), Style::fontSizeCaption * scale,
            colorSpecFromRole(ColorRole::OnSurfaceVariant), FontWeight::Normal
        ));
      }

      auto copy = ui::column({.align = FlexAlign::Start, .gap = Style::spaceXs * scale, .flexGrow = 1.0F});
      copy->addChild(std::move(title));
      const std::string description = fieldDescription(field);
      if (!description.empty()) {
        copy->addChild(makeSettingSubtitleLabel(description, scale));
      }

      std::unique_ptr<Node> control;
      switch (field.kind) {
      case DriftwmFieldKind::Boolean: {
        control = ui::toggle({
            .checked = booleanValue(state),
            .scale = scale,
            .onChange = [ctx, &field](bool checked) { commit(ctx, field, DriftwmScalar{checked}); },
        });
        break;
      }
      case DriftwmFieldKind::Choice: {
        const auto options = compositors::driftwm::driftwmFieldOptions(field);
        std::optional<std::size_t> selected;
        if (const auto it = std::ranges::find(options, textValue(state)); it != options.end()) {
          selected = static_cast<std::size_t>(std::distance(options.begin(), it));
        }
        control = ui::select({
            .options = options,
            .selectedIndex = selected,
            .fontSize = Style::fontSizeBody * scale,
            .controlHeight = Style::controlHeightSm * scale,
            .onSelectionChanged = [ctx, &field, options](std::size_t index, std::string_view) {
              if (index < options.size()) {
                commit(ctx, field, DriftwmScalar{options[index]});
              }
            },
        });
        break;
      }
      case DriftwmFieldKind::Number: {
        // Clamped for display only: the file may hold a value outside the range
        // this row offers, and truncating what it shows would misreport it.
        const double raw = state.value.has_value() ? numberValue(state) : field.minValue;
        const double shown = std::clamp(raw, field.minValue, field.maxValue);
        // Committed on release, not per pixel: every commit runs
        // `driftwm --check-config`, far too costly to run while dragging.
        auto pending = std::make_shared<double>(shown);
        control = ui::slider({
            .minValue = field.minValue,
            .maxValue = field.maxValue,
            .step = field.step,
            .value = shown,
            .wheelAdjustEnabled = true,
            .width = 200.0F * scale,
            .onValueChanged = [pending](double updated) { *pending = updated; },
            .onDragEnd = [ctx, &field, pending]() { commit(ctx, field, DriftwmScalar{*pending}); },
        });
        break;
      }
      case DriftwmFieldKind::Integer: {
        const auto value = static_cast<int>(state.value.has_value() ? integerValue(state) : 0);
        control = ui::stepper({
            .minValue = static_cast<int>(field.minValue),
            .maxValue = static_cast<int>(field.maxValue),
            .step = static_cast<int>(field.step > 0.0 ? field.step : 1.0),
            .value = value,
            .scale = scale,
            .onValueCommitted = [ctx, &field](int committed) { commit(ctx, field, DriftwmScalar{std::int64_t{committed}}); },
        });
        break;
      }
      case DriftwmFieldKind::Text: {
        control = ui::input({
            .value = textValue(state),
            .fontSize = Style::fontSizeBody * scale,
            .controlHeight = Style::controlHeightSm * scale,
            .width = 240.0F * scale,
            .onSubmit = [ctx, &field](const std::string& text) { commit(ctx, field, DriftwmScalar{text}); },
            // Enter and leaving the field both commit, so a typed value is never
            // silently dropped. A repeated commit is a no-op: apply() returns early
            // when the value is already on disk.
            .submitOnFocusLoss = true,
        });
        break;
      }
      case DriftwmFieldKind::StringList: {
        control = ui::input({
            .value = joinLines(listValue(state)),
            .placeholder = i18n::tr("settings.driftwm.autostart.placeholder"),
            .fontSize = Style::fontSizeBody * scale,
            .controlHeight = Style::controlHeightSm * scale,
            .width = 320.0F * scale,
            .height = 96.0F * scale,
            .onSubmit = [ctx, &field](const std::string& text) {
              commit(ctx, field, DriftwmScalar{splitLines(text)});
            },
            .submitOnFocusLoss = true,
            .configure = [](Input& input) { input.setMultiline(true); },
        });
        break;
      }
      }

      auto actions = ui::row({.align = FlexAlign::Center, .gap = Style::spaceSm * scale});
      actions->addChild(std::move(control));

      body.addChild(
          ui::row(
              {
                  .align = FlexAlign::Center,
                  .justify = FlexJustify::SpaceBetween,
                  .gap = Style::spaceXs * scale,
                  .paddingV = 2.0F * scale,
                  .minHeight = Style::controlHeight * scale,
              },
              std::move(copy), std::move(actions)
          )
      );
    }

  } // namespace

  std::size_t addSettingsDriftwm(Flex& content, SettingsDriftwmContext ctx) {
    const float scale = ctx.scale;

    if (ctx.config == nullptr) {
      return 0;
    }

    if (!ctx.statusMessage.empty()) {
      content.addChild(makeSettingsStatusBanner(
          SettingsStatusBannerProps{
              .message = ctx.statusMessage,
              .error = ctx.statusIsError,
              .scale = scale,
              .onDismiss = ctx.clearStatus,
          }
      ));
    }

    if (ctx.document == nullptr) {
      return 0;
    }

    std::size_t rows = 0;
    for (const auto& group : driftwmConfigFieldGroups()) {
      const auto fields = driftwmConfigFieldsInGroup(group.id);
      if (fields.empty()) {
        continue;
      }
      auto& expandedGroups = ctx.expandedGroupsByPage[std::string(ctx.selectedSection)];
      Flex* body = addSettingsGroupCard(
          SettingsGroupCardProps{
              .parent = content,
              .group = std::string(group.id),
              .title = groupLabel(group.id, group.label),
              .scale = scale,
              .expandedGroups = expandedGroups,
              .scrollToTop = ctx.scrollContentToTop,
          }
      );
      for (const auto* field : fields) {
        addRow(*body, *field, readField(*ctx.document, *field), ctx);
        ++rows;
      }
    }
    return rows;
  }

} // namespace settings
