# wup-stream-server (wupstreamd)

Stream your PC screens, windows or regions to a **Wii U GamePad** over Wi-Fi,
with GamePad controls sent back to the PC. Built on
[`wup-stream`](https://github.com/ravensclawx/wup-stream) (`libwupstream`).

Written in C: GTK3 settings UI + tray icon, selectable X11 or DRM/KMS capture,
PulseAudio audio, virtual gamepad or keyboard/mouse injection. KMS uses a separate
privileged framebuffer-export helper; the main server runs as a normal user.

## Features

- **Video sources**: full monitor (multi-monitor aware, XRandR geometry),
  any window (works while occluded, follows move/resize), or a free-selected
  screen region. Aspect modes: letterbox / crop / stretch.
- **Low latency**: four selectable capture backends, CPU or GPU scaling,
  and direct integration with `libwupstream`. x264 remains the CPU encoder;
  low-latency mode uses `zerolatency` with no B-frames/lookahead.
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
- Single instance (flock), autostart `.desktop`, tray icon, JSON themes and translations (English default).

## Requirements

- Linux, X11, GTK3, PulseAudio (or pipewire-pulse)
- DRM/KMS build dependencies: libdrm, GBM, EGL and GLESv2 development headers.
  KMS runtime requires a compatible DRM driver, access to the relevant DRM nodes,
  and the privileged helper. Graphical installation requires `pkexec`, a Polkit
  authentication agent, `setcap`/`getcap` and the `video` group.
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
2. Launch `wupstreamd` — its settings window opens immediately. The **X**
   button hides it to the notification area without stopping the process;
   click the tray icon to reopen it. Use **Quit** in the tray menu to exit.
3. Pick a video source, quality preset and input mode, then **Start stream**.

Settings live in `~/.config/wup-stream-serveur/settings.json`.
Autostart entry: `~/.config/autostart/wupstreamd.desktop`.

Debug: `WUPSTREAMD_DEBUG=1 ./build/wupstreamd` prints audio ring-buffer
fill levels and capture stats to stderr.

## Capture backends and permissions

Select the backend in **Quality > Capture**. No automatic fallback is used.

| Backend | Acquisition | Scaling |
| --- | --- | --- |
| XCB MIT-SHM | X11 shared memory | CPU |
| XCB GetImage | X11 GetImage | CPU |
| DRM/KMS CPU | DMA-BUF / GBM mapping | CPU |
| DRM/KMS EGL | DMA-BUF / EGL import | GPU, offscreen GLES framebuffer |

KMS supports monitor capture with multi-monitor geometry and modifier planes
(including AMD DCC on the tested RX 6600). Use XCB for independent window capture;
KMS reads display scanout, not an isolated window backing store.

Measured on one RX 6600, capturing a 2560x1440 monitor to 854x480 at around 60 FPS
(CRF 14, 10000 kbps), before privilege isolation:

| Backend | Capture time per frame |
| --- | ---: |
| DRM/KMS EGL | 1.0 ms |
| DRM/KMS CPU | 8.8 ms |
| XCB MIT-SHM | 16.5 ms |
| XCB GetImage | 17.4 ms |

These are local snapshot measurements, not guaranteed performance or end-to-end latency.

The AppImage includes `wup-kms-helper`. On first KMS activation the application
requests administrator authentication through Polkit to install it at
`/usr/local/libexec/wupstreamd/wup-kms-helper` with `cap_sys_admin=ep`.
The main server does not need this capability or root privileges. Subsequent
launches reuse a compatible installed helper. Helper updates and removal require
explicit authorization; XCB requires no helper. Use **Quality > KMS helper** to
manage it, after stopping streaming and selecting XCB. Install only trusted builds:
CAP_SYS_ADMIN is powerful and the helper is executable by the authorized video group.
Removing the file prevents new launches but does not terminate existing helpers.

The helper exports DMA-BUF file descriptors over a private Unix socket, while
EGL scaling and x264 encoding remain in the unprivileged server. Native Wayland
capture is not supported. CI checks virtual X11 UI startup, not real GPU capture.

## GitHub Actions: automatic AppImage releases

Every successful push to `main` runs the Linux x86_64 build, packages an
AppImage, and tests the first-launch window and close-to-tray behavior on
a virtual X11 display. Pull requests and manual workflow runs build/test
but do not publish releases. Only tested AppImages are released.

The first successful push creates **v1.0.0.0**, then **v1.0.0.1**,
**v1.0.0.2**, and so on. Versions are allocated atomically from Git tags
only **after** the build and tests pass. A rerun of the same commit reuses
its existing version (and can restore missing assets); concurrent builds
cannot claim the same tag. Each GitHub Release has the versioned AppImage,
SHA256SUMS and automatically generated release notes.

The workflow needs GitHub Actions enabled and permission to create
tags/releases (`GITHUB_TOKEN` with `contents: write` in the release job).
The full CI/CD configuration lives in
`.github/workflows/release-appimage.yml`. It uses the pinned standalone
`libwupstream` build already shipped by this repository, so it needs
no checked-out `../wup-stream` directory.

The first build downloads its pinned upstream code/x264 and linuxdeploy.
For local AppImages, use `bash tools/build-appimage.sh` after setting
`LINUXDEPLOY` to the path of the official linuxdeploy AppImage.

## License

Same as upstream wup-stream where applicable; button icons are CC0
(OpenGameArt "Controller Input Icons").


## Optional: click-to-pick and low-overhead stream metrics (V9.8)

- **Window one-click selection**: use the new mouse-pointer button in the
  Window row. The main window hides while the X11 picker runs. Left-click a
  visible EWMH client window, or press Escape/right-click to cancel.
- **Measured statistics** in GamePad Connection: transmitted frame rate,
  average capture, acquisition and scaling durations and average `wupstream_frame()` call duration
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
