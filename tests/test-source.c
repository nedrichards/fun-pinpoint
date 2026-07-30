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

int
main (int   argc,
      char *argv[])
{
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/source/analysis", test_analysis);
  g_test_add_func ("/source/incomplete", test_incomplete_source);
  g_test_add_func ("/source/apply-durations", test_apply_durations_preserves_source);
  g_test_add_func ("/source/duration-count", test_duration_count_mismatch);
  return g_test_run ();
}
