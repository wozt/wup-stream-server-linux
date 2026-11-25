#!/usr/bin/env bash
# Verify first launch shows GTK and WM_DELETE_WINDOW closes to tray.
# Run under: xvfb-run -a bash tools/test-ui-startup.sh path/to/AppImage
set -euo pipefail

APP="${1:?usage: test-ui-startup.sh path/to/AppImage}"
APP="$(readlink -f "$APP")"
[[ -s "$APP" ]] || { echo "No AppImage: $APP" >&2; exit 1; }
command -v xdotool >/dev/null

tmp="$(mktemp -d)"
pid=""
cleanup() {
  if [[ -n "$pid" ]]; then
    kill -TERM "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
  fi
  if [[ "${KEEP_SMOKE_LOG:-0}" == "1" ]]; then
    echo "Smoke-test logs: $tmp" >&2
  else
    rm -rf "$tmp"
  fi
}
trap cleanup EXIT

# A clean isolated HOME prevents autostart and lock-file interference.
export HOME="$tmp/home"
export XDG_CONFIG_HOME="$HOME/.config"
mkdir -p "$HOME"
export APPIMAGE_EXTRACT_AND_RUN="${APPIMAGE_EXTRACT_AND_RUN:-1}"

"$APP" >"$tmp/wupstreamd.log" 2>&1 &
pid="$!"

window=""
for i in $(seq 1 50); do
  if ! kill -0 "$pid" 2>/dev/null; then
    cat "$tmp/wupstreamd.log" >&2
    echo "wupstreamd died before showing its GTK window" >&2
    exit 1
  fi
  window="$(xdotool search --onlyvisible --class wupstreamd 2>/dev/null | head -n 1 || true)"
  [[ -n "$window" ]] && break
  sleep 0.25
done
if [[ -z "$window" ]]; then
  cat "$tmp/wupstreamd.log" >&2
  echo "No visible GTK window on first launch" >&2
  exit 1
fi
echo "PASS: window $window visible at startup"

# The custom titlebar X close must cause a hide, not destroy/quit.
# WM_DELETE_WINDOW exercises the same registered gtk_widget_hide_on_delete.
xdotool windowclose "$window"
hidden=0
for i in $(seq 1 24); do
  if ! xdotool search --onlyvisible --class wupstreamd >/dev/null 2>&1; then
    hidden=1
    break
  fi
  sleep 0.25
done
if [[ "$hidden" != 1 ]]; then
  cat "$tmp/wupstreamd.log" >&2
  echo "Window remained visible after close event" >&2
  exit 1
fi
if ! kill -0 "$pid" 2>/dev/null; then
  cat "$tmp/wupstreamd.log" >&2
  echo "Application exited instead of staying in tray" >&2
  exit 1
fi
echo "PASS: GTK window hidden, process still running (tray mode)"
