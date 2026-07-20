#include <gtk/gtk.h>
#include <librsvg/rsvg.h>
#include <stdlib.h>

#define RENDER_WIDTH 800
#define RENDER_HEIGHT 600

G_DEFINE_AUTOPTR_CLEANUP_FUNC (RsvgHandle, g_object_unref)

typedef struct
{
  guint total;
  guint ignored;
  guint not_implemented;
} SvgErrors;

typedef struct
{
  guint64 samples;
  guint64 channel_error;
  guint differing_pixels;
  guint max_error;
} PixelDifference;

static GBytes *comparison_svg;

static const char style_element_svg[] =
  "<svg xmlns='http://www.w3.org/2000/svg' width='80' height='60'>"
  "<style>.background { fill: #3584e4; }</style>"
  "<rect class='background' width='80' height='60'/>"
  "</svg>";

static const char text_path_svg[] =
  "<svg xmlns='http://www.w3.org/2000/svg' width='160' height='60'>"
  "<defs><path id='baseline' d='M 5 40 C 45 5 115 5 155 40'/></defs>"
  "<text font-size='14'><textPath href='#baseline'>Pinpoint</textPath></text>"
  "</svg>";

static const char turbulence_svg[] =
  "<svg xmlns='http://www.w3.org/2000/svg' width='80' height='60'>"
  "<filter id='noise'><feTurbulence baseFrequency='.08' numOctaves='2'/></filter>"
  "<rect width='80' height='60' filter='url(#noise)'/>"
  "</svg>";

static void
svg_error_cb (GtkSvg       *svg,
              const GError *error,
              gpointer      user_data)
{
  SvgErrors *errors = user_data;

  (void) svg;
  errors->total++;
  if (g_error_matches (error, GTK_SVG_ERROR, GTK_SVG_ERROR_IGNORED_ELEMENT))
    errors->ignored++;
  if (g_error_matches (error, GTK_SVG_ERROR, GTK_SVG_ERROR_NOT_IMPLEMENTED))
    errors->not_implemented++;
  g_test_message ("GtkSvg: %s", error->message);
}

static GtkSvg *
load_gtk_svg (GBytes    *bytes,
              SvgErrors *errors)
{
  GtkSvg *svg = gtk_svg_new ();

  gtk_svg_set_features (svg, GTK_SVG_SYSTEM_RESOURCES);
  g_signal_connect (svg, "error", G_CALLBACK (svg_error_cb), errors);
  gtk_svg_load_from_bytes (svg, bytes);
  return svg;
}

static GskRenderNode *
snapshot_gtk_svg (GtkSvg    *svg,
                  int        width,
                  int        height)
{
  GtkSnapshot *snapshot = gtk_snapshot_new ();

  gdk_paintable_snapshot (GDK_PAINTABLE (svg),
                          GDK_SNAPSHOT (snapshot),
                          width,
                          height);
  return gtk_snapshot_free_to_node (snapshot);
}

static GskRenderNode *
snapshot_librsvg (RsvgHandle *handle,
                  int         width,
                  int         height)
{
  graphene_rect_t bounds = GRAPHENE_RECT_INIT (0, 0, width, height);
  RsvgRectangle viewport = { 0, 0, width, height };
  g_autoptr (GError) error = NULL;
  GskRenderNode *node = gsk_cairo_node_new (&bounds);
  cairo_t *cr = gsk_cairo_node_get_draw_context (node);

  if (!rsvg_handle_render_document (handle, cr, &viewport, &error))
    g_error ("librsvg could not render comparison SVG: %s", error->message);
  cairo_destroy (cr);
  return node;
}

static RsvgHandle *
load_librsvg (GBytes  *bytes,
              GError **error)
{
  g_autoptr (GInputStream) stream = g_memory_input_stream_new_from_bytes (bytes);

  return rsvg_handle_new_from_stream_sync (stream,
                                           NULL,
                                           RSVG_HANDLE_FLAGS_NONE,
                                           NULL,
                                           error);
}

static GdkTexture *
render_node (GskRenderer   *renderer,
             GskRenderNode *node,
             int            width,
             int            height)
{
  graphene_rect_t viewport = GRAPHENE_RECT_INIT (0, 0, width, height);

  return gsk_renderer_render_texture (renderer, node, &viewport);
}

static PixelDifference
compare_textures (GdkTexture *first,
                  GdkTexture *second)
{
  g_autoptr (GBytes) first_bytes = NULL;
  g_autoptr (GBytes) second_bytes = NULL;
  GdkTextureDownloader *first_downloader;
  GdkTextureDownloader *second_downloader;
  const guint8 *first_pixels;
  const guint8 *second_pixels;
  PixelDifference difference = { 0 };
  gsize first_stride;
  gsize second_stride;
  int width = gdk_texture_get_width (first);
  int height = gdk_texture_get_height (first);

  g_assert_cmpint (gdk_texture_get_width (second), ==, width);
  g_assert_cmpint (gdk_texture_get_height (second), ==, height);
  first_downloader = gdk_texture_downloader_new (first);
  second_downloader = gdk_texture_downloader_new (second);
  gdk_texture_downloader_set_format (first_downloader,
                                     GDK_MEMORY_R8G8B8A8_PREMULTIPLIED);
  gdk_texture_downloader_set_format (second_downloader,
                                     GDK_MEMORY_R8G8B8A8_PREMULTIPLIED);
  first_bytes = gdk_texture_downloader_download_bytes (first_downloader,
                                                        &first_stride);
  second_bytes = gdk_texture_downloader_download_bytes (second_downloader,
                                                         &second_stride);
  gdk_texture_downloader_free (first_downloader);
  gdk_texture_downloader_free (second_downloader);
  first_pixels = g_bytes_get_data (first_bytes, NULL);
  second_pixels = g_bytes_get_data (second_bytes, NULL);

  for (int y = 0; y < height; y++)
    for (int x = 0; x < width; x++)
      {
        const guint8 *first_pixel = first_pixels + y * first_stride + x * 4;
        const guint8 *second_pixel = second_pixels + y * second_stride + x * 4;
        guint pixel_error = 0;

        for (guint channel = 0; channel < 4; channel++)
          {
            guint error = ABS ((int) first_pixel[channel] -
                               (int) second_pixel[channel]);

            difference.channel_error += error;
            difference.max_error = MAX (difference.max_error, error);
            pixel_error = MAX (pixel_error, error);
            difference.samples++;
          }
        if (pixel_error > 12)
          difference.differing_pixels++;
      }
  return difference;
}

static void
assert_librsvg_renders (GBytes *bytes)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (RsvgHandle) handle = load_librsvg (bytes, &error);
  g_autoptr (GskRenderNode) node = NULL;

  g_assert_no_error (error);
  g_assert_nonnull (handle);
  node = snapshot_librsvg (handle, 320, 240);
  g_assert_nonnull (node);
}

static void
test_compatibility (void)
{
  const struct
  {
    const char *name;
    const char *data;
  } cases[] = {
    { "CSS style element", style_element_svg },
    { "text on a path", text_path_svg },
    { "turbulence filter", turbulence_svg },
  };
  g_autoptr (GtkSvg) fixture_svg = NULL;
  g_autoptr (GskRenderNode) fixture_node = NULL;
  SvgErrors fixture_errors = { 0 };

  assert_librsvg_renders (comparison_svg);
  fixture_svg = load_gtk_svg (comparison_svg, &fixture_errors);
  fixture_node = snapshot_gtk_svg (fixture_svg, 320, 240);
  g_assert_nonnull (fixture_node);
  g_assert_cmpuint (fixture_errors.total - fixture_errors.ignored, ==, 0);
  g_test_message ("pixel-suite fixture: %u GtkSvg errors",
                  fixture_errors.total);

  for (guint i = 0; i < G_N_ELEMENTS (cases); i++)
    {
      g_autoptr (GBytes) bytes = g_bytes_new_static (cases[i].data,
                                                     strlen (cases[i].data));
      g_autoptr (GtkSvg) svg = NULL;
      g_autoptr (GskRenderNode) node = NULL;
      SvgErrors errors = { 0 };

      assert_librsvg_renders (bytes);
      svg = load_gtk_svg (bytes, &errors);
      node = snapshot_gtk_svg (svg, 320, 240);
      g_test_message ("%s: %u GtkSvg errors (%u not implemented)",
                      cases[i].name,
                      errors.total,
                      errors.not_implemented);
      g_assert_cmpuint (errors.total - errors.ignored,
                        ==,
                        errors.not_implemented);
    }
}

static void
test_pixels (gconstpointer user_data)
{
  GskRenderer *renderer = (GskRenderer *) user_data;
  g_autoptr (RsvgHandle) handle = NULL;
  g_autoptr (GtkSvg) svg = NULL;
  g_autoptr (GskRenderNode) librsvg_node = NULL;
  g_autoptr (GskRenderNode) gtk_node = NULL;
  g_autoptr (GdkTexture) librsvg_texture = NULL;
  g_autoptr (GdkTexture) gtk_texture = NULL;
  g_autoptr (GError) error = NULL;
  SvgErrors errors = { 0 };
  PixelDifference difference;
  double mean_error;
  double differing_percent;

  handle = load_librsvg (comparison_svg, &error);
  g_assert_no_error (error);
  svg = load_gtk_svg (comparison_svg, &errors);
  librsvg_node = snapshot_librsvg (handle, RENDER_WIDTH, RENDER_HEIGHT);
  gtk_node = snapshot_gtk_svg (svg, RENDER_WIDTH, RENDER_HEIGHT);
  g_assert_nonnull (librsvg_node);
  g_assert_nonnull (gtk_node);
  librsvg_texture = render_node (renderer, librsvg_node,
                                 RENDER_WIDTH, RENDER_HEIGHT);
  gtk_texture = render_node (renderer, gtk_node, RENDER_WIDTH, RENDER_HEIGHT);
  difference = compare_textures (librsvg_texture, gtk_texture);
  mean_error = (double) difference.channel_error / difference.samples;
  differing_percent = 100.0 * difference.differing_pixels /
                      (RENDER_WIDTH * RENDER_HEIGHT);
  g_test_message ("basic SVG pixel difference: mean %.3f/255, "
                  "max %u/255, %.3f%% pixels differ by more than 12",
                  mean_error,
                  difference.max_error,
                  differing_percent);
  g_assert_cmpfloat (mean_error, <, 1.0);
  g_assert_cmpfloat (differing_percent, <, 1.0);
}

static void
test_cost (void)
{
  const guint iterations = 250;
  gint64 start;
  gint64 librsvg_parse_us;
  gint64 gtk_parse_us;
  gint64 librsvg_snapshot_us;
  gint64 gtk_snapshot_us;
  g_autoptr (GError) error = NULL;
  g_autoptr (RsvgHandle) handle = load_librsvg (comparison_svg, &error);
  g_autoptr (GtkSvg) svg = NULL;
  SvgErrors errors = { 0 };

  g_assert_no_error (error);
  svg = load_gtk_svg (comparison_svg, &errors);
  start = g_get_monotonic_time ();
  for (guint i = 0; i < iterations; i++)
    {
      g_autoptr (RsvgHandle) parsed = load_librsvg (comparison_svg, &error);

      g_assert_no_error (error);
      g_assert_nonnull (parsed);
    }
  librsvg_parse_us = g_get_monotonic_time () - start;

  start = g_get_monotonic_time ();
  for (guint i = 0; i < iterations; i++)
    {
      SvgErrors parse_errors = { 0 };
      g_autoptr (GtkSvg) parsed = load_gtk_svg (comparison_svg, &parse_errors);

      g_assert_cmpuint (parse_errors.total, ==, 0);
    }
  gtk_parse_us = g_get_monotonic_time () - start;

  start = g_get_monotonic_time ();
  for (guint i = 0; i < iterations; i++)
    {
      g_autoptr (GskRenderNode) node = snapshot_librsvg (handle, 320, 240);

      g_assert_nonnull (node);
    }
  librsvg_snapshot_us = g_get_monotonic_time () - start;

  start = g_get_monotonic_time ();
  for (guint i = 0; i < iterations; i++)
    {
      g_autoptr (GskRenderNode) node = snapshot_gtk_svg (svg, 320, 240);

      g_assert_nonnull (node);
    }
  gtk_snapshot_us = g_get_monotonic_time () - start;

  g_test_message ("%u cold parses: librsvg %.1f us each; GtkSvg %.1f us each",
                  iterations,
                  (double) librsvg_parse_us / iterations,
                  (double) gtk_parse_us / iterations);
  g_test_message ("%u 320x240 snapshots: librsvg %.1f us each; "
                  "GtkSvg %.1f us each",
                  iterations,
                  (double) librsvg_snapshot_us / iterations,
                  (double) gtk_snapshot_us / iterations);
}

int
main (int   argc,
      char *argv[])
{
  GtkWindow *window;
  GskRenderer *renderer;
  g_autoptr (GError) error = NULL;
  char *contents = NULL;
  gsize length = 0;
  int result;

  if (argc < 2 ||
      !g_file_get_contents (argv[1], &contents, &length, &error))
    {
      g_printerr ("Unable to load SVG comparison fixture: %s\n",
                  error != NULL ? error->message : "expected one fixture path");
      return EXIT_FAILURE;
    }
  comparison_svg = g_bytes_new_take (contents, length);
  if (!gtk_init_check ())
    {
      g_clear_pointer (&comparison_svg, g_bytes_unref);
      return 77;
    }
  g_test_init (&argc, &argv, NULL);
  window = GTK_WINDOW (gtk_window_new ());
  gtk_window_set_default_size (window, 64, 64);
  gtk_window_present (window);
  while (g_main_context_iteration (NULL, FALSE))
    ;
  renderer = gtk_native_get_renderer (GTK_NATIVE (window));
  g_assert_nonnull (renderer);

  g_test_add_func ("/svg-renderers/compatibility", test_compatibility);
  g_test_add_data_func ("/svg-renderers/pixels", renderer, test_pixels);
  g_test_add_func ("/svg-renderers/cost", test_cost);
  result = g_test_run ();
  gtk_window_destroy (window);
  while (g_main_context_iteration (NULL, FALSE))
    ;
  g_clear_pointer (&comparison_svg, g_bytes_unref);
  return result;
}
