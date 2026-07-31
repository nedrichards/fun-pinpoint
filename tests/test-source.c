#include "pp-source.h"
#include "pp-presentation.h"

#include <glib.h>
#include <string.h>

static void
test_analysis (void)
{
  const char *source =
    "[stage-color=black]\n"
    "-- [transition-mode=sideways] [duration=nope]\n"
    "<b>Opening</b>\n"
    "#notes\n"
    "-- [mystery=value]\n"
    "\\#Visible text\n";
  g_autoptr (PpSourceAnalysis) analysis = pp_source_analyze (source, NULL);
  const PpSourceSlide *first;
  const PpSourceSlide *second;

  g_assert_cmpuint (pp_source_analysis_get_n_slides (analysis), ==, 2);
  first = pp_source_analysis_get_slide (analysis, 0);
  second = pp_source_analysis_get_slide (analysis, 1);
  g_assert_cmpstr (first->title, ==, "Opening");
  g_assert_cmpstr (second->title, ==, "#Visible text");
  g_assert_cmpuint (pp_source_analysis_find_slide (analysis, second->start + 2), ==, 1);
  g_assert_cmpuint (pp_source_analysis_get_n_diagnostics (analysis), ==, 3);
}

static void
test_incomplete_source (void)
{
  const char *source = "-- [duration=4\nSlide\n";
  g_autoptr (PpSourceAnalysis) analysis = pp_source_analyze (source, NULL);
  const PpSourceDiagnostic *diagnostic;

  g_assert_cmpuint (pp_source_analysis_get_n_slides (analysis), ==, 1);
  g_assert_cmpuint (pp_source_analysis_get_n_diagnostics (analysis), ==, 1);
  diagnostic = pp_source_analysis_get_diagnostic (analysis, 0);
  g_assert_nonnull (strstr (diagnostic->message, "closing bracket"));
}

static void
test_apply_durations_preserves_source (void)
{
  const char *source =
    "[duration=30]  # deliberately spaced defaults\n"
    "--   [top] [duration=1.25]   \n"
    "First\n"
    "# note\n"
    "-- [no-markup]\n"
    "Second\n";
  const double durations[] = { 5.0, 8.375 };
  const char *expected =
    "[duration=30]  # deliberately spaced defaults\n"
    "--   [top] [duration=5]   \n"
    "First\n"
    "# note\n"
    "-- [no-markup] [duration=8.375]\n"
    "Second\n";
  g_autoptr (GError) error = NULL;
  g_autofree char *updated = pp_source_apply_durations (source,
                                                        durations,
                                                        G_N_ELEMENTS (durations),
                                                        &error);

  g_assert_no_error (error);
  g_assert_cmpstr (updated, ==, expected);
}

static void
test_duration_count_mismatch (void)
{
  const double duration = 1.0;
  g_autoptr (GError) error = NULL;
  g_autofree char *updated = pp_source_apply_durations ("--\nOne\n--\nTwo\n",
                                                        &duration,
                                                        1,
                                                        &error);

  g_assert_null (updated);
  g_assert_error (error, PP_PRESENTATION_ERROR, PP_PRESENTATION_ERROR_INVALID);
}

static void
assert_values (const char         *setting,
               const char *const *expected)
{
  const char *const *values = pp_source_setting_values (setting);
  guint i;

  g_assert_nonnull (values);
  for (i = 0; expected[i] != NULL; i++)
    g_assert_cmpstr (values[i], ==, expected[i]);
  g_assert_null (values[i]);
}

static void
test_completion_catalogue (void)
{
  static const char *const align[] = { "left", "center", "right", NULL };
  static const char *const gravity[] = {
    "center", "top-left", "left", "bottom-left", "top-right", "right",
    "bottom-right", NULL
  };
  static const char *const direction[] = { "left", "right", "up", "down", NULL };
  static const char *const layer[] = { "default", "all", "background", "text", NULL };
  static const char *const mode[] = { "both", "in", "out", NULL };
  static const char *const easing[] = {
    "linear", "ease-in", "ease-out", "ease-in-out", NULL
  };
  const char *const *names = pp_source_setting_names ();
  gboolean found_duration = FALSE;
  gboolean found_new_markup = FALSE;

  g_assert_nonnull (names);
  for (guint i = 0; names[i] != NULL; i++)
    {
      found_duration |= g_str_equal (names[i], "duration=");
      found_new_markup |= g_str_equal (names[i], "markup");
    }
  g_assert_true (found_duration);
  g_assert_true (found_new_markup);
  assert_values ("text-align=", align);
  assert_values ("bg-position=", gravity);
  assert_values ("transition-direction=", direction);
  assert_values ("transition-layer=", layer);
  assert_values ("transition-mode=", mode);
  assert_values ("transition-easing=", easing);
  g_assert_null (pp_source_setting_values ("duration="));
  g_assert_null (pp_source_setting_values ("fill"));
}

int
main (int   argc,
      char *argv[])
{
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/source/analysis", test_analysis);
  g_test_add_func ("/source/incomplete", test_incomplete_source);
  g_test_add_func ("/source/apply-durations", test_apply_durations_preserves_source);
  g_test_add_func ("/source/duration-count", test_duration_count_mismatch);
  g_test_add_func ("/source/completion-catalogue", test_completion_catalogue);
  return g_test_run ();
}
