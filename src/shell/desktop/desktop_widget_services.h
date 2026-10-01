#pragma once

class ClipboardService;
class CalendarService;
class ConfigService;
class FileWatcher;
class HttpClient;
class MprisService;
class PipeWireService;
class PipeWireSpectrum;
class RenderContext;
class SharedTextureCache;
class SystemMonitorService;
class WaylandConnection;
class WeatherService;

namespace compositors::driftwm {
  class DriftwmStateSource;
}

namespace scripting {
  class ScriptApiContext;
}

// Dependencies for plugin-backed (`[[desktop_widget]]`) widgets. All-null means
// plugin desktop widgets are unavailable in this factory (e.g. tests).
struct DesktopWidgetScriptDeps {
  scripting::ScriptApiContext* scriptApi = nullptr;
  FileWatcher* fileWatcher = nullptr;
  ClipboardService* clipboard = nullptr;
  ConfigService* configService = nullptr;
};

struct DesktopWidgetRuntimeServices {
  CalendarService* calendar = nullptr;
  PipeWireService* pipewire = nullptr;
  PipeWireSpectrum* pipewireSpectrum = nullptr;
  const WeatherService* weather = nullptr;
  MprisService* mpris = nullptr;
  HttpClient* httpClient = nullptr;
  SystemMonitorService* sysmon = nullptr;
  // Null on every compositor but DriftWM. The minimap has no geometry fallback,
  // so the factory builds nothing without it.
  compositors::driftwm::DriftwmStateSource* driftwmStateSource = nullptr;
  DesktopWidgetScriptDeps scriptDeps;
};

struct DesktopWidgetServices {
  WaylandConnection& wayland;
  ConfigService* config = nullptr;
  RenderContext* renderContext = nullptr;
  DesktopWidgetRuntimeServices runtime;
  SharedTextureCache* textureCache = nullptr;
};
