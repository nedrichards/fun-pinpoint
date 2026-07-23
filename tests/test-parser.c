#include "pp-file-access.h"
#include "pp-introduction.h"
#include "pp-presentation.h"
#include "pp-pdf.h"
#include "pp-page-curl.h"
#include "pp-render.h"
#include "pp-transition.h"
#include "pp-video-thumbnail.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <gst/app/gstappsrc.h>
#include <gst/gst.h>
#include <stdio.h>
#include <jpeglib.h>
#include <math.h>
#include <string.h>

static const char *fixture_path;
static const char *test_program_path;

static void
test_compatibility_fixture (void)
{
  g_autoptr (GFile) file = g_file_new_for_path (fixture_path);
  g_autoptr (PpPresentation) presentation = NULL;
  g_autoptr (GError) error = NULL;
  const PpSlide *defaults;
  const PpSlide *slide;

  presentation = pp_presentation_load (file, FALSE, NULL, &error);
  g_assert_no_error (error);
  g_assert_nonnull (presentation);
  g_assert_cmpuint (pp_presentation_get_n_slides (presentation), ==, 3);

  defaults = pp_presentation_get_defaults (presentation);
  g_assert_cmpstr (defaults->stage_color, ==, "#112233");
  g_assert_cmpstr (defaults->font, ==, "Sans 48px");
  g_assert_cmpfloat_with_epsilon (defaults->duration, 12.0, 0.0001);
  g_assert_cmpint (defaults->text_position, ==, PP_GRAVITY_BOTTOM);

  slide = pp_presentation_get_slide (presentation, 0);
  g_assert_cmpstr (slide->text, ==, "Hello <b>world</b>");
  g_assert_cmpstr (slide->speaker_notes, ==, "First note\nSecond note\n");
  g_assert_cmpint (slide->text_position, ==, PP_GRAVITY_TOP_RIGHT);
  g_assert_cmpint (slide->text_align, ==, PP_TEXT_ALIGN_CENTER);
  g_assert_cmpint (slide->background_type, ==, PP_BACKGROUND_COLOR);
  g_assert_cmpstr (slide->background, ==, "white");

  slide = pp_presentation_get_slide (presentation, 1);
  g_assert_cmpint (slide->background_type, ==, PP_BACKGROUND_IMAGE);
  g_assert_cmpint (slide->background_scale, ==, PP_BACKGROUND_FILL);
  g_assert_cmpint (slide->background_position, ==, PP_GRAVITY_BOTTOM_RIGHT);
  g_assert_false (slide->use_markup);

  slide = pp_presentation_get_slide (presentation, 2);
  g_assert_cmpstr (slide->text,
                   ==,
                   "-- this is text, not a separator\n# this is text, not a note");
  g_assert_cmpstr (slide->transition, ==, "slide-left");
  g_assert_cmpstr (slide->command, ==, "printf hello");
}

static void
test_ignore_comments (void)
{
  g_autoptr (GFile) file = g_file_new_for_path (fixture_path);
  g_autoptr (PpPresentation) presentation = NULL;
  g_autoptr (GError) error = NULL;

  presentation = pp_presentation_load (file, TRUE, NULL, &error);
  g_assert_no_error (error);
  g_assert_nonnull (presentation);
  g_assert_null (pp_presentation_get_slide (presentation, 0)->speaker_notes);
}

static void
test_visual_description (void)
{
  static const char *source =
    "-- [photo.jpg]\n"
    "Audience text\n"
    "#@alt:A hand-drawn chart has three rising bars.\n"
    "#@alt:The final bar is tallest.\n"
    "#Speaker reminder\n";
  g_autoptr (PpPresentation) presentation = NULL;
  g_autoptr (PpPresentation) ignored = NULL;
  g_autoptr (PpPresentation) round_trip = NULL;
  g_autoptr (GError) error = NULL;
  g_autofree char *serialized = NULL;
  const PpSlide *slide;

  presentation = pp_presentation_parse (source, NULL, FALSE, &error);
  g_assert_no_error (error);
  slide = pp_presentation_get_slide (presentation, 0);
  g_assert_cmpstr (slide->speaker_notes, ==, "Speaker reminder\n");
  g_assert_cmpstr (slide->visual_description, ==,
                   "A hand-drawn chart has three rising bars.\n"
                   "The final bar is tallest.\n");

  ignored = pp_presentation_parse (source, NULL, TRUE, &error);
  g_assert_no_error (error);
  g_assert_null (pp_presentation_get_slide (ignored, 0)->speaker_notes);
  g_assert_cmpstr (pp_presentation_get_slide (ignored, 0)->visual_description,
                   ==,
                   "A hand-drawn chart has three rising bars.\n"
                   "The final bar is tallest.\n");

  serialized = pp_presentation_serialize (presentation);
  g_assert_nonnull (strstr (serialized,
                            "#@alt:A hand-drawn chart has three rising bars.\n"
                            "#@alt:The final bar is tallest.\n"
                            "#Speaker reminder\n"));
  round_trip = pp_presentation_parse (serialized, NULL, FALSE, &error);
  g_assert_no_error (error);
  slide = pp_presentation_get_slide (round_trip, 0);
  g_assert_cmpstr (slide->visual_description, ==,
                   "A hand-drawn chart has three rising bars.\n"
                   "The final bar is tallest.\n");
  g_assert_cmpstr (slide->speaker_notes, ==, "Speaker reminder\n");
}

static void
test_invalid_source (void)
{
  g_autoptr (PpPresentation) presentation = NULL;
  g_autoptr (GError) error = NULL;

  presentation = pp_presentation_parse ("There is no separator", NULL, FALSE, &error);
  g_assert_null (presentation);
  g_assert_error (error, PP_PRESENTATION_ERROR, PP_PRESENTATION_ERROR_INVALID);

  g_clear_error (&error);
  presentation = pp_presentation_parse ("--\n\xff", NULL, FALSE, &error);
  g_assert_null (presentation);
  g_assert_error (error, PP_PRESENTATION_ERROR, PP_PRESENTATION_ERROR_INVALID);
}

static void
test_historical_video_suffixes (void)
{
  static const char *const suffixes[] = {
    "avi", "ogg", "ogv", "mpg", "flv", "mpeg", "mov", "mp4", "wmv",
    "webm", "mkv", "3gp", "gif",
  };
  g_autoptr (GString) source = g_string_new (NULL);
  g_autoptr (PpPresentation) presentation = NULL;

  for (guint i = 0; i < G_N_ELEMENTS (suffixes); i++)
    g_string_append_printf (source, "-- [movie.%s]\nVideo\n", suffixes[i]);

  presentation = pp_presentation_parse (source->str, NULL, FALSE, NULL);
  g_assert_nonnull (presentation);
  g_assert_cmpuint (pp_presentation_get_n_slides (presentation),
                    ==,
                    G_N_ELEMENTS (suffixes));
  for (guint i = 0; i < G_N_ELEMENTS (suffixes); i++)
    g_assert_cmpint (pp_presentation_get_slide (presentation, i)->background_type,
                     ==,
                     PP_BACKGROUND_VIDEO);
}

static void
test_background_type_detection (void)
{
  const char *source =
    "-- [movie.mp4]\nvideo\n"
    "-- [movie.MP4]\nvideo uppercase\n"
    "-- [art.svg]\nsvg\n"
    "-- [art.SVG]\nsvg uppercase\n"
    "-- [movie.m4v]\nM4V\n"
    "-- [camera.mts]\nMTS\n"
    "-- [camera.m2ts]\nM2TS\n"
    "-- [capture.ts]\nTS\n"
    "-- [edit.mxf]\nMXF\n"
    "-- [stream.m2v]\nM2V\n"
    "-- [disc.vob]\nVOB\n"
    "-- [playlist.m3u8]\nHLS\n"
    "-- [photo.webp]\nimage\n";
  g_autoptr (PpPresentation) presentation = NULL;

  presentation = pp_presentation_parse (source, NULL, FALSE, NULL);
  g_assert_nonnull (presentation);
  g_assert_cmpuint (pp_presentation_get_n_slides (presentation), ==, 13);
  g_assert_cmpint (pp_presentation_get_slide (presentation, 0)->background_type,
                   ==,
                   PP_BACKGROUND_VIDEO);
  g_assert_cmpint (pp_presentation_get_slide (presentation, 1)->background_type,
                   ==,
                   PP_BACKGROUND_VIDEO);
  g_assert_cmpint (pp_presentation_get_slide (presentation, 2)->background_type,
                   ==,
                   PP_BACKGROUND_SVG);
  g_assert_cmpint (pp_presentation_get_slide (presentation, 3)->background_type,
                   ==,
                   PP_BACKGROUND_SVG);
  for (guint i = 4; i <= 11; i++)
    g_assert_cmpint (pp_presentation_get_slide (presentation, i)->background_type,
                     ==,
                     PP_BACKGROUND_VIDEO);
  g_assert_cmpint (pp_presentation_get_slide (presentation, 12)->background_type,
                   ==,
                   PP_BACKGROUND_IMAGE);
}

static void
test_background_content_sniffing (void)
{
  g_autofree char *fixtures = g_path_get_dirname (fixture_path);
  g_autofree char *source_path = g_build_filename (fixtures,
                                                   "media-formats",
                                                   "pinpoint-h264-high.mp4",
                                                   NULL);
  g_autofree char *temporary_path = NULL;
  g_autofree char *video_path = NULL;
  g_autofree char *video_uri = NULL;
  g_autofree char *presentation_path = NULL;
  g_autofree char *presentation_source = NULL;
  g_autofree char *contents = NULL;
  gsize length = 0;
  g_autoptr (GFile) file = NULL;
  g_autoptr (GFile) root = g_file_new_for_path ("/");
  g_autoptr (PpPresentation) presentation = NULL;
  g_autoptr (PpPresentation) root_presentation = NULL;
  g_autoptr (GError) error = NULL;

  g_assert_true (g_file_get_contents (source_path, &contents, &length, &error));
  g_assert_no_error (error);
  temporary_path = g_dir_make_tmp ("pinpoint-content-type-XXXXXX", &error);
  g_assert_no_error (error);
  video_path = g_build_filename (temporary_path, "extensionless-media", NULL);
  video_uri = g_filename_to_uri (video_path, NULL, &error);
  g_assert_no_error (error);
  presentation_path = g_build_filename (temporary_path, "talk.pin", NULL);
  presentation_source = g_strdup_printf (
    "-- [extensionless-media]\nRelative video\n-- [%s]\nFile URI video\n",
    video_uri);
  g_assert_true (g_file_set_contents (video_path, contents, length, &error));
  g_assert_no_error (error);
  g_assert_true (g_file_set_contents (presentation_path,
                                      presentation_source,
                                      -1,
                                      &error));
  g_assert_no_error (error);

  file = g_file_new_for_path (presentation_path);
  presentation = pp_presentation_load (file, FALSE, NULL, &error);
  g_assert_no_error (error);
  g_assert_nonnull (presentation);
  g_assert_cmpint (pp_presentation_get_slide (presentation, 0)->background_type,
                   ==,
                   PP_BACKGROUND_VIDEO);
  g_assert_cmpint (pp_presentation_get_slide (presentation, 1)->background_type,
                   ==,
                   PP_BACKGROUND_VIDEO);

  root_presentation = pp_presentation_parse ("-- [missing.mp4]\nVideo\n",
                                             root,
                                             FALSE,
                                             &error);
  g_assert_no_error (error);
  g_assert_nonnull (root_presentation);
  g_assert_cmpint (
    pp_presentation_get_slide (root_presentation, 0)->background_type,
    ==,
    PP_BACKGROUND_VIDEO);

  g_assert_cmpint (g_remove (presentation_path), ==, 0);
  g_assert_cmpint (g_remove (video_path), ==, 0);
  g_assert_cmpint (g_rmdir (temporary_path), ==, 0);
}

static void
test_first_changed_slide (void)
{
  const char *before = "[black]\n--\none\n--\ntwo\n--\nthree\n";
  const char *after = "[black]\n--\none\n--\nchanged\n--\nthree\n";
  g_autoptr (PpPresentation) old_presentation = NULL;
  g_autoptr (PpPresentation) new_presentation = NULL;

  old_presentation = pp_presentation_parse (before, NULL, FALSE, NULL);
  new_presentation = pp_presentation_parse (after, NULL, FALSE, NULL);
  g_assert_cmpuint (pp_presentation_first_changed_slide (old_presentation,
                                                         new_presentation),
                    ==,
                    1);
}

static void
test_rehearsal_serialization (void)
{
  const char *source =
    "[stage-color=#112233]\n"
    "[duration=10]\n"
    "-- [top] [white]\n"
    "First\n"
    "#A note\n"
    "-- [no-markup]\n"
    "Second\n";
  g_autoptr (PpPresentation) presentation = NULL;
  g_autoptr (PpPresentation) reparsed = NULL;
  g_autofree char *serialized = NULL;

  presentation = pp_presentation_parse (source, NULL, FALSE, NULL);
  g_assert_nonnull (presentation);
  pp_presentation_rehearsal_reset (presentation);
  pp_presentation_rehearsal_record (presentation, 0, 3.25);
  pp_presentation_rehearsal_record (presentation, 0, 1.75);
  pp_presentation_rehearsal_record (presentation, 1, 8.5);

  /* Finishing normally promotes the accumulated values before serialization. */
  ((PpSlide *) pp_presentation_get_slide (presentation, 0))->duration = 5.0;
  ((PpSlide *) pp_presentation_get_slide (presentation, 1))->duration = 8.5;
  serialized = pp_presentation_serialize (presentation);
  g_assert_false (g_str_has_prefix (serialized, "#!"));
  g_assert_true (g_str_has_prefix (serialized,
                                   "[stage-color=#112233][duration=10.000000]\n--"));
  g_assert_nonnull (strstr (serialized, "-- [white] [top] [duration=5.000000]"));
  g_assert_nonnull (strstr (serialized, "#A note\n"));
  g_assert_nonnull (strstr (serialized, "-- [duration=8.500000] [no-markup]"));

  reparsed = pp_presentation_parse (serialized, NULL, FALSE, NULL);
  g_assert_nonnull (reparsed);
  g_assert_cmpfloat_with_epsilon (
    pp_presentation_get_slide (reparsed, 0)->duration, 5.0, 0.0001);
  g_assert_cmpfloat_with_epsilon (
    pp_presentation_get_slide (reparsed, 1)->duration, 8.5, 0.0001);
}

static void
test_native_transition_settings (void)
{
  const char *source =
    "[transition=slide]\n"
    "[transition-direction=right]\n"
    "[transition-duration=900]\n"
    "[transition-easing=ease-out-cubic]\n"
    "--\n"
    "First\n"
    "-- [transition=flip] [transition-direction=down]"
    " [transition-layer=text] [transition-mode=in]"
    " [transition-duration=420] [transition-easing=ease-in-out]\n"
    "Second\n";
  g_autoptr (PpPresentation) presentation = NULL;
  g_autoptr (PpPresentation) reparsed = NULL;
  g_autofree char *serialized = NULL;
  const PpSlide *slide;

  presentation = pp_presentation_parse (source, NULL, FALSE, NULL);
  g_assert_nonnull (presentation);

  slide = pp_presentation_get_slide (presentation, 0);
  g_assert_cmpstr (slide->transition, ==, "slide");
  g_assert_cmpint (slide->transition_direction,
                   ==,
                   PP_TRANSITION_DIRECTION_RIGHT);
  g_assert_cmpint (slide->transition_layer,
                   ==,
                   PP_TRANSITION_LAYER_DEFAULT);
  g_assert_cmpint (slide->transition_mode, ==, PP_TRANSITION_MODE_BOTH);
  g_assert_cmpuint (slide->transition_duration_ms, ==, 900);
  g_assert_cmpstr (slide->transition_easing, ==, "ease-out-cubic");

  slide = pp_presentation_get_slide (presentation, 1);
  g_assert_cmpstr (slide->transition, ==, "flip");
  g_assert_cmpint (slide->transition_direction,
                   ==,
                   PP_TRANSITION_DIRECTION_DOWN);
  g_assert_cmpint (slide->transition_layer, ==, PP_TRANSITION_LAYER_TEXT);
  g_assert_cmpint (slide->transition_mode, ==, PP_TRANSITION_MODE_IN);
  g_assert_cmpuint (slide->transition_duration_ms, ==, 420);
  g_assert_cmpstr (slide->transition_easing, ==, "ease-in-out");

  serialized = pp_presentation_serialize (presentation);
  g_assert_nonnull (strstr (serialized, "[transition=slide]"));
  g_assert_nonnull (strstr (serialized, "[transition-direction=right]"));
  g_assert_nonnull (strstr (serialized, "[transition-layer=text]"));
  g_assert_nonnull (strstr (serialized, "[transition-mode=in]"));
  g_assert_nonnull (strstr (serialized, "[transition-duration=420]"));
  g_assert_nonnull (strstr (serialized,
                            "[transition-easing=ease-in-out]"));

  reparsed = pp_presentation_parse (serialized, NULL, FALSE, NULL);
  g_assert_nonnull (reparsed);
  slide = pp_presentation_get_slide (reparsed, 1);
  g_assert_cmpint (slide->transition_direction,
                   ==,
                   PP_TRANSITION_DIRECTION_DOWN);
  g_assert_cmpint (slide->transition_layer, ==, PP_TRANSITION_LAYER_TEXT);
  g_assert_cmpint (slide->transition_mode, ==, PP_TRANSITION_MODE_IN);
  g_assert_cmpuint (slide->transition_duration_ms, ==, 420);

  g_assert_true (pp_transition_is_builtin ("slide"));
  g_assert_true (pp_transition_is_builtin ("zoom"));
  g_assert_true (pp_transition_is_builtin ("flip"));
  g_assert_cmpfloat_with_epsilon (
    pp_transition_apply_easing ("ease-in", 0.5), 0.125, 0.0001);
  g_assert_cmpfloat_with_epsilon (
    pp_transition_apply_easing ("ease-out-cubic", 0.5), 0.875, 0.0001);

  g_clear_pointer (&reparsed, pp_presentation_free);
  reparsed = pp_presentation_parse (
    "-- [transition-duration=-1]\nInvalid duration\n",
    NULL,
    FALSE,
    NULL);
  g_assert_nonnull (reparsed);
  g_assert_cmpuint (
    pp_presentation_get_slide (reparsed, 0)->transition_duration_ms,
    ==,
    0);
}

static void
test_rehearsal_finish (void)
{
  const char *source = "[duration=1]\n--\nFirst\n--\nSecond\n";
  g_autoptr (GFileIOStream) stream = NULL;
  g_autoptr (GFile) file = NULL;
  g_autoptr (PpPresentation) presentation = NULL;
  g_autoptr (PpPresentation) saved = NULL;
  g_autoptr (GError) error = NULL;

  file = g_file_new_tmp ("pinpoint-rehearsal-XXXXXX", &stream, &error);
  g_assert_no_error (error);
  g_assert_true (g_io_stream_close (G_IO_STREAM (stream), NULL, &error));
  g_assert_no_error (error);
  g_clear_object (&stream);
  g_assert_true (g_file_replace_contents (file,
                                          source,
                                          strlen (source),
                                          NULL,
                                          FALSE,
                                          G_FILE_CREATE_NONE,
                                          NULL,
                                          NULL,
                                          &error));
  g_assert_no_error (error);

  presentation = pp_presentation_load (file, FALSE, NULL, &error);
  g_assert_no_error (error);
  pp_presentation_rehearsal_reset (presentation);
  pp_presentation_rehearsal_record (presentation, 0, 2.5);
  pp_presentation_rehearsal_record (presentation, 1, 4.75);
  g_assert_true (pp_presentation_rehearsal_finish (presentation, &error));
  g_assert_no_error (error);

  saved = pp_presentation_load (file, FALSE, NULL, &error);
  g_assert_no_error (error);
  g_assert_cmpfloat_with_epsilon (
    pp_presentation_get_slide (saved, 0)->duration, 2.5, 0.0001);
  g_assert_cmpfloat_with_epsilon (
    pp_presentation_get_slide (saved, 1)->duration, 4.75, 0.0001);
  g_assert_true (g_file_delete (file, NULL, &error));
  g_assert_no_error (error);
}

static void
test_geometry (void)
{
  PpSlide slide = { 0 };
  PpRect rect;
  float scale;

  slide.background_scale = PP_BACKGROUND_FIT;
  pp_render_get_background_rect (&slide, 800, 600, 0, 1080, &rect);
  g_assert_cmpfloat (rect.width, ==, 0.0f);
  g_assert_cmpfloat (rect.height, ==, 0.0f);

  slide.background_scale = PP_BACKGROUND_FIT;
  slide.background_position = PP_GRAVITY_CENTER;
  pp_render_get_background_rect (&slide, 800, 600, 1920, 1080, &rect);
  g_assert_cmpfloat_with_epsilon (rect.width, 800.0, 0.01);
  g_assert_cmpfloat_with_epsilon (rect.height, 450.0, 0.01);
  g_assert_cmpfloat_with_epsilon (rect.y, 75.0, 0.01);

  slide.background_scale = PP_BACKGROUND_FILL;
  slide.background_position = PP_GRAVITY_TOP_LEFT;
  pp_render_get_background_rect (&slide, 800, 600, 400, 400, &rect);
  g_assert_cmpfloat_with_epsilon (rect.width, 800.0, 0.01);
  g_assert_cmpfloat_with_epsilon (rect.height, 800.0, 0.01);
  g_assert_cmpfloat_with_epsilon (rect.x, 40.0, 0.01);
  g_assert_cmpfloat_with_epsilon (rect.y, 30.0, 0.01);

  slide.background_scale = PP_BACKGROUND_UNSCALED;
  slide.background_position = PP_GRAVITY_BOTTOM_RIGHT;
  pp_render_get_background_rect (&slide, 800, 600, 200, 100, &rect);
  g_assert_cmpfloat_with_epsilon (rect.width, 200.0, 0.01);
  g_assert_cmpfloat_with_epsilon (rect.height, 100.0, 0.01);
  g_assert_cmpfloat_with_epsilon (rect.x, 560.0, 0.01);
  g_assert_cmpfloat_with_epsilon (rect.y, 470.0, 0.01);

  slide.background_scale = PP_BACKGROUND_STRETCH;
  slide.background_position = PP_GRAVITY_CENTER;
  pp_render_get_background_rect (&slide, 800, 600, 200, 100, &rect);
  g_assert_cmpfloat_with_epsilon (rect.width, 800.0, 0.01);
  g_assert_cmpfloat_with_epsilon (rect.height, 600.0, 0.01);

  slide.text_position = PP_GRAVITY_BOTTOM_RIGHT;
  pp_render_get_text_rect (&slide, 800, 600, 200, 100, &rect, &scale);
  g_assert_cmpfloat_with_epsilon (scale, 1.0, 0.01);
  g_assert_cmpfloat_with_epsilon (rect.x, 560.0, 0.01);
  g_assert_cmpfloat_with_epsilon (rect.y, 470.0, 0.01);

  pp_render_get_text_rect (&slide, 800, 600, 0, 100, &rect, &scale);
  g_assert_cmpfloat (rect.width, ==, 0.0f);
  g_assert_cmpfloat (rect.height, ==, 0.0f);
  g_assert_cmpfloat (scale, ==, 1.0f);

  slide.text_position = PP_GRAVITY_TOP_LEFT;
  pp_render_get_text_rect (&slide, 800, 600, 1600, 600, &rect, &scale);
  g_assert_cmpfloat_with_epsilon (scale, 0.4, 0.01);
  g_assert_cmpfloat_with_epsilon (rect.x, 40.0, 0.01);
  g_assert_cmpfloat_with_epsilon (rect.y, 30.0, 0.01);

  slide.text_position = PP_GRAVITY_CENTER;
  pp_render_get_text_rect (&slide, 800, 600, 200, 100, &rect, &scale);
  g_assert_cmpfloat_with_epsilon (rect.x, 300.0, 0.01);
  g_assert_cmpfloat_with_epsilon (rect.y, 250.0, 0.01);

  rect = pp_render_get_shading_rect (800, &rect);
  g_assert_cmpfloat_with_epsilon (rect.x, 292.0, 0.01);
  g_assert_cmpfloat_with_epsilon (rect.y, 242.0, 0.01);
  g_assert_cmpfloat_with_epsilon (rect.width, 216.0, 0.01);
  g_assert_cmpfloat_with_epsilon (rect.height, 116.0, 0.01);
}

static void
test_asset_resolution (void)
{
  const char *source =
    "-- [https://example.com/remote.svg]\nRemote\n"
    "-- [/definitely-missing-pinpoint.png]\nAbsolute\n"
    "-- [relative-missing.png]\nRelative\n";
  g_autoptr (PpPresentation) presentation = NULL;
  g_autoptr (PpPresentation) root_presentation = NULL;
  g_autoptr (GFile) resolved = NULL;
  g_autoptr (GFile) root = g_file_new_for_path ("/");
  g_autofree char *path = NULL;
  g_autofree char *missing = NULL;

  presentation = pp_presentation_parse (source, NULL, FALSE, NULL);
  g_assert_nonnull (presentation);

  resolved = pp_render_resolve_asset (presentation, "/tmp/pinpoint.png");
  path = g_file_get_path (resolved);
  g_assert_cmpstr (path, ==, "/tmp/pinpoint.png");
  g_clear_object (&resolved);
  g_clear_pointer (&path, g_free);

  resolved = pp_render_resolve_asset (presentation, "relative.png");
  path = g_file_get_path (resolved);
  g_assert_true (g_str_has_suffix (path, "/relative.png"));

  missing = pp_render_find_missing_relative_asset (presentation);
  g_assert_cmpstr (missing, ==, "relative-missing.png");

  root_presentation = pp_presentation_parse ("--\nRoot\n",
                                             root,
                                             FALSE,
                                             NULL);
  g_assert_nonnull (root_presentation);
  g_clear_object (&resolved);
  g_clear_pointer (&path, g_free);
  resolved = pp_render_resolve_asset (root_presentation, "relative.png");
  path = g_file_get_path (resolved);
  g_assert_true (g_str_has_suffix (path, "/relative.png"));
}

static void
test_page_curl_deformation (void)
{
  PpPageCurlVertex vertex = { 500.0f, 300.0f, 12.0f, 0.0f };

  pp_page_curl_deform_vertex (800.0f, 600.0f, 0.0, 0.0, 50.0f, &vertex);
  g_assert_cmpfloat_with_epsilon (vertex.x, 500.0f, 0.001f);
  g_assert_cmpfloat_with_epsilon (vertex.y, 300.0f, 0.001f);
  g_assert_cmpfloat_with_epsilon (vertex.z, 0.0f, 0.001f);
  g_assert_cmpfloat_with_epsilon (vertex.shade, 1.0f, 0.001f);

  vertex = (PpPageCurlVertex) { 500.0f, 300.0f, 0.0f, 1.0f };
  pp_page_curl_deform_vertex (800.0f, 600.0f, 0.5, 0.0, 50.0f, &vertex);
  g_assert_cmpfloat_with_epsilon (vertex.x, 500.0f, 0.001f);
  g_assert_cmpfloat_with_epsilon (vertex.y, 300.0f, 0.001f);
  g_assert_cmpfloat_with_epsilon (vertex.z, 50.0f, 0.001f);
  g_assert_cmpfloat_with_epsilon (vertex.shade, 159.0f / 255.0f, 0.001f);

  vertex = (PpPageCurlVertex) { 100.0f, 100.0f, 0.0f, 1.0f };
  pp_page_curl_deform_vertex (800.0f, 600.0f, 0.5, 0.0, 50.0f, &vertex);
  g_assert_cmpfloat_with_epsilon (vertex.x, 100.0f, 0.001f);
  g_assert_cmpfloat_with_epsilon (vertex.y, 100.0f, 0.001f);
  g_assert_cmpfloat_with_epsilon (vertex.z, 0.0f, 0.001f);
  g_assert_cmpfloat_with_epsilon (vertex.shade, 1.0f, 0.001f);
}

static void
test_folder_access (void)
{
  g_autofree char *fixtures = g_path_get_dirname (fixture_path);
  g_autofree char *folder_path = g_build_filename (fixtures,
                                                   "folder-access",
                                                   NULL);
  g_autoptr (GFile) folder = g_file_new_for_path (folder_path);
  g_autoptr (GPtrArray) files = NULL;
  g_autoptr (PpPresentation) presentation = NULL;
  g_autoptr (PpPresentation) missing_presentation = NULL;
  g_autoptr (GError) error = NULL;
  g_autofree char *basename = NULL;
  g_autofree char *missing = NULL;
  g_autofree char *temporary_path = NULL;
  g_autofree char *first_path = NULL;
  g_autofree char *second_path = NULL;
  g_autofree char *ignored_path = NULL;
  g_autofree char *subdirectory_path = NULL;
  g_autoptr (GFile) temporary_folder = NULL;
  g_autoptr (GFile) regular_file = NULL;

  files = pp_file_access_find_presentations (folder, NULL, &error);
  g_assert_no_error (error);
  g_assert_nonnull (files);
  g_assert_cmpuint (files->len, ==, 1);
  basename = g_file_get_basename (g_ptr_array_index (files, 0));
  g_assert_cmpstr (basename, ==, "demo.pin");

  presentation = pp_presentation_load (g_ptr_array_index (files, 0),
                                       FALSE,
                                       NULL,
                                       &error);
  g_assert_no_error (error);
  g_assert_nonnull (presentation);
  missing = pp_render_find_missing_relative_asset (presentation);
  g_assert_null (missing);

  missing_presentation = pp_presentation_parse ("-- [missing.png]\nMissing\n",
                                                g_ptr_array_index (files, 0),
                                                FALSE,
                                                &error);
  g_assert_no_error (error);
  g_assert_nonnull (missing_presentation);
  missing = pp_render_find_missing_relative_asset (missing_presentation);
  g_assert_cmpstr (missing, ==, "missing.png");

  temporary_path = g_dir_make_tmp ("pinpoint-access-XXXXXX", &error);
  g_assert_no_error (error);
  first_path = g_build_filename (temporary_path, "a.pin", NULL);
  second_path = g_build_filename (temporary_path, "b.pin", NULL);
  ignored_path = g_build_filename (temporary_path, "ignored.txt", NULL);
  subdirectory_path = g_build_filename (temporary_path, "directory.pin", NULL);
  g_assert_true (g_file_set_contents (second_path, "--\nB\n", -1, &error));
  g_assert_no_error (error);
  g_assert_true (g_file_set_contents (first_path, "--\nA\n", -1, &error));
  g_assert_no_error (error);
  g_assert_true (g_file_set_contents (ignored_path, "ignored", -1, &error));
  g_assert_no_error (error);
  g_assert_cmpint (g_mkdir (subdirectory_path, 0700), ==, 0);

  temporary_folder = g_file_new_for_path (temporary_path);
  g_clear_pointer (&files, g_ptr_array_unref);
  files = pp_file_access_find_presentations (temporary_folder, NULL, &error);
  g_assert_no_error (error);
  g_assert_cmpuint (files->len, ==, 2);
  g_clear_pointer (&basename, g_free);
  basename = g_file_get_basename (g_ptr_array_index (files, 0));
  g_assert_cmpstr (basename, ==, "a.pin");
  g_clear_pointer (&basename, g_free);
  basename = g_file_get_basename (g_ptr_array_index (files, 1));
  g_assert_cmpstr (basename, ==, "b.pin");

  regular_file = g_file_new_for_path (first_path);
  g_clear_pointer (&files, g_ptr_array_unref);
  files = pp_file_access_find_presentations (regular_file, NULL, &error);
  g_assert_null (files);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_DIRECTORY);
  g_clear_error (&error);

  g_assert_cmpint (g_rmdir (subdirectory_path), ==, 0);
  g_assert_cmpint (g_remove (ignored_path), ==, 0);
  g_assert_cmpint (g_remove (second_path), ==, 0);
  g_assert_cmpint (g_remove (first_path), ==, 0);
  g_assert_cmpint (g_rmdir (temporary_path), ==, 0);
}

static void
test_bundled_introduction (void)
{
  static const char *const copied_files[] = {
    "introduction.pin",
    "bg.jpg",
    "bowls.jpg",
    "linus.jpg",
    "bunny.webm",
    "ORIGIN.md",
    NULL,
  };
  g_autofree char *temporary_path = NULL;
  g_autoptr (GFile) bundled = pp_introduction_get_presentation ();
  g_autoptr (GFile) folder = NULL;
  g_autoptr (GFile) copied_presentation = NULL;
  g_autoptr (PpPresentation) presentation = NULL;
  g_autoptr (PpPresentation) copied = NULL;
  g_autoptr (GError) error = NULL;
  g_autofree char *missing = NULL;
  const PpSlide *video_slide;
  g_autoptr (GFileIOStream) invalid_folder_stream = NULL;
  g_autoptr (GFile) invalid_folder = NULL;

  presentation = pp_presentation_load (bundled, FALSE, NULL, &error);
  g_assert_no_error (error);
  g_assert_nonnull (presentation);
  g_assert_cmpuint (pp_presentation_get_n_slides (presentation), ==, 33);
  missing = pp_render_find_missing_relative_asset (presentation);
  g_assert_null (missing);
  video_slide = pp_presentation_get_slide (presentation, 7);
  g_assert_cmpint (video_slide->background_type, ==, PP_BACKGROUND_VIDEO);
  g_assert_cmpstr (video_slide->background, ==, "bunny.webm");

  temporary_path = g_dir_make_tmp ("pinpoint-introduction-XXXXXX", &error);
  g_assert_no_error (error);
  g_assert_nonnull (temporary_path);
  folder = g_file_new_for_path (temporary_path);
  g_assert_true (pp_introduction_copy_to_folder (folder,
                                                 &copied_presentation,
                                                 NULL,
                                                 &error));
  g_assert_no_error (error);
  copied = pp_presentation_load (copied_presentation, FALSE, NULL, &error);
  g_assert_no_error (error);
  g_assert_nonnull (copied);
  missing = pp_render_find_missing_relative_asset (copied);
  g_assert_null (missing);

  g_assert_false (pp_introduction_copy_to_folder (folder,
                                                  NULL,
                                                  NULL,
                                                  &error));
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_EXISTS);
  g_clear_error (&error);

  invalid_folder = g_file_new_tmp ("pinpoint-not-a-folder-XXXXXX",
                                   &invalid_folder_stream,
                                   &error);
  g_assert_no_error (error);
  g_assert_true (g_io_stream_close (G_IO_STREAM (invalid_folder_stream),
                                    NULL,
                                    &error));
  g_assert_no_error (error);
  g_clear_object (&invalid_folder_stream);
  g_assert_false (pp_introduction_copy_to_folder (invalid_folder,
                                                  NULL,
                                                  NULL,
                                                  &error));
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_DIRECTORY);
  g_clear_error (&error);
  g_assert_true (g_file_delete (invalid_folder, NULL, &error));
  g_assert_no_error (error);

  g_clear_object (&copied_presentation);
  g_clear_pointer (&copied, pp_presentation_free);
  for (guint i = 0; copied_files[i] != NULL; i++)
    {
      g_autoptr (GFile) child = g_file_get_child (folder, copied_files[i]);

      g_assert_true (g_file_delete (child, NULL, &error));
      g_assert_no_error (error);
    }
  g_assert_true (g_file_delete (folder, NULL, &error));
  g_assert_no_error (error);
}

static void
test_pdf_export (void)
{
  const char *source = "[white] [text-color=black]\n--\nPDF slide\n#Speaker note\n";
  g_autoptr (PpPresentation) presentation = NULL;
  g_autoptr (GFileIOStream) stream = NULL;
  g_autoptr (GFile) file = NULL;
  g_autoptr (GFileInfo) info = NULL;
  g_autoptr (GError) error = NULL;

  presentation = pp_presentation_parse (source, NULL, FALSE, &error);
  g_assert_no_error (error);
  file = g_file_new_tmp ("pinpoint-test-XXXXXX", &stream, &error);
  g_assert_no_error (error);
  g_assert_true (g_io_stream_close (G_IO_STREAM (stream), NULL, &error));
  g_assert_no_error (error);
  g_clear_object (&stream);

  g_assert_true (pp_pdf_export (presentation, file, &error));
  g_assert_no_error (error);
  info = g_file_query_info (file,
                            G_FILE_ATTRIBUTE_STANDARD_SIZE,
                            G_FILE_QUERY_INFO_NONE,
                            NULL,
                            &error);
  g_assert_no_error (error);
  g_assert_cmpuint (g_file_info_get_size (info), >, 0);
  g_assert_true (g_file_delete (file, NULL, &error));
  g_assert_no_error (error);
}

static guint
count_pdf_tokens (const char *contents,
                  gsize       length,
                  const char *token)
{
  gsize token_length = strlen (token);
  guint count = 0;

  for (gsize offset = 0; offset + token_length <= length;)
    {
      if (memcmp (contents + offset, token, token_length) == 0)
        {
          count++;
          offset += token_length;
        }
      else
        offset++;
    }
  return count;
}

static gboolean
save_test_jpeg (GdkPixbuf  *pixbuf,
                const char *path)
{
  struct jpeg_compress_struct compressor;
  struct jpeg_error_mgr error;
  FILE *file = g_fopen (path, "wb");
  const guchar *pixels = gdk_pixbuf_read_pixels (pixbuf);
  int stride = gdk_pixbuf_get_rowstride (pixbuf);

  if (file == NULL || pixels == NULL)
    {
      if (file != NULL)
        fclose (file);
      return FALSE;
    }
  compressor.err = jpeg_std_error (&error);
  jpeg_create_compress (&compressor);
  jpeg_stdio_dest (&compressor, file);
  compressor.image_width = gdk_pixbuf_get_width (pixbuf);
  compressor.image_height = gdk_pixbuf_get_height (pixbuf);
  compressor.input_components = 3;
  compressor.in_color_space = JCS_RGB;
  jpeg_set_defaults (&compressor);
  jpeg_set_quality (&compressor, 92, TRUE);
  jpeg_start_compress (&compressor, TRUE);
  while (compressor.next_scanline < compressor.image_height)
    {
      JSAMPROW row = (JSAMPROW) (pixels +
        (gsize) compressor.next_scanline * stride);

      jpeg_write_scanlines (&compressor, &row, 1);
    }
  jpeg_finish_compress (&compressor);
  jpeg_destroy_compress (&compressor);
  return fclose (file) == 0;
}

static void
test_pdf_jpeg_compression_and_deduplication (void)
{
  static const char source[] =
    "-- [large.jpg] [fill]\nFirst use\n"
    "-- [white]\nIntervening slide\n"
    "-- [large.jpg] [fill]\nSecond use\n";
  const int image_width = 1800;
  const int image_height = 1350;
  PpPdfOptions options = PP_PDF_OPTIONS_DEFAULT;
  g_autofree char *directory_path = NULL;
  g_autofree char *image_path = NULL;
  g_autofree char *presentation_path = NULL;
  g_autofree char *output_path = NULL;
  g_autofree char *pdf_contents = NULL;
  g_autoptr (GdkPixbuf) pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB,
                                                 FALSE,
                                                 8,
                                                 image_width,
                                                 image_height);
  g_autoptr (GFile) presentation_file = NULL;
  g_autoptr (GFile) output_file = NULL;
  g_autoptr (PpPresentation) presentation = NULL;
  g_autoptr (GError) error = NULL;
  guchar *pixels;
  int stride;
  gsize pdf_length = 0;
  GStatBuf image_stat;
  GStatBuf output_stat;

  g_assert_nonnull (pixbuf);
  pixels = gdk_pixbuf_get_pixels (pixbuf);
  stride = gdk_pixbuf_get_rowstride (pixbuf);
  directory_path = g_dir_make_tmp ("pinpoint-pdf-images-XXXXXX", &error);
  g_assert_no_error (error);
  g_assert_nonnull (directory_path);
  image_path = g_build_filename (directory_path, "large.jpg", NULL);
  presentation_path = g_build_filename (directory_path, "images.pin", NULL);
  output_path = g_build_filename (directory_path, "images.pdf", NULL);
  presentation_file = g_file_new_for_path (presentation_path);
  output_file = g_file_new_for_path (output_path);
  for (int y = 0; y < image_height; y++)
    for (int x = 0; x < image_width; x++)
      {
        guchar *pixel = pixels + (gsize) y * stride + x * 3;

        pixel[0] = (x * 7 + y * 3) & 0xff;
        pixel[1] = (x * 2 + y * 11) & 0xff;
        pixel[2] = ((x / 8) * 29 + (y / 8) * 17) & 0xff;
      }
  g_assert_true (save_test_jpeg (pixbuf, image_path));
  g_assert_true (g_file_set_contents (presentation_path,
                                      source,
                                      -1,
                                      &error));
  g_assert_no_error (error);
  presentation = pp_presentation_load_for_pdf (presentation_file,
                                               FALSE,
                                               NULL,
                                               &error);
  g_assert_no_error (error);
  g_assert_nonnull (presentation);
  options.include_speaker_notes = FALSE;
  g_assert_true (pp_pdf_export_with_options (presentation,
                                             output_file,
                                             &options,
                                             &error));
  g_assert_no_error (error);
  g_assert_true (g_file_get_contents (output_path,
                                      &pdf_contents,
                                      &pdf_length,
                                      &error));
  g_assert_no_error (error);
  g_assert_cmpuint (count_pdf_tokens (pdf_contents,
                                      pdf_length,
                                      "/DCTDecode"),
                    ==,
                    1);
  g_assert_cmpuint (count_pdf_tokens (pdf_contents,
                                      pdf_length,
                                      "/Subtype /Image"),
                    ==,
                    1);
  g_assert_cmpint (g_stat (image_path, &image_stat), ==, 0);
  g_assert_cmpint (g_stat (output_path, &output_stat), ==, 0);
  g_test_message ("source JPEG: %" G_GOFFSET_FORMAT
                  " bytes; deduplicated PDF: %" G_GOFFSET_FORMAT " bytes",
                  (goffset) image_stat.st_size,
                  (goffset) output_stat.st_size);
  g_assert_cmpuint ((guint64) output_stat.st_size,
                    <,
                    (guint64) image_stat.st_size);

  g_assert_cmpint (g_remove (output_path), ==, 0);
  g_assert_cmpint (g_remove (presentation_path), ==, 0);
  g_assert_cmpint (g_remove (image_path), ==, 0);
  g_assert_cmpint (g_rmdir (directory_path), ==, 0);
}

typedef struct
{
  GCancellable *cancellable;
  guint calls;
  guint completed;
  guint total;
} PdfProgressState;

static void
cancel_pdf_after_first_slide (guint    completed_slides,
                              guint    total_slides,
                              gpointer user_data)
{
  PdfProgressState *state = user_data;

  state->calls++;
  state->completed = completed_slides;
  state->total = total_slides;
  if (completed_slides == 1)
    g_cancellable_cancel (state->cancellable);
}

static void
test_pdf_export_cancellation (void)
{
  static const char source[] =
    "-- [white]\nFirst\n"
    "-- [white]\nSecond\n"
    "-- [white]\nThird\n";
  static const char sentinel[] = "existing destination";
  const PpPdfOptions options = PP_PDF_OPTIONS_DEFAULT;
  g_autoptr (PpPresentation) presentation = NULL;
  g_autoptr (GCancellable) cancellable = g_cancellable_new ();
  g_autoptr (GFileIOStream) stream = NULL;
  g_autoptr (GFile) output = NULL;
  g_autofree char *contents = NULL;
  g_autoptr (GError) error = NULL;
  gsize length = 0;
  PdfProgressState state = { .cancellable = cancellable };

  presentation = pp_presentation_parse (source, NULL, FALSE, &error);
  g_assert_no_error (error);
  output = g_file_new_tmp ("pinpoint-cancelled-pdf-XXXXXX",
                           &stream,
                           &error);
  g_assert_no_error (error);
  g_assert_true (g_io_stream_close (G_IO_STREAM (stream), NULL, &error));
  g_assert_no_error (error);
  g_clear_object (&stream);
  g_assert_true (g_file_replace_contents (output,
                                          sentinel,
                                          strlen (sentinel),
                                          NULL,
                                          FALSE,
                                          G_FILE_CREATE_NONE,
                                          NULL,
                                          NULL,
                                          &error));
  g_assert_no_error (error);

  g_assert_false (pp_pdf_export_with_options_full (presentation,
                                                   output,
                                                   &options,
                                                   cancellable,
                                                   cancel_pdf_after_first_slide,
                                                   &state,
                                                   &error));
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
  g_clear_error (&error);
  g_assert_cmpuint (state.calls, ==, 2);
  g_assert_cmpuint (state.completed, ==, 1);
  g_assert_cmpuint (state.total, ==, 3);
  g_assert_true (g_file_load_contents (output,
                                       NULL,
                                       &contents,
                                       &length,
                                       NULL,
                                       &error));
  g_assert_no_error (error);
  g_assert_cmpuint (length, ==, strlen (sentinel));
  g_assert_cmpmem (contents, length, sentinel, strlen (sentinel));
  g_assert_true (g_file_delete (output, NULL, &error));
  g_assert_no_error (error);
}

typedef struct
{
  GMainLoop *loop;
  GFile *expected_output;
  gboolean success;
  GError *error;
} AsyncPdfState;

static void
pdf_export_finished_cb (GObject      *source,
                        GAsyncResult *result,
                        gpointer      user_data)
{
  AsyncPdfState *state = user_data;

  (void) source;
  g_assert_true (g_file_equal (pp_pdf_export_file_get_output (result),
                               state->expected_output));
  state->success = pp_pdf_export_file_finish (result, &state->error);
  g_main_loop_quit (state->loop);
}

static void
test_pdf_export_async (void)
{
  static const char source[] = "--\nAsynchronous PDF\n";
  const PpPdfOptions options = PP_PDF_OPTIONS_DEFAULT;
  g_autoptr (GFileIOStream) input_stream = NULL;
  g_autoptr (GFileIOStream) output_stream = NULL;
  g_autoptr (GFile) input = NULL;
  g_autoptr (GFile) output = NULL;
  g_autoptr (GFileInfo) info = NULL;
  g_autoptr (GMainLoop) loop = g_main_loop_new (NULL, FALSE);
  g_autoptr (GError) error = NULL;
  AsyncPdfState state = { .loop = loop };

  input = g_file_new_tmp ("pinpoint-async-input-XXXXXX",
                          &input_stream,
                          &error);
  g_assert_no_error (error);
  output = g_file_new_tmp ("pinpoint-async-output-XXXXXX",
                           &output_stream,
                           &error);
  g_assert_no_error (error);
  g_assert_true (g_io_stream_close (G_IO_STREAM (input_stream), NULL, &error));
  g_assert_no_error (error);
  g_assert_true (g_io_stream_close (G_IO_STREAM (output_stream), NULL, &error));
  g_assert_no_error (error);
  g_clear_object (&input_stream);
  g_clear_object (&output_stream);
  state.expected_output = output;
  g_assert_true (g_file_replace_contents (input,
                                          source,
                                          strlen (source),
                                          NULL,
                                          FALSE,
                                          G_FILE_CREATE_NONE,
                                          NULL,
                                          NULL,
                                          &error));
  g_assert_no_error (error);

  pp_pdf_export_file_async (input,
                            FALSE,
                            output,
                            &options,
                            NULL,
                            pdf_export_finished_cb,
                            &state);
  g_main_loop_run (loop);
  g_assert_true (state.success);
  g_assert_no_error (state.error);
  info = g_file_query_info (output,
                            G_FILE_ATTRIBUTE_STANDARD_SIZE,
                            G_FILE_QUERY_INFO_NONE,
                            NULL,
                            &error);
  g_assert_no_error (error);
  g_assert_cmpuint (g_file_info_get_size (info), >, 0);

  g_assert_true (g_file_delete (input, NULL, &error));
  g_assert_no_error (error);
  state.success = TRUE;
  pp_pdf_export_file_async (input,
                            FALSE,
                            output,
                            &options,
                            NULL,
                            pdf_export_finished_cb,
                            &state);
  g_main_loop_run (loop);
  g_assert_false (state.success);
  g_assert_nonnull (state.error);
  g_clear_error (&state.error);
  g_assert_true (g_file_delete (output, NULL, &error));
  g_assert_no_error (error);
}

static void
test_pdf_default_stage_color (void)
{
  const char *source = "--\nDefault PDF background\n";
  g_autoptr (GFileIOStream) stream = NULL;
  g_autoptr (GFile) file = NULL;
  g_autoptr (PpPresentation) presentation = NULL;
  g_autoptr (GError) error = NULL;

  file = g_file_new_tmp ("pinpoint-pdf-default-XXXXXX", &stream, &error);
  g_assert_no_error (error);
  g_assert_true (g_io_stream_close (G_IO_STREAM (stream), NULL, &error));
  g_assert_no_error (error);
  g_clear_object (&stream);
  g_assert_true (g_file_replace_contents (file,
                                          source,
                                          strlen (source),
                                          NULL,
                                          FALSE,
                                          G_FILE_CREATE_NONE,
                                          NULL,
                                          NULL,
                                          &error));
  g_assert_no_error (error);

  presentation = pp_presentation_load_for_pdf (file, FALSE, NULL, &error);
  g_assert_no_error (error);
  g_assert_cmpstr (pp_presentation_get_defaults (presentation)->stage_color,
                   ==,
                   "white");
  g_assert_cmpstr (pp_presentation_get_slide (presentation, 0)->stage_color,
                   ==,
                   "white");
  g_assert_true (g_file_delete (file, NULL, &error));
  g_assert_no_error (error);
}

static void
test_pdf_options (void)
{
  const char *source = "--\nPDF slide\n#A detailed speaker note for export\n";
  PpPdfOptions options = PP_PDF_OPTIONS_DEFAULT;
  g_autoptr (PpPresentation) presentation = NULL;
  g_autoptr (GFileIOStream) notes_stream = NULL;
  g_autoptr (GFileIOStream) slides_stream = NULL;
  g_autoptr (GFile) notes_file = NULL;
  g_autoptr (GFile) slides_file = NULL;
  g_autoptr (GFileInfo) notes_info = NULL;
  g_autoptr (GFileInfo) slides_info = NULL;
  g_autoptr (GError) error = NULL;
  double width;
  double height;

  pp_pdf_options_get_page_dimensions (&options, &width, &height);
  g_assert_cmpfloat_with_epsilon (width, 841.88976378, 0.0001);
  g_assert_cmpfloat_with_epsilon (height, 595.275590551, 0.0001);
  options.page_size = PP_PDF_PAGE_SIZE_LETTER;
  options.orientation = PP_PDF_ORIENTATION_PORTRAIT;
  pp_pdf_options_get_page_dimensions (&options, &width, &height);
  g_assert_cmpfloat_with_epsilon (width, 612.0, 0.0001);
  g_assert_cmpfloat_with_epsilon (height, 792.0, 0.0001);

  presentation = pp_presentation_parse (source, NULL, FALSE, &error);
  g_assert_no_error (error);
  notes_file = g_file_new_tmp ("pinpoint-notes-pdf-XXXXXX",
                               &notes_stream,
                               &error);
  g_assert_no_error (error);
  slides_file = g_file_new_tmp ("pinpoint-slides-pdf-XXXXXX",
                                &slides_stream,
                                &error);
  g_assert_no_error (error);
  g_assert_true (g_io_stream_close (G_IO_STREAM (notes_stream), NULL, &error));
  g_assert_no_error (error);
  g_assert_true (g_io_stream_close (G_IO_STREAM (slides_stream), NULL, &error));
  g_assert_no_error (error);
  g_clear_object (&notes_stream);
  g_clear_object (&slides_stream);

  options = PP_PDF_OPTIONS_DEFAULT;
  g_assert_true (pp_pdf_export_with_options (presentation,
                                             notes_file,
                                             &options,
                                             &error));
  g_assert_no_error (error);
  options.include_speaker_notes = FALSE;
  g_assert_true (pp_pdf_export_with_options (presentation,
                                             slides_file,
                                             &options,
                                             &error));
  g_assert_no_error (error);
  notes_info = g_file_query_info (notes_file,
                                  G_FILE_ATTRIBUTE_STANDARD_SIZE,
                                  G_FILE_QUERY_INFO_NONE,
                                  NULL,
                                  &error);
  g_assert_no_error (error);
  slides_info = g_file_query_info (slides_file,
                                   G_FILE_ATTRIBUTE_STANDARD_SIZE,
                                   G_FILE_QUERY_INFO_NONE,
                                   NULL,
                                   &error);
  g_assert_no_error (error);
  g_assert_cmpuint (g_file_info_get_size (notes_info),
                    >,
                    g_file_info_get_size (slides_info));
  g_assert_true (g_file_delete (notes_file, NULL, &error));
  g_assert_no_error (error);
  g_assert_true (g_file_delete (slides_file, NULL, &error));
  g_assert_no_error (error);
}

static void
test_pdf_sibling_asset (void)
{
  g_autofree char *fixtures = g_path_get_dirname (fixture_path);
  g_autofree char *presentation_path = g_build_filename (fixtures,
                                                         "folder-access",
                                                         "demo.pin",
                                                         NULL);
  g_autoptr (GFile) presentation_file = g_file_new_for_path (presentation_path);
  g_autoptr (GFileIOStream) stream = NULL;
  g_autoptr (GFile) output = NULL;
  g_autoptr (GFileInfo) info = NULL;
  g_autoptr (PpPresentation) presentation = NULL;
  g_autoptr (GError) error = NULL;

  presentation = pp_presentation_load_for_pdf (presentation_file,
                                               FALSE,
                                               NULL,
                                               &error);
  g_assert_no_error (error);
  g_assert_nonnull (presentation);
  output = g_file_new_tmp ("pinpoint-sibling-pdf-XXXXXX", &stream, &error);
  g_assert_no_error (error);
  g_assert_true (g_io_stream_close (G_IO_STREAM (stream), NULL, &error));
  g_assert_no_error (error);
  g_clear_object (&stream);

  g_assert_true (pp_pdf_export (presentation, output, &error));
  g_assert_no_error (error);
  info = g_file_query_info (output,
                            G_FILE_ATTRIBUTE_STANDARD_SIZE,
                            G_FILE_QUERY_INFO_NONE,
                            NULL,
                            &error);
  g_assert_no_error (error);
  g_assert_cmpuint (g_file_info_get_size (info), >, 0);
  g_assert_true (g_file_delete (output, NULL, &error));
  g_assert_no_error (error);
}

static void
test_video_thumbnail_scoring (void)
{
  g_autoptr (GdkPixbuf) black = gdk_pixbuf_new (GDK_COLORSPACE_RGB,
                                                FALSE,
                                                8,
                                                64,
                                                36);
  g_autoptr (GdkPixbuf) flat = gdk_pixbuf_new (GDK_COLORSPACE_RGB,
                                               FALSE,
                                               8,
                                               64,
                                               36);
  g_autoptr (GdkPixbuf) detailed = gdk_pixbuf_new (GDK_COLORSPACE_RGB,
                                                   FALSE,
                                                   8,
                                                   64,
                                                   36);
  guchar *pixels = gdk_pixbuf_get_pixels (detailed);
  int stride = gdk_pixbuf_get_rowstride (detailed);
  gboolean black_acceptable;
  gboolean flat_acceptable;
  gboolean detailed_acceptable;
  double black_score;
  double flat_score;
  double detailed_score;

  gdk_pixbuf_fill (black, 0x000000ff);
  gdk_pixbuf_fill (flat, 0x808080ff);
  for (int y = 0; y < 36; y++)
    for (int x = 0; x < 64; x++)
      {
        guchar value = ((x / 4) + (y / 4)) % 2 == 0 ? 24 : 232;
        guchar *pixel = pixels + y * stride + x * 3;

        pixel[0] = value;
        pixel[1] = value;
        pixel[2] = value;
      }

  black_score = pp_video_thumbnail_score (black, &black_acceptable);
  flat_score = pp_video_thumbnail_score (flat, &flat_acceptable);
  detailed_score = pp_video_thumbnail_score (detailed, &detailed_acceptable);
  g_assert_false (black_acceptable);
  g_assert_false (flat_acceptable);
  g_assert_true (detailed_acceptable);
  g_assert_cmpfloat (detailed_score, >, black_score);
  g_assert_cmpfloat (detailed_score, >, flat_score);
}

static void
test_video_thumbnail (void)
{
  g_autoptr (GFileIOStream) stream = NULL;
  g_autoptr (GFile) file = NULL;
  g_autoptr (GdkPixbuf) thumbnail = NULL;
  g_autoptr (GError) error = NULL;
  g_autofree char *path = NULL;
  GstElement *pipeline = NULL;
  GstElement *source = NULL;
  GstElement *filter = NULL;
  GstElement *muxer = NULL;
  GstElement *sink = NULL;
  GstCaps *caps = NULL;
  GstBus *bus = NULL;
  GstMessage *message = NULL;

  source = gst_element_factory_make ("appsrc", NULL);
  filter = gst_element_factory_make ("capsfilter", NULL);
  muxer = gst_element_factory_make ("avimux", NULL);
  sink = gst_element_factory_make ("filesink", NULL);
  if (source == NULL || filter == NULL || muxer == NULL || sink == NULL)
    {
      if (source != NULL)
        gst_object_unref (source);
      if (filter != NULL)
        gst_object_unref (filter);
      if (muxer != NULL)
        gst_object_unref (muxer);
      if (sink != NULL)
        gst_object_unref (sink);
      g_test_skip ("GStreamer test-video elements are unavailable");
      return;
    }

  file = g_file_new_tmp ("pinpoint-video-XXXXXX", &stream, &error);
  g_assert_no_error (error);
  g_assert_true (g_io_stream_close (G_IO_STREAM (stream), NULL, &error));
  g_assert_no_error (error);
  g_clear_object (&stream);
  path = g_file_get_path (file);
  pipeline = gst_pipeline_new ("thumbnail-test-video");
  gst_object_ref_sink (pipeline);
  caps = gst_caps_new_simple ("video/x-raw",
                              "format", G_TYPE_STRING, "I420",
                              "width", G_TYPE_INT, 96,
                              "height", G_TYPE_INT, 54,
                              "framerate", GST_TYPE_FRACTION, 5, 1,
                              NULL);
  g_object_set (source,
                "caps", caps,
                "format", GST_FORMAT_TIME,
                "is-live", FALSE,
                NULL);
  g_object_set (filter, "caps", caps, NULL);
  g_object_set (sink, "location", path, NULL);
  gst_caps_unref (caps);
  gst_bin_add_many (GST_BIN (pipeline), source, filter, muxer, sink, NULL);
  g_assert_true (gst_element_link_many (source, filter, muxer, sink, NULL));
  g_assert_cmpint (gst_element_set_state (pipeline, GST_STATE_PLAYING),
                   !=,
                   GST_STATE_CHANGE_FAILURE);
  for (guint frame_number = 0; frame_number < 40; frame_number++)
    {
      const gsize luma_size = 96 * 54;
      const gsize chroma_size = 48 * 27;
      GstBuffer *buffer = gst_buffer_new_allocate (NULL,
                                                   luma_size + chroma_size * 2,
                                                   NULL);
      GstMapInfo map;

      g_assert_true (gst_buffer_map (buffer, &map, GST_MAP_WRITE));
      for (guint y = 0; y < 54; y++)
        for (guint x = 0; x < 96; x++)
          map.data[y * 96 + x] = frame_number < 20
            ? 16
            : (((x / 6) + (y / 6)) % 2 == 0 ? 32 : 220);
      memset (map.data + luma_size, 128, chroma_size * 2);
      gst_buffer_unmap (buffer, &map);
      GST_BUFFER_PTS (buffer) = gst_util_uint64_scale (frame_number,
                                                       GST_SECOND,
                                                       5);
      GST_BUFFER_DURATION (buffer) = GST_SECOND / 5;
      g_assert_cmpint (gst_app_src_push_buffer (GST_APP_SRC (source), buffer),
                       ==,
                       GST_FLOW_OK);
    }
  g_assert_cmpint (gst_app_src_end_of_stream (GST_APP_SRC (source)),
                   ==,
                   GST_FLOW_OK);
  bus = gst_element_get_bus (pipeline);
  message = gst_bus_timed_pop_filtered (bus,
                                        5 * GST_SECOND,
                                        GST_MESSAGE_EOS | GST_MESSAGE_ERROR);
  g_assert_nonnull (message);
  g_assert_cmpint (GST_MESSAGE_TYPE (message), ==, GST_MESSAGE_EOS);
  gst_message_unref (message);
  gst_object_unref (bus);
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);

  thumbnail = pp_video_thumbnail_new (file, NULL, &error);
  g_assert_no_error (error);
  g_assert_nonnull (thumbnail);
  g_assert_cmpint (gdk_pixbuf_get_width (thumbnail), ==, 96);
  g_assert_cmpint (gdk_pixbuf_get_height (thumbnail), ==, 54);
  {
    gboolean acceptable;

    pp_video_thumbnail_score (thumbnail, &acceptable);
    g_assert_true (acceptable);
  }
  g_clear_object (&thumbnail);
  thumbnail = pp_video_thumbnail_new_for_size (file, 24, 24, NULL, &error);
  g_assert_no_error (error);
  g_assert_nonnull (thumbnail);
  g_assert_cmpint (gdk_pixbuf_get_width (thumbnail), <=, 24);
  g_assert_cmpint (gdk_pixbuf_get_height (thumbnail), <=, 24);
  g_assert_cmpfloat ((double) gdk_pixbuf_get_width (thumbnail) /
                     gdk_pixbuf_get_height (thumbnail),
                     >,
                     1.7);
  {
    g_autoptr (GCancellable) cancellable = g_cancellable_new ();

    g_cancellable_cancel (cancellable);
    g_clear_object (&thumbnail);
    thumbnail = pp_video_thumbnail_new_for_size (file,
                                                 24,
                                                 24,
                                                 cancellable,
                                                 &error);
    g_assert_null (thumbnail);
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
    g_clear_error (&error);
  }
  g_assert_true (g_file_delete (file, NULL, &error));
  g_assert_no_error (error);
}

static void
test_legacy_transition (void)
{
  g_autofree char *fixtures = g_path_get_dirname (fixture_path);
  g_autofree char *presentation_path = g_build_filename (fixtures,
                                                         "compatibility.pin",
                                                         NULL);
  g_autoptr (GFile) file = g_file_new_for_path (presentation_path);
  g_autoptr (PpPresentation) presentation = NULL;
  g_autoptr (PpLegacyTransition) transition = NULL;
  g_autoptr (GError) error = NULL;
  PpTransitionState state;

  presentation = pp_presentation_load (file, FALSE, NULL, &error);
  g_assert_no_error (error);
  transition = pp_legacy_transition_load (presentation,
                                          "legacy-transition",
                                          &error);
  g_assert_no_error (error);
  g_assert_nonnull (transition);
  g_assert_cmpuint (pp_legacy_transition_get_duration (transition,
                                                       TRUE,
                                                       FALSE),
                    ==,
                    750);
  g_assert_cmpuint (pp_legacy_transition_get_duration (transition,
                                                       FALSE,
                                                       FALSE),
                    ==,
                    1200);
  g_assert_cmpuint (pp_legacy_transition_get_duration (transition,
                                                       FALSE,
                                                       TRUE),
                    ==,
                    1000);
  pp_legacy_transition_calculate (transition, TRUE, FALSE, 0.0, &state);
  g_assert_cmpfloat_with_epsilon (state.actor.x, 1024.0, 0.001);
  g_assert_cmpfloat_with_epsilon (state.actor.opacity, 0.0, 0.001);
  pp_legacy_transition_calculate (transition, TRUE, FALSE, 0.5, &state);
  g_assert_cmpfloat_with_epsilon (state.actor.x, 256.0, 0.001);
  g_assert_cmpfloat_with_epsilon (state.actor.opacity, 0.75, 0.001);
  g_assert_cmpfloat_with_epsilon (state.foreground.scale_x, 1.375, 0.001);
  pp_legacy_transition_calculate (transition, FALSE, FALSE, 1.0, &state);
  g_assert_cmpfloat_with_epsilon (state.actor.x, -512.0, 0.001);
  g_assert_cmpfloat_with_epsilon (state.actor.opacity, 0.0, 0.001);
  pp_legacy_transition_calculate (transition, TRUE, TRUE, 1.0, &state);
  g_assert_cmpfloat_with_epsilon (state.actor.x, 0.0, 0.001);
  pp_legacy_transition_calculate (transition, FALSE, TRUE, 1.0, &state);
  g_assert_cmpfloat_with_epsilon (state.actor.x, 1024.0, 0.001);
  g_assert_true (pp_transition_is_builtin ("page-curl"));
  g_assert_false (pp_transition_is_builtin ("legacy-transition"));

  g_assert_cmpfloat_with_epsilon (
    pp_transition_apply_easing (NULL, 0.25), 0.25, 0.0001);
  g_assert_cmpfloat_with_epsilon (
    pp_transition_apply_easing ("linear", 0.25), 0.25, 0.0001);
  g_assert_cmpfloat_with_epsilon (
    pp_transition_apply_easing ("ease-in-quad", 0.5), 0.25, 0.0001);
  g_assert_cmpfloat_with_epsilon (
    pp_transition_apply_easing ("ease-out-quad", 0.5), 0.75, 0.0001);
  g_assert_cmpfloat_with_epsilon (
    pp_transition_apply_easing ("ease-in-out-quad", 0.25), 0.125, 0.0001);
  g_assert_cmpfloat_with_epsilon (
    pp_transition_apply_easing ("ease-in-out-quad", 0.75), 0.875, 0.0001);
  g_assert_cmpfloat_with_epsilon (
    pp_transition_apply_easing ("ease-in-out-cubic", 0.25), 0.0625, 0.0001);
  g_assert_cmpfloat_with_epsilon (
    pp_transition_apply_easing ("ease-in-out-cubic", 0.75), 0.9375, 0.0001);
  g_assert_cmpfloat_with_epsilon (
    pp_transition_apply_easing ("ease-out-quint", 0.5), 0.96875, 0.0001);
  g_assert_cmpfloat_with_epsilon (
    pp_transition_apply_easing ("unknown", 0.25), 0.25, 0.0001);
}

static PpLegacyTransition *
load_inline_transition (const char  *json,
                        GError     **error)
{
  g_autofree char *temporary_path = NULL;
  g_autofree char *presentation_path = NULL;
  g_autofree char *transition_path = NULL;
  g_autoptr (GFile) presentation_file = NULL;
  g_autoptr (PpPresentation) presentation = NULL;
  g_autoptr (GError) setup_error = NULL;
  PpLegacyTransition *transition;

  temporary_path = g_dir_make_tmp ("pinpoint-transition-XXXXXX", &setup_error);
  g_assert_no_error (setup_error);
  presentation_path = g_build_filename (temporary_path, "talk.pin", NULL);
  transition_path = g_build_filename (temporary_path, "custom.json", NULL);
  g_assert_true (g_file_set_contents (presentation_path,
                                      "--\nTransition\n",
                                      -1,
                                      &setup_error));
  g_assert_no_error (setup_error);
  g_assert_true (g_file_set_contents (transition_path,
                                      json,
                                      -1,
                                      &setup_error));
  g_assert_no_error (setup_error);

  presentation_file = g_file_new_for_path (presentation_path);
  presentation = pp_presentation_load (presentation_file,
                                       FALSE,
                                       NULL,
                                       &setup_error);
  g_assert_no_error (setup_error);
  g_assert_nonnull (presentation);
  transition = pp_legacy_transition_load (presentation, "custom.json", error);

  g_assert_cmpint (g_remove (transition_path), ==, 0);
  g_assert_cmpint (g_remove (presentation_path), ==, 0);
  g_assert_cmpint (g_rmdir (temporary_path), ==, 0);
  return transition;
}

static void
test_legacy_transition_validation (void)
{
  static const char comprehensive[] =
    "["
    "{\"type\":\"ClutterState\",\"transitions\":["
    "{\"target\":\"show\",\"keys\":["
    "[\"background\",\"y\",\"easeInQuad\",20],"
    "[\"midground\",\"rotation-angle-x\",\"linear\",30],"
    "[\"foreground\",\"rotation-angle-y\",\"linear\",40],"
    "[\"actor\",\"rotation-angle-z\",\"linear\",50],"
    "[\"actor\",\"scale-x\",\"linear\",2],"
    "[\"actor\",\"scale-y\",\"linear\",3],"
    "[\"actor\",\"opacity\",\"linear\",300]"
    "]},"
    "{\"target\":\"post\",\"keys\":["
    "[\"actor\",\"y\",\"linear\",-2]"
    "]}"
    "]}"
    "]";
  static const char no_supported_properties[] =
    "[null,{},"
    "{\"type\":\"ClutterEffect\",\"effects\":[]},"
    "{\"type\":\"ClutterState\",\"transitions\":["
    "null,{},"
    "{\"target\":\"unknown\",\"keys\":[]},"
    "{\"target\":\"pre\"},"
    "{\"target\":\"pre\",\"keys\":["
    "[],"
    "[\"missing\",\"x\",\"linear\",1],"
    "[\"actor\",\"x\",\"linear\",{}],"
    "[\"actor\",\"unsupported\",\"linear\",1]"
    "]}]}]";
  g_autoptr (PpLegacyTransition) transition = NULL;
  g_autoptr (GError) error = NULL;
  g_autofree char *helper_stdout = NULL;
  g_autofree char *helper_stderr = NULL;
  PpTransitionState state;
  int helper_status;

  transition = load_inline_transition ("not json", &error);
  g_assert_null (transition);
  g_assert_nonnull (error);
  g_clear_error (&error);

  transition = load_inline_transition ("{}", &error);
  g_assert_null (transition);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
  g_clear_error (&error);

  transition = load_inline_transition ("[]", &error);
  g_assert_null (transition);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
  g_clear_error (&error);

  transition = load_inline_transition ("[{\"type\":\"ClutterState\"}]",
                                       &error);
  g_assert_null (transition);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
  g_clear_error (&error);

  transition = load_inline_transition (no_supported_properties, &error);
  g_assert_null (transition);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED);
  g_clear_error (&error);

  transition = load_inline_transition (comprehensive, &error);
  g_assert_no_error (error);
  g_assert_nonnull (transition);
  g_assert_cmpuint (pp_legacy_transition_get_duration (transition,
                                                       TRUE,
                                                       FALSE),
                    ==,
                    1000);
  pp_legacy_transition_calculate (transition, TRUE, FALSE, 1.0, &state);
  g_assert_cmpfloat_with_epsilon (state.background.y, 20.0, 0.001);
  g_assert_cmpfloat_with_epsilon (state.midground.angle_x, 30.0, 0.001);
  g_assert_cmpfloat_with_epsilon (state.foreground.angle_y, 40.0, 0.001);
  g_assert_cmpfloat_with_epsilon (state.actor.angle, 50.0, 0.001);
  g_assert_cmpfloat_with_epsilon (state.actor.scale_x, 2.0, 0.001);
  g_assert_cmpfloat_with_epsilon (state.actor.scale_y, 3.0, 0.001);
  g_assert_cmpfloat_with_epsilon (state.actor.opacity, 1.0, 0.001);
  pp_legacy_transition_free (NULL);

  {
    char *helper_argv[] = {
      (char *) test_program_path,
      (char *) "--legacy-transition-warning-helper",
      NULL,
    };

    g_assert_true (g_spawn_sync (NULL,
                                 helper_argv,
                                 NULL,
                                 G_SPAWN_DEFAULT,
                                 NULL,
                                 NULL,
                                 &helper_stdout,
                                 &helper_stderr,
                                 &helper_status,
                                 &error));
  }
  g_assert_no_error (error);
  g_assert_true (g_spawn_check_wait_status (helper_status, &error));
  g_assert_no_error (error);
  g_assert_nonnull (strstr (helper_stderr,
                            "ignores 1 unsupported custom actor"));
}

static int
run_legacy_transition_warning_helper (void)
{
  const char *source =
    "[{\"type\":\"ClutterState\",\"transitions\":["
    "{\"target\":\"show\",\"keys\":["
    "[\"actor\",\"x\",\"linear\",1],"
    "[\"actor\",\"unsupported\",\"linear\",1]"
    "]}]}]";
  g_autoptr (PpLegacyTransition) transition = NULL;
  g_autoptr (GError) error = NULL;

  transition = load_inline_transition (source, &error);
  if (transition == NULL)
    {
      g_printerr ("Unable to load warning fixture: %s\n", error->message);
      return EXIT_FAILURE;
    }
  return EXIT_SUCCESS;
}

static void
test_pixel_layout_validation (void)
{
  g_autoptr (GError) error = NULL;
  gsize row_bytes = 0;
  gsize required_bytes = 0;

  g_assert_true (pp_render_validate_pixel_layout (2,
                                                  2,
                                                  8,
                                                  3,
                                                  14,
                                                  &row_bytes,
                                                  &required_bytes,
                                                  &error));
  g_assert_no_error (error);
  g_assert_cmpuint (row_bytes, ==, 6);
  g_assert_cmpuint (required_bytes, ==, 14);

  g_assert_false (pp_render_validate_pixel_layout (0,
                                                   2,
                                                   8,
                                                   3,
                                                   14,
                                                   NULL,
                                                   NULL,
                                                   &error));
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
  g_clear_error (&error);

  g_assert_false (pp_render_validate_pixel_layout (2,
                                                   2,
                                                   8,
                                                   2,
                                                   14,
                                                   NULL,
                                                   NULL,
                                                   &error));
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
  g_clear_error (&error);

  g_assert_false (pp_render_validate_pixel_layout (3,
                                                   2,
                                                   8,
                                                   3,
                                                   17,
                                                   NULL,
                                                   NULL,
                                                   &error));
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
  g_clear_error (&error);

  g_assert_false (pp_render_validate_pixel_layout (2,
                                                   2,
                                                   8,
                                                   3,
                                                   13,
                                                   NULL,
                                                   NULL,
                                                   &error));
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
}

int
main (int   argc,
      char *argv[])
{
  if (argc == 2 &&
      g_str_equal (argv[1], "--legacy-transition-warning-helper"))
    return run_legacy_transition_warning_helper ();

  gst_init (&argc, &argv);
  g_test_init (&argc, &argv, NULL);
  g_assert_cmpint (argc, ==, 2);
  test_program_path = argv[0];
  fixture_path = argv[1];

  g_test_add_func ("/parser/compatibility", test_compatibility_fixture);
  g_test_add_func ("/parser/ignore-comments", test_ignore_comments);
  g_test_add_func ("/parser/visual-description", test_visual_description);
  g_test_add_func ("/parser/invalid-source", test_invalid_source);
  g_test_add_func ("/parser/historical-video-suffixes",
                   test_historical_video_suffixes);
  g_test_add_func ("/parser/background-type-detection",
                   test_background_type_detection);
  g_test_add_func ("/parser/background-content-sniffing",
                   test_background_content_sniffing);
  g_test_add_func ("/parser/first-changed-slide", test_first_changed_slide);
  g_test_add_func ("/parser/rehearsal-serialization", test_rehearsal_serialization);
  g_test_add_func ("/parser/native-transition-settings",
                   test_native_transition_settings);
  g_test_add_func ("/parser/rehearsal-finish", test_rehearsal_finish);
  g_test_add_func ("/render/geometry", test_geometry);
  g_test_add_func ("/render/asset-resolution", test_asset_resolution);
  g_test_add_func ("/render/pixel-layout-validation",
                   test_pixel_layout_validation);
  g_test_add_func ("/render/page-curl-deformation", test_page_curl_deformation);
  g_test_add_func ("/file-access/folder", test_folder_access);
  g_test_add_func ("/file-access/bundled-introduction",
                   test_bundled_introduction);
  g_test_add_func ("/render/pdf-export", test_pdf_export);
  g_test_add_func ("/render/pdf-jpeg-compression-and-deduplication",
                   test_pdf_jpeg_compression_and_deduplication);
  g_test_add_func ("/render/pdf-export-cancellation",
                   test_pdf_export_cancellation);
  g_test_add_func ("/render/pdf-export-async", test_pdf_export_async);
  g_test_add_func ("/render/pdf-default-stage-color", test_pdf_default_stage_color);
  g_test_add_func ("/render/pdf-options", test_pdf_options);
  g_test_add_func ("/render/pdf-sibling-asset", test_pdf_sibling_asset);
  g_test_add_func ("/render/video-thumbnail-scoring",
                   test_video_thumbnail_scoring);
  g_test_add_func ("/render/video-thumbnail", test_video_thumbnail);
  g_test_add_func ("/render/legacy-transition", test_legacy_transition);
  g_test_add_func ("/render/legacy-transition-validation",
                   test_legacy_transition_validation);

  return g_test_run ();
}
