#include "pp-control.h"
#include "pp-dbus-prototype.h"
#include "pp-mpris-prototype.h"

#include <glib-unix.h>
#include <stdlib.h>
#include <unistd.h>

typedef struct
{
  PpControl *control;
  PpMprisPrototype *mpris;
  GMainLoop *loop;
  guint slide_index;
  guint slide_count;
} Prototype;

static void
print_state (Prototype  *prototype,
             const char *command)
{
  g_print ("COMMAND=%s SLIDE=%u/%u BLANK=%s FULLSCREEN=%s SPEAKER=%s\n",
           command,
           prototype->slide_index + 1,
           prototype->slide_count,
           pp_control_get_blank (prototype->control) ? "true" : "false",
           pp_control_get_fullscreen (prototype->control) ? "true" : "false",
           pp_control_get_speaker (prototype->control) ? "true" : "false");
}

static void
command_cb (PpControl *control,
            guint      command,
            gboolean   requested_state,
            gpointer   user_data)
{
  Prototype *prototype = user_data;
  const char *name;

  switch ((PpControlCommand) command)
    {
    case PP_CONTROL_COMMAND_NEXT:
      name = "next";
      if (prototype->slide_index + 1 < prototype->slide_count)
        prototype->slide_index++;
      break;
    case PP_CONTROL_COMMAND_PREVIOUS:
      name = "previous";
      if (prototype->slide_index > 0)
        prototype->slide_index--;
      break;
    case PP_CONTROL_COMMAND_FIRST:
      name = "first";
      prototype->slide_index = 0;
      break;
    case PP_CONTROL_COMMAND_SET_BLANK:
      name = "blank";
      pp_control_set_blank (control, requested_state);
      break;
    case PP_CONTROL_COMMAND_SET_FULLSCREEN:
      name = "fullscreen";
      pp_control_set_fullscreen (control, requested_state);
      break;
    case PP_CONTROL_COMMAND_SET_SPEAKER:
      name = "speaker";
      pp_control_set_speaker (control, requested_state);
      break;
    case PP_CONTROL_COMMAND_SWAP_DISPLAYS:
      name = "swap-displays";
      break;
    default:
      g_assert_not_reached ();
    }

  pp_control_set_slide (control,
                        prototype->slide_index,
                        prototype->slide_count);
  if (prototype->mpris != NULL)
    pp_mpris_prototype_sync (prototype->mpris);
  print_state (prototype, name);
}

static gboolean
quit_cb (gpointer user_data)
{
  Prototype *prototype = user_data;

  g_main_loop_quit (prototype->loop);
  return G_SOURCE_REMOVE;
}

int
main (int   argc,
      char *argv[])
{
  gboolean enable_dbus = FALSE;
  gboolean enable_mpris = FALSE;
  gint slides = 5;
  GOptionEntry options[] = {
    { "dbus", 0, 0, G_OPTION_ARG_NONE, &enable_dbus,
      "Export the standard org.gtk.Actions group", NULL },
    { "mpris", 0, 0, G_OPTION_ARG_NONE, &enable_mpris,
      "Export the experimental MPRIS adapter", NULL },
    { "slides", 0, 0, G_OPTION_ARG_INT, &slides,
      "Number of simulated slides", "COUNT" },
    { NULL }
  };
  g_autoptr (GOptionContext) context = g_option_context_new (NULL);
  g_autoptr (GError) error = NULL;
  g_autoptr (GDBusConnection) connection = NULL;
  g_autoptr (GSimpleActionGroup) actions = NULL;
  g_autoptr (PpControl) control = NULL;
  g_autoptr (PpDbusPrototype) dbus = NULL;
  g_autoptr (PpMprisPrototype) mpris = NULL;
  g_autofree char *dbus_name = NULL;
  g_autofree char *mpris_name = NULL;
  g_autoptr (GMainLoop) loop = NULL;
  Prototype prototype = { 0 };
  guint sigint_id;
  guint sigterm_id;

  g_option_context_add_main_entries (context, options, NULL);
  if (!g_option_context_parse (context, &argc, &argv, &error))
    {
      g_printerr ("prototype: %s\n", error->message);
      return EXIT_FAILURE;
    }
  if (!enable_dbus && !enable_mpris)
    enable_dbus = enable_mpris = TRUE;
  if (slides < 1)
    {
      g_printerr ("prototype: --slides must be at least 1\n");
      return EXIT_FAILURE;
    }

  setvbuf (stdout, NULL, _IONBF, 0);
  connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
  if (connection == NULL)
    {
      g_printerr ("prototype: %s\n", error->message);
      return EXIT_FAILURE;
    }

  actions = g_simple_action_group_new ();
  control = pp_control_new (G_ACTION_MAP (actions), G_ACTION_GROUP (actions));
  prototype.control = control;
  prototype.slide_count = (guint) slides;
  g_signal_connect (control, "command", G_CALLBACK (command_cb), &prototype);
  pp_control_set_slide (control, 0, prototype.slide_count);
  pp_control_set_presenting (control, TRUE);
  pp_control_set_swap_displays_available (control, TRUE);

  if (enable_dbus)
    {
      dbus_name = g_strdup_printf (
        "com.nedrichards.pinpoint.Prototype.instance_%ld",
        (long) getpid ());
      dbus = pp_dbus_prototype_new (connection,
                                    dbus_name,
                                    G_ACTION_GROUP (actions),
                                    &error);
      if (dbus == NULL)
        {
          g_printerr ("prototype: %s\n", error->message);
          return EXIT_FAILURE;
        }
      g_print ("DBUS_NAME=%s\n", dbus_name);
      g_print ("DBUS_PATH=/com/nedrichards/pinpoint/Control\n");
    }

  if (enable_mpris)
    {
      mpris_name = g_strdup_printf (
        "org.mpris.MediaPlayer2.com.nedrichards.pinpoint.instance_%ld",
        (long) getpid ());
      mpris = pp_mpris_prototype_new (connection,
                                      mpris_name,
                                      control,
                                      &error);
      if (mpris == NULL)
        {
          g_printerr ("prototype: %s\n", error->message);
          return EXIT_FAILURE;
        }
      prototype.mpris = mpris;
      g_print ("MPRIS_NAME=%s\n", mpris_name);
      g_print ("MPRIS_PATH=/org/mpris/MediaPlayer2\n");
      pp_mpris_prototype_sync (mpris);
    }

  print_state (&prototype, "ready");
  loop = g_main_loop_new (NULL, FALSE);
  prototype.loop = loop;
  sigint_id = g_unix_signal_add (SIGINT, quit_cb, &prototype);
  sigterm_id = g_unix_signal_add (SIGTERM, quit_cb, &prototype);
  g_main_loop_run (loop);
  g_source_remove (sigint_id);
  g_source_remove (sigterm_id);
  prototype.mpris = NULL;
  return EXIT_SUCCESS;
}
