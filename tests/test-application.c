#include <gio/gio.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *pinpoint_path;
static const char *presentation_path;
static const char *media_presentation_path;

#define MPRIS_OBJECT_PATH "/org/mpris/MediaPlayer2"
#define MPRIS_PLAYER_INTERFACE "org.mpris.MediaPlayer2.Player"

static void
run_loop_for (guint milliseconds)
{
  gint64 deadline = g_get_monotonic_time () + (gint64) milliseconds * 1000;

  while (g_get_monotonic_time () < deadline)
    {
      while (g_main_context_iteration (NULL, FALSE));
      g_usleep (1000);
    }
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

  g_assert_cmpuint (count, >, 0);
  return count - 1;
}

static void
assert_file_descriptor_ceiling (guint       baseline,
                                const char *context)
{
  guint actual;

  run_loop_for (50);
  actual = count_open_file_descriptors ();
  if (actual > baseline)
    g_error ("%s leaked file descriptors (baseline %u, now %u)",
             context,
             baseline,
             actual);
}

static char *
finish_process (GSubprocess *process,
                int          expected_status)
{
  g_autofree char *stdout_text = NULL;
  g_autofree char *stderr_text = NULL;
  g_autoptr (GError) error = NULL;

  g_assert_true (g_subprocess_communicate_utf8 (process,
                                                NULL,
                                                NULL,
                                                &stdout_text,
                                                &stderr_text,
                                                &error));
  g_assert_no_error (error);
  if (g_subprocess_get_if_signaled (process))
    g_error ("Application terminated with signal %d: %s",
             g_subprocess_get_term_sig (process),
             stderr_text);
  g_assert_cmpint (g_subprocess_get_exit_status (process), ==, expected_status);
  g_assert_null (strstr (stderr_text, "CRITICAL"));
  g_assert_null (strstr (stderr_text, "AddressSanitizer"));
  return g_steal_pointer (&stderr_text);
}

static GSubprocess *
launch_application_with_renderer (const char *const *arguments,
                                  const char        *renderer)
{
  g_autoptr (GSubprocessLauncher) launcher = g_subprocess_launcher_new (
    G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE);
  g_autoptr (GError) error = NULL;
  GSubprocess *process;

  g_subprocess_launcher_setenv (launcher, "G_DEBUG", "fatal-criticals", TRUE);
  if (renderer != NULL)
    g_subprocess_launcher_setenv (launcher, "GSK_RENDERER", renderer, TRUE);
  process = g_subprocess_launcher_spawnv (launcher, arguments, &error);
  g_assert_no_error (error);
  g_assert_nonnull (process);
  return process;
}

static GSubprocess *
launch_application (const char *const *arguments)
{
  return launch_application_with_renderer (arguments, NULL);
}

static gboolean
process_is_running (GSubprocess *process)
{
  const char *identifier = g_subprocess_get_identifier (process);
  gint64 pid;

  g_assert_nonnull (identifier);
  pid = g_ascii_strtoll (identifier, NULL, 10);
  g_assert_cmpint (pid, >, 0);
  return kill ((pid_t) pid, 0) == 0 || errno == EPERM;
}

static void
terminate_running_application (GSubprocess *process)
{
  run_loop_for (750);
  g_assert_true (process_is_running (process));
  g_subprocess_send_signal (process, SIGTERM);
}

static GHashTable *
list_bus_names (GDBusConnection *connection)
{
  g_autoptr (GVariant) reply = NULL;
  g_auto (GStrv) names = NULL;
  g_autoptr (GError) error = NULL;
  GHashTable *result = g_hash_table_new_full (g_str_hash,
                                               g_str_equal,
                                               g_free,
                                               NULL);

  reply = g_dbus_connection_call_sync (connection,
                                       "org.freedesktop.DBus",
                                       "/org/freedesktop/DBus",
                                       "org.freedesktop.DBus",
                                       "ListNames",
                                       NULL,
                                       G_VARIANT_TYPE ("(as)"),
                                       G_DBUS_CALL_FLAGS_NONE,
                                       -1,
                                       NULL,
                                       &error);
  g_assert_no_error (error);
  g_assert_nonnull (reply);
  g_variant_get (reply, "(^as)", &names);
  for (guint i = 0; names[i] != NULL; i++)
    g_hash_table_add (result, g_strdup (names[i]));
  return result;
}

static char *
wait_for_new_mpris_name (GDBusConnection *connection,
                         GHashTable      *ignored)
{
  const char *application_id = g_getenv ("FLATPAK_ID");
  g_autofree char *prefix = NULL;

  if (application_id == NULL ||
      !g_application_id_is_valid (application_id))
    application_id = "com.nedrichards.pinpoint";
  prefix = g_strdup_printf ("org.mpris.MediaPlayer2.%s.instance_",
                            application_id);
  for (guint attempt = 0; attempt < 100; attempt++)
    {
      g_autoptr (GHashTable) names = list_bus_names (connection);
      GHashTableIter iter;
      gpointer key;

      g_hash_table_iter_init (&iter, names);
      while (g_hash_table_iter_next (&iter, &key, NULL))
        if (g_str_has_prefix (key, prefix) &&
            !g_hash_table_contains (ignored, key))
          return g_strdup (key);
      run_loop_for (50);
    }
  return NULL;
}

static GVariant *
get_mpris_property (GDBusConnection *connection,
                    const char      *bus_name,
                    const char      *interface_name,
                    const char      *property_name)
{
  g_autoptr (GVariant) reply = NULL;
  g_autoptr (GVariant) boxed = NULL;
  g_autoptr (GError) error = NULL;

  reply = g_dbus_connection_call_sync (connection,
                                       bus_name,
                                       MPRIS_OBJECT_PATH,
                                       "org.freedesktop.DBus.Properties",
                                       "Get",
                                       g_variant_new ("(ss)",
                                                      interface_name,
                                                      property_name),
                                       G_VARIANT_TYPE ("(v)"),
                                       G_DBUS_CALL_FLAGS_NONE,
                                       -1,
                                       NULL,
                                       &error);
  g_assert_no_error (error);
  g_assert_nonnull (reply);
  g_variant_get (reply, "(@v)", &boxed);
  return g_variant_get_variant (boxed);
}

static char *
get_mpris_title (GDBusConnection *connection,
                 const char      *bus_name)
{
  g_autoptr (GVariant) metadata = get_mpris_property (
    connection,
    bus_name,
    MPRIS_PLAYER_INTERFACE,
    "Metadata");
  const char *title = NULL;

  if (!g_variant_lookup (metadata, "xesam:title", "&s", &title))
    return NULL;
  return g_strdup (title);
}

static void
wait_for_mpris_title (GDBusConnection *connection,
                      const char      *bus_name,
                      const char      *expected)
{
  for (guint attempt = 0; attempt < 100; attempt++)
    {
      g_autofree char *title = get_mpris_title (connection, bus_name);

      if (g_strcmp0 (title, expected) == 0)
        return;
      run_loop_for (50);
    }
  g_error ("MPRIS player %s did not reach metadata title %s",
           bus_name,
           expected);
}

static void
call_mpris_method (GDBusConnection *connection,
                   const char      *bus_name,
                   const char      *method_name)
{
  g_autoptr (GVariant) reply = NULL;
  g_autoptr (GError) error = NULL;

  reply = g_dbus_connection_call_sync (connection,
                                       bus_name,
                                       MPRIS_OBJECT_PATH,
                                       MPRIS_PLAYER_INTERFACE,
                                       method_name,
                                       NULL,
                                       NULL,
                                       G_DBUS_CALL_FLAGS_NONE,
                                       -1,
                                       NULL,
                                       &error);
  g_assert_no_error (error);
  g_assert_nonnull (reply);
}

static void
test_mpris_multi_instance_control (void)
{
  const char *arguments[] = { pinpoint_path, presentation_path, NULL };
  g_autoptr (GDBusConnection) connection = g_bus_get_sync (
    G_BUS_TYPE_SESSION,
    NULL,
    NULL);
  g_autoptr (GHashTable) ignored = list_bus_names (connection);
  g_autoptr (GSubprocess) first = launch_application (arguments);
  g_autofree char *first_name = wait_for_new_mpris_name (connection, ignored);
  g_autoptr (GSubprocess) second = NULL;
  g_autofree char *second_name = NULL;
  g_autoptr (GVariant) playback = NULL;
  g_autoptr (GVariant) can_pause = NULL;
  g_autoptr (GVariant) reply = NULL;
  g_autoptr (GError) error = NULL;
  g_autofree char *remote_error = NULL;

  g_assert_nonnull (first_name);
  g_hash_table_add (ignored, g_strdup (first_name));
  second = launch_application (arguments);
  second_name = wait_for_new_mpris_name (connection, ignored);
  g_assert_nonnull (second_name);
  g_assert_cmpstr (first_name, !=, second_name);
  wait_for_mpris_title (connection, first_name, "Slide 1 of 3");
  wait_for_mpris_title (connection, second_name, "Slide 1 of 3");

  call_mpris_method (connection, second_name, "Next");
  wait_for_mpris_title (connection, second_name, "Slide 2 of 3");
  wait_for_mpris_title (connection, first_name, "Slide 1 of 3");
  playback = get_mpris_property (connection,
                                 second_name,
                                 MPRIS_PLAYER_INTERFACE,
                                 "PlaybackStatus");
  can_pause = get_mpris_property (connection,
                                  second_name,
                                  MPRIS_PLAYER_INTERFACE,
                                  "CanPause");
  g_assert_cmpstr (g_variant_get_string (playback, NULL), ==, "Stopped");
  g_assert_false (g_variant_get_boolean (can_pause));

  reply = g_dbus_connection_call_sync (connection,
                                       second_name,
                                       MPRIS_OBJECT_PATH,
                                       MPRIS_PLAYER_INTERFACE,
                                       "PlayPause",
                                       NULL,
                                       NULL,
                                       G_DBUS_CALL_FLAGS_NONE,
                                       -1,
                                       NULL,
                                       &error);
  g_assert_null (reply);
  g_assert_nonnull (error);
  remote_error = g_dbus_error_get_remote_error (error);
  g_assert_cmpstr (remote_error,
                   ==,
                   "org.freedesktop.DBus.Error.NotSupported");

  terminate_running_application (second);
  g_autofree char *second_stderr = finish_process (second, EXIT_SUCCESS);
  g_assert_null (strstr (second_stderr, "pinpoint: "));
  terminate_running_application (first);
  g_autofree char *first_stderr = finish_process (first, EXIT_SUCCESS);
  g_assert_null (strstr (first_stderr, "pinpoint: "));
}

static void
test_fullscreen_speaker_shutdown (void)
{
  const char *arguments[] = {
    pinpoint_path,
    "--speakermode",
    "--fullscreen",
    presentation_path,
    NULL,
  };
  g_autoptr (GSubprocess) process = launch_application (arguments);

  terminate_running_application (process);
  g_autofree char *stderr_text = finish_process (process, EXIT_SUCCESS);
  g_assert_null (strstr (stderr_text, "pinpoint: "));
}

static void
test_playing_media_shutdown (void)
{
  const char *arguments[] = {
    pinpoint_path,
    media_presentation_path,
    NULL,
  };
  g_autoptr (GSubprocess) process = launch_application (arguments);

  terminate_running_application (process);
  g_autofree char *stderr_text = finish_process (process, EXIT_SUCCESS);
  g_assert_null (strstr (stderr_text, "pinpoint: "));
}

static void
test_invalid_file_shutdown (void)
{
  g_autofree char *missing = g_build_filename (g_get_tmp_dir (),
                                               "pinpoint-does-not-exist.pin",
                                               NULL);
  const char *arguments[] = { pinpoint_path, missing, NULL };
  g_autoptr (GSubprocess) process = launch_application (arguments);

  terminate_running_application (process);
  g_autofree char *stderr_text = finish_process (process, EXIT_SUCCESS);
  g_assert_nonnull (strstr (stderr_text, "Unable to load presentation"));
}

static void
test_reload_then_shutdown (void)
{
  g_autofree char *directory = g_dir_make_tmp ("pinpoint-application-XXXXXX",
                                               NULL);
  g_autofree char *path = g_build_filename (directory, "reload.pin", NULL);
  g_autofree char *asset_path = g_build_filename (directory, "asset.svg", NULL);
  const char *arguments[] = { pinpoint_path, path, NULL };
  g_autoptr (GSubprocess) process = NULL;
  g_autoptr (GError) error = NULL;

  g_assert_true (g_file_set_contents (path,
                                      "-- [asset.svg]\nFirst\n",
                                      -1,
                                      &error));
  g_assert_no_error (error);
  g_assert_true (g_file_set_contents (
    asset_path,
    "<svg xmlns='http://www.w3.org/2000/svg' width='32' height='32'>"
    "<rect width='32' height='32' fill='red'/></svg>",
    -1,
    &error));
  g_assert_no_error (error);
  process = launch_application (arguments);
  run_loop_for (500);
  g_assert_true (process_is_running (process));
  g_assert_true (g_file_set_contents (
    asset_path,
    "<svg xmlns='http://www.w3.org/2000/svg' width='32' height='32'>"
    "<rect width='32' height='32' fill='blue'/></svg>",
    -1,
    &error));
  g_assert_no_error (error);
  run_loop_for (300);
  g_assert_true (g_file_set_contents (path,
                                      "-- [white]\nSecond\n",
                                      -1,
                                      &error));
  g_assert_no_error (error);
  run_loop_for (500);
  g_subprocess_send_signal (process, SIGTERM);
  g_autofree char *stderr_text = finish_process (process, EXIT_SUCCESS);
  g_assert_null (strstr (stderr_text, "pinpoint:"));
  g_assert_cmpint (g_remove (path), ==, 0);
  g_assert_cmpint (g_remove (asset_path), ==, 0);
  g_assert_cmpint (g_rmdir (directory), ==, 0);
}

static void
test_invalid_pdf_option (void)
{
  const char *arguments[] = {
    pinpoint_path,
    "--pdf-page-size=invalid",
    presentation_path,
    NULL,
  };
  g_autoptr (GSubprocess) process = launch_application (arguments);
  g_autofree char *stderr_text = finish_process (process, EXIT_FAILURE);

  g_assert_nonnull (strstr (stderr_text, "unknown PDF paper size"));
}

static void
test_software_renderer_is_rejected (void)
{
  const char *arguments[] = { pinpoint_path, presentation_path, NULL };
  g_autoptr (GSubprocess) process = launch_application_with_renderer (
    arguments,
    "cairo");
  g_autofree char *stdout_text = NULL;
  g_autofree char *stderr_text = NULL;
  g_autoptr (GError) error = NULL;

  g_assert_true (g_subprocess_communicate_utf8 (process,
                                                NULL,
                                                NULL,
                                                &stdout_text,
                                                &stderr_text,
                                                &error));
  g_assert_no_error (error);
  g_assert_false (g_subprocess_get_successful (process));
  g_assert_nonnull (strstr (stderr_text,
                            "requires an OpenGL or Vulkan GSK renderer"));
}

static void
test_pdf_export (void)
{
  g_autofree char *output = NULL;
  int output_fd = g_file_open_tmp ("pinpoint-application-XXXXXX.pdf",
                                   &output,
                                   NULL);
  g_autofree char *output_option = NULL;
  const char *arguments[4];
  g_autoptr (GSubprocess) process = NULL;
  GStatBuf info;

  g_assert_cmpint (output_fd, >=, 0);
  g_assert_cmpint (close (output_fd), ==, 0);
  output_option = g_strdup_printf ("--output=%s", output);
  arguments[0] = pinpoint_path;
  arguments[1] = output_option;
  arguments[2] = presentation_path;
  arguments[3] = NULL;
  process = launch_application (arguments);
  g_autofree char *stderr_text = finish_process (process, EXIT_SUCCESS);
  g_assert_null (strstr (stderr_text, "pinpoint:"));
  g_assert_cmpint (g_stat (output, &info), ==, 0);
  g_assert_cmpint (info.st_size, >, 0);
  g_assert_cmpint (g_remove (output), ==, 0);
}

static void
test_pdf_export_ctrl_c (void)
{
  static const char sentinel[] = "existing destination";
  g_autofree char *directory = g_dir_make_tmp ("pinpoint-pdf-signal-XXXXXX",
                                               NULL);
  g_autofree char *presentation = g_build_filename (directory,
                                                    "long.pin",
                                                    NULL);
  g_autofree char *output = g_build_filename (directory,
                                              "protected.pdf",
                                              NULL);
  g_autofree char *output_option = g_strdup_printf ("--output=%s", output);
  g_autoptr (GString) source = g_string_sized_new (500000);
  g_autoptr (GError) error = NULL;
  g_autofree char *contents = NULL;
  gsize length = 0;
  const char *arguments[] = {
    pinpoint_path,
    output_option,
    presentation,
    NULL,
  };
  g_autoptr (GSubprocess) process = NULL;

  for (guint i = 0; i < 20000; i++)
    g_string_append_printf (source, "-- [white]\nSlide %u\n", i);
  g_assert_true (g_file_set_contents (presentation,
                                      source->str,
                                      source->len,
                                      &error));
  g_assert_no_error (error);
  g_assert_true (g_file_set_contents (output,
                                      sentinel,
                                      strlen (sentinel),
                                      &error));
  g_assert_no_error (error);

  process = launch_application (arguments);
  /* Allow startup to reach the CLI export main loop before delivering SIGINT. */
  run_loop_for (250);
  g_assert_true (process_is_running (process));
  g_subprocess_send_signal (process, SIGINT);
  g_autofree char *stderr_text = finish_process (process, 128 + SIGINT);
  g_assert_null (strstr (stderr_text, "pinpoint: "));
  g_assert_true (g_file_get_contents (output,
                                      &contents,
                                      &length,
                                      &error));
  g_assert_no_error (error);
  g_assert_cmpuint (length, ==, strlen (sentinel));
  g_assert_cmpmem (contents, length, sentinel, strlen (sentinel));
  g_assert_cmpint (g_remove (presentation), ==, 0);
  g_assert_cmpint (g_remove (output), ==, 0);
  g_assert_cmpint (g_rmdir (directory), ==, 0);
}

static void
test_subprocess_file_descriptor_stability (void)
{
  const char *arguments[] = { pinpoint_path, presentation_path, NULL };
  guint baseline;

  /* Establish GLib's process-watch machinery before measuring steady state. */
  {
    g_autoptr (GSubprocess) process = launch_application (arguments);

    run_loop_for (300);
    g_assert_true (process_is_running (process));
    g_subprocess_send_signal (process, SIGTERM);
    g_autofree char *stderr_text = finish_process (process, EXIT_SUCCESS);
    g_assert_null (strstr (stderr_text, "pinpoint:"));
  }
  run_loop_for (50);
  baseline = count_open_file_descriptors ();

  for (guint i = 0; i < 3; i++)
    {
      GSubprocess *process = launch_application (arguments);
      g_autofree char *stderr_text = NULL;

      run_loop_for (300);
      g_assert_true (process_is_running (process));
      g_subprocess_send_signal (process, SIGTERM);
      stderr_text = finish_process (process, EXIT_SUCCESS);
      g_assert_null (strstr (stderr_text, "pinpoint:"));
      g_object_unref (process);
    }
  assert_file_descriptor_ceiling (baseline,
                                  "Repeated application subprocess teardown");
}

int
main (int   argc,
      char *argv[])
{
  if (argc != 4)
    return EXIT_FAILURE;
  pinpoint_path = argv[1];
  presentation_path = argv[2];
  media_presentation_path = argv[3];
  if (!gtk_init_check ())
    return 77;

  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/application/fullscreen-speaker-shutdown",
                   test_fullscreen_speaker_shutdown);
  g_test_add_func ("/application/mpris-multi-instance-control",
                   test_mpris_multi_instance_control);
  g_test_add_func ("/application/playing-media-shutdown",
                   test_playing_media_shutdown);
  g_test_add_func ("/application/invalid-file-shutdown",
                   test_invalid_file_shutdown);
  g_test_add_func ("/application/reload-then-shutdown",
                   test_reload_then_shutdown);
  g_test_add_func ("/application/invalid-pdf-option", test_invalid_pdf_option);
  g_test_add_func ("/application/software-renderer-is-rejected",
                   test_software_renderer_is_rejected);
  g_test_add_func ("/application/pdf-export", test_pdf_export);
  g_test_add_func ("/application/pdf-export-ctrl-c", test_pdf_export_ctrl_c);
  g_test_add_func ("/application/process-fd-stability",
                   test_subprocess_file_descriptor_stability);
  return g_test_run ();
}
