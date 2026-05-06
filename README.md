# driftwm-noctalia

**_Noctalia Shell — native driftwm infinite canvas integration_**

<p align="center">
  <img src="https://assets.noctalia.dev/noctalia-logo.svg?v=2" alt="Noctalia Logo" style="width: 192px" />
</p>

<p align="center">
  <a href="https://github.com/youssefvdel/driftwm-noctalia">
    <img src="https://img.shields.io/badge/🌙_driftwm--noctalia-A8AEFF?style=for-the-badge&labelColor=0C0D11" alt="driftwm-noctalia" />
  </a>
</p>

<p align="center">
  <a href="https://github.com/youssefvdel/driftwm-noctalia/commits">
    <img src="https://img.shields.io/github/last-commit/youssefvdel/driftwm-noctalia?style=for-the-badge&labelColor=0C0D11&color=A8AEFF&logo=git&logoColor=FFFFFF&label=commit" alt="Last commit" />
  </a>
  <a href="https://github.com/youssefvdel/driftwm-noctalia">
    <img src="https://img.shields.io/badge/driftwm_compatible-A8AEFF?style=for-the-badge&labelColor=0C0D11&logo=wayland&logoColor=FFFFFF" alt="driftwm compatible" />
  </a>
</p>

---

## What is this?

A fork of [Noctalia Shell](https://github.com/noctalia-dev/noctalia-shell) adapted to work natively with [driftwm](https://github.com/malbiruk/driftwm) — a trackpad-first infinite canvas Wayland compositor. Think Figma or Google Maps for your desktop.

### driftwm-native adaptations

| Area | Behavior |
|------|----------|
| **Workspaces** | Single synthetic "Canvas" workspace (driftwm has no workspaces) |
| **Wallpaper** | Auto-disabled — driftwm renders its own background via GLSL shaders |
| **Blur** | Auto-disabled — `ext-background-effect-v1` not supported. Toggle hidden in settings |
| **Overview** | Auto-disabled — no Niri-style overview (use `Mod+W` / `Mod+A` instead) |
| **Bar** | `WlrLayer.Overlay` + `keyboardFocus: OnDemand` for clickability above MainScreen mask |
| **Canvas state** | Polls `$XDG_RUNTIME_DIR/driftwm/state` (500ms) for viewport, zoom, layout, windows, per-output cameras |
| **Detection** | `XDG_CURRENT_DESKTOP=driftwm` (display manager) with state file fallback (TTY mode) |
| **Window tracking** | `foreign-toplevel-management` protocol — clicking a taskbar window pans the viewport to center it |
| **Display scales** | Queried via `wlr-randr --json` at startup |
| **Keyboard layout** | Reads active layout from state file — displayed in bar widget |
| **Canvas zoom widget** | Bar widget showing real-time zoom level (driftwm-only) |
| **IPC** | All `qs ipc` commands work (launcher, control center, notifications, session menu, bar toggle) |

---

## Installation

### Prerequisites

- **driftwm** installed and running (`curl -fsSL https://raw.githubusercontent.com/malbiruk/driftwm/main/install.sh | sudo sh`)
- **Build tools**: cmake, ninja, gcc/clang
- **Qt6 development**: qt6-qtbase-devel, qt6-qtdeclarative-devel, qt6-qtwayland-devel
- **Runtime deps**: pipewire, wireplumber (for audio), upower (for battery)

### Fedora

```bash
# Install build dependencies
sudo dnf install cmake ninja-build gcc-c++ \
  qt6-qtbase-devel qt6-qtdeclarative-devel qt6-qtwayland-devel \
  qt6-qtbase-private-devel wayland-devel wayland-protocols-devel \
  pipewire-devel libxkbcommon-devel

# Install runtime dependencies
sudo dnf install wlr-randr wl-clipboard brightnessctl
```

### Arch Linux

```bash
sudo pacman -S cmake ninja gcc qt6-base qt6-declarative qt6-wayland \
  wayland wayland-protocols pipewire libxkbcommon wlr-randr wl-clipboard
```

### Ubuntu/Debian

```bash
sudo apt install cmake ninja-build g++ qt6-base-dev qt6-declarative-dev \
  qt6-wayland-dev libwayland-dev wayland-protocols libpipewire-0.3-dev \
  libxkbcommon-dev wlr-randr wl-clipboard
```

---

### Step 1: Install noctalia-qs

Build the Quickshell fork from source (required — standard quickshell lacks noctalia's QML APIs):

```bash
git clone --depth 1 https://github.com/noctalia-dev/noctalia-qs.git ~/builds/noctalia-qs
cd ~/builds/noctalia-qs
cmake -GNinja -B build \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build build
sudo cmake --install build
```

Verify:
```bash
qs --version
# Expected: noctalia-qs 0.0.12 (or newer)
```

### Step 2: Install driftwm-noctalia

```bash
mkdir -p ~/.config/quickshell
git clone https://github.com/youssefvdel/driftwm-noctalia.git \
  ~/.config/quickshell/noctalia-shell
```

### Step 3: Configure driftwm

Add to `~/.config/driftwm/config.toml`:

```toml
# Launch the shell on startup
autostart = ["qs -c noctalia-shell -d"]

# Keybindings for noctalia panels
[keybindings]
"mod+d"   = "spawn qs ipc -c noctalia-shell call launcher toggle"
"mod+n"   = "spawn qs ipc -c noctalia-shell call notifications toggleHistory"
"mod+v"   = "spawn qs ipc -c noctalia-shell call launcher clipboard"
"mod+tab" = "spawn qs ipc -c noctalia-shell call sessionMenu toggle"
```

> **Important:** Use `-d` (daemonize) in the autostart — without it, the shell may not launch properly inside driftwm's autostart.

Apply config:
```bash
touch ~/.config/driftwm/config.toml   # driftwm auto-reloads on config change
```

Or restart driftwm.

### Step 4: Verify

After restarting driftwm, you should see the noctalia bar at the top of each monitor.

```bash
# Check the shell is running
ps aux | grep quickshell

# Test launcher via keybinding (Mod+d) or command line
qs ipc -c noctalia-shell call launcher toggle

# Test control center
qs ipc -c noctalia-shell call controlCenter toggle

# Check for errors
ls -t /run/user/1000/quickshell/by-id/ | head -1 | xargs -I{} \
  strings /run/user/1000/quickshell/by-id/{}/log.qslog | grep ERROR
```

---

## Feature Compatibility

| Feature | Status | Notes |
|---------|--------|-------|
| Bar / taskbar | **Full** | Window list, tray, clock, widgets, auto-hide, canvas zoom indicator |
| Launcher | **Full** | App search, clipboard history, session search, settings search |
| Control Center | **Full** | Audio, brightness, network, bluetooth, power profiles |
| Settings | **Full** | All panels functional. Blur/workspace options hidden on driftwm |
| Notifications | **Full** | D-Bus notifications, history, do-not-disturb |
| OSD | **Full** | Volume, brightness on-screen display |
| Session Menu | **Full** | Logout, suspend, reboot, lock |
| Lock Screen | **Full** | Clock, keyboard layout, session controls |
| System Tray | **Full** | XDG StatusNotifierItem support |
| Desktop Widgets | **Full** | Clock, stickers, system stats on canvas |
| Dock | **Full** | macOS-style dock with window previews |
| Display Scales | **Full** | Queried from `wlr-randr --json` on startup |
| Keyboard Layout | **Display** | Shows active layout from driftwm state file |
| Plugins | **Full** | Plugin system inherited from upstream |
| Themes / Color Schemes | **Full** | Material Design 3 theming, custom schemes |
| Canvas Zoom Widget | **Full** | Bar widget: real-time zoom level (driftwm-only) |
| Wallpaper | **Off** | Auto-disabled — driftwm renders GLSL shader background |
| Blur Effects | **Off** | Protocol not supported by driftwm |
| Overview Mode | **Off** | Use driftwm `Mod+W` (zoom-to-fit) / `Mod+A` (home) |
| Keyboard Layout Cycling | **No-op** | Widget shows layout, click does nothing — compositor has no layout-switch IPC |

---

## IPC Commands

All commands use `qs ipc -c noctalia-shell call <target> <action>`:

```bash
qs ipc -c noctalia-shell call launcher toggle
qs ipc -c noctalia-shell call notifications toggleHistory
qs ipc -c noctalia-shell call controlCenter toggle
qs ipc -c noctalia-shell call sessionMenu toggle
qs ipc -c noctalia-shell call bar toggle
qs ipc -c noctalia-shell call launcher clipboard
qs ipc -c noctalia-shell show
```

---

## Troubleshooting

### Shell not launching from autostart

Make sure `-d` (daemonize) is in the autostart command:
```toml
autostart = ["qs -c noctalia-shell -d"]
```

### Bar not visible / not clickable

The bar uses `WlrLayer.Overlay` on driftwm. If you changed the bar position or monitor config, the bar might be off-screen or hidden. Check:
```bash
cat /run/user/1000/driftwm/state | grep layers
```
You should see entries like `noctalia-bar-content-<output-name>`.

### IPC commands fail with "No running instances"

This means the shell process isn't registered properly. Kill any stale instances and restart:
```bash
pkill -f "qs -c noctalia-shell"
qs -c noctalia-shell -d
```

### High CPU usage

During first startup, the shell loads icons, plugins, and fonts (~1 minute at high CPU). After settling, expect ~10-15% CPU. If it stays above 30%, check for plugin issues:
```bash
# Check which plugins are loaded
cat ~/.config/noctalia/plugins.json
```

### Settings not persisting

Settings are stored in `~/.config/noctalia/settings.json`. If driftwm-specific settings (wallpaper=enabled, blur=enabled) keep reverting, the DriftwmService.initialize() auto-applies them at every startup. This is expected behavior.

---

## Known Limitations

| Limitation | Workaround |
|-----------|-----------|
| Keyboard layout can't be cycled via widget click | Use driftwm's built-in `grp:win_space_toggle` in `[input]` config |
| No overview/zoom-to-fit via shell | Use driftwm `Mod+W` (zoom-to-fit) or `Mod+A` (home toggle) |
| Canvas zoom widget not auto-added to bar | Add it manually: Settings → Bar → Widgets → CanvasPosition |
| Wallpaper management not available | Configure background in `~/.config/driftwm/config.toml` `[background]` section |

---

## What We Can Improve

### ✅ Done
- Display scale detection (wlr-randr)
- Keyboard layout display (state file)
- Canvas zoom widget
- State poll optimization (500ms)

### Next up
1. **Window rules management** — Settings panel for driftwm `[[window_rules]]`
2. **Home/Zoom-to-fit keybindings** — Expose `Mod+A`/`Mod+W` as Noctalia actions
3. **Background shader picker** — Preview and switch GLSL shaders from settings
4. **Multi-monitor canvas indicators** — Show other viewport positions on the canvas
5. **State file inotify** — Replace polling with file-watch for zero-overhead updates

---

## Supported Compositors

| Compositor | Status | Backend |
|------------|--------|---------|
| **driftwm** | **Native** | `DriftwmService` (state file polling + foreign-toplevel) |
| Hyprland | Inherited | `HyprlandService` (hyprctl IPC) |
| Niri | Inherited | `NiriService` (native C++ bindings) |
| Sway / Scroll | Inherited | `SwayService` (swaymsg IPC) |
| Labwc | Inherited | `LabwcService` (labwc-ipc) |
| MangoWC | Inherited | `MangoService` (native IPC) |
| Generic | Inherited | `ExtWorkspaceService` (ext-workspace-v1 protocol) |

---

## Architecture

```
shell.qml (entry point)
├── Services/Compositor/CompositorService.qml  (detection router)
│   └── DriftwmService.qml  ★ this fork's backend
├── Modules/
│   ├── Bar/            (taskbar + widgets including CanvasPosition)
│   ├── Panels/         (launcher, control center, settings, etc.)
│   ├── MainScreen/     (per-output panel host + bar)
│   ├── Dock/           (macOS-style dock)
│   ├── Background/     (wallpaper — disabled on driftwm)
│   ├── Notification/   (notification popups)
│   ├── OSD/            (on-screen display)
│   └── LockScreen/     (session lock)
├── Services/           (backend logic)
└── Commons/            (shared utilities, settings, i18n)
```

---

## Upstream

This is a fork of [Noctalia Shell](https://github.com/noctalia-dev/noctalia-shell) by [noctalia-dev](https://github.com/noctalia-dev). All credit for the original shell goes to the Noctalia team.

[PR #2630](https://github.com/noctalia-dev/noctalia-shell/pull/2630) open to merge driftwm support upstream.

## License

MIT License — see [LICENSE](./LICENSE) for details.
