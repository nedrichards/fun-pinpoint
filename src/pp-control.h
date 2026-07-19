#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

#define PP_TYPE_CONTROL (pp_control_get_type ())
G_DECLARE_FINAL_TYPE (PpControl, pp_control, PP, CONTROL, GObject)

typedef enum
{
  PP_CONTROL_COMMAND_NEXT,
  PP_CONTROL_COMMAND_PREVIOUS,
  PP_CONTROL_COMMAND_FIRST,
  PP_CONTROL_COMMAND_SET_BLANK,
  PP_CONTROL_COMMAND_SET_FULLSCREEN,
  PP_CONTROL_COMMAND_SET_SPEAKER,
  PP_CONTROL_COMMAND_SWAP_DISPLAYS,
} PpControlCommand;

typedef enum
{
  PP_CONTROL_KEY_AUDIENCE,
  PP_CONTROL_KEY_SPEAKER,
} PpControlKeyContext;

#define PP_CONTROL_ACTION_NEXT "presentation-next"
#define PP_CONTROL_ACTION_PREVIOUS "presentation-previous"
#define PP_CONTROL_ACTION_FIRST "presentation-first"
#define PP_CONTROL_ACTION_BLANK "presentation-blank"
#define PP_CONTROL_ACTION_FULLSCREEN "presentation-fullscreen"
#define PP_CONTROL_ACTION_SPEAKER "presentation-speaker"
#define PP_CONTROL_ACTION_SWAP_DISPLAYS "presentation-swap-displays"

PpControl *pp_control_new (GActionMap   *action_map,
                           GActionGroup *action_group);

void       pp_control_activate (PpControl  *self,
                                const char *action_name);
gboolean   pp_control_handle_key (PpControl           *self,
                                  guint                keyval,
                                  PpControlKeyContext  context);

void       pp_control_set_presenting (PpControl *self,
                                      gboolean   presenting);
gboolean   pp_control_get_presenting (PpControl *self);
void       pp_control_set_slide (PpControl *self,
                                 guint      index,
                                 guint      count);
guint      pp_control_get_slide_index (PpControl *self);
guint      pp_control_get_slide_count (PpControl *self);
gboolean   pp_control_can_go_next (PpControl *self);
gboolean   pp_control_can_go_previous (PpControl *self);
void       pp_control_set_blank (PpControl *self,
                                 gboolean   blank);
gboolean   pp_control_get_blank (PpControl *self);
void       pp_control_set_fullscreen (PpControl *self,
                                      gboolean   fullscreen);
gboolean   pp_control_get_fullscreen (PpControl *self);
void       pp_control_set_speaker (PpControl *self,
                                   gboolean   speaker);
gboolean   pp_control_get_speaker (PpControl *self);
void       pp_control_set_swap_displays_available (PpControl *self,
                                                   gboolean   available);

G_END_DECLS
