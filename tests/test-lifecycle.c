#include "pp-introduction.h"
#include "pp-page-curl-view.h"
#include "pp-presentation.h"
#include "pp-speaker.h"
#include "pp-stage.h"

#include <gst/gst.h>
#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>

static const char *fixture_path;
static const char *transition_fixture_path;
static const char *native_transition_fixture_path;
static const char *page_curl_fixture_path;
static const char *multi_monitor_fixture_path;
static const char *camera_fixture_path;

static void
swap_displays_requested_cb (PpSpeaker *speaker,
                            gpointer   user_data)
{
  guint *requests = user_data;

  (void) speaker;
  (*requests)++;
}
static guint media_warning_count;

static GtkApplication *create_application (const char *application_id);

static GLogWriterOutput
test_log_writer (GLogLevelFlags   log_level,
                 const GLogField *fields,
                 gsize            n_fields,
                 gpointer         user_data)
{
  for (gsize i = 0; i < n_fields; i++)
    if (g_str_equal (fields[i].key, "MESSAGE") &&
        fields[i].value != NULL &&
        g_str_has_prefix (fields[i].value,
                          "Unable to play video background:"))
      media_warning_count++;

  return g_log_writer_default (log_level, fields, n_fields, user_data);
}

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

static guint
count_open_file_descriptors (void)
{
  g_autoptr (GError) error = NULL;
  GDir *directory = g_dir_open ("/proc/self/fd", 0, &error);
  guint count = 0;

  g_assert_no_error (error);
  g_assert_nonnull (directory);
  while (g_dir_read_name (directory) != NULL)
    count++;
  g_dir_close (directory);

  /* Do not count the descriptor used to enumerate /proc/self/fd itself. */
  g_assert_cmpuint (count, >, 0);
  return count - 1;
}

static void
assert_file_descriptor_ceiling (guint       baseline,
                                const char *context)
{
  guint actual;

  run_loop_for (300);
  actual = count_open_file_descriptors ();
  if (actual > baseline)
    g_error ("%s leaked file descriptors (baseline %u, now %u)",
             context,
             baseline,
             actual);
}

static void
wait_for_transition_to_finish (PpStage *stage,
                               guint    timeout_ms)
{
  gint64 deadline = g_get_monotonic_time () + (gint64) timeout_ms * 1000;

  while (pp_stage_is_transitioning (stage) &&
         g_get_monotonic_time () < deadline)
    run_loop_for (20);
  g_assert_false (pp_stage_is_transitioning (stage));
}

static void
assert_page_curl_gl_ready (GtkWidget *stage)
{
  GtkGLArea *area = NULL;

  for (GtkWidget *child = gtk_widget_get_first_child (stage);
       child != NULL;
       child = gtk_widget_get_next_sibling (child))
    if (GTK_IS_GL_AREA (child))
      {
        area = GTK_GL_AREA (child);
        break;
      }

  g_assert_nonnull (area);
  gtk_gl_area_make_current (area);
  g_assert_no_error ((GError *) gtk_gl_area_get_error (area));
}

static GdkTexture *
create_page_texture (guint8 red,
                     guint8 green,
                     guint8 blue)
{
  guint8 pixels[4 * 4 * 4];
  g_autoptr (GBytes) bytes = NULL;

  for (guint i = 0; i < G_N_ELEMENTS (pixels); i += 4)
    {
      pixels[i] = red;
      pixels[i + 1] = green;
      pixels[i + 2] = blue;
      pixels[i + 3] = 0xff;
    }
  bytes = g_bytes_new (pixels, sizeof pixels);
  return GDK_TEXTURE (gdk_memory_texture_new (
    4,
    4,
    GDK_MEMORY_R8G8B8A8_PREMULTIPLIED,
    bytes,
    4 * 4));
}

static void
test_page_curl_gl_render (void)
{
  g_autoptr (GtkApplication) application = create_application (
    "com.nedrichards.pinpoint.LifecyclePageCurlGlTest");
  g_autoptr (GdkTexture) previous = create_page_texture (0xff, 0x20, 0x20);
  g_autoptr (GdkTexture) current = create_page_texture (0x20, 0x40, 0xff);
  GtkWindow *window = GTK_WINDOW (gtk_application_window_new (application));
  GtkWidget *view = pp_page_curl_view_new ();

  gtk_window_set_default_size (window, 320, 240);
  gtk_window_set_child (window, view);
  gtk_window_present (window);
  run_loop_for (100);
  gtk_gl_area_make_current (GTK_GL_AREA (view));
  g_assert_no_error ((GError *) gtk_gl_area_get_error (GTK_GL_AREA (view)));

  pp_page_curl_view_set_transition (PP_PAGE_CURL_VIEW (view),
                                    previous,
                                    current,
                                    0.5,
                                    0.0,
                                    0.0,
                                    0.0,
                                    FALSE);
  run_loop_for (100);
  pp_page_curl_view_set_transition (PP_PAGE_CURL_VIEW (view),
                                    previous,
                                    current,
                                    0.0,
                                    0.0,
                                    0.5,
                                    15.0,
                                    TRUE);
  run_loop_for (100);
  pp_page_curl_view_clear (PP_PAGE_CURL_VIEW (view));
  gtk_window_destroy (window);
  run_loop_for (100);
}

static GtkApplication *
create_application (const char *application_id)
{
  GtkApplication *application = gtk_application_new (
    application_id,
    G_APPLICATION_NON_UNIQUE);
  g_autoptr (GError) error = NULL;

  g_assert_true (g_application_register (G_APPLICATION (application),
                                         NULL,
                                         &error));
  g_assert_no_error (error);
  return application;
}

static void
test_wayland_media_sink_caps (void)
{
  GstElement *source = gst_element_factory_make ("pipewiresrc", NULL);
  GstElement *sink = gst_element_factory_make ("gtk4paintablesink", NULL);
  GstElement *pipeline;
  GstPad *pad;
  g_autoptr (GstCaps) available = NULL;
  g_autoptr (GstCaps) dmabuf = gst_caps_from_string (
    "video/x-raw(memory:DMABuf),format=DMA_DRM");
  g_autoptr (GstCaps) native_yuv = gst_caps_from_string (
    "video/x-raw,format=NV12");

  g_assert_nonnull (source);
  g_assert_nonnull (sink);
  pad = gst_element_get_static_pad (sink, "sink");
  g_assert_nonnull (pad);
  available = gst_pad_query_caps (pad, NULL);
  gst_object_unref (pad);
  g_assert_true (gst_caps_can_intersect (available, dmabuf));
  g_assert_true (gst_caps_can_intersect (available, native_yuv));

  pipeline = gst_pipeline_new ("pinpoint-camera-caps-test");
  gst_object_ref_sink (pipeline);
  gst_bin_add_many (GST_BIN (pipeline), source, sink, NULL);
  g_assert_true (gst_element_link (source, sink));
  gst_object_unref (pipeline);
}

static PpPresentation *
load_fixture (const char *path)
{
  g_autoptr (GFile) file = g_file_new_for_path (path);
  g_autoptr (GError) error = NULL;
  PpPresentation *presentation = pp_presentation_load (file,
                                                       FALSE,
                                                       NULL,
                                                       &error);

  g_assert_no_error (error);
  g_assert_nonnull (presentation);
  return presentation;
}

static PpPresentation *
load_introduction (void)
{
  g_autoptr (GFile) file = pp_introduction_get_presentation ();
  g_autoptr (GError) error = NULL;
  PpPresentation *presentation = pp_presentation_load (file,
                                                       FALSE,
                                                       NULL,
                                                       &error);

  g_assert_no_error (error);
  g_assert_nonnull (presentation);
  return presentation;
}

static GtkWidget *
find_button (GtkWidget  *root,
             const char *label)
{
  if (GTK_IS_BUTTON (root) &&
      g_strcmp0 (gtk_button_get_label (GTK_BUTTON (root)), label) == 0)
    return root;

  for (GtkWidget *child = gtk_widget_get_first_child (root);
       child != NULL;
       child = gtk_widget_get_next_sibling (child))
    {
      GtkWidget *match = find_button (child, label);

      if (match != NULL)
        return match;
    }
  return NULL;
}

static GtkWindow *
find_speaker_window (GtkApplication *application)
{
  for (GList *windows = gtk_application_get_windows (application);
       windows != NULL;
       windows = windows->next)
    if (g_strcmp0 (gtk_window_get_title (windows->data),
                   "Pinpoint Speaker View") == 0)
      return windows->data;
  return NULL;
}

static void
test_speaker_keeps_stage_alive (void)
{
  g_autoptr (GtkApplication) application = create_application (
    "com.nedrichards.pinpoint.LifecycleSpeakerTest");
  GtkWindow *window = GTK_WINDOW (gtk_application_window_new (application));
  GtkWidget *stage = pp_stage_new ();
  PpSpeaker *speaker;

  gtk_window_set_child (window, stage);
  speaker = pp_speaker_new (application, PP_STAGE (stage));
  gtk_window_present (window);
  run_loop_for (100);
  gtk_window_destroy (window);
  run_loop_for (100);
  pp_speaker_free (speaker);
}

static void
test_speaker_control_and_repeated_lifecycle (void)
{
  g_autoptr (GtkApplication) application = create_application (
    "com.nedrichards.pinpoint.LifecycleSpeakerControlsTest");
  GtkWindow *window = GTK_WINDOW (gtk_application_window_new (application));
  GtkWidget *stage = pp_stage_new ();

  pp_stage_set_presentation (PP_STAGE (stage),
                             load_fixture (multi_monitor_fixture_path),
                             0);
  gtk_window_set_child (window, stage);
  gtk_window_present (window);
  run_loop_for (100);

  for (guint i = 0; i < 3; i++)
    {
      g_autoptr (PpSpeaker) speaker = pp_speaker_new (application,
                                                      PP_STAGE (stage));
      GtkWindow *speaker_window;
      GtkWidget *start;
      GtkWidget *pause;
      GtkWidget *autoadvance;
      GtkWidget *swap_displays;
      guint swap_requests = 0;

      pp_speaker_set_swap_displays_request_func (
        speaker,
        swap_displays_requested_cb,
        &swap_requests);
      pp_speaker_show (speaker);
      run_loop_for (100);
      g_assert_true (pp_speaker_is_visible (speaker));
      speaker_window = find_speaker_window (application);
      g_assert_nonnull (speaker_window);
      start = find_button (GTK_WIDGET (speaker_window), "Start");
      pause = find_button (GTK_WIDGET (speaker_window), "Pause");
      autoadvance = find_button (GTK_WIDGET (speaker_window), "Autoadvance");
      swap_displays = find_button (GTK_WIDGET (speaker_window), "Swap Displays");
      g_assert_nonnull (start);
      g_assert_nonnull (pause);
      g_assert_nonnull (autoadvance);
      g_assert_nonnull (swap_displays);
      g_signal_emit_by_name (start, "clicked");
      g_signal_emit_by_name (pause, "clicked");
      g_signal_emit_by_name (pause, "clicked");
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (autoadvance), TRUE);
      pp_speaker_set_swap_displays_available (speaker, TRUE);
      g_signal_emit_by_name (swap_displays, "clicked");
      g_assert_cmpuint (swap_requests, ==, 1);
      pp_speaker_set_fullscreen (speaker, TRUE, NULL);
      run_loop_for (75);
      pp_speaker_set_fullscreen (speaker, FALSE, NULL);
      run_loop_for (75);
      pp_speaker_toggle (speaker);
      run_loop_for (75);
      g_assert_false (pp_speaker_is_visible (speaker));
      pp_speaker_toggle (speaker);
      run_loop_for (75);
      g_assert_true (pp_speaker_is_visible (speaker));
      g_clear_pointer (&speaker, pp_speaker_free);
      run_loop_for (100);
    }

  gtk_window_destroy (window);
  run_loop_for (100);
}

static void
test_replace_presentation_during_transition (void)
{
  g_autoptr (GtkApplication) application = create_application (
    "com.nedrichards.pinpoint.LifecycleReplaceTransitionTest");
  GtkWindow *window = GTK_WINDOW (gtk_application_window_new (application));
  GtkWidget *stage = pp_stage_new ();

  pp_stage_set_presentation (PP_STAGE (stage),
                             load_fixture (multi_monitor_fixture_path),
                             0);
  gtk_window_set_child (window, stage);
  gtk_window_present (window);
  run_loop_for (100);
  g_assert_true (pp_stage_next (PP_STAGE (stage)));
  g_assert_true (pp_stage_is_transitioning (PP_STAGE (stage)));

  pp_stage_set_presentation (PP_STAGE (stage),
                             load_fixture (multi_monitor_fixture_path),
                             2);
  g_assert_false (pp_stage_is_transitioning (PP_STAGE (stage)));
  g_assert_cmpuint (pp_stage_get_current_slide (PP_STAGE (stage)), ==, 2);
  g_assert_true (pp_stage_previous (PP_STAGE (stage)));
  g_assert_true (pp_stage_is_transitioning (PP_STAGE (stage)));
  gtk_window_destroy (window);
  run_loop_for (100);
}

static void
test_replace_and_dispose_playing_media (void)
{
  g_autoptr (GtkApplication) application = create_application (
    "com.nedrichards.pinpoint.LifecycleReplaceMediaTest");
  GtkWindow *window = GTK_WINDOW (gtk_application_window_new (application));
  GtkWidget *stage = pp_stage_new ();

  pp_stage_set_audio_enabled (PP_STAGE (stage), FALSE);
  pp_stage_set_presentation (PP_STAGE (stage), load_introduction (), 7);
  gtk_window_set_child (window, stage);
  gtk_window_present (window);
  run_loop_for (500);
  pp_stage_set_media_enabled (PP_STAGE (stage), FALSE);
  pp_stage_set_media_enabled (PP_STAGE (stage), TRUE);
  pp_stage_set_audio_enabled (PP_STAGE (stage), TRUE);
  pp_stage_set_presentation (PP_STAGE (stage),
                             load_fixture (multi_monitor_fixture_path),
                             1);
  run_loop_for (100);
  pp_stage_set_presentation (PP_STAGE (stage), load_introduction (), 7);
  run_loop_for (250);
  gtk_window_destroy (window);
  run_loop_for (150);
}

static void
test_dispose_queued_camera_request (void)
{
  g_autoptr (GtkApplication) application = create_application (
    "com.nedrichards.pinpoint.LifecycleCameraQueueTest");
  GtkWindow *window = GTK_WINDOW (gtk_application_window_new (application));
  GtkWidget *stage = pp_stage_new ();
  g_autoptr (GdkPaintable) paintable = NULL;
  GtkSnapshot *snapshot;
  GskRenderNode *node;

  pp_stage_set_presentation (PP_STAGE (stage),
                             load_fixture (camera_fixture_path),
                             0);
  gtk_window_set_child (window, stage);
  gtk_widget_realize (GTK_WIDGET (window));
  gtk_widget_allocate (stage, 800, 600, -1, NULL);
  paintable = gtk_widget_paintable_new (stage);
  snapshot = gtk_snapshot_new ();
  gdk_paintable_snapshot (paintable, snapshot, 800, 600);
  node = gtk_snapshot_free_to_node (snapshot);
  g_clear_pointer (&node, gsk_render_node_unref);
  gtk_window_destroy (window);
  run_loop_for (150);
}

static void
exercise_media_stage (GtkApplication *application)
{
  GtkWindow *window = GTK_WINDOW (gtk_application_window_new (application));
  GtkWidget *stage = pp_stage_new ();

  pp_stage_set_audio_enabled (PP_STAGE (stage), FALSE);
  pp_stage_set_presentation (PP_STAGE (stage), load_introduction (), 7);
  gtk_window_set_child (window, stage);
  gtk_window_present (window);
  run_loop_for (350);
  gtk_window_destroy (window);
  run_loop_for (250);
}

static void
exercise_queued_camera_stage (GtkApplication *application)
{
  GtkWindow *window = GTK_WINDOW (gtk_application_window_new (application));
  GtkWidget *stage = pp_stage_new ();
  g_autoptr (GdkPaintable) paintable = NULL;
  GtkSnapshot *snapshot;
  GskRenderNode *node;

  pp_stage_set_presentation (PP_STAGE (stage),
                             load_fixture (camera_fixture_path),
                             0);
  gtk_window_set_child (window, stage);
  gtk_widget_realize (GTK_WIDGET (window));
  gtk_widget_allocate (stage, 800, 600, -1, NULL);
  paintable = gtk_widget_paintable_new (stage);
  snapshot = gtk_snapshot_new ();
  gdk_paintable_snapshot (paintable, snapshot, 800, 600);
  node = gtk_snapshot_free_to_node (snapshot);
  g_clear_pointer (&node, gsk_render_node_unref);
  gtk_window_destroy (window);
  run_loop_for (150);
}

static void
test_media_and_camera_file_descriptor_stability (void)
{
  g_autoptr (GtkApplication) application = create_application (
    "com.nedrichards.pinpoint.LifecycleFdTest");
  guint baseline;

  /* Warm process-lifetime GTK, GStreamer, portal, and D-Bus machinery first. */
  exercise_media_stage (application);
  exercise_queued_camera_stage (application);
  baseline = count_open_file_descriptors ();

  for (guint i = 0; i < 2; i++)
    {
      exercise_media_stage (application);
      exercise_queued_camera_stage (application);
    }
  assert_file_descriptor_ceiling (baseline,
                                  "Repeated media and camera teardown");
}

static void
test_missing_video_fails_once_and_tears_down (void)
{
  g_autoptr (GtkApplication) application = create_application (
    "com.nedrichards.pinpoint.LifecycleMediaTest");
  g_autoptr (GFile) file = g_file_new_for_path (fixture_path);
  g_autoptr (PpPresentation) presentation = NULL;
  g_autoptr (GError) error = NULL;
  GtkWindow *window = GTK_WINDOW (gtk_application_window_new (application));
  GtkWidget *stage = pp_stage_new ();

  presentation = pp_presentation_load (file, FALSE, NULL, &error);
  g_assert_no_error (error);
  g_assert_nonnull (presentation);
  pp_stage_set_presentation (PP_STAGE (stage),
                             g_steal_pointer (&presentation),
                             0);
  gtk_window_set_child (window, stage);

  gtk_window_present (window);
  run_loop_for (750);
  gtk_window_destroy (window);
  run_loop_for (100);
}

static void
test_bundled_introduction_video_lifecycle (void)
{
  g_autoptr (GtkApplication) application = create_application (
    "com.nedrichards.pinpoint.LifecycleIntroductionTest");
  g_autoptr (GFile) file = pp_introduction_get_presentation ();
  g_autoptr (PpPresentation) presentation = NULL;
  g_autoptr (GError) error = NULL;
  GtkWindow *window = GTK_WINDOW (gtk_application_window_new (application));
  GtkWidget *stage = pp_stage_new ();

  presentation = pp_presentation_load (file, FALSE, NULL, &error);
  g_assert_no_error (error);
  g_assert_nonnull (presentation);
  pp_stage_set_audio_enabled (PP_STAGE (stage), FALSE);
  pp_stage_set_presentation (PP_STAGE (stage),
                             g_steal_pointer (&presentation),
                             7);
  gtk_window_set_child (window, stage);

  gtk_window_present (window);
  run_loop_for (1250);
  gtk_window_destroy (window);
  run_loop_for (150);
}

static void
test_legacy_transition_lifecycle (void)
{
  g_autoptr (GtkApplication) application = create_application (
    "com.nedrichards.pinpoint.LifecycleTransitionTest");
  g_autoptr (GFile) file = g_file_new_for_path (transition_fixture_path);
  g_autoptr (PpPresentation) presentation = NULL;
  g_autoptr (GError) error = NULL;
  GtkWindow *window = GTK_WINDOW (gtk_application_window_new (application));
  GtkWidget *stage = pp_stage_new ();

  presentation = pp_presentation_load (file, FALSE, NULL, &error);
  g_assert_no_error (error);
  g_assert_nonnull (presentation);
  pp_stage_set_presentation (PP_STAGE (stage),
                             g_steal_pointer (&presentation),
                             0);
  gtk_window_set_child (window, stage);
  gtk_window_present (window);
  run_loop_for (100);
  g_assert_true (pp_stage_next (PP_STAGE (stage)));
  wait_for_transition_to_finish (PP_STAGE (stage), 5000);
  gtk_window_destroy (window);
  run_loop_for (100);
}

static void
test_page_curl_lifecycle (void)
{
  g_autoptr (GtkApplication) application = create_application (
    "com.nedrichards.pinpoint.LifecyclePageCurlTest");
  g_autoptr (GFile) file = g_file_new_for_path (page_curl_fixture_path);
  g_autoptr (PpPresentation) presentation = NULL;
  g_autoptr (GError) error = NULL;
  GtkWindow *window = GTK_WINDOW (gtk_application_window_new (application));
  GtkWidget *stage = pp_stage_new ();

  presentation = pp_presentation_load (file, FALSE, NULL, &error);
  g_assert_no_error (error);
  g_assert_nonnull (presentation);
  pp_stage_set_presentation (PP_STAGE (stage),
                             g_steal_pointer (&presentation),
                             0);
  gtk_window_set_child (window, stage);
  gtk_window_present (window);
  run_loop_for (100);
  g_assert_true (pp_stage_next (PP_STAGE (stage)));
  run_loop_for (100);
  assert_page_curl_gl_ready (stage);
  wait_for_transition_to_finish (PP_STAGE (stage), 6000);
  gtk_window_destroy (window);
  run_loop_for (100);
}

static void
test_native_transition_lifecycle (void)
{
  g_autoptr (GtkApplication) application = create_application (
    "com.nedrichards.pinpoint.LifecycleNativeTransitionTest");
  g_autoptr (GFile) file = g_file_new_for_path (
    native_transition_fixture_path);
  g_autoptr (PpPresentation) presentation = NULL;
  g_autoptr (GError) error = NULL;
  GtkWindow *window = GTK_WINDOW (gtk_application_window_new (application));
  GtkWidget *stage = pp_stage_new ();

  presentation = pp_presentation_load (file, FALSE, NULL, &error);
  g_assert_no_error (error);
  g_assert_nonnull (presentation);
  pp_stage_set_presentation (PP_STAGE (stage),
                             g_steal_pointer (&presentation),
                             0);
  gtk_window_set_child (window, stage);
  gtk_window_present (window);
  run_loop_for (100);

  for (guint i = 0; i < 4; i++)
    {
      g_assert_true (pp_stage_next (PP_STAGE (stage)));
      wait_for_transition_to_finish (PP_STAGE (stage), 1000);
    }
  g_assert_true (pp_stage_previous (PP_STAGE (stage)));
  wait_for_transition_to_finish (PP_STAGE (stage), 1000);

  gtk_window_destroy (window);
  run_loop_for (100);
}

int
main (int   argc,
      char *argv[])
{
  if (argc != 7)
    return EXIT_FAILURE;
  fixture_path = argv[1];
  transition_fixture_path = argv[2];
  native_transition_fixture_path = argv[3];
  page_curl_fixture_path = argv[4];
  multi_monitor_fixture_path = argv[5];
  camera_fixture_path = argv[6];
  g_log_set_writer_func (test_log_writer, NULL, NULL);
  gst_init (NULL, NULL);
  if (!gtk_init_check ())
    return 77;

  test_wayland_media_sink_caps ();
  test_speaker_keeps_stage_alive ();
  test_speaker_control_and_repeated_lifecycle ();
  test_replace_presentation_during_transition ();
  test_replace_and_dispose_playing_media ();
  test_dispose_queued_camera_request ();
  test_media_and_camera_file_descriptor_stability ();
  test_missing_video_fails_once_and_tears_down ();
  test_bundled_introduction_video_lifecycle ();
  test_legacy_transition_lifecycle ();
  test_page_curl_gl_render ();
  test_page_curl_lifecycle ();
  test_native_transition_lifecycle ();
  if (media_warning_count != 1)
    {
      g_printerr ("Expected one missing-video warning, received %u\n",
                  media_warning_count);
      return EXIT_FAILURE;
    }
  return EXIT_SUCCESS;
}
