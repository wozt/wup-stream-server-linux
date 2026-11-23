project('wupstreamd', 'c',
  version: '0.1.0',
  default_options: ['c_std=c11', 'warning_level=2'])

gtkdep = dependency('gtk+-3.0')
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
threads = dependency('threads')
m_dep = meson.get_compiler('c').find_library('m', required: true)

# libwupstream is built by ../wup-stream/host/build.sh
wup_lib = meson.project_source_root() / '../wup-stream/build/host/libwupstream.a'
wup_inc = include_directories('../wup-stream/host')

executable('wupstreamd',
  'src/main.c',
  'src/settings.c',
  'src/x11util.c',
  'src/capture.c',
  'src/capture_async.c', # WUP_V912_CAPTURE_OPTIMIZATION
  'src/audio.c',
  'src/input.c',
  'src/select.c',
  'src/window_pick.c',
  'src/server.c',
  'src/ui.c',
  'src/viz.c',
  'src/i18n.c', # WUP_V6_JSON_LANGUAGES
  include_directories: [include_directories('src'), wup_inc],
  dependencies: [gtkdep, x11, xrandr, xtst, xext, xinerama, json_glib, threads, m_dep, swscale, xcb, xcbshm, x11xcb, pulse, cairo],
  link_args: [wup_lib],
  install: true)

install_data('data/wupstreamd.desktop', install_dir: get_option('datadir') / 'applications')
install_data('data/wupstreamd.svg', install_dir: get_option('datadir') / 'icons' / 'hicolor' / 'scalable' / 'apps')
# WUP_V4_THEME_SWITCHER: raster fallback, 256px taskbar/launcher icon
install_data('data/wupstreamd.png', install_dir: get_option('datadir') / 'icons' / 'hicolor' / '256x256' / 'apps')
# WUP_V5_JSON_THEMES: install bundled palettes for installed binary
install_subdir('data/themes', install_dir: get_option('datadir') / 'wupstreamd')
install_subdir('data/languages', install_dir: get_option('datadir') / 'wupstreamd') # WUP_V6_JSON_LANGUAGES
# WUP_V8_UI_POLISH: install Wii U vector button glyphs
install_subdir('data/icons/wiiu', install_dir: get_option('datadir') / 'wupstreamd' / 'icons')
