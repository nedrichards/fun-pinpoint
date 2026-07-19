#include "pp-asset-monitor.h"
#include "pp-presentation.h"

#include <glib/gstdio.h>
#include <stdlib.h>

typedef struct
{
  GFile *expected;
  guint changes;
} MonitorState;

static void
asset_changed_cb (GFile    *file,
                  gpointer  user_data)
{
  MonitorState *state = user_data;

  if (g_file_equal (file, state->expected))
    state->changes++;
}

static void
test_asset_monitor_deduplication_and_change (void)
{
  static const char source[] =
    "[asset.png]\nFirst\n"
    "-- [asset.png] [transition=motion.json]\nSecond\n";
  g_autofree char *directory = NULL;
  g_autofree char *presentation_path = NULL;
  g_autofree char *asset_path = NULL;
  g_autofree char *transition_path = NULL;
  g_autoptr (GFile) presentation_file = NULL;
  g_autoptr (GFile) asset_file = NULL;
  g_autoptr (GFile) transition_file = NULL;
  g_autoptr (PpPresentation) presentation = NULL;
  g_autoptr (PpAssetMonitors) monitors = NULL;
  g_autoptr (GError) error = NULL;
  MonitorState state = { 0 };
  gint64 deadline;

  directory = g_dir_make_tmp ("pinpoint-asset-monitor-XXXXXX", &error);
  g_assert_no_error (error);
  presentation_path = g_build_filename (directory, "slides.pin", NULL);
  asset_path = g_build_filename (directory, "asset.png", NULL);
  transition_path = g_build_filename (directory, "motion.json", NULL);
  g_assert_true (g_file_set_contents (presentation_path, source, -1, &error));
  g_assert_no_error (error);
  g_assert_true (g_file_set_contents (asset_path, "asset", -1, &error));
  g_assert_no_error (error);
  g_assert_true (g_file_set_contents (transition_path, "{}", -1, &error));
  g_assert_no_error (error);

  presentation_file = g_file_new_for_path (presentation_path);
  asset_file = g_file_new_for_path (asset_path);
  transition_file = g_file_new_for_path (transition_path);
  presentation = pp_presentation_load (presentation_file,
                                       FALSE,
                                       NULL,
                                       &error);
  g_assert_no_error (error);
  g_assert_nonnull (presentation);
  state.expected = asset_file;
  monitors = pp_asset_monitors_new (presentation,
                                    asset_changed_cb,
                                    &state);
  g_assert_cmpuint (pp_asset_monitors_get_count (monitors), ==, 2);

  g_assert_true (g_file_replace_contents (asset_file,
                                          "changed",
                                          7,
                                          NULL,
                                          FALSE,
                                          G_FILE_CREATE_NONE,
                                          NULL,
                                          NULL,
                                          &error));
  g_assert_no_error (error);
  deadline = g_get_monotonic_time () + 2 * G_TIME_SPAN_SECOND;
  while (state.changes == 0 && g_get_monotonic_time () < deadline)
    g_main_context_iteration (NULL, TRUE);
  g_assert_cmpuint (state.changes, >, 0);

  g_clear_pointer (&monitors, pp_asset_monitors_free);
  g_assert_true (g_file_delete (presentation_file, NULL, &error));
  g_assert_no_error (error);
  g_assert_true (g_file_delete (asset_file, NULL, &error));
  g_assert_no_error (error);
  g_assert_true (g_file_delete (transition_file, NULL, &error));
  g_assert_no_error (error);
  g_assert_cmpint (g_rmdir (directory), ==, 0);
}

int
main (int   argc,
      char *argv[])
{
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/asset-monitor/deduplication-and-change",
                   test_asset_monitor_deduplication_and_change);
  return g_test_run ();
}
