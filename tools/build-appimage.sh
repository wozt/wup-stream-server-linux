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
# Unprivileged payload; installation requires explicit Polkit authentication.
# Keep privileged payload out of linuxdeploy ELF rewriting.
base64 build/wup-kms-helper > "$APPDIR/usr/bin/wup-kms-helper.b64"
chmod 0644 "$APPDIR/usr/bin/wup-kms-helper.b64"
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

# WUP_APPIMAGE_PIXBUF_RUNTIME: deploy matching loaders and their dependencies.
PIXBUF_DIR="$(pkg-config --variable=gdk_pixbuf_moduledir gdk-pixbuf-2.0)"
PIXBUF_QUERY="$(pkg-config --variable=gdk_pixbuf_query_loaders gdk-pixbuf-2.0)"
[[ -x "$PIXBUF_QUERY" ]] || PIXBUF_QUERY="$(dirname "$PIXBUF_DIR")/gdk-pixbuf-query-loaders"
loaders=("$PIXBUF_DIR"/*.so)
(( ${#loaders[@]} > 0 ))
[[ -x "$PIXBUF_QUERY" && -r "$PIXBUF_DIR/libpixbufloader-svg.so" ]]
loader_args=()
for loader in "${loaders[@]}"; do loader_args+=(--library "$loader"); done
"$PIXBUF_QUERY" "${loaders[@]}" | sed "s|$PIXBUF_DIR/|@APPDIR@/usr/lib/|g" > "$APPDIR/usr/share/wupstreamd/pixbuf-loaders.cache.in"
grep -Fq '@APPDIR@/usr/lib/libpixbufloader-svg.so' "$APPDIR/usr/share/wupstreamd/pixbuf-loaders.cache.in"
for theme in Adwaita hicolor; do
  mkdir -p "$APPDIR/usr/share/icons/$theme"
  cp -a "/usr/share/icons/$theme/." "$APPDIR/usr/share/icons/$theme/"
done
find "$APPDIR/usr/share/icons" -name icon-theme.cache -delete
mkdir -p "$APPDIR/usr/share/mime"
cp -a /usr/share/mime/. "$APPDIR/usr/share/mime/"
# Custom AppRun: never use the host's pixbuf module cache.
cat > "$OUT/wupstreamd-AppRun" <<'LAUNCHER'
#!/bin/sh
set -eu
APPDIR="$(CDPATH= cd -- "$(dirname "$0")" && pwd)"
export APPDIR
export LD_LIBRARY_PATH="$APPDIR/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export XDG_DATA_DIRS="$APPDIR/usr/share:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
export GTK_DATA_PREFIX="$APPDIR/usr"
export GDK_PIXBUF_MODULEDIR="$APPDIR/usr/lib"
cache="$(mktemp "${TMPDIR:-/tmp}/wup-pixbuf.XXXXXXXX")"
trap 'rm -f "$cache"' EXIT
escaped="$(printf '%s' "$APPDIR" | sed 's/[\\&|]/\\&/g')"
sed "s|@APPDIR@|$escaped|g" "$APPDIR/usr/share/wupstreamd/pixbuf-loaders.cache.in" > "$cache"
export GDK_PIXBUF_MODULE_FILE="$cache"
if [ "${1:-}" = --self-test-icons ]; then
  "$APPDIR/usr/bin/check-gtk-icons"
  exit $?
fi
"$APPDIR/usr/bin/wupstreamd" "$@" &
child=$!
trap 'kill -TERM "$child" 2>/dev/null || true' TERM
trap 'kill -INT "$child" 2>/dev/null || true' INT
wait "$child"
LAUNCHER
chmod 755 "$OUT/wupstreamd-AppRun"
cc -O2 -Wall -Wextra tools/check-gtk-icons.c -o "$APPDIR/usr/bin/check-gtk-icons" $(pkg-config --cflags --libs gtk+-3.0)

chmod -R a+rX "$APPDIR"

# linuxdeploy collects non-system runtime dependencies of the binary.
# Use no GTK plugin by default: the GTK plugin can be supplied via env when installed.
export OUTPUT="${OUTPUT:-$OUT/wupstreamd-x86_64.AppImage}"
# Keep the host GLVND/Mesa stack and GLib module ABI coherent.
exclude=()
for pattern in 'libEGL*.so*' 'libGLES*.so*' 'libGL*.so*' 'libgbm.so*' \
    'libdrm*.so*' 'libglapi.so*' 'libLLVM*.so*' 'libvulkan*.so*' \
    'libwayland-*.so*' 'libxcb*.so*' 'libxkbcommon*.so*' \
    'libglib-2.0.so*' 'libgio-2.0.so*' 'libgobject-2.0.so*' \
    'libgmodule-2.0.so*' 'libgthread-2.0.so*' 'libffi.so*' 'libpcre2-*.so*' \
    'libmount.so*' 'libblkid.so*' \
    'libstdc++.so*' 'libgcc_s.so*' 'libzstd.so*' 'libexpat.so*'; do
  exclude+=("--exclude-library=$pattern")
done
"$LD" "${exclude[@]}" --appdir "$APPDIR" \
   --executable "$APPDIR/usr/bin/wupstreamd" \
   --executable "$APPDIR/usr/bin/check-gtk-icons" \
   "${loader_args[@]}" \
   --custom-apprun "$OUT/wupstreamd-AppRun" \
   --desktop-file "$APPDIR/usr/share/applications/wupstreamd.desktop" \
   --icon-file "$APPDIR/usr/share/icons/hicolor/256x256/apps/wupstreamd.png" \
   --output appimage
# WUP_APPIMAGE_HOST_MOUNT_ABI: never publish an incompatible host GIO dependency.
for pattern in 'libmount.so*' 'libblkid.so*'; do
  if [[ -n "$(find "$APPDIR" -name "$pattern" -print -quit)" ]]; then
    echo "Unexpected bundled host dependency: $pattern" >&2
    exit 1
  fi
done
printf '\nCreated: %s\n' "$OUTPUT"

