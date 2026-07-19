#pragma once

#include "pp-control.h"
#include "pp-stage.h"

#include <gtk/gtk.h>

G_BEGIN_DECLS

typedef struct _PpSpeaker PpSpeaker;
PpSpeaker *pp_speaker_new (GtkApplication *application,
                           PpStage         *audience_stage,
                           PpControl       *control);
void       pp_speaker_free (PpSpeaker *self);
void       pp_speaker_show (PpSpeaker *self);
void       pp_speaker_set_visible (PpSpeaker *self,
                                   gboolean   visible);
gboolean   pp_speaker_is_visible (PpSpeaker *self);
void       pp_speaker_set_fullscreen (PpSpeaker *self,
                                      gboolean   fullscreen,
                                      GdkMonitor *monitor);
void       pp_speaker_start_rehearsal (PpSpeaker *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (PpSpeaker, pp_speaker_free)

G_END_DECLS
