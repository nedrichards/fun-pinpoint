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
  g_assert_null (strstr (stderr_text, "pinpoint:"));
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
  g_assert_null (strstr (stderr_text, "pinpoint:"));
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
  const char *arguments[] = { pinpoint_path, path, NULL };
  g_autoptr (GSubprocess) process = NULL;
  g_autoptr (GError) error = NULL;

  g_assert_true (g_file_set_contents (path,
                                      "-- [black]\nFirst\n",
                                      -1,
                                      &error));
  g_assert_no_error (error);
  process = launch_application (arguments);
  run_loop_for (500);
  g_assert_true (process_is_running (process));
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
  g_test_add_func ("/application/process-fd-stability",
                   test_subprocess_file_descriptor_stability);
  return g_test_run ();
}
