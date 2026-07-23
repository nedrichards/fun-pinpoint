project(
  'pinpoint',
  'c',
  version: '0.2.0',
  license: 'LGPL-2.1-or-later',
  meson_version: '>= 0.63.0',
  default_options: [
    'c_std=c17',
    'warning_level=2',
    'werror=true',
  ],
)

gnome = import('gnome')
i18n = import('i18n')
cc = meson.get_compiler('c')

pinpoint_id = 'com.nedrichards.pinpoint'
pinpoint_name = 'Pinpoint'
pinpoint_tagline = 'Excellent presentations for hackers'
pinpoint_version = meson.project_version()

glib_req = '>= 2.78'
gtk_req = '>= 4.22'

glib_dep = dependency('glib-2.0', version: glib_req)
gio_dep = dependency('gio-2.0', version: glib_req)
gio_unix_dep = dependency('gio-unix-2.0', version: glib_req)
gtk_dep = dependency('gtk4', version: gtk_req)
libadwaita_dep = dependency('libadwaita-1', version: '>= 1.8')
pango_dep = dependency('pango', version: '>= 1.50')
pangocairo_dep = dependency('pangocairo', version: '>= 1.50')
cairo_dep = dependency('cairo', version: '>= 1.16')
cairo_pdf_dep = dependency('cairo-pdf', version: '>= 1.16')
gdk_pixbuf_dep = dependency('gdk-pixbuf-2.0', version: '>= 2.42')
jpeg_dep = dependency('libjpeg')
rsvg_dep = dependency('librsvg-2.0', version: '>= 2.56')
gstreamer_dep = dependency('gstreamer-1.0', version: '>= 1.22')
gstreamer_app_dep = dependency('gstreamer-app-1.0', version: '>= 1.22')
gstreamer_pbutils_dep = dependency('gstreamer-pbutils-1.0', version: '>= 1.22')
gstreamer_video_dep = dependency('gstreamer-video-1.0', version: '>= 1.22')
json_glib_dep = dependency('json-glib-1.0', version: '>= 1.8')
epoxy_dep = dependency('epoxy', version: '>= 1.5')
math_dep = cc.find_library('m', required: true)

config_h = configuration_data()
config_h.set_quoted('GETTEXT_PACKAGE', 'pinpoint')
config_h.set_quoted('LOCALEDIR', get_option('prefix') / get_option('localedir'))
config_h.set_quoted('PINPOINT_APPLICATION_ID', pinpoint_id)
config_h.set_quoted('PINPOINT_TAGLINE', pinpoint_tagline)
config_h.set_quoted('PINPOINT_VERSION', pinpoint_version)
configure_file(output: 'config.h', configuration: config_h)

add_project_arguments(
  '-D_GNU_SOURCE',
  '-DG_LOG_USE_STRUCTURED=1',
  language: 'c',
)

subdir('src')
subdir('data')
subdir('tests')

if get_option('remote_prototypes')
  subdir('prototypes')
endif

summary({
  'Version': pinpoint_version,
  'Application ID': pinpoint_id,
  'Prefix': get_option('prefix'),
}, section: 'Pinpoint')
