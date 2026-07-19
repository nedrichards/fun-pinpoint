pinpoint_resources = gnome.compile_resources(
  'pinpoint-resources',
  '../data/pinpoint.gresource.xml',
  source_dir: '../data',
  c_name: 'pinpoint',
  export: true,
)

pinpoint_core_sources = files(
  'pp-asset-monitor.c',
  'pp-asset-monitor.h',
  'pp-file-access.c',
  'pp-file-access.h',
  'pp-control.c',
  'pp-control.h',
  'pp-display-selection.c',
  'pp-display-selection.h',
  'pp-introduction.c',
  'pp-introduction.h',
  'pp-presentation.c',
  'pp-presentation.h',
  'pp-pdf.c',
  'pp-pdf.h',
  'pp-pdf-async.c',
  'pp-video-thumbnail.c',
  'pp-video-thumbnail.h',
  'pp-transition.c',
  'pp-transition.h',
  'pp-page-curl.c',
  'pp-page-curl.h',
  'pp-render.c',
  'pp-render.h',
)
pinpoint_core_sources += pinpoint_resources

pinpoint_core = static_library(
  'pinpoint-core',
  pinpoint_core_sources,
  dependencies: [glib_dep, gio_dep, gtk_dep, pango_dep, pangocairo_dep, cairo_dep, cairo_pdf_dep, gdk_pixbuf_dep, rsvg_dep, gstreamer_dep, gstreamer_app_dep, gstreamer_video_dep, json_glib_dep, math_dep],
  include_directories: include_directories('..'),
)

pinpoint_core_dep = declare_dependency(
  link_with: pinpoint_core,
  dependencies: [glib_dep, gio_dep, gtk_dep, pango_dep, pangocairo_dep, cairo_dep, cairo_pdf_dep, gdk_pixbuf_dep, rsvg_dep, gstreamer_dep, gstreamer_app_dep, gstreamer_video_dep, json_glib_dep, math_dep],
  include_directories: [include_directories('.'), include_directories('..')],
)

pinpoint_ui_dep = declare_dependency(
  sources: files(
    'pp-page-curl-view.c',
    'pp-page-curl-view.h',
    'pp-stage.c',
    'pp-stage.h',
    'pp-speaker.c',
    'pp-speaker.h',
  ),
  dependencies: [pinpoint_core_dep, gio_unix_dep, gstreamer_dep, epoxy_dep, libadwaita_dep],
  include_directories: include_directories('.'),
)

pinpoint_exe = executable(
  'pinpoint',
  'main.c',
  dependencies: pinpoint_ui_dep,
  include_directories: include_directories('..'),
  install: true,
)
