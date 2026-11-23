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

```sh
git clone https://github.com/wozt/wup-stream-server-linux
cd wup-stream-server-linux
# build libwupstream first: https://github.com/ravensclawx/wup-stream
meson setup build
meson compile -C build
./build/wupstreamd
```

`libwupstream.a` is expected at `../wup-stream/build/host/libwupstream.a`
(adjust `meson.build` if your checkout lives elsewhere).

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
