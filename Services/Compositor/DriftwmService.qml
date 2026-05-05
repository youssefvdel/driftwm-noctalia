import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import qs.Commons

Item {
  id: root

  // Single synthetic workspace — driftwm has no workspaces
  property ListModel workspaces: ListModel {}
  property var windows: []
  property int focusedWindowIndex: -1
  property var trackedToplevels: new Set()

  // driftwm infinite canvas — all outputs share one canvas
  property bool globalWorkspaces: true

  // Canvas state from state file polling
  property real viewportX: 0
  property real viewportY: 0
  property real viewportZoom: 1.0
  property string activeLayout: ""
  property var windowList: []
  property var layerList: []
  property var outputStates: ({})

  signal workspaceChanged
  signal activeWindowChanged
  signal windowListChanged
  signal displayScalesChanged
  signal canvasStateChanged

  // ── State file path ──
  property string stateFilePath: ""
  property var lastStateMtime: 0

  function initialize() {
    const runtimeDir = Quickshell.env("XDG_RUNTIME_DIR") || "/run/user/1000";
    stateFilePath = runtimeDir + "/driftwm/state";

    // driftwm doesn't support ext-background-effect-v1 — disable blur
    if (typeof Settings !== 'undefined' && Settings.data?.general?.enableBlurBehind) {
      Settings.data.general.enableBlurBehind = false;
      Logger.i("DriftwmService", "Disabled blur (driftwm lacks ext-background-effect-v1)");
    }

    // Create single synthetic workspace
    workspaces.clear();
    workspaces.append({
      "id": "canvas",
      "idx": 1,
      "name": "Canvas",
      "output": "",
      "isFocused": true,
      "isActive": true,
      "isUrgent": false,
      "isOccupied": true,
      "oid": "canvas"
    });

    updateWindows();
    statePollTimer.start();
    pollStateFile();
    Logger.i("DriftwmService", "Service started (driftwm infinite canvas)");
  }

  // ── State file polling ──

  Timer {
    id: statePollTimer
    interval: 250
    running: false
    repeat: true
    onTriggered: root.pollStateFile()
  }

  property var statProcess: null
  property var readProcess: null

  function pollStateFile() {
    if (!stateFilePath) return;

    // Use Process to stat then read the state file
    if (!statProcess) {
      statProcess = new Process();
      statProcess.onFinished = function (exitCode, exitStatus) {
        if (exitCode !== 0) return;
        const mtime = parseInt(statProcess.stdout.trim()) || 0;
        if (mtime !== lastStateMtime) {
          lastStateMtime = mtime;
          readStateFile();
        }
      };
    }
    statProcess.command = ["stat", "-c", "%Y", stateFilePath];
    statProcess.running = true;
  }

  function readStateFile() {
    if (!readProcess) {
      readProcess = new Process();
      readProcess.onFinished = function (exitCode, exitStatus) {
        if (exitCode !== 0) return;
        parseStateFile(readProcess.stdout);
      };
    }
    readProcess.command = ["cat", stateFilePath];
    readProcess.running = true;
  }

  function parseStateFile(content) {
    if (!content) return;

    const lines = content.split("\n");
    let changed = false;
    const newOutputStates = {};
    const newWindowList = [];
    const newLayerList = [];

    for (const line of lines) {
      const eqIdx = line.indexOf("=");
      if (eqIdx < 0) continue;
      const key = line.substring(0, eqIdx).trim();
      const value = line.substring(eqIdx + 1).trim();

      switch (key) {
        case "x":
          const newX = parseFloat(value) || 0;
          if (Math.abs(viewportX - newX) > 0.5) { viewportX = newX; changed = true; }
          break;
        case "y":
          const newY = parseFloat(value) || 0;
          if (Math.abs(viewportY - newY) > 0.5) { viewportY = newY; changed = true; }
          break;
        case "zoom":
          const newZoom = parseFloat(value) || 1.0;
          if (Math.abs(viewportZoom - newZoom) > 0.001) { viewportZoom = newZoom; changed = true; }
          break;
        case "layout":
          if (activeLayout !== value) { activeLayout = value; changed = true; }
          break;
        case "windows":
          if (value) newWindowList.push(...value.split(",").map(s => s.trim()).filter(Boolean));
          break;
        case "layers":
          if (value) newLayerList.push(...value.split(",").map(s => s.trim()).filter(Boolean));
          break;
        default:
          // Per-output state: outputs.<name>.camera_x, outputs.<name>.camera_y, outputs.<name>.zoom
          if (key.startsWith("outputs.")) {
            const parts = key.split(".");
            if (parts.length === 3) {
              const outName = parts[1];
              const field = parts[2];
              if (!newOutputStates[outName]) newOutputStates[outName] = {};
              newOutputStates[outName][field] = parseFloat(value) || 0;
            }
          }
          break;
      }
    }

    // Update window/layer lists
    if (JSON.stringify(windowList) !== JSON.stringify(newWindowList)) {
      windowList = newWindowList;
      changed = true;
    }
    if (JSON.stringify(layerList) !== JSON.stringify(newLayerList)) {
      layerList = newLayerList;
      changed = true;
    }

    // Update output states
    if (JSON.stringify(outputStates) !== JSON.stringify(newOutputStates)) {
      outputStates = newOutputStates;
      changed = true;
    }

    if (changed) {
      canvasStateChanged();
    }
  }

  // ── Window tracking via foreign-toplevel ──

  Connections {
    target: ToplevelManager.toplevels
    function onValuesChanged() {
      updateWindows();
    }
  }

  function connectToToplevel(toplevel) {
    if (!toplevel) return;

    toplevel.activatedChanged.connect(() => {
      Qt.callLater(onToplevelActivationChanged);
    });

    toplevel.titleChanged.connect(() => {
      Qt.callLater(updateWindows);
    });
  }

  function onToplevelActivationChanged() {
    updateWindows();
    activeWindowChanged();
  }

  function updateWindows() {
    const newWindows = [];
    const toplevels = ToplevelManager.toplevels?.values || [];

    let focusedIdx = -1;
    let idx = 0;

    for (const toplevel of toplevels) {
      if (!toplevel) continue;

      if (!trackedToplevels.has(toplevel)) {
        connectToToplevel(toplevel);
        trackedToplevels.add(toplevel);
      }

      const output = (toplevel.screens && toplevel.screens.length > 0)
        ? (toplevel.screens[0].name || "") : "";

      const windowId = (toplevel.appId || "") + ":" + idx;

      newWindows.push({
        "id": windowId,
        "appId": toplevel.appId || "",
        "title": toplevel.title || "",
        "output": output,
        "workspaceId": "canvas",
        "isFocused": toplevel.activated || false,
        "toplevel": toplevel
      });

      if (toplevel.activated) {
        focusedIdx = idx;
      }
      idx++;
    }

    windows = newWindows;
    focusedWindowIndex = focusedIdx;
    windowListChanged();
  }

  // ── Window actions ──

  function focusWindow(window) {
    if (window.toplevel && typeof window.toplevel.activate === "function") {
      window.toplevel.activate();
    }
  }

  function closeWindow(window) {
    if (window.toplevel && typeof window.toplevel.close === "function") {
      window.toplevel.close();
    }
  }

  // ── Workspace switching is a no-op on driftwm ──

  function switchToWorkspace(workspace) {
    // driftwm has no workspaces — nothing to switch
  }

  // ── Session management ──

  function spawn(command) {
    try {
      Quickshell.execDetached(command);
    } catch (e) {
      Logger.e("DriftwmService", "Failed to spawn:", e);
    }
  }

  function logout() {
    try {
      Quickshell.execDetached(["sh", "-c", "kill -s SIGTERM $(pgrep driftwm) || loginctl terminate-session $XDG_SESSION_ID"]);
    } catch (e) {
      Logger.e("DriftwmService", "Failed to logout:", e);
    }
  }

  function turnOffMonitors() {
    try {
      Quickshell.execDetached(["wlr-randr", "--off"]);
    } catch (e) {
      Logger.e("DriftwmService", "Failed to turn off monitors:", e);
    }
  }

  function turnOnMonitors() {
    try {
      Quickshell.execDetached(["wlr-randr", "--on"]);
    } catch (e) {
      Logger.e("DriftwmService", "Failed to turn on monitors:", e);
    }
  }

  function cycleKeyboardLayout() {
    Logger.w("DriftwmService", "Keyboard layout cycling not supported");
  }

  function queryDisplayScales() {
    Logger.w("DriftwmService", "Display scale queries not supported via ToplevelManager");
  }

  function getFocusedScreen() {
    return null;
  }
}
