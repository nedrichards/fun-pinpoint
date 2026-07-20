parser_test = executable(
  'test-parser',
  'test-parser.c',
  dependencies: [pinpoint_core_dep],
)

asset_monitor_test = executable(
  'test-asset-monitor',
  'test-asset-monitor.c',
  dependencies: [pinpoint_core_dep],
)

test('asset-monitor', asset_monitor_test)

display_selection_test = executable(
  'test-display-selection',
  'test-display-selection.c',
  dependencies: [pinpoint_core_dep],
)

test('display-selection', display_selection_test)

control_test = executable(
  'test-control',
  'test-control.c',
  dependencies: [pinpoint_core_dep],
)

test('control', control_test)

test(
  'parser',
  parser_test,
  args: files('fixtures/compatibility.pin'),
)

xmllint = find_program('xmllint', required: false)
gtksourceview_test_dep = dependency('gtksourceview-5', required: false)

if xmllint.found()
  language_validation_args = ['--noout', pinpoint_language]
  if gtksourceview_test_dep.found()
    gtksourceview_prefix = gtksourceview_test_dep.get_variable(
      pkgconfig: 'prefix',
      default_value: '/usr',
    )
    language_schema = gtksourceview_prefix / 'share/gtksourceview-5/language-specs/language2.rng'
    language_validation_args = [
      '--relaxng',
      language_schema,
      '--noout',
      pinpoint_language,
    ]
  endif
  test('validate Pinpoint language definition', xmllint, args: language_validation_args)
endif

if gtksourceview_test_dep.found()
  language_definition_test = executable(
    'test-language-definition',
    'test-language-definition.c',
    dependencies: [gtksourceview_test_dep],
  )
  test(
    'Pinpoint language highlighting',
    language_definition_test,
    args: [pinpoint_language, files('fixtures/editor-highlighting.pin')],
  )
endif

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

media_formats_test = executable(
  'test-media-formats',
  'test-media-formats.c',
  dependencies: [glib_dep, gio_dep, gstreamer_dep, gstreamer_pbutils_dep],
)

test(
  'media-formats',
  media_formats_test,
  args: [
    files('../data/introduction/bunny.webm'),
    files('fixtures/media-formats/pinpoint-vp8-vorbis.webm'),
    files('fixtures/media-formats/pinpoint-h264-baseline-aac.mp4'),
    files('fixtures/media-formats/pinpoint-h264-main.mov'),
    files('fixtures/media-formats/pinpoint-h264-high.mp4'),
    files('fixtures/media-formats/pinpoint-theora-vorbis.ogv'),
    files('fixtures/media-formats/pinpoint-animated.gif'),
    files('fixtures/media-formats/pinpoint-av1-opus.webm'),
    files('fixtures/media-formats/pinpoint-av1-aac.mp4'),
    files('fixtures/media-formats/corrupt.mp4'),
  ],
  timeout: 30,
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
    'fixtures/media-formats.pin',
    'fixtures/corrupt-video.pin',
  ),
  is_parallel: false,
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

svg_renderer_test = executable(
  'test-svg-renderers',
  'test-svg-renderers.c',
  dependencies: [gtk_dep, rsvg_dep],
)

test(
  'svg-renderers',
  svg_renderer_test,
  args: files('fixtures/svg-quality.svg'),
  timeout: 10,
)

pixel_fixture = files('fixtures/pixel-reference.pin')
svg_pixel_fixture = files('fixtures/svg-quality.pin')
raster_cache_fixture = files('fixtures/raster-cache.pin')
asset_store_fixture = files('fixtures/asset-store.pin')
test(
  'pixels-800x600',
  pixel_test,
  args: [
    pixel_fixture,
    svg_pixel_fixture,
    '800',
    '600',
    raster_cache_fixture,
    asset_store_fixture,
  ],
  timeout: 10,
)
test(
  'pixels-widescreen',
  pixel_test,
  args: [
    pixel_fixture,
    svg_pixel_fixture,
    '1280',
    '720',
    raster_cache_fixture,
    asset_store_fixture,
  ],
  timeout: 10,
)
test(
  'pixels-hidpi',
  pixel_test,
  args: [
    pixel_fixture,
    svg_pixel_fixture,
    '800',
    '600',
    raster_cache_fixture,
    asset_store_fixture,
  ],
  env: { 'GDK_SCALE': '2' },
  timeout: 10,
)
