# wup-stream-server (wupstreamd)

Stream your PC screens, windows or regions to a **Wii U GamePad** over Wi-Fi,
with GamePad controls sent back to the PC. Built on
[`wup-stream`](https://github.com/ravensclawx/wup-stream) (`libwupstream`).

One process, written in C: GTK3 settings UI + tray icon, direct X11 capture,
PulseAudio audio, virtual gamepad or keyboard/mouse injection.

## Features

- **Video sources**: full monitor (multi-monitor aware, XRandR geometry),
  any window (works while occluded, follows move/resize), or a free-selected
  screen region. Aspect modes: letterbox / crop / stretch.
- **Low latency**: direct XCB MIT-SHM capture + `libswscale`
  (`SWS_FAST_BILINEAR`) straight into `libwupstream` — no ffmpeg anywhere.
  x264 runs `veryfast`+`zerolatency`, zero B-frames/lookahead.
- **Audio both ways**: PC monitor → GamePad speakers and GamePad mic → PC,
  via direct `libpulse` streams (~1-3 ms fragments, bounded ring buffers).
  Audio is paced by wall clock, decoupled from video frames.
- **Inputs**: virtual evdev gamepad (`/dev/uinput`) **or** keyboard/mouse via
  XTest — selectable per session. Configurable key mapping for all buttons
  and the 8 stick directions, with button icons.
- **Touch gestures** (touch-as-mouse, works in both modes, confined to the
  streamed area): tap = click, drag = draw, long press = right click,
  double-tap + hold/move = drag & drop, triple-tap + move = scroll.
- **Command presets**: named per-app keyboard/mouse mappings (buttons +
  stick directions) saved as JSON, with chord support (`Ctrl+A`,
  `Alt+Ctrl+G`) and a built-in syntax help.
- **Gyroscope**: mouse or right-stick control, with several-second bias
  calibration persisted in settings. Off by default.
- **TV button**: cycles to the next monitor while streaming a full screen.
- **GamePad-only mode**: connects the pad and streams a test image (or black)
  with no capture — for sensor calibration. Audio is disabled in this mode.
- **Live pad visualization**: buttons, sticks, touch, artificial-horizon
  attitude indicator and gyro bars, drawn on a gamepad silhouette
  (`src/viz.c`, fed via `pad_viz_feed()`).
- Single instance (flock), autostart `.desktop`, tray icon, all UI in English.

## Requirements

- Linux, X11 (XCB + MIT-SHM), GTK3, PulseAudio (or pipewire-pulse)
- `libswscale`, `libx11-xcb`, `libXtst`, `libjson-glib`, `libpulse`, `libevdev`-free
  (`/dev/uinput` access for virtual gamepad mode)
- A Wii U GamePad running the
  [wup-stream pad app](https://github.com/ravensclawx/wup-stream) on the
  Homebrew Menu, joined to the same Wi-Fi

## Build

The server builds **without any sibling checkout of `wup-stream`**. On the
first compilation, Meson fetches a **pinned** revision of
[`wozt/wup-stream`](https://github.com/wozt/wup-stream), then runs its original
`host/build.sh` to fetch/patch x264 and compile the PC library. The downloaded
sources, generated `libwupstream.a`, and generated `wupstream.h` all live in
this project's `build/` directory, which is git-ignored. Rebuilds reuse them.

Requirements: `meson`, `ninja`, `python3`, `git`, `make`, a C compiler, `ar`,
and the system development libraries listed above. `nasm` is recommended for
fast x264 encoding. An internet connection is required for the initial build.

```sh
git clone https://github.com/wozt/wup-stream-server-linux
cd wup-stream-server-linux
bash tools/build.sh
./build/wupstreamd
```

The direct Meson commands work as well:

```sh
meson setup build
meson compile -C build
```

The upstream revision is pinned in `tools/build_wupstream_dep.py`. The upstream
build script separately pins x264. To change upstream version, deliberately
update that pin and rebuild; no generated binaries should be committed.

## Usage

1. On the GamePad: open **WUP Stream** in the Homebrew Menu ("Waiting for a
   Stream").
2. Launch `wupstreamd` — the tray icon opens the settings window.
3. Pick a video source, quality preset and input mode, then **Start stream**.

Settings live in `~/.config/wup-stream-serveur/settings.json`.
Autostart entry: `~/.config/autostart/wupstreamd.desktop`.

Debug: `WUPSTREAMD_DEBUG=1 ./build/wupstreamd` prints audio ring-buffer
fill levels and capture stats to stderr.

## License

Same as upstream wup-stream where applicable; button icons are CC0
(OpenGameArt "Controller Input Icons").


## Optional: click-to-pick and low-overhead stream metrics (V9.8)

- **Window one-click selection**: use the new mouse-pointer button in the
  Window row. The main window hides while the X11 picker runs. Left-click a
  visible EWMH client window, or press Escape/right-click to cancel.
- **Measured statistics** in GamePad Connection: transmitted frame rate,
  average X11 capture duration and average `wupstream_frame()` call duration
  (including pacing), plus capture errors, refreshed once a second. **These are
  not measurements of network bitrate or end-to-end Wi-Fi latency**: the current
  public `libwupstream` API exposes neither encoded bytes nor render timestamps.
- **Aspect handling**: the dropdown's order (Letterbox, Crop, Stretch) is now
  mapped explicitly to the persisted enum; no saved settings migration needed.
- **Runtime threading**: stream-state flags use C11 atomics and GUI changes
  publish complete settings snapshots to the stream thread at control changes,
  not per-frame.

## Optional: build an x86_64 AppImage

After `meson setup build` and compiling `libwupstream`, install `linuxdeploy`
for x86_64 from its official releases and run:

```sh
LINUXDEPLOY="$HOME/Downloads/linuxdeploy-x86_64.AppImage" \
  bash tools/build-appimage.sh
```

The script places the AppImage in `dist/`. Check that GTK3 and platform-specific
runtime plugins work on a second Debian machine before publishing a release.
Note: this X11 application cannot run as a Wayland-native capture program.
