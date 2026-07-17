parser_test = executable(
  'test-parser',
  'test-parser.c',
  dependencies: [pinpoint_core_dep],
)

test(
  'parser',
  parser_test,
  args: files('fixtures/compatibility.pin'),
)

performance_test = executable(
  'test-performance',
  'test-performance.c',
  dependencies: [pinpoint_core_dep],
)
test(
  'performance',
  performance_test,
  timeout: 10,
)

size_test = executable(
  'test-size',
  'test-size.c',
  dependencies: [glib_dep],
)
test(
  'size-budgets',
  size_test,
  args: [pinpoint_exe, files('../data/introduction/bunny.webm')],
)

lifecycle_test = executable(
  'test-lifecycle',
  'test-lifecycle.c',
  dependencies: pinpoint_ui_dep,
)

test(
  'lifecycle',
  lifecycle_test,
  args: files(
    'fixtures/missing-video.pin',
    'fixtures/legacy-transition.pin',
    'fixtures/native-transitions.pin',
    'fixtures/page-curl.pin',
    'fixtures/multi-monitor.pin',
    'fixtures/camera.pin',
  ),
  timeout: 30,
)

application_test = executable(
  'test-application',
  'test-application.c',
  dependencies: [glib_dep, gio_dep, gtk_dep],
)

media_pipeline_fixture = files('fixtures/media-pipeline.pin')

test(
  'application',
  application_test,
  args: [
    pinpoint_exe,
    files('fixtures/multi-monitor.pin'),
    media_pipeline_fixture,
  ],
  is_parallel: false,
  timeout: 15,
)

pixel_test = executable(
  'test-pixels',
  'test-pixels.c',
  dependencies: pinpoint_ui_dep,
)

pixel_fixture = files('fixtures/pixel-reference.pin')
svg_pixel_fixture = files('fixtures/svg-quality.pin')
test(
  'pixels-800x600',
  pixel_test,
  args: [pixel_fixture, svg_pixel_fixture, '800', '600'],
  timeout: 10,
)
test(
  'pixels-widescreen',
  pixel_test,
  args: [pixel_fixture, svg_pixel_fixture, '1280', '720'],
  timeout: 10,
)
test(
  'pixels-hidpi',
  pixel_test,
  args: [pixel_fixture, svg_pixel_fixture, '800', '600'],
  env: { 'GDK_SCALE': '2' },
  timeout: 10,
)
