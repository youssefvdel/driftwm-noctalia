#include "compositors/driftwm/driftwm_config_field.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace compositors::driftwm {

  namespace {

    using Kind = DriftwmFieldKind;

    // Every entry mirrors driftwm's config.reference.toml. `defaultLiteral` is the
    // reference's active default line, verbatim minus the comment, so an absent
    // key still shows what driftwm will actually do.
    constexpr DriftwmField kFields[] = {
        // General (top-level keys, i.e. the root table).
        {"general.mod_key", "mod_key", Kind::Choice, "general", "Modifier key", "Window manager modifier.",
         0.0, 0.0, 0.0, "super alt mod3", "\"super\""},
        {"general.focus_follows_mouse", "focus_follows_mouse", Kind::Boolean, "general", "Focus follows mouse",
         "Keyboard focus follows the pointer.", 0.0, 0.0, 0.0, "", "false"},
        {"general.window_placement", "window_placement", Kind::Choice, "general", "New window placement",
         "Where a window spawns when no rule positions it.",
         0.0, 0.0, 0.0, "center cursor auto", "\"center\""},
        {"general.focus_placement", "focus_placement", Kind::Choice, "general", "Focus placement",
         "Where a centering navigation parks the focused window.",
         0.0, 0.0, 0.0,
         "center top bottom left right top-left top-right bottom-left bottom-right", "\"center\""},

        // Navigation.
        {"navigation.trackpad_speed", "navigation.trackpad_speed", Kind::Number, "navigation", "Trackpad pan speed",
         "Trackpad scroll/gesture pan multiplier.", 0.0, 8.0, 0.1, "", "1.5"},
        {"navigation.mouse_speed", "navigation.mouse_speed", Kind::Number, "navigation", "Mouse pan speed",
         "Mouse drag pan multiplier.", 0.0, 8.0, 0.1, "", "1.0"},
        {"navigation.touch_speed", "navigation.touch_speed", Kind::Number, "navigation", "Touch pan speed",
         "Touchscreen gesture pan multiplier.", 0.0, 8.0, 0.1, "", "1.0"},
        {"navigation.drift", "navigation.drift", Kind::Number, "navigation", "Drift", "Momentum coast strength.",
         0.0, 1.0, 0.05, "", "0.5"},
        {"navigation.camera_speed", "navigation.camera_speed", Kind::Number, "navigation", "Camera speed",
         "Camera lerp factor; higher is faster.", 0.0, 1.0, 0.05, "", "0.3"},
        {"navigation.auto_navigate_on_close", "navigation.auto_navigate_on_close", Kind::Boolean, "navigation",
         "Navigate on close", "Pan to the newly focused window when it closes off-screen.", 0.0, 0.0, 0.0, "", "true"},
        {"navigation.auto_navigate_on_click", "navigation.auto_navigate_on_click", Kind::Boolean, "navigation",
         "Navigate on click", "A click on a partly off-screen window also pans it in.", 0.0, 0.0, 0.0, "", "false"},
        {"navigation.nudge_step", "navigation.nudge_step", Kind::Integer, "navigation", "Nudge step",
         "Pixels per nudge action.", 1.0, 500.0, 1.0, "", "20"},
        {"navigation.resize_step", "navigation.resize_step", Kind::Integer, "navigation", "Resize step",
         "Pixels per grow/shrink action.", 1.0, 500.0, 1.0, "", "20"},
        {"navigation.pan_step", "navigation.pan_step", Kind::Number, "navigation", "Pan step",
         "Pixels per pan-viewport action.", 1.0, 1000.0, 10.0, "", "100.0"},

        // Zoom.
        {"zoom.step", "zoom.step", Kind::Number, "zoom", "Zoom step", "Multiplier per keypress.", 1.0, 3.0, 0.05, "",
         "1.1"},
        {"zoom.trackpad_speed", "zoom.trackpad_speed", Kind::Number, "zoom", "Trackpad zoom speed",
         "Trackpad pinch-zoom multiplier.", 0.0, 8.0, 0.1, "", "1.0"},
        {"zoom.mouse_speed", "zoom.mouse_speed", Kind::Number, "zoom", "Mouse zoom speed",
         "Mouse-wheel zoom multiplier.", 0.0, 8.0, 0.1, "", "1.0"},
        {"zoom.touch_speed", "zoom.touch_speed", Kind::Number, "zoom", "Touch zoom speed",
         "Touchscreen gesture zoom multiplier.", 0.0, 8.0, 0.1, "", "1.0"},
        {"zoom.fit_padding", "zoom.fit_padding", Kind::Number, "zoom", "Fit padding",
         "Viewport padding for zoom-to-fit, in screen pixels.", 0.0, 400.0, 5.0, "", "80.0"},
        {"zoom.reset_on_new_window", "zoom.reset_on_new_window", Kind::Boolean, "zoom", "Reset on new window",
         "Animate zoom back to 1.0 when a window maps.", 0.0, 0.0, 0.0, "", "true"},
        {"zoom.reset_on_activation", "zoom.reset_on_activation", Kind::Boolean, "zoom", "Reset on activation",
         "Animate zoom back to 1.0 when an off-screen window takes focus.", 0.0, 0.0, 0.0, "", "true"},
        {"zoom.interact_min", "zoom.interact_min", Kind::Number, "zoom", "Interaction zoom threshold",
         "Below this zoom a window is too small to touch; 0 disables.", 0.0, 1.0, 0.01, "", "0.0"},

        // Snap.
        {"snap.enabled", "snap.enabled", Kind::Boolean, "snap", "Edge snapping",
         "Magnetic edge snapping during a window drag.", 0.0, 0.0, 0.0, "", "true"},
        {"snap.gap", "snap.gap", Kind::Number, "snap", "Gap", "Spacing kept between windows, in canvas pixels.",
         0.0, 200.0, 1.0, "", "12.0"},
        {"snap.outer_gap", "snap.outer_gap", Kind::Number, "snap", "Outer gap",
         "Inset from the usable edge to a window's title bar or content edge.", 0.0, 200.0, 1.0, "", "0.0"},
        {"snap.distance", "snap.distance", Kind::Number, "snap", "Snap distance",
         "Activation threshold from an edge, in screen pixels.", 1.0, 200.0, 1.0, "", "24.0"},
        {"snap.break_force", "snap.break_force", Kind::Number, "snap", "Break force",
         "Screen pixels past the snap to break free.", 1.0, 300.0, 1.0, "", "32.0"},
        {"snap.corners", "snap.corners", Kind::Boolean, "snap", "Snap corners",
         "Also align corners, not just edges.", 0.0, 0.0, 0.0, "", "false"},
        {"snap.centers", "snap.centers", Kind::Boolean, "snap", "Snap centers",
         "Also align centers along the moved axis.", 0.0, 0.0, 0.0, "", "false"},

        // Decorations.
        {"decorations.default_mode", "decorations.default_mode", Kind::Choice, "decorations", "Decoration mode",
         "Chrome for windows without a rule.", 0.0, 0.0, 0.0, "client minimal none", "\"client\""},
        {"decorations.bg_color", "decorations.bg_color", Kind::Text, "decorations", "Title bar background",
         "Hex colour of the SSD title bar.", 0.0, 0.0, 0.0, "", "\"#303030\""},
        {"decorations.fg_color", "decorations.fg_color", Kind::Text, "decorations", "Title text colour",
         "Hex colour of the title text and close button.", 0.0, 0.0, 0.0, "", "\"#FFFFFF\""},
        {"decorations.border_color", "decorations.border_color", Kind::Text, "decorations", "Border colour",
         "Hex colour of an unfocused border.", 0.0, 0.0, 0.0, "", "\"#303030\""},
        {"decorations.border_color_focused", "decorations.border_color_focused", Kind::Text, "decorations",
         "Focused border colour", "Hex colour of the focused border.", 0.0, 0.0, 0.0, "", "\"#303030\""},
        {"decorations.border_width", "decorations.border_width", Kind::Integer, "decorations", "Border width",
         "Border thickness in pixels; 0 disables it.", 0.0, 32.0, 1.0, "", "0"},
        {"decorations.corner_radius", "decorations.corner_radius", Kind::Integer, "decorations", "Corner radius",
         "Window corner radius in pixels.", 0.0, 64.0, 1.0, "", "10"},
        {"decorations.shadow", "decorations.shadow", Kind::Boolean, "decorations", "Shadow",
         "Drop shadow under window chrome.", 0.0, 0.0, 0.0, "", "true"},
        {"decorations.title_bar_height", "decorations.title_bar_height", Kind::Integer, "decorations",
         "Title bar height", "SSD title bar height in pixels.", 0.0, 128.0, 1.0, "", "25"},
        {"decorations.font", "decorations.font", Kind::Text, "decorations", "Title font",
         "Title text font family, resolved through fontconfig.", 0.0, 0.0, 0.0, "", "\"Adwaita Sans\""},
        {"decorations.font_size", "decorations.font_size", Kind::Integer, "decorations", "Title font size",
         "Title text size in points.", 4.0, 72.0, 1.0, "", "11"},
        {"decorations.font_weight", "decorations.font_weight", Kind::Choice, "decorations", "Title font weight",
         "Weight of the title text.",
         0.0, 0.0, 0.0,
         "thin extralight light regular medium semibold bold extrabold black", "\"medium\""},
        {"decorations.title_align", "decorations.title_align", Kind::Choice, "decorations", "Title alignment",
         "Left-aligned or centred title text.", 0.0, 0.0, 0.0, "left center", "\"center\""},
        {"decorations.opacity", "decorations.opacity", Kind::Number, "decorations", "Unfocused opacity",
         "Opacity of unfocused windows with no opacity of their own.", 0.0, 1.0, 0.05, "", "1.0"},
        {"decorations.opacity_focused", "decorations.opacity_focused", Kind::Number, "decorations",
         "Focused opacity", "Opacity of focused windows with no opacity of their own.", 0.0, 1.0, 0.05, "", "1.0"},
        {"decorations.blur", "decorations.blur", Kind::Boolean, "decorations", "Frost window backdrop",
         "Blur behind every window whose rules never set blur.", 0.0, 0.0, 0.0, "", "false"},

        // Effects.
        {"effects.blur_radius", "effects.blur_radius", Kind::Integer, "effects", "Blur passes",
         "Kawase down and up passes.", 0.0, 12.0, 1.0, "", "2"},
        {"effects.blur_strength", "effects.blur_strength", Kind::Number, "effects", "Blur strength",
         "Per-pass texel spread.", 0.0, 5.0, 0.1, "", "1.1"},
        {"effects.animate_blur_fps", "effects.animate_blur_fps", Kind::Integer, "effects", "Animated blur cap",
         "Cap on how often frost re-samples a moving background; 0 never does.", 0.0, 144.0, 1.0, "", "20"},
        {"effects.animation_speed", "effects.animation_speed", Kind::Number, "effects", "Animation speed",
         "Window open/close/move/resize lerp factor.", 0.0, 1.0, 0.05, "", "0.5"},
        {"effects.animation_scale", "effects.animation_scale", Kind::Number, "effects", "Animation scale",
         "Open/close grow and shrink amplitude; 1 fades only.", 0.0, 1.0, 0.01, "", "0.95"},

        // Background.
        {"background.type", "background.type", Kind::Choice, "background", "Background type",
         "Built-in grid, shader, tiled image, wallpaper, or none.",
         0.0, 0.0, 0.0, "default shader tile wallpaper none", "\"default\""},
        {"background.path", "background.path", Kind::Text, "background", "Background source",
         "Shader, image or wallpaper path, depending on the type.", 0.0, 0.0, 0.0, "", "\"\""},
        {"background.texture", "background.texture", Kind::Text, "background", "Shader texture",
         "Image a shader samples, bound to its tex sampler.", 0.0, 0.0, 0.0, "", "\"\""},
        {"background.mirror_tile", "background.mirror_tile", Kind::Boolean, "background", "Mirror tile",
         "Mirror-fold a tiled image so its edges always meet.", 0.0, 0.0, 0.0, "", "false"},
        {"background.cache_shader", "background.cache_shader", Kind::Boolean, "background", "Cache shader",
         "Bake a heavy static shader to a texture instead of recomputing it.", 0.0, 0.0, 0.0, "", "false"},
        {"background.transparent_shader", "background.transparent_shader", Kind::Boolean, "background",
         "Transparent shader", "Honour a shader's output alpha.", 0.0, 0.0, 0.0, "", "false"},
        {"background.cache_budget_mb", "background.cache_budget_mb", Kind::Integer, "background", "Cache budget",
         "Memory ceiling in MB for the shader and wallpaper caches.", 16.0, 4096.0, 16.0, "", "128"},
        {"background.animate_fps", "background.animate_fps", Kind::Integer, "background", "Background frame cap",
         "Frame-rate cap for animated shaders; 0 renders every frame.", 0.0, 1000.0, 1.0, "", "0"},

        // Input — keyboard.
        {"keyboard.layout", "input.keyboard.layout", Kind::Text, "input", "Keyboard layout",
         "XKB layout, comma separated for several.", 0.0, 0.0, 0.0, "", "\"us\""},
        {"keyboard.variant", "input.keyboard.variant", Kind::Text, "input", "Keyboard variant",
         "XKB layout variant, comma separated.", 0.0, 0.0, 0.0, "", "\"\""},
        {"keyboard.options", "input.keyboard.options", Kind::Text, "input", "Keyboard options",
         "XKB options, e.g. grp:win_space_toggle.", 0.0, 0.0, 0.0, "", "\"\""},
        {"keyboard.model", "input.keyboard.model", Kind::Text, "input", "Keyboard model", "XKB model name.",
         0.0, 0.0, 0.0, "", "\"\""},
        {"keyboard.repeat_rate", "input.keyboard.repeat_rate", Kind::Integer, "input", "Repeat rate",
         "Key repeat rate in keys per second.", 1.0, 100.0, 1.0, "", "25"},
        {"keyboard.repeat_delay", "input.keyboard.repeat_delay", Kind::Integer, "input", "Repeat delay",
         "Delay in milliseconds before repeat starts.", 0.0, 2000.0, 10.0, "", "200"},
        {"keyboard.layout_independent", "input.keyboard.layout_independent", Kind::Boolean, "input",
         "Layout independent bindings", "Match bindings by physical key position across layouts.", 0.0, 0.0, 0.0, "",
         "true"},
        {"keyboard.num_lock", "input.keyboard.num_lock", Kind::Boolean, "input", "Num lock on startup",
         "Num lock state at launch.", 0.0, 0.0, 0.0, "", "true"},
        {"keyboard.caps_lock", "input.keyboard.caps_lock", Kind::Boolean, "input", "Caps lock on startup",
         "Caps lock state at launch.", 0.0, 0.0, 0.0, "", "false"},
        {"keyboard.remember_layout_per_window", "input.keyboard.remember_layout_per_window", Kind::Boolean, "input",
         "Remember layout per window", "Track a separate layout per window.", 0.0, 0.0, 0.0, "", "false"},

        // Input — trackpad.
        {"trackpad.tap_to_click", "input.trackpad.tap_to_click", Kind::Boolean, "input", "Tap to click",
         "Enable tap-to-click.", 0.0, 0.0, 0.0, "", "true"},
        {"trackpad.natural_scroll", "input.trackpad.natural_scroll", Kind::Boolean, "input", "Natural scroll",
         "Reverse scroll direction so content follows the fingers.", 0.0, 0.0, 0.0, "", "true"},
        {"trackpad.tap_and_drag", "input.trackpad.tap_and_drag", Kind::Boolean, "input", "Tap and drag",
         "Double-tap-hold to drag.", 0.0, 0.0, 0.0, "", "true"},
        {"trackpad.accel_speed", "input.trackpad.accel_speed", Kind::Number, "input", "Trackpad acceleration",
         "Pointer acceleration, -1.0 to 1.0.", -1.0, 1.0, 0.05, "", "0.0"},
        {"trackpad.accel_profile", "input.trackpad.accel_profile", Kind::Choice, "input", "Trackpad acceleration profile",
         "Flat disables acceleration; adaptive keeps it.", 0.0, 0.0, 0.0, "flat adaptive", "\"adaptive\""},
        {"trackpad.click_method", "input.trackpad.click_method", Kind::Choice, "input", "Click method",
         "Device default, finger count, or button areas.",
         0.0, 0.0, 0.0, "none clickfinger button_areas", "\"none\""},
        {"trackpad.disable_while_typing", "input.trackpad.disable_while_typing", Kind::Boolean, "input",
         "Disable while typing", "Ignore trackpad input shortly after a key press.", 0.0, 0.0, 0.0, "", "true"},
        {"trackpad.enable", "input.trackpad.enable", Kind::Boolean, "input", "Trackpad enabled",
         "Turn the trackpad off entirely when false.", 0.0, 0.0, 0.0, "", "true"},
        {"trackpad.disable_on_external_mouse", "input.trackpad.disable_on_external_mouse", Kind::Boolean, "input",
         "Disable with external mouse", "Turn the trackpad off while a mouse is connected.", 0.0, 0.0, 0.0, "", "false"},

        // Input — mouse.
        {"mouse.accel_speed", "input.mouse.accel_speed", Kind::Number, "input", "Mouse acceleration",
         "Pointer acceleration, -1.0 to 1.0.", -1.0, 1.0, 0.05, "", "0.0"},
        {"mouse.accel_profile", "input.mouse.accel_profile", Kind::Choice, "input", "Mouse acceleration profile",
         "Flat disables acceleration; adaptive keeps it.", 0.0, 0.0, 0.0, "flat adaptive", "\"flat\""},
        {"mouse.natural_scroll", "input.mouse.natural_scroll", Kind::Boolean, "input", "Mouse natural scroll",
         "Reverse the mouse scroll direction.", 0.0, 0.0, 0.0, "", "false"},
        {"mouse.left_handed", "input.mouse.left_handed", Kind::Boolean, "input", "Left handed",
         "Swap the left and right mouse buttons.", 0.0, 0.0, 0.0, "", "false"},

        // Cursor.
        {"cursor.theme", "cursor.theme", Kind::Text, "cursor", "Cursor theme",
         "XCURSOR_THEME; an empty value inherits from the environment.", 0.0, 0.0, 0.0, "", "\"none\""},
        {"cursor.size", "cursor.size", Kind::Integer, "cursor", "Cursor size",
         "XCURSOR_SIZE; 0 inherits from the environment.", 0.0, 256.0, 1.0, "", "0"},
        {"cursor.inactive_opacity", "cursor.inactive_opacity", Kind::Number, "cursor", "Inactive output opacity",
         "Cursor opacity on non-active outputs.", 0.0, 1.0, 0.05, "", "0.5"},

        // Mouse behaviour on windows.
        {"mouse.resize_on_border", "mouse.resize_on_border", Kind::Boolean, "mouse", "Resize on border",
         "Drag a window edge or corner to resize it.", 0.0, 0.0, 0.0, "", "true"},
        {"mouse.decoration_resize_snapped", "mouse.decoration_resize_snapped", Kind::Boolean, "mouse",
         "Decoration resize propagates", "A border drag resizes the whole snapped cluster.", 0.0, 0.0, 0.0, "", "false"},
        {"mouse.decoration_fit_snapped", "mouse.decoration_fit_snapped", Kind::Boolean, "mouse",
         "Decoration fit propagates", "A decoration maximize/fit resizes the whole snapped cluster.", 0.0, 0.0, 0.0,
         "", "false"},

        // Session.
        {"session.suspend_on_close", "session.suspend_on_close", Kind::Boolean, "session", "Suspend on close",
         "Suspend rather than close when a client closes its own window.", 0.0, 0.0, 0.0, "", "false"},
        {"session.restore_windows", "session.restore_windows", Kind::Boolean, "session", "Restore windows",
         "Bring still-open windows back as suspended windows after a restart.", 0.0, 0.0, 0.0, "", "false"},
        {"session.restore_camera", "session.restore_camera", Kind::Boolean, "session", "Restore camera",
         "Restore each output's camera position and zoom. Read at launch.", 0.0, 0.0, 0.0, "", "false"},
        {"session.restore_bookmarks", "session.restore_bookmarks", Kind::Boolean, "session", "Restore bookmarks",
         "Persist runtime bookmark edits across restarts. Read at launch.", 0.0, 0.0, 0.0, "", "false"},

        // Xwayland.
        {"xwayland.enabled", "xwayland.enabled", Kind::Boolean, "xwayland", "Xwayland",
         "Spawn xwayland-satellite at startup so X11 apps connect.", 0.0, 0.0, 0.0, "", "true"},
        {"xwayland.path", "xwayland.path", Kind::Text, "xwayland", "xwayland-satellite path",
         "Binary to spawn; a bare name is looked up on PATH.", 0.0, 0.0, 0.0, "", "\"xwayland-satellite\""},

        // Autostart.
        {"autostart.commands", "autostart", Kind::StringList, "autostart", "Autostart commands",
         "One shell command per line, run at startup after WAYLAND_DISPLAY is set.", 0.0, 0.0, 0.0, "", "[]"},
    };

    constexpr DriftwmFieldGroup kGroups[] = {
        {"general", "General", "Top-level behaviour: modifier key, focus and placement."},
        {"navigation", "Navigation", "Camera panning, drift and auto-navigation."},
        {"zoom", "Zoom", "Zoom multipliers, fit padding and the reset rules."},
        {"snap", "Snap", "Magnetic edge snapping and the gaps it keeps."},
        {"decorations", "Decorations", "Title bars, borders, corners and per-focus opacity."},
        {"effects", "Effects", "Blur cost and window animation."},
        {"background", "Background", "Canvas background: shader, tile, wallpaper or none."},
        {"input", "Input", "Keyboard, trackpad and mouse devices."},
        {"cursor", "Cursor", "Cursor theme, size and inactive-output opacity."},
        {"mouse", "Mouse", "Pointer behaviour on window edges and decorations."},
        {"session", "Session", "Session restore. Camera and bookmark flags apply at launch."},
        {"xwayland", "Xwayland", "X11 support through xwayland-satellite."},
        {"autostart", "Autostart", "Commands run once the compositor is up."},
    };

    [[nodiscard]] std::vector<std::string> splitTokens(std::string_view text) {
      std::vector<std::string> tokens;
      std::size_t offset = 0;
      while (offset < text.size()) {
        while (offset < text.size() && (text[offset] == ' ' || text[offset] == '\t')) {
          ++offset;
        }
        const std::size_t start = offset;
        while (offset < text.size() && text[offset] != ' ' && text[offset] != '\t') {
          ++offset;
        }
        if (offset > start) {
          tokens.emplace_back(text.substr(start, offset - start));
        }
      }
      return tokens;
    }

    [[nodiscard]] bool valueMatchesKind(DriftwmFieldKind kind, const DriftwmScalar& value) {
      switch (kind) {
      case Kind::Boolean:
        return std::holds_alternative<bool>(value);
      case Kind::Number:
        return std::holds_alternative<double>(value);
      case Kind::Integer:
        return std::holds_alternative<std::int64_t>(value);
      case Kind::Choice:
      case Kind::Text:
        return std::holds_alternative<std::string>(value);
      case Kind::StringList:
        return std::holds_alternative<std::vector<std::string>>(value);
      }
      return false;
    }

    // Reads the numeric out of any literal the catalog can hold, so bounds checks
    // accept the same spelling the reference documents.
    [[nodiscard]] std::optional<double> numericOf(const DriftwmScalar& value) {
      if (const auto* number = std::get_if<double>(&value)) {
        return *number;
      }
      if (const auto* integer = std::get_if<std::int64_t>(&value)) {
        return static_cast<double>(*integer);
      }
      return std::nullopt;
    }

  } // namespace

  std::span<const DriftwmField> driftwmConfigFields() { return kFields; }

  std::span<const DriftwmFieldGroup> driftwmConfigFieldGroups() { return kGroups; }

  std::vector<const DriftwmField*> driftwmConfigFieldsInGroup(std::string_view groupId) {
    std::vector<const DriftwmField*> fields;
    for (const auto& field : kFields) {
      if (field.groupId == groupId) {
        fields.push_back(&field);
      }
    }
    return fields;
  }

  const DriftwmField* findDriftwmFieldById(std::string_view id) {
    const auto it = std::ranges::find_if(kFields, [&](const DriftwmField& field) { return field.id == id; });
    return it == std::ranges::end(kFields) ? nullptr : &*it;
  }

  const DriftwmField* findDriftwmFieldByKeyPath(std::string_view keyPath) {
    const auto it = std::ranges::find_if(kFields, [&](const DriftwmField& field) { return field.keyPath == keyPath; });
    return it == std::ranges::end(kFields) ? nullptr : &*it;
  }

  std::vector<std::string> driftwmFieldOptions(const DriftwmField& field) { return splitTokens(field.options); }

  std::optional<DriftwmScalar> driftwmFieldDefaultValue(const DriftwmField& field) {
    return parseScalarLiteral(field.defaultLiteral);
  }

  std::string_view driftwmFieldKindName(DriftwmFieldKind kind) {
    switch (kind) {
    case Kind::Boolean:
      return "Boolean";
    case Kind::Number:
      return "Float";
    case Kind::Integer:
      return "Integer";
    case Kind::Choice:
    case Kind::Text:
      return "Text";
    case Kind::StringList:
      return "StringList";
    }
    return "";
  }

  bool coerceScalarToField(const DriftwmScalar& value, const DriftwmField& field, DriftwmScalar& out) {
    const auto* asBool = std::get_if<bool>(&value);
    const auto* asInt = std::get_if<std::int64_t>(&value);
    const auto* asFloat = std::get_if<double>(&value);
    const auto* asText = std::get_if<std::string>(&value);
    const auto* asList = std::get_if<std::vector<std::string>>(&value);

    switch (field.kind) {
    case Kind::Boolean:
      if (asBool == nullptr) {
        return false;
      }
      out = *asBool;
      return true;
    case Kind::Number:
      if (asFloat != nullptr) {
        out = *asFloat;
        return true;
      }
      if (asInt == nullptr) {
        return false;
      }
      out = static_cast<double>(*asInt);
      return true;
    case Kind::Integer:
      if (asInt != nullptr) {
        out = *asInt;
        return true;
      }
      // A whole float is unambiguous; a fractional one would lose its meaning.
      if (asFloat == nullptr || *asFloat != std::floor(*asFloat)) {
        return false;
      }
      out = static_cast<std::int64_t>(*asFloat);
      return true;
    case Kind::Choice:
    case Kind::Text:
      if (asText == nullptr) {
        return false;
      }
      out = *asText;
      return true;
    case Kind::StringList:
      if (asList == nullptr) {
        return false;
      }
      out = *asList;
      return true;
    }
    return false;
  }

  std::string driftwmFieldValueError(const DriftwmField& field, const DriftwmScalar& value) {
    if (!valueMatchesKind(field.kind, value)) {
      return std::format("{} expects a {} value", field.keyPath, driftwmFieldKindName(field.kind));
    }

    if (field.kind == Kind::Choice) {
      const auto& options = driftwmFieldOptions(field);
      const auto& text = std::get<std::string>(value);
      if (!std::ranges::contains(options, text)) {
        return std::format("{} must be one of: {}", field.keyPath, field.options);
      }
    }

    if (field.kind == Kind::Number || field.kind == Kind::Integer) {
      if (const auto numeric = numericOf(value)) {
        if (*numeric < field.minValue || *numeric > field.maxValue) {
          return std::format("{} must be between {} and {}", field.keyPath, field.minValue, field.maxValue);
        }
      }
    }

    return {};
  }

} // namespace compositors::driftwm
