#include "pp-control.h"

#include <gdk/gdkkeysyms.h>
#include <glib.h>

typedef struct
{
  guint counts[PP_CONTROL_COMMAND_SWAP_DISPLAYS + 1];
  gboolean requested_state;
} CommandLog;

static void
command_cb (PpControl *control,
            guint      command,
            gboolean   requested_state,
            gpointer   user_data)
{
  CommandLog *log = user_data;

  log->counts[command]++;
  log->requested_state = requested_state;
  switch ((PpControlCommand) command)
    {
    case PP_CONTROL_COMMAND_SET_BLANK:
      pp_control_set_blank (control, requested_state);
      break;
    case PP_CONTROL_COMMAND_SET_FULLSCREEN:
      pp_control_set_fullscreen (control, requested_state);
      break;
    case PP_CONTROL_COMMAND_SET_SPEAKER:
      pp_control_set_speaker (control, requested_state);
      break;
    default:
      break;
    }
}

static void
assert_action_enabled (GActionGroup *group,
                       const char   *name,
                       gboolean      enabled)
{
  g_assert_true (g_action_group_has_action (group, name));
  g_assert_cmpint (g_action_group_get_action_enabled (group, name), ==, enabled);
}

static void
test_control_state_and_commands (void)
{
  g_autoptr (GSimpleActionGroup) group = g_simple_action_group_new ();
  g_autoptr (PpControl) control = pp_control_new (G_ACTION_MAP (group),
                                                  G_ACTION_GROUP (group));
  CommandLog log = { 0 };

  g_signal_connect (control, "command", G_CALLBACK (command_cb), &log);
  pp_control_set_slide (control, 0, 3);
  assert_action_enabled (G_ACTION_GROUP (group), PP_CONTROL_ACTION_NEXT, FALSE);
  pp_control_set_presenting (control, TRUE);
  g_assert_true (pp_control_get_presenting (control));
  g_assert_true (pp_control_can_go_next (control));
  g_assert_false (pp_control_can_go_previous (control));
  assert_action_enabled (G_ACTION_GROUP (group), PP_CONTROL_ACTION_NEXT, TRUE);
  assert_action_enabled (G_ACTION_GROUP (group), PP_CONTROL_ACTION_PREVIOUS, FALSE);

  pp_control_activate (control, PP_CONTROL_ACTION_NEXT);
  g_assert_cmpuint (log.counts[PP_CONTROL_COMMAND_NEXT], ==, 1);
  pp_control_set_slide (control, 1, 3);
  g_assert_cmpuint (pp_control_get_slide_index (control), ==, 1);
  g_assert_cmpuint (pp_control_get_slide_count (control), ==, 3);
  g_assert_true (pp_control_can_go_previous (control));
  assert_action_enabled (G_ACTION_GROUP (group), PP_CONTROL_ACTION_PREVIOUS, TRUE);

  pp_control_activate (control, PP_CONTROL_ACTION_BLANK);
  g_assert_cmpuint (log.counts[PP_CONTROL_COMMAND_SET_BLANK], ==, 1);
  g_assert_true (log.requested_state);
  g_assert_true (pp_control_get_blank (control));
  pp_control_activate (control, PP_CONTROL_ACTION_BLANK);
  g_assert_false (pp_control_get_blank (control));

  pp_control_activate (control, PP_CONTROL_ACTION_FULLSCREEN);
  g_assert_true (pp_control_get_fullscreen (control));
  pp_control_activate (control, PP_CONTROL_ACTION_SPEAKER);
  g_assert_true (pp_control_get_speaker (control));

  pp_control_set_slide (control, 2, 3);
  g_assert_false (pp_control_can_go_next (control));
  /* The final advance is retained to finish rehearsal timing. */
  assert_action_enabled (G_ACTION_GROUP (group), PP_CONTROL_ACTION_NEXT, TRUE);

  pp_control_set_presenting (control, FALSE);
  g_assert_false (pp_control_get_presenting (control));
  assert_action_enabled (G_ACTION_GROUP (group), PP_CONTROL_ACTION_NEXT, FALSE);
  assert_action_enabled (G_ACTION_GROUP (group), PP_CONTROL_ACTION_BLANK, FALSE);
}

static void
test_key_mapping (void)
{
  g_autoptr (GSimpleActionGroup) group = g_simple_action_group_new ();
  g_autoptr (PpControl) control = pp_control_new (G_ACTION_MAP (group),
                                                  G_ACTION_GROUP (group));
  CommandLog log = { 0 };

  g_signal_connect (control, "command", G_CALLBACK (command_cb), &log);
  pp_control_set_slide (control, 1, 3);
  pp_control_set_presenting (control, TRUE);
  pp_control_set_swap_displays_available (control, TRUE);

  g_assert_true (pp_control_handle_key (control,
                                        GDK_KEY_Page_Down,
                                        PP_CONTROL_KEY_AUDIENCE));
  g_assert_true (pp_control_handle_key (control,
                                        GDK_KEY_AudioNext,
                                        PP_CONTROL_KEY_AUDIENCE));
  g_assert_true (pp_control_handle_key (control,
                                        GDK_KEY_Forward,
                                        PP_CONTROL_KEY_AUDIENCE));
  g_assert_cmpuint (log.counts[PP_CONTROL_COMMAND_NEXT], ==, 3);

  g_assert_true (pp_control_handle_key (control,
                                        GDK_KEY_Page_Up,
                                        PP_CONTROL_KEY_SPEAKER));
  g_assert_true (pp_control_handle_key (control,
                                        GDK_KEY_AudioPrev,
                                        PP_CONTROL_KEY_SPEAKER));
  g_assert_true (pp_control_handle_key (control,
                                        GDK_KEY_Back,
                                        PP_CONTROL_KEY_SPEAKER));
  g_assert_cmpuint (log.counts[PP_CONTROL_COMMAND_PREVIOUS], ==, 3);

  g_assert_true (pp_control_handle_key (control,
                                        GDK_KEY_F11,
                                        PP_CONTROL_KEY_AUDIENCE));
  g_assert_true (pp_control_get_fullscreen (control));
  g_assert_true (pp_control_handle_key (control,
                                        GDK_KEY_F1,
                                        PP_CONTROL_KEY_AUDIENCE));
  g_assert_true (pp_control_get_speaker (control));
  g_assert_true (pp_control_handle_key (control,
                                        GDK_KEY_b,
                                        PP_CONTROL_KEY_AUDIENCE));
  g_assert_true (pp_control_get_blank (control));
  g_assert_true (pp_control_handle_key (control,
                                        GDK_KEY_Home,
                                        PP_CONTROL_KEY_AUDIENCE));
  g_assert_cmpuint (log.counts[PP_CONTROL_COMMAND_FIRST], ==, 1);

  g_assert_false (pp_control_handle_key (control,
                                         GDK_KEY_s,
                                         PP_CONTROL_KEY_AUDIENCE));
  g_assert_true (pp_control_handle_key (control,
                                        GDK_KEY_s,
                                        PP_CONTROL_KEY_SPEAKER));
  g_assert_cmpuint (log.counts[PP_CONTROL_COMMAND_SWAP_DISPLAYS], ==, 1);
  g_assert_false (pp_control_handle_key (control,
                                         GDK_KEY_Escape,
                                         PP_CONTROL_KEY_SPEAKER));
}

static void
test_actions_removed_on_dispose (void)
{
  g_autoptr (GSimpleActionGroup) group = g_simple_action_group_new ();
  PpControl *control = pp_control_new (G_ACTION_MAP (group),
                                       G_ACTION_GROUP (group));

  g_assert_true (g_action_group_has_action (G_ACTION_GROUP (group),
                                            PP_CONTROL_ACTION_NEXT));
  g_object_unref (control);
  g_assert_false (g_action_group_has_action (G_ACTION_GROUP (group),
                                             PP_CONTROL_ACTION_NEXT));
}

int
main (int   argc,
      char *argv[])
{
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/control/state-and-commands", test_control_state_and_commands);
  g_test_add_func ("/control/key-mapping", test_key_mapping);
  g_test_add_func ("/control/actions-removed", test_actions_removed_on_dispose);
  return g_test_run ();
}
