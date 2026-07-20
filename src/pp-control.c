#include "pp-control.h"

#include <gdk/gdkkeysyms.h>

struct _PpControl
{
  GObject parent_instance;
  GActionMap *action_map;
  GActionGroup *action_group;
  GSimpleAction *next;
  GSimpleAction *previous;
  GSimpleAction *first;
  GSimpleAction *blank;
  GSimpleAction *fullscreen;
  GSimpleAction *speaker;
  GSimpleAction *swap_displays;
  guint slide_index;
  guint slide_count;
  gboolean presenting;
  gboolean swap_displays_available;
};

G_DEFINE_FINAL_TYPE (PpControl, pp_control, G_TYPE_OBJECT)

enum
{
  COMMAND,
  CHANGED,
  N_SIGNALS,
};

static guint signals[N_SIGNALS];

static const char *const action_names[] = {
  PP_CONTROL_ACTION_NEXT,
  PP_CONTROL_ACTION_PREVIOUS,
  PP_CONTROL_ACTION_FIRST,
  PP_CONTROL_ACTION_BLANK,
  PP_CONTROL_ACTION_FULLSCREEN,
  PP_CONTROL_ACTION_SPEAKER,
  PP_CONTROL_ACTION_SWAP_DISPLAYS,
};

static void
emit_command (PpControl       *self,
              PpControlCommand command,
              gboolean         requested_state)
{
  g_signal_emit (self, signals[COMMAND], 0, command, requested_state);
}

static void
stateless_action_cb (GSimpleAction *action,
                     GVariant      *parameter,
                     gpointer       user_data)
{
  PpControl *self = user_data;
  const char *name = g_action_get_name (G_ACTION (action));
  PpControlCommand command;

  (void) parameter;
  if (g_str_equal (name, PP_CONTROL_ACTION_NEXT))
    command = PP_CONTROL_COMMAND_NEXT;
  else if (g_str_equal (name, PP_CONTROL_ACTION_PREVIOUS))
    command = PP_CONTROL_COMMAND_PREVIOUS;
  else if (g_str_equal (name, PP_CONTROL_ACTION_FIRST))
    command = PP_CONTROL_COMMAND_FIRST;
  else
    command = PP_CONTROL_COMMAND_SWAP_DISPLAYS;
  emit_command (self, command, FALSE);
}

static void
stateful_action_cb (GSimpleAction *action,
                    GVariant      *value,
                    gpointer       user_data)
{
  PpControl *self = user_data;
  const char *name = g_action_get_name (G_ACTION (action));
  PpControlCommand command;
  gboolean requested_state = g_variant_get_boolean (value);

  if (g_str_equal (name, PP_CONTROL_ACTION_BLANK))
    command = PP_CONTROL_COMMAND_SET_BLANK;
  else if (g_str_equal (name, PP_CONTROL_ACTION_FULLSCREEN))
    command = PP_CONTROL_COMMAND_SET_FULLSCREEN;
  else
    command = PP_CONTROL_COMMAND_SET_SPEAKER;
  emit_command (self, command, requested_state);
}

static GSimpleAction *
add_stateless_action (PpControl  *self,
                      const char *name)
{
  GSimpleAction *action = g_simple_action_new (name, NULL);

  g_signal_connect (action, "activate", G_CALLBACK (stateless_action_cb), self);
  g_action_map_add_action (self->action_map, G_ACTION (action));
  return action;
}

static GSimpleAction *
add_stateful_action (PpControl  *self,
                     const char *name)
{
  GSimpleAction *action = g_simple_action_new_stateful (name,
                                                        NULL,
                                                        g_variant_new_boolean (FALSE));

  g_signal_connect (action, "change-state", G_CALLBACK (stateful_action_cb), self);
  g_action_map_add_action (self->action_map, G_ACTION (action));
  return action;
}

static void
update_enabled (PpControl *self)
{
  gboolean has_slides = self->presenting && self->slide_count > 0;
  gboolean can_go_previous = has_slides && self->slide_index > 0;

  /* Advancing once more from the final slide completes rehearsal timing, so
   * the internal action remains enabled there even though remote protocols
   * should advertise pp_control_can_go_next() as false. */
  g_simple_action_set_enabled (self->next, has_slides);
  g_simple_action_set_enabled (self->previous, can_go_previous);
  g_simple_action_set_enabled (self->first, can_go_previous);
  g_simple_action_set_enabled (self->blank, self->presenting);
  g_simple_action_set_enabled (self->fullscreen, self->presenting);
  g_simple_action_set_enabled (self->speaker, self->presenting);
  g_simple_action_set_enabled (self->swap_displays,
                               self->presenting &&
                               self->swap_displays_available);
}

static void
pp_control_dispose (GObject *object)
{
  PpControl *self = PP_CONTROL (object);

  if (self->action_map != NULL)
    for (guint i = 0; i < G_N_ELEMENTS (action_names); i++)
      g_action_map_remove_action (self->action_map, action_names[i]);
  g_clear_object (&self->next);
  g_clear_object (&self->previous);
  g_clear_object (&self->first);
  g_clear_object (&self->blank);
  g_clear_object (&self->fullscreen);
  g_clear_object (&self->speaker);
  g_clear_object (&self->swap_displays);
  g_clear_object (&self->action_map);
  g_clear_object (&self->action_group);
  G_OBJECT_CLASS (pp_control_parent_class)->dispose (object);
}

static void
pp_control_class_init (PpControlClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = pp_control_dispose;
  signals[COMMAND] = g_signal_new ("command",
                                   G_TYPE_FROM_CLASS (klass),
                                   G_SIGNAL_RUN_LAST,
                                   0,
                                   NULL,
                                   NULL,
                                   NULL,
                                   G_TYPE_NONE,
                                   2,
                                   G_TYPE_UINT,
                                   G_TYPE_BOOLEAN);
  signals[CHANGED] = g_signal_new ("changed",
                                   G_TYPE_FROM_CLASS (klass),
                                   G_SIGNAL_RUN_LAST,
                                   0,
                                   NULL,
                                   NULL,
                                   NULL,
                                   G_TYPE_NONE,
                                   0);
}

static void
pp_control_init (PpControl *self)
{
  (void) self;
}

PpControl *
pp_control_new (GActionMap   *action_map,
                GActionGroup *action_group)
{
  PpControl *self;

  g_return_val_if_fail (G_IS_ACTION_MAP (action_map), NULL);
  g_return_val_if_fail (G_IS_ACTION_GROUP (action_group), NULL);
  self = g_object_new (PP_TYPE_CONTROL, NULL);
  self->action_map = g_object_ref (action_map);
  self->action_group = g_object_ref (action_group);
  self->next = add_stateless_action (self, PP_CONTROL_ACTION_NEXT);
  self->previous = add_stateless_action (self, PP_CONTROL_ACTION_PREVIOUS);
  self->first = add_stateless_action (self, PP_CONTROL_ACTION_FIRST);
  self->blank = add_stateful_action (self, PP_CONTROL_ACTION_BLANK);
  self->fullscreen = add_stateful_action (self, PP_CONTROL_ACTION_FULLSCREEN);
  self->speaker = add_stateful_action (self, PP_CONTROL_ACTION_SPEAKER);
  self->swap_displays = add_stateless_action (self,
                                              PP_CONTROL_ACTION_SWAP_DISPLAYS);
  update_enabled (self);
  return self;
}

void
pp_control_activate (PpControl  *self,
                     const char *action_name)
{
  g_return_if_fail (PP_IS_CONTROL (self));
  g_return_if_fail (action_name != NULL);
  g_action_group_activate_action (self->action_group, action_name, NULL);
}

gboolean
pp_control_handle_key (PpControl          *self,
                       guint               keyval,
                       PpControlKeyContext context)
{
  g_return_val_if_fail (PP_IS_CONTROL (self), FALSE);

  switch (keyval)
    {
    case GDK_KEY_Left:
    case GDK_KEY_Up:
    case GDK_KEY_BackSpace:
    case GDK_KEY_Page_Up:
    case GDK_KEY_AudioPrev:
    case GDK_KEY_Back:
      pp_control_activate (self, PP_CONTROL_ACTION_PREVIOUS);
      return TRUE;

    case GDK_KEY_Right:
    case GDK_KEY_Down:
    case GDK_KEY_space:
    case GDK_KEY_Page_Down:
    case GDK_KEY_AudioNext:
    case GDK_KEY_Forward:
      pp_control_activate (self, PP_CONTROL_ACTION_NEXT);
      return TRUE;

    case GDK_KEY_F11:
    case GDK_KEY_f:
    case GDK_KEY_F:
      pp_control_activate (self, PP_CONTROL_ACTION_FULLSCREEN);
      return TRUE;

    case GDK_KEY_F1:
      pp_control_activate (self, PP_CONTROL_ACTION_SPEAKER);
      return TRUE;

    case GDK_KEY_b:
    case GDK_KEY_B:
      pp_control_activate (self, PP_CONTROL_ACTION_BLANK);
      return TRUE;

    case GDK_KEY_h:
    case GDK_KEY_H:
    case GDK_KEY_Home:
      pp_control_activate (self, PP_CONTROL_ACTION_FIRST);
      return TRUE;

    case GDK_KEY_s:
    case GDK_KEY_S:
      if (context == PP_CONTROL_KEY_SPEAKER)
        {
          pp_control_activate (self, PP_CONTROL_ACTION_SWAP_DISPLAYS);
          return TRUE;
        }
      return FALSE;

    default:
      return FALSE;
    }
}

void
pp_control_set_presenting (PpControl *self,
                           gboolean   presenting)
{
  g_return_if_fail (PP_IS_CONTROL (self));
  presenting = !!presenting;
  if (self->presenting == presenting)
    return;
  self->presenting = presenting;
  update_enabled (self);
  g_signal_emit (self, signals[CHANGED], 0);
}

gboolean
pp_control_get_presenting (PpControl *self)
{
  g_return_val_if_fail (PP_IS_CONTROL (self), FALSE);
  return self->presenting;
}

void
pp_control_set_slide (PpControl *self,
                      guint      index,
                      guint      count)
{
  g_return_if_fail (PP_IS_CONTROL (self));
  g_return_if_fail (count == 0 || index < count);
  if (self->slide_index == index && self->slide_count == count)
    return;
  self->slide_index = index;
  self->slide_count = count;
  update_enabled (self);
  g_signal_emit (self, signals[CHANGED], 0);
}

guint
pp_control_get_slide_index (PpControl *self)
{
  g_return_val_if_fail (PP_IS_CONTROL (self), 0);
  return self->slide_index;
}

guint
pp_control_get_slide_count (PpControl *self)
{
  g_return_val_if_fail (PP_IS_CONTROL (self), 0);
  return self->slide_count;
}

gboolean
pp_control_can_go_next (PpControl *self)
{
  g_return_val_if_fail (PP_IS_CONTROL (self), FALSE);
  return self->presenting &&
         self->slide_count > 0 &&
         self->slide_index + 1 < self->slide_count;
}

gboolean
pp_control_can_go_previous (PpControl *self)
{
  g_return_val_if_fail (PP_IS_CONTROL (self), FALSE);
  return self->presenting && self->slide_count > 0 && self->slide_index > 0;
}

static gboolean
set_boolean_state (GSimpleAction *action,
                   gboolean       state)
{
  g_autoptr (GVariant) current = g_action_get_state (G_ACTION (action));

  state = !!state;
  if (g_variant_get_boolean (current) == state)
    return FALSE;
  g_simple_action_set_state (action, g_variant_new_boolean (state));
  return TRUE;
}

void
pp_control_set_blank (PpControl *self,
                      gboolean   blank)
{
  g_return_if_fail (PP_IS_CONTROL (self));
  if (set_boolean_state (self->blank, blank))
    g_signal_emit (self, signals[CHANGED], 0);
}

gboolean
pp_control_get_blank (PpControl *self)
{
  g_autoptr (GVariant) state = NULL;

  g_return_val_if_fail (PP_IS_CONTROL (self), FALSE);
  state = g_action_get_state (G_ACTION (self->blank));
  return g_variant_get_boolean (state);
}

void
pp_control_set_fullscreen (PpControl *self,
                           gboolean   fullscreen)
{
  g_return_if_fail (PP_IS_CONTROL (self));
  if (set_boolean_state (self->fullscreen, fullscreen))
    g_signal_emit (self, signals[CHANGED], 0);
}

gboolean
pp_control_get_fullscreen (PpControl *self)
{
  g_autoptr (GVariant) state = NULL;

  g_return_val_if_fail (PP_IS_CONTROL (self), FALSE);
  state = g_action_get_state (G_ACTION (self->fullscreen));
  return g_variant_get_boolean (state);
}

void
pp_control_set_speaker (PpControl *self,
                        gboolean   speaker)
{
  g_return_if_fail (PP_IS_CONTROL (self));
  if (set_boolean_state (self->speaker, speaker))
    g_signal_emit (self, signals[CHANGED], 0);
}

gboolean
pp_control_get_speaker (PpControl *self)
{
  g_autoptr (GVariant) state = NULL;

  g_return_val_if_fail (PP_IS_CONTROL (self), FALSE);
  state = g_action_get_state (G_ACTION (self->speaker));
  return g_variant_get_boolean (state);
}

void
pp_control_set_swap_displays_available (PpControl *self,
                                        gboolean   available)
{
  g_return_if_fail (PP_IS_CONTROL (self));
  self->swap_displays_available = !!available;
  update_enabled (self);
}
