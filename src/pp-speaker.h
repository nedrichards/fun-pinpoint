#pragma once

#include "pp-stage.h"

#include <gtk/gtk.h>

G_BEGIN_DECLS

typedef struct _PpSpeaker PpSpeaker;
typedef void (*PpSpeakerFullscreenRequestFunc) (PpSpeaker *speaker,
                                                gboolean   fullscreen,
                                                gpointer   user_data);
typedef void (*PpSpeakerSwapDisplaysRequestFunc) (PpSpeaker *speaker,
                                                  gpointer   user_data);

PpSpeaker *pp_speaker_new (GtkApplication *application,
                           PpStage         *audience_stage);
void       pp_speaker_free (PpSpeaker *self);
void       pp_speaker_show (PpSpeaker *self);
void       pp_speaker_toggle (PpSpeaker *self);
gboolean   pp_speaker_is_visible (PpSpeaker *self);
void       pp_speaker_set_fullscreen (PpSpeaker *self,
                                      gboolean   fullscreen,
                                      GdkMonitor *monitor);
void       pp_speaker_set_fullscreen_request_func (
             PpSpeaker                       *self,
             PpSpeakerFullscreenRequestFunc   callback,
             gpointer                         user_data);
void       pp_speaker_set_swap_displays_request_func (
             PpSpeaker                         *self,
             PpSpeakerSwapDisplaysRequestFunc   callback,
             gpointer                           user_data);
void       pp_speaker_set_swap_displays_available (PpSpeaker *self,
                                                   gboolean   available);
void       pp_speaker_start_rehearsal (PpSpeaker *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (PpSpeaker, pp_speaker_free)

G_END_DECLS
