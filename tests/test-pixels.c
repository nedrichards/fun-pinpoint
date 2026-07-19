#include "pp-presentation.h"
#include "pp-render.h"
#include "pp-stage.h"

#include <gtk/gtk.h>
#include <stdlib.h>

typedef struct
{
  guint8 red;
  guint8 green;
  guint8 blue;
} Pixel;

static gboolean
quit_loop_cb (gpointer user_data)
{
  g_main_loop_quit (user_data);
  return G_SOURCE_REMOVE;
}

static void
run_loop_for (guint milliseconds)
{
  g_autoptr (GMainLoop) loop = g_main_loop_new (NULL, FALSE);

  g_timeout_add (milliseconds, quit_loop_cb, loop);
  g_main_loop_run (loop);
}

static GdkTexture *
snapshot_widget (GtkWidget *widget)
{
  g_autoptr (GdkPaintable) paintable = gtk_widget_paintable_new (widget);
  GtkSnapshot *snapshot = gtk_snapshot_new ();
  GskRenderNode *node;
  GdkTexture *texture;
  GtkNative *native = gtk_widget_get_native (widget);
  GskRenderer *renderer;

  g_assert_nonnull (native);
  renderer = gtk_native_get_renderer (native);
  g_assert_nonnull (renderer);
  gdk_paintable_snapshot (paintable,
                          GDK_SNAPSHOT (snapshot),
                          gtk_widget_get_width (widget),
                          gtk_widget_get_height (widget));
  node = gtk_snapshot_free_to_node (snapshot);
  if (node == NULL)
    g_error ("Snapshot was empty for %s/%s at %dx%d (mapped=%d, visible=%d, opacity=%.1f)",
             G_OBJECT_TYPE_NAME (widget),
             gtk_widget_get_name (widget),
             gtk_widget_get_width (widget),
             gtk_widget_get_height (widget),
             gtk_widget_get_mapped (widget),
             gtk_widget_get_visible (widget),
             gtk_widget_get_opacity (widget));
  texture = gsk_renderer_render_texture (renderer, node, NULL);
  gsk_render_node_unref (node);
  return texture;
}

static Pixel
get_pixel (const guint8 *pixels,
           gsize         stride,
           int           x,
           int           y)
{
  const guint8 *pixel = pixels + (gsize) y * stride + x * 4;

  return (Pixel) { pixel[0], pixel[1], pixel[2] };
}

static gboolean
pixel_near (Pixel pixel,
            Pixel expected,
            guint tolerance)
{
  return ABS ((int) pixel.red - expected.red) <= (int) tolerance &&
         ABS ((int) pixel.green - expected.green) <= (int) tolerance &&
         ABS ((int) pixel.blue - expected.blue) <= (int) tolerance;
}

static void
assert_pixel_near (const guint8 *pixels,
                   gsize         stride,
                   int           x,
                   int           y,
                   Pixel         expected)
{
  Pixel actual = get_pixel (pixels, stride, x, y);

  if (!pixel_near (actual, expected, 12))
    g_error ("Pixel at %d,%d was #%02x%02x%02x; expected #%02x%02x%02x",
             x,
             y,
             actual.red,
             actual.green,
             actual.blue,
             expected.red,
             expected.green,
             expected.blue);
}

static void
test_svg_background (GtkApplication *application,
                     const char     *fixture,
                     int             width,
                     int             height)
{
  const Pixel blue = { 0x35, 0x84, 0xe4 };
  const Pixel yellow = { 0xf9, 0xf0, 0x6b };
  const Pixel white = { 0xff, 0xff, 0xff };
  g_autoptr (GFile) file = g_file_new_for_path (fixture);
  g_autoptr (PpPresentation) presentation = NULL;
  g_autoptr (GError) error = NULL;
  g_autoptr (GdkTexture) texture = NULL;
  g_autoptr (GBytes) bytes = NULL;
  GdkTextureDownloader *downloader;
  const guint8 *pixels;
  GtkWindow *window;
  GtkWidget *stage;
  gsize stride;
  double scale = MIN ((double) width / 80.0, (double) height / 60.0);
  double content_width = 80.0 * scale;
  double content_height = 60.0 * scale;
  double content_x = (width - content_width) / 2.0;
  double content_y = (height - content_height) / 2.0;

  presentation = pp_presentation_load (file, FALSE, NULL, &error);
  g_assert_no_error (error);
  g_assert_nonnull (presentation);
  window = GTK_WINDOW (gtk_application_window_new (application));
  stage = pp_stage_new ();
  pp_stage_set_media_enabled (PP_STAGE (stage), FALSE);
  pp_stage_set_presentation (PP_STAGE (stage),
                             g_steal_pointer (&presentation),
                             0);
  gtk_widget_set_size_request (stage, width, height);
  gtk_window_set_child (window, stage);
  gtk_window_set_default_size (window, width, height);
  gtk_window_set_resizable (window, FALSE);
  gtk_window_present (window);
  run_loop_for (200);

  texture = snapshot_widget (stage);
  downloader = gdk_texture_downloader_new (texture);
  gdk_texture_downloader_set_format (downloader,
                                     GDK_MEMORY_R8G8B8A8_PREMULTIPLIED);
  bytes = gdk_texture_downloader_download_bytes (downloader, &stride);
  gdk_texture_downloader_free (downloader);
  pixels = g_bytes_get_data (bytes, NULL);
  assert_pixel_near (pixels,
                     stride,
                     (int) (content_x + content_width / 8.0),
                     (int) (content_y + content_height / 8.0),
                     blue);
  assert_pixel_near (pixels, stride, width / 2, height / 2, yellow);
  assert_pixel_near (pixels,
                     stride,
                     (int) (content_x + content_width * 0.2),
                     (int) (content_y + content_height * 46.5 / 60.0),
                     white);

  gtk_window_destroy (window);
  run_loop_for (100);
}

static void
test_shared_raster_cache (GtkApplication *application,
                          const char     *fixture)
{
  g_autoptr (GFile) file = g_file_new_for_path (fixture);
  g_autoptr (GFile) asset = NULL;
  g_autoptr (PpPresentation) presentation = NULL;
  g_autoptr (GError) error = NULL;
  g_autoptr (GdkTexture) first = NULL;
  g_autoptr (GdkTexture) second = NULL;
  const PpSlide *slide;
  GtkWindow *window;
  GtkWindow *preview_window;
  GtkWidget *audience;
  GtkWidget *preview;

  presentation = pp_presentation_load (file, FALSE, NULL, &error);
  g_assert_no_error (error);
  g_assert_nonnull (presentation);
  window = GTK_WINDOW (gtk_application_window_new (application));
  preview_window = GTK_WINDOW (gtk_application_window_new (application));
  audience = pp_stage_new ();
  preview = pp_stage_new ();
  gtk_widget_set_name (audience, "raster-audience");
  gtk_widget_set_name (preview, "raster-preview");
  pp_stage_set_media_enabled (PP_STAGE (audience), FALSE);
  pp_stage_set_media_enabled (PP_STAGE (preview), FALSE);
  pp_stage_share_asset_cache (PP_STAGE (preview), PP_STAGE (audience));
  pp_stage_set_presentation (PP_STAGE (audience),
                             pp_presentation_ref (presentation),
                             0);
  pp_stage_set_presentation (PP_STAGE (preview),
                             g_steal_pointer (&presentation),
                             0);
  gtk_widget_set_size_request (audience, 320, 240);
  gtk_widget_set_size_request (preview, 320, 240);
  gtk_window_set_child (window, audience);
  gtk_window_set_child (preview_window, preview);
  gtk_window_set_default_size (window, 320, 240);
  gtk_window_set_default_size (preview_window, 320, 240);
  gtk_window_set_resizable (window, FALSE);
  gtk_window_set_resizable (preview_window, FALSE);
  gtk_window_present (window);
  gtk_window_present (preview_window);
  run_loop_for (200);

  first = snapshot_widget (audience);
  second = snapshot_widget (preview);
  g_assert_nonnull (first);
  g_assert_nonnull (second);
  g_assert_false (pp_stage_get_blank (PP_STAGE (audience)));
  pp_stage_set_slide (PP_STAGE (preview), 1);
  slide = pp_presentation_get_slide (
    pp_stage_get_presentation (PP_STAGE (audience)), 0);
  asset = pp_render_resolve_asset (
    pp_stage_get_presentation (PP_STAGE (audience)), slide->background);
  pp_stage_invalidate_asset (PP_STAGE (audience), asset);
  pp_stage_invalidate_asset (PP_STAGE (preview), asset);
  gtk_widget_set_name (preview, "raster-preview-invalidated");
  run_loop_for (100);
  g_clear_object (&second);
  second = snapshot_widget (preview);
  g_assert_nonnull (second);

  gtk_window_destroy (window);
  gtk_window_destroy (preview_window);
  run_loop_for (100);
}

int
main (int   argc,
      char *argv[])
{
  const Pixel background_color = { 0x30, 0x50, 0xd0 };
  g_autoptr (GtkApplication) application = NULL;
  g_autoptr (GFile) file = NULL;
  g_autoptr (PpPresentation) presentation = NULL;
  g_autoptr (GError) error = NULL;
  g_autoptr (GdkTexture) texture = NULL;
  g_autoptr (GBytes) bytes = NULL;
  GdkTextureDownloader *downloader;
  const guint8 *pixels;
  GtkWindow *window;
  GtkWidget *stage;
  gsize stride;
  int width;
  int height;
  guint white_pixels = 0;
  guint shaded_pixels = 0;

  if (argc != 6)
    return EXIT_FAILURE;
  width = (int) g_ascii_strtoll (argv[3], NULL, 10);
  height = (int) g_ascii_strtoll (argv[4], NULL, 10);
  if (width <= 0 || height <= 0)
    return EXIT_FAILURE;
  if (!gtk_init_check ())
    return 77;

  application = gtk_application_new ("com.nedrichards.pinpoint.PixelTest",
                                      G_APPLICATION_NON_UNIQUE);
  g_assert_true (g_application_register (G_APPLICATION (application),
                                         NULL,
                                         &error));
  g_assert_no_error (error);
  file = g_file_new_for_path (argv[1]);
  presentation = pp_presentation_load (file, FALSE, NULL, &error);
  g_assert_no_error (error);
  g_assert_nonnull (presentation);
  window = GTK_WINDOW (gtk_application_window_new (application));
  stage = pp_stage_new ();
  pp_stage_set_media_enabled (PP_STAGE (stage), FALSE);
  pp_stage_set_presentation (PP_STAGE (stage),
                             g_steal_pointer (&presentation),
                             0);
  gtk_widget_set_size_request (stage, width, height);
  gtk_window_set_child (window, stage);
  gtk_window_set_default_size (window, width, height);
  gtk_window_set_resizable (window, FALSE);
  gtk_window_present (window);
  run_loop_for (300);

  g_assert_cmpint (gtk_widget_get_width (stage), ==, width);
  g_assert_cmpint (gtk_widget_get_height (stage), ==, height);
  texture = snapshot_widget (stage);
  g_assert_cmpint (gdk_texture_get_width (texture), ==, width);
  g_assert_cmpint (gdk_texture_get_height (texture), ==, height);
  downloader = gdk_texture_downloader_new (texture);
  gdk_texture_downloader_set_format (downloader,
                                     GDK_MEMORY_R8G8B8A8_PREMULTIPLIED);
  bytes = gdk_texture_downloader_download_bytes (downloader, &stride);
  gdk_texture_downloader_free (downloader);
  pixels = g_bytes_get_data (bytes, NULL);

  assert_pixel_near (pixels, stride, width / 8, height / 8,
                     background_color);
  assert_pixel_near (pixels, stride, width * 7 / 8, height / 8,
                     background_color);
  assert_pixel_near (pixels, stride, width / 8, height * 7 / 8,
                     background_color);
  assert_pixel_near (pixels, stride, width * 7 / 8, height * 7 / 8,
                     background_color);

  for (int y = height / 3; y < height * 2 / 3; y++)
    for (int x = width / 4; x < width * 3 / 4; x++)
      {
        Pixel pixel = get_pixel (pixels, stride, x, y);

        if (pixel.red > 235 && pixel.green > 235 && pixel.blue > 235)
          white_pixels++;
        if (pixel.red < 55 && pixel.green < 55 && pixel.blue < 55)
          shaded_pixels++;
      }
  g_assert_cmpuint (white_pixels, >, 250);
  g_assert_cmpuint (shaded_pixels, >, 2000);

  gtk_window_destroy (window);
  run_loop_for (100);
  test_svg_background (application, argv[2], width, height);
  test_shared_raster_cache (application, argv[5]);
  return EXIT_SUCCESS;
}
