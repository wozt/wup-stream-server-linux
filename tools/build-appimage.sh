#!/usr/bin/env bash
# WUP_V98_ALL_IN_ONE: optional x86_64 AppImage packaging helper.
# Requires linuxdeploy x86_64 in PATH or LINUXDEPLOY=/path/to/linuxdeploy.AppImage.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
if [[ "$(uname -m)" != "x86_64" ]]; then
  echo "AppImage builder targets x86_64 only" >&2; exit 1
fi
LD="${LINUXDEPLOY:-}"
if [[ -z "$LD" ]]; then LD="$(command -v linuxdeploy || true)"; fi
if [[ -z "$LD" ]]; then
  cat >&2 <<'EOF'
linuxdeploy is required. Download its x86_64 AppImage from:
https://github.com/linuxdeploy/linuxdeploy/releases
Then: chmod +x ~/Downloads/linuxdeploy-x86_64.AppImage
      LINUXDEPLOY=~/Downloads/linuxdeploy-x86_64.AppImage bash tools/build-appimage.sh
EOF
  exit 1
fi
if [[ ! -e "$LD" ]]; then echo "linuxdeploy not found: $LD" >&2; exit 1; fi
meson compile -C build
OUT="${OUT_DIR:-$ROOT/dist}"
APPDIR="$OUT/wupstreamd.AppDir"
mkdir -p "$OUT"
rm -rf "$APPDIR"
install -Dm755 build/wupstreamd "$APPDIR/usr/bin/wupstreamd"
install -Dm644 data/wupstreamd.desktop "$APPDIR/usr/share/applications/wupstreamd.desktop"
install -Dm644 data/wupstreamd.png "$APPDIR/usr/share/icons/hicolor/256x256/apps/wupstreamd.png"
# WUP_V911_APPIMAGE_ASSETS: bundle every runtime data directory into the squashfs.
mkdir -p "$APPDIR/usr/share/wupstreamd"
cp -a data/themes data/languages data/icons "$APPDIR/usr/share/wupstreamd/"
install -Dm644 data/wupstreamd.svg "$APPDIR/usr/share/icons/hicolor/scalable/apps/wupstreamd.svg"
# Fail early if a future packaging edit accidentally omits JSON resources.
shopt -s nullglob
theme_files=("$APPDIR"/usr/share/wupstreamd/themes/*.json)
language_files=("$APPDIR"/usr/share/wupstreamd/languages/*.json)
if (( ${#theme_files[@]} < 1 || ${#language_files[@]} < 1 )); then
  echo "Missing embedded themes or languages in AppDir" >&2
  exit 1
fi
for icon in btn_a.svg btn_tv.svg; do
  if [[ ! -f "$APPDIR/usr/share/wupstreamd/icons/wiiu/$icon" ]]; then
    echo "Missing embedded Wii U icon: $icon" >&2
    exit 1
  fi
done
printf "Bundled %d themes and %d language files into AppDir\n" "${#theme_files[@]}" "${#language_files[@]}"

# linuxdeploy collects non-system runtime dependencies of the binary.
# Use no GTK plugin by default: the GTK plugin can be supplied via env when installed.
export OUTPUT="${OUTPUT:-$OUT/wupstreamd-x86_64.AppImage}"
"$LD" --appdir "$APPDIR" \
   --executable "$APPDIR/usr/bin/wupstreamd" \
   --desktop-file "$APPDIR/usr/share/applications/wupstreamd.desktop" \
   --icon-file "$APPDIR/usr/share/icons/hicolor/256x256/apps/wupstreamd.png" \
   --output appimage
printf '\nCreated: %s\n' "$OUTPUT"
