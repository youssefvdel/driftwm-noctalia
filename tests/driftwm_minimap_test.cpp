// Covers the three pieces of the built-in DriftWM minimap that carry real logic:
// the `State` decode (driftwm_state), the pure canvas->pixel projection, and the
// desktop widget setting schema. The widget's own scene building needs a live
// compositor, so it is exercised by running the desktop widget host, not here.
#include "compositors/driftwm/driftwm_state.h"
#include "shell/desktop/desktop_widget_settings_registry.h"
#include "shell/driftwm_minimap_projection.h"
#include "shell/settings/widget_settings_registry.h"
#include "tests/test_check.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace {

  using compositors::driftwm::DriftwmState;
  using compositors::driftwm::decodeDriftwmBookmarks;
  using compositors::driftwm::decodeDriftwmState;
  using compositors::driftwm::sameDriftwmState;
  using driftwm_minimap::CanvasRect;
  using driftwm_minimap::PixelRect;
  using driftwm_minimap::Projection;

  [[nodiscard]] bool near(float left, float right, float tolerance = 1.0e-3F) {
    return std::abs(left - right) <= tolerance;
  }

  [[nodiscard]] bool near(double left, double right, double tolerance = 1.0e-3) {
    return std::abs(left - right) <= tolerance;
  }

  // A payload shaped exactly like the `State` reply, carrying one of everything.
  [[nodiscard]] nlohmann::json fullState() {
    return nlohmann::json{
        {"camera", {-960.0, -600.0}},
        {"zoom", 1.0},
        {"layout", "English (US)"},
        {"layout_short", "us"},
        {"active_bookmark", "home"},
        {"windows",
         {{{"id", 3},
           {"app_id", "foot"},
           {"title", "~"},
           {"position", {0, 0}},
           {"size", {800, 480}},
           {"is_focused", true},
           {"is_widget", false},
           {"suspended", false},
           {"mode", "Fit"}},
          {{"id", 4},
           {"app_id", "foot"},
           {"title", "gone"},
           {"position", {120, 80}},
           {"size", {400, 300}},
           {"is_focused", false},
           {"suspended", true}}}},
        {"fullscreen", {{{"id", 9}, {"output", "DP-1"}, {"app_id", "mpv"}, {"title", "clip"}}}},
        {"pinned",
         {{{"id", 7},
           {"output", "DP-2"},
           {"app_id", "foot"},
           {"title", "sticky"},
           {"position", {100, -50}},
           {"size", {200, 150}}}}},
        {"layers", {"noctalia", "osk"}},
        {"canvas_layers", {{{"app_id", "panel"}, {"position", {-10, 20}}, {"size", {500, 40}}}}},
        {"outputs",
         {{{"name", "DP-1"}, {"camera", {0.0, 0.0}}, {"zoom", 1.0}, {"size", {1920, 1080}}, {"active", true},
           {"active_bookmark", "home"}},
          {{"name", "DP-2"},
           {"camera", {500.0, 250.0}},
           {"zoom", 0.5},
           {"size", {2560, 1440}},
           {"active", false},
           {"active_bookmark", nullptr}}}},
    };
  }

  void testStateDecoding() {
    const auto state = decodeDriftwmState(fullState());
    TEST_CHECK(state.has_value());
    if (!state.has_value()) {
      return;
    }

    // The top-level camera is the focused output's viewport.
    TEST_CHECK(near(state->cameraX, -960.0F));
    TEST_CHECK(near(state->cameraY, -600.0F));
    TEST_CHECK(near(state->zoom, 1.0F));
    TEST_CHECK(state->layout == "English (US)");
    TEST_CHECK(state->layoutShort == "us");
    TEST_CHECK(state->activeBookmark == "home");

    TEST_CHECK(state->outputs.size() == 2);
    TEST_CHECK(state->outputs[0].name == "DP-1");
    TEST_CHECK(state->outputs[0].active);
    TEST_CHECK(state->outputs[0].activeBookmark == "home");
    TEST_CHECK(near(state->outputs[0].width, 1920.0F));
    TEST_CHECK(near(state->outputs[1].zoom, 0.5F));
    // A null active_bookmark decodes to "none", not to a crash.
    TEST_CHECK(state->outputs[1].activeBookmark.empty());

    TEST_CHECK(state->windows.size() == 2);
    TEST_CHECK(state->windows[0].id == 3);
    TEST_CHECK(state->windows[0].focused);
    TEST_CHECK(!state->windows[0].suspended);
    TEST_CHECK(!state->windows[0].widget);
    TEST_CHECK(state->windows[0].mode == "Fit");
    TEST_CHECK(near(state->windows[0].centerX, 0.0F));
    TEST_CHECK(near(state->windows[0].width, 800.0F));
    TEST_CHECK(state->windows[1].suspended);
    // A compositor older than the `mode` field omits it; that means Normal.
    TEST_CHECK(state->windows[1].mode == "Normal");

    TEST_CHECK(state->fullscreen.size() == 1);
    TEST_CHECK(state->fullscreen[0].output == "DP-1");
    TEST_CHECK(state->fullscreen[0].title == "clip");

    TEST_CHECK(state->pinned.size() == 1);
    TEST_CHECK(state->pinned[0].output == "DP-2");
    TEST_CHECK(near(state->pinned[0].centerX, 100.0F));
    TEST_CHECK(near(state->pinned[0].height, 150.0F));

    TEST_CHECK(state->layers == std::vector<std::string>({"noctalia", "osk"}));
    TEST_CHECK(state->canvasLayers.size() == 1);
    TEST_CHECK(state->canvasLayers[0].appId == "panel");
    TEST_CHECK(near(state->canvasLayers[0].centerY, 20.0F));
    TEST_CHECK(near(state->canvasLayers[0].width, 500.0F));

    // Equality is what lets the state source skip a redundant change signal.
    TEST_CHECK(sameDriftwmState(*state, *decodeDriftwmState(fullState())));
    auto moved = *state;
    moved.cameraX += 1.0F;
    TEST_CHECK(!sameDriftwmState(*state, moved));
  }

  void testStateDecodingRejectsUnusablePayloads() {
    // No camera or a non-positive zoom leaves nothing to frame, so the previous
    // state must survive rather than be replaced by a half-decoded one.
    TEST_CHECK(!decodeDriftwmState(nlohmann::json::array()).has_value());
    TEST_CHECK(!decodeDriftwmState(nlohmann::json::object()).has_value());
    TEST_CHECK(!decodeDriftwmState(nlohmann::json{{"zoom", 1.0}}).has_value());
    TEST_CHECK(!decodeDriftwmState(nlohmann::json{{"camera", {0.0, 0.0}}}).has_value());
    TEST_CHECK(!decodeDriftwmState(nlohmann::json{{"camera", {0.0}}}).has_value());
    TEST_CHECK(!decodeDriftwmState(nlohmann::json{{"camera", {0.0, 0.0}}, {"zoom", 0.0}}).has_value());
    TEST_CHECK(!decodeDriftwmState(nlohmann::json{{"camera", {0.0, 0.0}}, {"zoom", -1.0}}).has_value());
  }

  void testStateDecodingToleratesPartialAndMalformedEntries() {
    // Only camera and zoom are load-bearing; everything else defaults.
    const auto minimal = decodeDriftwmState(nlohmann::json{{"camera", {1.0, 2.0}}, {"zoom", 1.0}});
    TEST_CHECK(minimal.has_value());
    TEST_CHECK(minimal->outputs.empty());
    TEST_CHECK(minimal->windows.empty());
    TEST_CHECK(minimal->layers.empty());
    TEST_CHECK(minimal->activeBookmark.empty());

    // A malformed array entry drops that entry, not the whole snapshot, and a
    // non-numeric coordinate never becomes an inf.
    auto mixed = fullState();
    mixed["windows"].push_back({{"id", 5}, {"position", {"nope", 0.0}}});
    mixed["windows"].push_back({{"id", 6}, {"position", {0.0, 0.0}}});
    const auto decoded = decodeDriftwmState(mixed);
    TEST_CHECK(decoded.has_value());
    TEST_CHECK(decoded->windows.size() == 3);
    TEST_CHECK(decoded->windows[2].id == 6);
    TEST_CHECK(std::isfinite(decoded->windows[2].width));
    TEST_CHECK(std::isfinite(decoded->windows[2].height));
  }

  void testBookmarkDecoding() {
    const auto bookmarks = decodeDriftwmBookmarks(
      nlohmann::json{{"Ok", {{"Bookmarks", {{"home", {0.0, 0.0}}, {"work", {500.0, 250.0}}}}}}}
    );
    TEST_CHECK(bookmarks.has_value());
    TEST_CHECK(bookmarks->size() == 2);
    // Sorted by name, whatever order the compositor replied in.
    TEST_CHECK((*bookmarks)[0].first == "home");
    TEST_CHECK((*bookmarks)[1].first == "work");
    TEST_CHECK(near((*bookmarks)[1].second.first, 500.0));
    TEST_CHECK(near((*bookmarks)[1].second.second, 250.0));

    // An empty bookmark set is a valid answer, an unrelated reply is not.
    TEST_CHECK(decodeDriftwmBookmarks(nlohmann::json{{"Ok", {{"Bookmarks", nlohmann::json::object()}}}}).has_value());
    TEST_CHECK(!decodeDriftwmBookmarks(nlohmann::json{{"Ok", 5}}).has_value());
    TEST_CHECK(!decodeDriftwmBookmarks(nlohmann::json{{"Err", "no"}}).has_value());
    TEST_CHECK(!decodeDriftwmBookmarks(nlohmann::json::object()).has_value());
  }

  void testFitCentersAndScales() {
    CanvasRect content;
    content.include(0.0F, 0.0F, 100.0F, 100.0F);
    content.include(100.0F, 50.0F, 200.0F, 200.0F);
    // Canvas extent: x [-50, 200], y [-50, 150].
    TEST_CHECK(near(content.minX, -50.0F));
    TEST_CHECK(near(content.maxX, 200.0F));
    TEST_CHECK(near(content.minY, -50.0F));
    TEST_CHECK(near(content.maxY, 150.0F));

    const auto projection = driftwm_minimap::fit(content, 400.0F, 200.0F, 0.0F);
    // The limiting axis is height: 200 canvas units into 200 px, so the content
    // fills the height and is inset horizontally (250 units into 400 px).
    TEST_CHECK(near(projection.scale, 1.0F));
    const auto topLeft = driftwm_minimap::project(projection, content.minX, content.maxY);
    TEST_CHECK(near(topLeft.x, 75.0F));
    TEST_CHECK(near(topLeft.y, 0.0F));
    const auto bottomRight = driftwm_minimap::project(projection, content.maxX, content.minY);
    TEST_CHECK(near(bottomRight.x, 325.0F));
    TEST_CHECK(near(bottomRight.y, 200.0F));
    // The fit is centered: opposite corners are equidistant from the edges.
    TEST_CHECK(near(topLeft.x, 400.0F - bottomRight.x));
    TEST_CHECK(near(topLeft.y, 200.0F - bottomRight.y));
  }

  void testFitAppliesPadding() {
    CanvasRect content;
    content.includePoint(0.0F, 0.0F);
    content.includePoint(100.0F, 0.0F);
    // 100 canvas units wide, 1 tall, so width limits: 400 - 2*20 available.
    const auto projection = driftwm_minimap::fit(content, 400.0F, 100.0F, 20.0F);
    TEST_CHECK(near(projection.scale, 3.6F));
    const auto left = driftwm_minimap::project(projection, 0.0F, 0.0F);
    const auto right = driftwm_minimap::project(projection, 100.0F, 0.0F);
    TEST_CHECK(near(left.x, 20.0F));
    TEST_CHECK(near(right.x, 380.0F));
  }

  void testFitDegenerateInputs() {
    // Empty content, a zero-sized widget, and a single point all stay usable
    // rather than dividing by zero or producing a NaN offset.
    const auto empty = driftwm_minimap::fit(CanvasRect{}, 200.0F, 100.0F, 4.0F);
    TEST_CHECK(near(driftwm_minimap::project(empty, 0.0F, 0.0F).x, 100.0F));
    TEST_CHECK(near(driftwm_minimap::project(empty, 0.0F, 0.0F).y, 50.0F));

    const auto zeroWidget = driftwm_minimap::fit(CanvasRect{}, 0.0F, 0.0F, 0.0F);
    TEST_CHECK(std::isfinite(zeroWidget.scale));
    TEST_CHECK(std::isfinite(zeroWidget.offsetX));

    CanvasRect point;
    point.includePoint(10.0F, 10.0F);
    const auto pointProjection = driftwm_minimap::fit(point, 100.0F, 100.0F, 0.0F);
    TEST_CHECK(std::isfinite(pointProjection.scale));
    TEST_CHECK(near(driftwm_minimap::project(pointProjection, 10.0F, 10.0F).x, 50.0F));
    TEST_CHECK(near(driftwm_minimap::project(pointProjection, 10.0F, 10.0F).y, 50.0F));

    // A non-finite contribution never joins the bounds.
    CanvasRect poisoned;
    poisoned.include(0.0F, 0.0F, 10.0F, 10.0F);
    poisoned.include(std::numeric_limits<float>::infinity(), 0.0F, 10.0F, 10.0F);
    TEST_CHECK(near(poisoned.maxX, 5.0F));
  }

  void testUnprojectInvertsProject() {
    CanvasRect content;
    content.include(-400.0F, -300.0F, 800.0F, 600.0F);
    content.include(400.0F, 300.0F, 800.0F, 600.0F);
    const auto projection = driftwm_minimap::fit(content, 220.0F, 44.0F, 3.0F);

    // Clicking anywhere on the map must land on the canvas point it shows,
    // including off-center pixels and negative canvas coordinates.
    for (const auto& [canvasX, canvasY] :
         {std::pair<double, double>{0.0, 0.0},
          std::pair<double, double>{-1234.5, 987.25},
          std::pair<double, double>{0.0, -0.5},
          std::pair<double, double>{4200.0, -4200.0}}) {
      const auto pixel = driftwm_minimap::project(projection, static_cast<float>(canvasX), static_cast<float>(canvasY));
      const auto [backX, backY] = driftwm_minimap::unproject(projection, pixel.x, pixel.y);
      TEST_CHECK(near(backX, canvasX, 1.0e-2));
      TEST_CHECK(near(backY, canvasY, 1.0e-2));
    }

    // A projection that cannot be inverted yields the origin, not an infinity.
    const auto broken = driftwm_minimap::unproject(Projection{.scale = 0.0F}, 10.0F, 10.0F);
    TEST_CHECK(near(broken.first, 0.0));
    TEST_CHECK(near(broken.second, 0.0));
  }

  void testProjectRectUsesCenterAndSize() {
    CanvasRect content;
    content.include(-100.0F, -100.0F, 200.0F, 200.0F);
    content.include(100.0F, 100.0F, 200.0F, 200.0F);
    const auto projection = driftwm_minimap::fit(content, 200.0F, 200.0F, 0.0F);
    // 400 canvas units across 200 px.
    TEST_CHECK(near(projection.scale, 0.5F));

    // Center (0, 0) is the middle of the map in both axes; Y-up means positive
    // canvas Y lands above the widget origin.
    const auto centered = driftwm_minimap::projectRect(projection, 0.0F, 0.0F, 100.0F, 50.0F);
    TEST_CHECK(near(centered.x, 75.0F));
    TEST_CHECK(near(centered.y, 87.5F));
    TEST_CHECK(near(centered.width, 50.0F));
    TEST_CHECK(near(centered.height, 25.0F));

    const auto above = driftwm_minimap::projectRect(projection, 0.0F, 100.0F, 100.0F, 50.0F);
    TEST_CHECK(near(above.y, 37.5F));
    const auto below = driftwm_minimap::projectRect(projection, 0.0F, -100.0F, 100.0F, 50.0F);
    TEST_CHECK(near(below.y, 137.5F));

    // A sub-pixel window stays visible as the requested minimum, and a window
    // with no geometry at all collapses rather than drawing a stray sliver.
    const auto tiny = driftwm_minimap::projectRect(projection, 0.0F, 0.0F, 0.0F, 0.0F, 3.0F);
    TEST_CHECK(near(tiny.width, 3.0F));
    TEST_CHECK(near(tiny.height, 3.0F));
    const auto dot = driftwm_minimap::projectRect(projection, 0.0F, 0.0F, 0.0F, 0.0F);
    TEST_CHECK(near(dot.width, 1.0F));
    TEST_CHECK(near(dot.height, 1.0F));
    const auto empty = driftwm_minimap::projectRect(projection, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F);
    TEST_CHECK(near(empty.width, 0.0F));
    TEST_CHECK(near(empty.height, 0.0F));
    // A negative size is nonsense rather than a flipped rect.
    const auto negative = driftwm_minimap::projectRect(projection, 0.0F, 0.0F, -50.0F, -50.0F, 0.0F);
    TEST_CHECK(near(negative.width, 0.0F));
    TEST_CHECK(near(negative.height, 0.0F));

    const auto point = driftwm_minimap::projectPoint(projection, 0.0F, 0.0F, 4.0F);
    TEST_CHECK(near(point.x, 98.0F));
    TEST_CHECK(near(point.y, 98.0F));
    TEST_CHECK(near(point.width, 4.0F));
  }

  void testDesktopWidgetSchema() {
    // The minimap is a desktop-widget type, not a bar widget: the type has to be
    // pickable, and the editor has to be able to label it.
    const auto& typeSpecs = desktop_settings::desktopWidgetTypeSpecs();
    const bool isType =
        std::ranges::any_of(typeSpecs, [](const desktop_settings::DesktopWidgetTypeSpec& spec) {
          return spec.type == "driftwm_minimap";
        });
    TEST_CHECK(isType);
    TEST_CHECK(!desktop_settings::desktopWidgetTypeLabel("driftwm_minimap").empty());

    const auto specs = desktop_settings::desktopWidgetSettingSpecs("driftwm_minimap");
    TEST_CHECK(!specs.empty());

    const auto defaultOf = [&specs](std::string_view key) -> std::optional<WidgetSettingValue> {
      for (const auto& spec : specs) {
        if (spec.schema.key == key) {
          return spec.schema.defaultValue;
        }
      }
      return std::nullopt;
    };
    const auto asBool = [](const std::optional<WidgetSettingValue>& value) {
      const auto* held = value.has_value() ? std::get_if<bool>(&*value) : nullptr;
      return held != nullptr && *held;
    };
    const auto asInt = [](const std::optional<WidgetSettingValue>& value) -> std::int64_t {
      const auto* held = value.has_value() ? std::get_if<std::int64_t>(&*value) : nullptr;
      return held != nullptr ? *held : -1;
    };

    // Every field the widget reads must be declared, and every declaration must
    // carry a translation key pair the editor can render.
    for (const char* key : {
             "map_width",
             "map_height",
             "show_outputs",
             "show_windows",
             "show_titles",
             "show_layers",
             "show_pinned",
             "show_bookmarks",
             "show_fullscreen_indicator",
             "click_to_move",
             "smoothing_ms",
             "map_background_color",
             "map_output_color",
             "map_output_active_color",
             "map_window_color",
             "map_window_focused_color",
             "map_window_suspended_color",
             "map_viewport_color",
             "map_bookmark_color",
             "map_layer_color",
             "map_pinned_color",
             "map_fullscreen_color",
             "map_text_color",
         }) {
      TEST_CHECK(defaultOf(key).has_value());
    }
    for (const auto& spec : specs) {
      TEST_CHECK(!spec.schema.key.empty());
      TEST_CHECK(!spec.labelKey.empty());
      TEST_CHECK(!spec.descriptionKey.empty());
    }

    TEST_CHECK(asInt(defaultOf("map_width")) == 220);
    TEST_CHECK(asInt(defaultOf("map_height")) == 120);
    TEST_CHECK(asInt(defaultOf("smoothing_ms")) == 120);
    TEST_CHECK(asBool(defaultOf("show_outputs")));
    TEST_CHECK(asBool(defaultOf("show_windows")));
    TEST_CHECK(asBool(defaultOf("show_titles")));
    TEST_CHECK(asBool(defaultOf("show_pinned")));
    TEST_CHECK(asBool(defaultOf("show_bookmarks")));
    TEST_CHECK(asBool(defaultOf("click_to_move")));
    TEST_CHECK(asBool(defaultOf("show_fullscreen_indicator")));
    // Canvas layers are off by default: they are niche and add clutter.
    TEST_CHECK(!asBool(defaultOf("show_layers")));

    // The size and easing knobs stay inside a range the widget can draw.
    const auto rangeOf = [&specs](std::string_view key, double minValue, double maxValue) {
      for (const auto& spec : specs) {
        if (spec.schema.key == key) {
          TEST_CHECK(spec.schema.minValue == minValue);
          TEST_CHECK(spec.schema.maxValue == maxValue);
          return;
        }
      }
      TEST_CHECK(false);
    };
    rangeOf("map_width", 32.0, 2048.0);
    rangeOf("map_height", 32.0, 1024.0);
    rangeOf("smoothing_ms", 0.0, 1000.0);

    // The map draws its own framed backdrop, so the shared tile background is
    // off unless it is asked for.
    const auto commonSpecs = desktop_settings::commonDesktopWidgetSettingSpecs("driftwm_minimap");
    bool sawBackground = false;
    for (const auto& spec : commonSpecs) {
      if (spec.schema.key == "background") {
        sawBackground = true;
        const auto* held = std::get_if<bool>(&spec.schema.defaultValue);
        TEST_CHECK(held != nullptr && !*held);
      }
    }
    TEST_CHECK(sawBackground);

    // A fresh widget of this type is seeded with every declared default, and the
    // schema projection `config validate` uses carries the same fields.
    std::unordered_map<std::string, WidgetSettingValue> defaults;
    desktop_settings::applyAllDesktopWidgetDefaultSettings(defaults, "driftwm_minimap");
    TEST_CHECK(defaults.size() == specs.size() + commonSpecs.size());
    TEST_CHECK(defaults.contains("map_width"));
    TEST_CHECK(defaults.contains("map_height"));

    const auto schema = desktop_settings::desktopWidgetSettingSchema("driftwm_minimap");
    TEST_CHECK(schema.size() == specs.size() + commonSpecs.size());
    const bool schemaHasMapHeight =
        std::ranges::any_of(schema, [](const auto& field) { return field.key == "map_height"; });
    TEST_CHECK(schemaHasMapHeight);

    // The type is no longer a bar widget: the bar registry must not know it.
    TEST_CHECK(!settings::isBuiltInWidgetType("driftwm_minimap"));
    const auto barSpecs = settings::widgetSettingSpecs("driftwm_minimap", nullptr, "sans-serif", false);
    const bool barKnowsIt =
        std::ranges::any_of(barSpecs, [](const settings::WidgetSettingSpec& spec) {
          return spec.schema.key == "map_width";
        });
    TEST_CHECK(!barKnowsIt);
  }

  void testDecodedStateProjectsOntoTheMap() {
    // Ties the two halves together: a real snapshot's window and viewport rects
    // must land inside the widget the projection was fitted to.
    const auto state = decodeDriftwmState(fullState());
    TEST_CHECK(state.has_value());
    if (!state.has_value()) {
      return;
    }

    CanvasRect content;
    for (const auto& window : state->windows) {
      content.include(window.centerX, window.centerY, window.width, window.height);
    }
    const auto& output = state->outputs[0];
    content.include(output.cameraX, output.cameraY, output.width / output.zoom, output.height / output.zoom);

    const auto projection = driftwm_minimap::fit(content, 220.0F, 44.0F, 3.0F);
    for (const auto& window : state->windows) {
      const auto rect = driftwm_minimap::projectRect(
        projection, window.centerX, window.centerY, window.width, window.height
      );
      TEST_CHECK(rect.x >= 0.0F);
      TEST_CHECK(rect.y >= 0.0F);
      TEST_CHECK(rect.x + rect.width <= 220.0F);
      TEST_CHECK(rect.y + rect.height <= 44.0F);
    }

    // The focused output's viewport projects inside the map, and its corners
    // unproject back to the canvas bounds the fit was built from.
    const auto viewport = driftwm_minimap::projectRect(
      projection, output.cameraX, output.cameraY, output.width / output.zoom, output.height / output.zoom
    );
    TEST_CHECK(viewport.width <= 220.0F);
    TEST_CHECK(viewport.height <= 44.0F);
    const auto [canvasX, canvasY] = driftwm_minimap::unproject(projection, viewport.x, viewport.y);
    TEST_CHECK(near(canvasX, -960.0, 1.0));
    TEST_CHECK(near(canvasY, 540.0, 1.0));
  }

} // namespace

int main() {
  testStateDecoding();
  testStateDecodingRejectsUnusablePayloads();
  testStateDecodingToleratesPartialAndMalformedEntries();
  testBookmarkDecoding();
  testFitCentersAndScales();
  testFitAppliesPadding();
  testFitDegenerateInputs();
  testUnprojectInvertsProject();
  testProjectRectUsesCenterAndSize();
  testDesktopWidgetSchema();
  testDecodedStateProjectsOntoTheMap();
  return 0;
}
