#include "pp-introduction.h"
#include "pp-page-curl-view.h"
#include "pp-presentation.h"
#include "pp-speaker.h"
#include "pp-stage.h"
#include "pp-transition.h"

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
static const char *media_formats_fixture_path;
static const char *corrupt_video_fixture_path;

static void
control_command_cb (PpControl *control,
                    guint      command,
                    gboolean   requested_state,
                    gpointer   user_data)
{
  guint *requests = user_data;

  (void) control;
  (void) requested_state;
  if (command == PP_CONTROL_COMMAND_SWAP_DISPLAYS)
    (*requests)++;
}
static guint media_warning_count;
static guint reduced_motion_message_count;

static GtkApplication *create_application (const char *application_id);

static GLogWriterOutput
test_log_writer (GLogLevelFlags   log_level,
                 const GLogField *fields,
                 gsize            n_fields,
                 gpointer         user_data)
{
  for (gsize i = 0; i < n_fields; i++)
    if (g_str_equal (fields[i].key, "MESSAGE") && fields[i].value != NULL)
      {
        if (g_str_has_prefix (fields[i].value,
                              "Unable to play video background:"))
          media_warning_count++;
        else if (g_str_has_prefix (
                   fields[i].value,
                   "Reduced motion is enabled; slide transitions"))
          reduced_motion_message_count++;
      }

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

typedef struct
{
  gboolean recording;
  gint64 previous_frame_time;
  guint transitions;
  guint frames;
  guint intervals;
  double total_interval_ms;
  double longest_interval_ms;
  double current_longest_interval_ms;
  guint over_20_ms;
  guint over_25_ms;
  guint over_34_ms;
} ProfileFrameMetrics;

typedef struct
{
  GMainLoop *loop;
  PpStage *stage;
  guint tick_id;
  gboolean timed_out;
} ProfileTransitionWait;

static gboolean
profile_transition_tick_cb (GtkWidget     *widget,
                            GdkFrameClock *frame_clock,
                            gpointer       user_data)
{
  ProfileTransitionWait *wait = user_data;

  (void) widget;
  (void) frame_clock;
  if (pp_stage_is_transitioning (wait->stage))
    return G_SOURCE_CONTINUE;

  wait->tick_id = 0;
  g_main_loop_quit (wait->loop);
  return G_SOURCE_REMOVE;
}

static gboolean
profile_transition_timeout_cb (gpointer user_data)
{
  ProfileTransitionWait *wait = user_data;

  wait->timed_out = TRUE;
  g_main_loop_quit (wait->loop);
  return G_SOURCE_REMOVE;
}

static void
wait_for_profile_transition (PpStage *stage,
                             guint    timeout_ms)
{
  g_autoptr (GMainLoop) loop = g_main_loop_new (NULL, FALSE);
  ProfileTransitionWait wait = {
    .loop = loop,
    .stage = stage,
  };
  guint timeout_id;

  if (!pp_stage_is_transitioning (stage))
    return;

  wait.tick_id = gtk_widget_add_tick_callback (
    GTK_WIDGET (stage),
    profile_transition_tick_cb,
    &wait,
    NULL);
  timeout_id = g_timeout_add (timeout_ms,
                              profile_transition_timeout_cb,
                              &wait);
  g_main_loop_run (loop);
  if (!wait.timed_out)
    g_source_remove (timeout_id);
  if (wait.tick_id != 0)
    gtk_widget_remove_tick_callback (GTK_WIDGET (stage), wait.tick_id);
  g_assert_false (pp_stage_is_transitioning (stage));
}

static void
profile_after_paint_cb (GdkFrameClock *frame_clock,
                        gpointer       user_data)
{
  ProfileFrameMetrics *metrics = user_data;
  gint64 frame_time;

  if (!metrics->recording)
    return;

  frame_time = gdk_frame_clock_get_frame_time (frame_clock);
  metrics->frames++;
  if (metrics->previous_frame_time != 0)
    {
      double interval_ms =
        (frame_time - metrics->previous_frame_time) / 1000.0;

      metrics->intervals++;
      metrics->total_interval_ms += interval_ms;
      metrics->longest_interval_ms = MAX (metrics->longest_interval_ms,
                                          interval_ms);
      metrics->current_longest_interval_ms = MAX (
        metrics->current_longest_interval_ms,
        interval_ms);
      if (interval_ms > 20.0)
        metrics->over_20_ms++;
      if (interval_ms > 25.0)
        metrics->over_25_ms++;
      if (interval_ms > 34.0)
        metrics->over_34_ms++;
    }
  metrics->previous_frame_time = frame_time;
}

static void
profile_transition (PpStage             *stage,
                    gboolean             forwards,
                    ProfileFrameMetrics *metrics)
{
  const PpPresentation *presentation = pp_stage_get_presentation (stage);
  guint from = pp_stage_get_current_slide (stage);
  guint to = forwards ? from + 1 : from - 1;
  const PpSlide *old_slide = pp_presentation_get_slide (presentation, from);
  const PpSlide *new_slide = pp_presentation_get_slide (presentation, to);
  guint frames_before = metrics->frames;
  guint intervals_before = metrics->intervals;
  double interval_ms_before = metrics->total_interval_ms;
  gint64 started = g_get_monotonic_time ();
  guint frames;
  guint intervals;
  double total_interval_ms;

  metrics->recording = TRUE;
  metrics->previous_frame_time = 0;
  metrics->current_longest_interval_ms = 0.0;
  metrics->transitions++;
  if (forwards)
    g_assert_true (pp_stage_next (stage));
  else
    g_assert_true (pp_stage_previous (stage));
  wait_for_profile_transition (stage, 6000);
  metrics->recording = FALSE;

  frames = metrics->frames - frames_before;
  intervals = metrics->intervals - intervals_before;
  total_interval_ms = metrics->total_interval_ms - interval_ms_before;
  g_print ("Transition %u->%u: old=%s new=%s, %.1f ms elapsed, "
           "%u frames, %.3f ms average interval, %.3f ms longest\n",
           from + 1,
           to + 1,
           old_slide->transition,
           new_slide->transition,
           (g_get_monotonic_time () - started) / 1000.0,
           frames,
           intervals > 0 ? total_interval_ms / intervals : 0.0,
           metrics->current_longest_interval_ms);
}

static void
print_profile_frame_metrics (const ProfileFrameMetrics *metrics)
{
  double average_ms = metrics->intervals > 0
    ? metrics->total_interval_ms / metrics->intervals
    : 0.0;

  g_print ("Transition frame clock: %u transitions, %u frames, "
           "%u intervals, %.3f ms average, %.3f ms longest, "
           "%u >20 ms, %u >25 ms, %u >34 ms\n",
           metrics->transitions,
           metrics->frames,
           metrics->intervals,
           average_ms,
           metrics->longest_interval_ms,
           metrics->over_20_ms,
           metrics->over_25_ms,
           metrics->over_34_ms);
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
test_stage_accessibility (void)
{
  GtkWidget *stage = pp_stage_new ();
  g_autoptr (PpPresentation) invalid_markup = NULL;
  g_autoptr (PpPresentation) empty_slide = NULL;
  g_autoptr (GError) error = NULL;

  g_object_ref_sink (stage);

  gtk_test_accessible_assert_role (GTK_ACCESSIBLE (stage),
                                   GTK_ACCESSIBLE_ROLE_GROUP);
  gtk_test_accessible_assert_property (GTK_ACCESSIBLE (stage),
                                       GTK_ACCESSIBLE_PROPERTY_LABEL,
                                       "Presentation slide");
  gtk_test_accessible_assert_property (GTK_ACCESSIBLE (stage),
                                       GTK_ACCESSIBLE_PROPERTY_DESCRIPTION,
                                       "No presentation loaded");

  pp_stage_set_presentation (PP_STAGE (stage),
                             load_fixture (multi_monitor_fixture_path),
                             0);
  gtk_test_accessible_assert_property (GTK_ACCESSIBLE (stage),
                                       GTK_ACCESSIBLE_PROPERTY_LABEL,
                                       "Presentation slide 1 of 3");
  gtk_test_accessible_assert_property (GTK_ACCESSIBLE (stage),
                                       GTK_ACCESSIBLE_PROPERTY_DESCRIPTION,
                                       "AUDIENCE SLIDE 1");

  g_assert_true (pp_stage_next (PP_STAGE (stage)));
  gtk_test_accessible_assert_property (GTK_ACCESSIBLE (stage),
                                       GTK_ACCESSIBLE_PROPERTY_LABEL,
                                       "Presentation slide 2 of 3");
  gtk_test_accessible_assert_property (GTK_ACCESSIBLE (stage),
                                       GTK_ACCESSIBLE_PROPERTY_DESCRIPTION,
                                       "AUDIENCE SLIDE 2");

  pp_stage_set_blank (PP_STAGE (stage), TRUE);
  gtk_test_accessible_assert_property (GTK_ACCESSIBLE (stage),
                                       GTK_ACCESSIBLE_PROPERTY_DESCRIPTION,
                                       "Blank screen");
  pp_stage_set_accessible_context (PP_STAGE (stage), "Current slide");
  gtk_test_accessible_assert_property (GTK_ACCESSIBLE (stage),
                                       GTK_ACCESSIBLE_PROPERTY_LABEL,
                                       "Current slide 2 of 3");
  pp_stage_set_blank (PP_STAGE (stage), FALSE);

  invalid_markup = pp_presentation_parse ("--\n<b>broken\n",
                                          NULL,
                                          FALSE,
                                          &error);
  g_assert_no_error (error);
  g_assert_nonnull (invalid_markup);
  pp_stage_set_presentation (PP_STAGE (stage),
                             g_steal_pointer (&invalid_markup),
                             0);
  gtk_test_accessible_assert_property (GTK_ACCESSIBLE (stage),
                                       GTK_ACCESSIBLE_PROPERTY_DESCRIPTION,
                                       "<b>broken");

  empty_slide = pp_presentation_parse ("--\n", NULL, FALSE, &error);
  g_assert_no_error (error);
  g_assert_nonnull (empty_slide);
  pp_stage_set_presentation (PP_STAGE (stage),
                             g_steal_pointer (&empty_slide),
                             0);
  gtk_test_accessible_assert_property (GTK_ACCESSIBLE (stage),
                                       GTK_ACCESSIBLE_PROPERTY_DESCRIPTION,
                                       "Slide has no audience text");

  g_object_unref (stage);
}

static void
test_reduced_motion_disables_transitions (void)
{
  GtkSettings *settings = gtk_settings_get_default ();
  GtkWidget *stage = pp_stage_new ();
  GtkReducedMotion reduced_motion;
  gboolean animations_enabled;
  guint messages_before = reduced_motion_message_count;

  g_object_ref_sink (stage);
  g_assert_nonnull (settings);
  g_object_get (settings,
                "gtk-enable-animations",
                &animations_enabled,
                "gtk-interface-reduced-motion",
                &reduced_motion,
                NULL);
  g_object_set (settings,
                "gtk-enable-animations", TRUE,
                "gtk-interface-reduced-motion", GTK_REDUCED_MOTION_REDUCE,
                NULL);
  pp_stage_set_presentation (PP_STAGE (stage),
                             load_fixture (native_transition_fixture_path),
                             0);
  g_assert_true (pp_stage_next (PP_STAGE (stage)));
  g_assert_false (pp_stage_is_transitioning (PP_STAGE (stage)));
  g_assert_cmpuint (reduced_motion_message_count, ==, messages_before + 1);
  g_assert_true (pp_stage_previous (PP_STAGE (stage)));
  g_assert_false (pp_stage_is_transitioning (PP_STAGE (stage)));
  g_assert_cmpuint (reduced_motion_message_count, ==, messages_before + 1);
  g_object_set (settings,
                "gtk-enable-animations", FALSE,
                "gtk-interface-reduced-motion",
                GTK_REDUCED_MOTION_NO_PREFERENCE,
                NULL);
  g_assert_true (pp_stage_next (PP_STAGE (stage)));
  g_assert_false (pp_stage_is_transitioning (PP_STAGE (stage)));
  g_assert_cmpuint (reduced_motion_message_count, ==, messages_before + 1);
  g_object_set (settings,
                "gtk-enable-animations",
                animations_enabled,
                "gtk-interface-reduced-motion",
                reduced_motion,
                NULL);
  g_object_unref (stage);
}

static void
test_speaker_keeps_stage_alive (void)
{
  g_autoptr (GtkApplication) application = create_application (
    "com.nedrichards.pinpoint.LifecycleSpeakerTest");
  GtkWindow *window = GTK_WINDOW (gtk_application_window_new (application));
  GtkWidget *stage = pp_stage_new ();
  g_autoptr (PpControl) control = pp_control_new (G_ACTION_MAP (application),
                                                  G_ACTION_GROUP (application));
  PpSpeaker *speaker;

  gtk_window_set_child (window, stage);
  speaker = pp_speaker_new (application, PP_STAGE (stage), control);
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
  g_autoptr (PpControl) control = pp_control_new (G_ACTION_MAP (application),
                                                  G_ACTION_GROUP (application));
  guint swap_requests = 0;

  pp_stage_set_presentation (PP_STAGE (stage),
                             load_fixture (multi_monitor_fixture_path),
                             0);
  gtk_window_set_child (window, stage);
  gtk_window_present (window);
  run_loop_for (100);
  pp_control_set_presenting (control, TRUE);
  pp_control_set_slide (control,
                        0,
                        pp_presentation_get_n_slides (
                          pp_stage_get_presentation (PP_STAGE (stage))));
  pp_control_set_swap_displays_available (control, TRUE);
  g_signal_connect (control,
                    "command",
                    G_CALLBACK (control_command_cb),
                    &swap_requests);

  for (guint i = 0; i < 3; i++)
    {
      g_autoptr (PpSpeaker) speaker = pp_speaker_new (application,
                                                      PP_STAGE (stage),
                                                      control);
      GtkWindow *speaker_window;
      GtkWidget *start;
      GtkWidget *pause;
      GtkWidget *autoadvance;
      GtkWidget *swap_displays;
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
      g_assert_null (g_main_context_find_source_by_user_data (NULL, speaker));
      g_signal_emit_by_name (start, "clicked");
      g_assert_nonnull (g_main_context_find_source_by_user_data (NULL, speaker));
      g_signal_emit_by_name (pause, "clicked");
      g_assert_null (g_main_context_find_source_by_user_data (NULL, speaker));
      g_signal_emit_by_name (pause, "clicked");
      g_assert_nonnull (g_main_context_find_source_by_user_data (NULL, speaker));
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (autoadvance), TRUE);
      g_signal_emit_by_name (swap_displays, "clicked");
      g_assert_cmpuint (swap_requests, ==, i + 1);
      pp_speaker_set_fullscreen (speaker, TRUE, NULL);
      run_loop_for (75);
      pp_speaker_set_fullscreen (speaker, FALSE, NULL);
      run_loop_for (75);
      pp_speaker_set_visible (speaker, FALSE);
      run_loop_for (75);
      g_assert_false (pp_speaker_is_visible (speaker));
      pp_speaker_set_visible (speaker, TRUE);
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
exercise_failed_video (const char *path,
                       const char *application_id)
{
  g_autoptr (GtkApplication) application = create_application (application_id);
  g_autoptr (GFile) file = g_file_new_for_path (path);
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
test_missing_video_fails_once_and_tears_down (void)
{
  exercise_failed_video (fixture_path,
                         "com.nedrichards.pinpoint.LifecycleMissingMediaTest");
}

static void
test_corrupt_video_fails_once_and_tears_down (void)
{
  exercise_failed_video (corrupt_video_fixture_path,
                         "com.nedrichards.pinpoint.LifecycleCorruptMediaTest");
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
  g_assert_true (pp_stage_is_media_offload_configured (PP_STAGE (stage)));
  pp_stage_set_blank (PP_STAGE (stage), TRUE);
  g_assert_false (pp_stage_is_media_offload_configured (PP_STAGE (stage)));
  pp_stage_set_blank (PP_STAGE (stage), FALSE);
  run_loop_for (100);
  g_assert_true (pp_stage_is_media_offload_configured (PP_STAGE (stage)));
  gtk_window_destroy (window);
  run_loop_for (150);
}

static void
test_supported_media_matrix (void)
{
  g_autoptr (GtkApplication) application = create_application (
    "com.nedrichards.pinpoint.LifecycleMediaFormatsTest");
  g_autoptr (PpPresentation) presentation = load_fixture (
    media_formats_fixture_path);
  GtkWindow *window = GTK_WINDOW (gtk_application_window_new (application));
  GtkWidget *stage = pp_stage_new ();
  guint warnings_before = media_warning_count;
  guint slide_count = pp_presentation_get_n_slides (presentation);

  pp_stage_set_audio_enabled (PP_STAGE (stage), FALSE);
  pp_stage_set_presentation (PP_STAGE (stage),
                             g_steal_pointer (&presentation),
                             0);
  gtk_window_set_child (window, stage);
  gtk_window_present (window);
  for (guint i = 0; i < slide_count; i++)
    {
      run_loop_for (350);
      if (i + 1 < slide_count)
        g_assert_true (pp_stage_next (PP_STAGE (stage)));
    }
  g_assert_cmpuint (media_warning_count, ==, warnings_before);
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
  {
    const PpPresentation *loaded = pp_stage_get_presentation (PP_STAGE (stage));
    const PpSlide *slide = pp_presentation_get_slide (loaded, 0);
    g_autoptr (GFile) transition_file =
      pp_legacy_transition_resolve_file (loaded, slide->transition);

    pp_stage_invalidate_asset (PP_STAGE (stage), transition_file);
  }
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

static int
profile_presentation (const char *path,
                      guint       cycles,
                      guint       dwell_ms,
                      gboolean    require_page_curl,
                      guint       first_slide_number,
                      guint       last_slide_number)
{
  g_autoptr (GtkApplication) application = create_application (
    "com.nedrichards.pinpoint.PageCurlProfile");
  g_autoptr (GFile) file = g_file_new_for_path (path);
  g_autoptr (PpPresentation) presentation = NULL;
  g_autoptr (GError) error = NULL;
  GtkWindow *window = GTK_WINDOW (gtk_application_window_new (application));
  GtkWidget *stage = pp_stage_new ();
  GtkSettings *settings;
  GtkReducedMotion reduced_motion = GTK_REDUCED_MOTION_NO_PREFERENCE;
  gboolean animations_enabled = TRUE;
  guint slide_count;
  guint first_slide;
  guint last_slide;
  ProfileFrameMetrics frame_metrics = { 0 };
  GdkFrameClock *frame_clock;
  gulong after_paint_id;

  presentation = pp_presentation_load (file, FALSE, NULL, &error);
  if (presentation == NULL)
    {
      g_printerr ("Unable to load profile presentation: %s\n",
                  error->message);
      return EXIT_FAILURE;
    }
  slide_count = pp_presentation_get_n_slides (presentation);
  if (slide_count < 2)
    {
      g_printerr ("The profile presentation needs at least two slides\n");
      return EXIT_FAILURE;
    }
  if (first_slide_number == 0)
    first_slide_number = 1;
  if (last_slide_number == 0)
    last_slide_number = slide_count;
  if (first_slide_number >= last_slide_number ||
      last_slide_number > slide_count)
    {
      g_printerr ("Profile slide range %u-%u is outside the %u-slide deck\n",
                  first_slide_number,
                  last_slide_number,
                  slide_count);
      return EXIT_FAILURE;
    }
  first_slide = first_slide_number - 1;
  last_slide = last_slide_number - 1;

  pp_stage_set_presentation (PP_STAGE (stage),
                             g_steal_pointer (&presentation),
                             first_slide);
  gtk_window_set_child (window, stage);
  gtk_window_maximize (window);
  gtk_window_present (window);
  run_loop_for (500);

  settings = gtk_widget_get_settings (stage);
  g_object_get (settings,
                "gtk-enable-animations", &animations_enabled,
                "gtk-interface-reduced-motion", &reduced_motion,
                NULL);
  if (!animations_enabled || reduced_motion == GTK_REDUCED_MOTION_REDUCE)
    {
      g_printerr ("Presentation profiling requires desktop animations and "
                  "no reduced-motion preference\n");
      gtk_window_destroy (window);
      run_loop_for (100);
      return 77;
    }

  frame_clock = gtk_widget_get_frame_clock (stage);
  g_assert_nonnull (frame_clock);
  after_paint_id = g_signal_connect (frame_clock,
                                     "after-paint",
                                     G_CALLBACK (profile_after_paint_cb),
                                     &frame_metrics);

  g_print ("Presentation profile: %dx%d logical pixels at %dx scale, "
           "%u slides, range %u-%u, %u forward/backward cycles, "
           "%u ms dwell\n",
           gtk_widget_get_width (stage),
           gtk_widget_get_height (stage),
           gtk_widget_get_scale_factor (stage),
           slide_count,
           first_slide_number,
           last_slide_number,
           cycles,
           dwell_ms);

  /* Give an attaching profiler a stable idle baseline before the first move. */
  run_loop_for (2000);
  for (guint cycle = 0; cycle < cycles; cycle++)
    {
      for (guint slide = first_slide + 1; slide <= last_slide; slide++)
        {
          profile_transition (PP_STAGE (stage), TRUE, &frame_metrics);
          if (dwell_ms > 0)
            run_loop_for (dwell_ms);
        }
      for (guint slide = last_slide; slide > first_slide; slide--)
        {
          profile_transition (PP_STAGE (stage), FALSE, &frame_metrics);
          if (dwell_ms > 0)
            run_loop_for (dwell_ms);
        }
    }
  if (require_page_curl)
    assert_page_curl_gl_ready (stage);

  /* Make post-transition idle and retained texture residency visible. */
  run_loop_for (2000);
  print_profile_frame_metrics (&frame_metrics);
  g_signal_handler_disconnect (frame_clock, after_paint_id);
  gtk_window_destroy (window);
  run_loop_for (100);
  return EXIT_SUCCESS;
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
  if (argc == 3 && g_str_equal (argv[1], "--profile-page-curl"))
    {
      g_log_set_writer_func (test_log_writer, NULL, NULL);
      gst_init (NULL, NULL);
      if (!gtk_init_check ())
        return 77;
      return profile_presentation (argv[2], 2, 0, TRUE, 1, 0);
    }
  if (argc >= 3 && argc <= 7 &&
      g_str_equal (argv[1], "--profile-presentation"))
    {
      guint64 cycles = argc >= 4 ? g_ascii_strtoull (argv[3], NULL, 10) : 1;
      guint64 dwell_ms = argc >= 5 ? g_ascii_strtoull (argv[4], NULL, 10) : 250;
      guint64 first_slide = argc >= 6 ? g_ascii_strtoull (argv[5], NULL, 10) : 1;
      guint64 last_slide = argc >= 7 ? g_ascii_strtoull (argv[6], NULL, 10) : 0;

      if (cycles == 0 || cycles > G_MAXUINT || dwell_ms > G_MAXUINT ||
          first_slide == 0 || first_slide > G_MAXUINT ||
          last_slide > G_MAXUINT)
        return EXIT_FAILURE;
      g_log_set_writer_func (test_log_writer, NULL, NULL);
      gst_init (NULL, NULL);
      if (!gtk_init_check ())
        return 77;
      return profile_presentation (argv[2],
                                   cycles,
                                   dwell_ms,
                                   FALSE,
                                   first_slide,
                                   last_slide);
    }
  if (argc != 9)
    return EXIT_FAILURE;
  fixture_path = argv[1];
  transition_fixture_path = argv[2];
  native_transition_fixture_path = argv[3];
  page_curl_fixture_path = argv[4];
  multi_monitor_fixture_path = argv[5];
  camera_fixture_path = argv[6];
  media_formats_fixture_path = argv[7];
  corrupt_video_fixture_path = argv[8];
  g_log_set_writer_func (test_log_writer, NULL, NULL);
  gst_init (NULL, NULL);
  if (!gtk_init_check ())
    return 77;

  test_wayland_media_sink_caps ();
  test_stage_accessibility ();
  test_reduced_motion_disables_transitions ();
  test_speaker_keeps_stage_alive ();
  test_speaker_control_and_repeated_lifecycle ();
  test_replace_presentation_during_transition ();
  test_replace_and_dispose_playing_media ();
  test_dispose_queued_camera_request ();
  test_media_and_camera_file_descriptor_stability ();
  test_missing_video_fails_once_and_tears_down ();
  test_corrupt_video_fails_once_and_tears_down ();
  test_bundled_introduction_video_lifecycle ();
  test_supported_media_matrix ();
  test_legacy_transition_lifecycle ();
  test_page_curl_gl_render ();
  test_page_curl_lifecycle ();
  test_native_transition_lifecycle ();
  if (media_warning_count != 2)
    {
      g_printerr ("Expected two failed-video warnings, received %u\n",
                  media_warning_count);
      return EXIT_FAILURE;
    }
  return EXIT_SUCCESS;
}
