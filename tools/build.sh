#!/usr/bin/env bash
# project: wup-stream-server-linux
# Reconfigure and compile the server with its built-in pinned PC dependency.
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
if [[ -f build/meson-private/coredata.dat ]]; then
  meson setup build --reconfigure
else
  meson setup build
fi
meson compile -C build
printf '\nBuild complete: %s/build/wupstreamd\n' "$ROOT"
