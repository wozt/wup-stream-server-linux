project('wupstreamd', 'c',
  version: '0.1.0',
  default_options: ['c_std=c11', 'warning_level=2'])

gtkdep = dependency('gtk+-3.0')
pangocairo = dependency('pangocairo') # WUP_V914_GAMEPAD_LOCALE
x11 = dependency('x11')
xrandr = dependency('xrandr')
xtst = dependency('xtst')
xext = dependency('xext')
xcb = dependency('xcb')
xcbshm = dependency('xcb-shm')
x11xcb = dependency('x11-xcb')
pulse = dependency('libpulse-simple')
cairo = dependency('cairo')
xinerama = dependency('xinerama')
json_glib = dependency('json-glib-1.0')
swscale = dependency('libswscale')
drm = dependency('libdrm')
gbm = dependency('gbm')
egl = dependency('egl')
gles = dependency('glesv2')
threads = dependency('threads')
m_dep = meson.get_compiler('c').find_library('m', required: true)

# WUP_SELF_CONTAINED_LIB: download the pinned sources and build the static
# library within this project's Meson build/ directory (no sibling checkout).
# Both the header and the archive come from this one custom target.
wup_python = find_program('python3')
wup_dep = custom_target('wupstream-embedded',
  output: ['libwupstream.a', 'wupstream.h'],
  command: [wup_python, files('tools/build_wupstream_dep.py'),
            '@OUTPUT0@', '@OUTPUT1@'],
  build_by_default: true)

executable('wupstreamd',
  'src/main.c',
  'src/settings.c',
  'src/x11util.c',
  'src/capture.c',
  'src/capture_kms.c',
  'src/capture_async.c', # WUP_V912_CAPTURE_OPTIMIZATION
  'src/audio.c',
  'src/input.c',
  'src/select.c',
  'src/window_pick.c',
  'src/server.c',
  'src/ui.c',
  'src/viz.c',
  'src/i18n.c', # WUP_V6_JSON_LANGUAGES
  wup_dep[1], # generated wupstream.h (orders C compilation after fetch)
  include_directories: [include_directories('src')],
  dependencies: [gtkdep, pangocairo, x11, xrandr, xtst, xext, xinerama, json_glib, threads, m_dep, swscale, drm, gbm, egl, gles, xcb, xcbshm, x11xcb, pulse, cairo],
  link_with: wup_dep[0], # generated libwupstream.a
  install: true)

executable('wup-kms-helper', 'src/kms_helper.c', dependencies: [drm], install: false)

install_data('data/wupstreamd.desktop', install_dir: get_option('datadir') / 'applications')
install_data('data/wupstreamd.svg', install_dir: get_option('datadir') / 'icons' / 'hicolor' / 'scalable' / 'apps')
# WUP_V4_THEME_SWITCHER: raster fallback, 256px taskbar/launcher icon
install_data('data/wupstreamd.png', install_dir: get_option('datadir') / 'icons' / 'hicolor' / '256x256' / 'apps')
# WUP_V5_JSON_THEMES: install bundled palettes for installed binary
install_subdir('data/themes', install_dir: get_option('datadir') / 'wupstreamd')
install_subdir('data/languages', install_dir: get_option('datadir') / 'wupstreamd') # WUP_V6_JSON_LANGUAGES
# WUP_V8_UI_POLISH: install Wii U vector button glyphs
install_subdir('data/icons/wiiu', install_dir: get_option('datadir') / 'wupstreamd' / 'icons')
