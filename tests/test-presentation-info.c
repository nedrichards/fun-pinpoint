#include "pp-presentation-info.h"

#include <glib.h>
#include <glib/gstdio.h>

static void
test_valid_presentation (void)
{
  static const char source[] =
    "-- [photo.jpg]\nImage\n# Remember this\n"
    "-- [diagram.svg]\nDiagram\n"
    "-- [movie.webm]\nVideo\n"
    "-- [background.png]\nAnother image\n";
  g_autoptr (GError) error = NULL;
  g_autofree char *directory = g_dir_make_tmp ("pinpoint-info-XXXXXX", &error);
  g_autofree char *path = NULL;
  g_autoptr (GFile) file = NULL;
  g_autoptr (PpPresentationInfo) info = NULL;

  g_assert_no_error (error);
  path = g_build_filename (directory, "summary.pin", NULL);
  g_assert_true (g_file_set_contents (path, source, -1, &error));
  g_assert_no_error (error);
  file = g_file_new_for_path (path);
  info = pp_presentation_info_new (file, FALSE);
  g_assert_nonnull (info);
  g_assert_cmpstr (pp_presentation_info_get_name (info), ==, "summary.pin");
  g_assert_cmpstr (pp_presentation_info_get_details (info), ==,
                   "4 slides · 3 visual assets · 1 video · speaker notes");
  g_assert_true (pp_presentation_info_is_presentable (info));

  g_clear_pointer (&info, pp_presentation_info_free);
  info = pp_presentation_info_new (file, TRUE);
  g_assert_nonnull (info);
  g_assert_cmpstr (pp_presentation_info_get_details (info), ==,
                   "4 slides · 3 visual assets · 1 video");
  g_assert_true (pp_presentation_info_is_presentable (info));

  g_assert_cmpint (g_remove (path), ==, 0);
  g_assert_cmpint (g_rmdir (directory), ==, 0);
}

static void
test_singular_details (void)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (GFileIOStream) stream = NULL;
  g_autoptr (GFile) file = g_file_new_tmp ("pinpoint-info-XXXXXX.pin",
                                           &stream,
                                           &error);
  g_autoptr (PpPresentationInfo) info = NULL;
  static const char source[] = "-- [image.jpg]\nOnly slide\n";

  g_assert_no_error (error);
  g_assert_true (g_output_stream_write_all (
    g_io_stream_get_output_stream (G_IO_STREAM (stream)),
    source,
    sizeof source - 1,
    NULL,
    NULL,
    &error));
  g_assert_no_error (error);
  g_assert_true (g_io_stream_close (G_IO_STREAM (stream), NULL, &error));
  g_assert_no_error (error);
  info = pp_presentation_info_new (file, FALSE);
  g_assert_nonnull (info);
  g_assert_cmpstr (pp_presentation_info_get_details (info), ==,
                   "1 slide · 1 visual asset");
  g_assert_true (pp_presentation_info_is_presentable (info));
  g_assert_true (g_file_delete (file, NULL, &error));
  g_assert_no_error (error);
}

static void
test_invalid_presentation (void)
{
  g_autoptr (GError) error = NULL;
  g_autofree char *directory = g_dir_make_tmp ("pinpoint-info-XXXXXX", &error);
  g_autofree char *path = NULL;
  g_autoptr (GFile) file = NULL;
  g_autoptr (PpPresentationInfo) info = NULL;

  g_assert_no_error (error);
  path = g_build_filename (directory, "pinpoint-info.pin", NULL);
  file = g_file_new_for_path (path);
  info = pp_presentation_info_new (file, FALSE);
  g_assert_nonnull (info);
  g_assert_cmpstr (pp_presentation_info_get_name (info), ==,
                   "pinpoint-info.pin");
  g_assert_true (g_str_has_prefix (pp_presentation_info_get_details (info),
                                   "Cannot open: "));
  g_assert_false (pp_presentation_info_is_presentable (info));
  pp_presentation_info_free (NULL);
  g_assert_cmpint (g_rmdir (directory), ==, 0);
}

int
main (int   argc,
      char *argv[])
{
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/presentation-info/valid", test_valid_presentation);
  g_test_add_func ("/presentation-info/singular", test_singular_details);
  g_test_add_func ("/presentation-info/invalid", test_invalid_presentation);
  return g_test_run ();
}
