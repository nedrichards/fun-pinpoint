#pragma once

#include "pp-presentation.h"

#include <gtk/gtk.h>

G_BEGIN_DECLS

typedef struct
{
  guint texture_count;
  guint pending_texture_count;
  guint64 texture_bytes;
  guint64 texture_budget;
  guint svg_source_count;
} PpAssetStoreStats;

#define PP_TYPE_STAGE (pp_stage_get_type ())
G_DECLARE_FINAL_TYPE (PpStage, pp_stage, PP, STAGE, GtkWidget)

GtkWidget *pp_stage_new (void);

void       pp_stage_set_presentation (PpStage        *self,
                                      PpPresentation *presentation,
                                      guint           initial_slide);
const PpPresentation *pp_stage_get_presentation (PpStage *self);
guint      pp_stage_get_current_slide (PpStage *self);
void       pp_stage_set_slide (PpStage *self,
                               guint    slide);
gboolean   pp_stage_next (PpStage *self);
gboolean   pp_stage_previous (PpStage *self);
void       pp_stage_first (PpStage *self);
void       pp_stage_set_blank (PpStage  *self,
                               gboolean  blank);
gboolean   pp_stage_get_blank (PpStage *self);
gboolean   pp_stage_is_transitioning (PpStage *self);
gboolean   pp_stage_is_media_offload_configured (PpStage *self);
void       pp_stage_set_media_enabled (PpStage  *self,
                                       gboolean  enabled);
void       pp_stage_set_accessible_context (PpStage    *self,
                                            const char *context);
void       pp_stage_set_audio_enabled (PpStage  *self,
                                       gboolean  enabled);
void       pp_stage_set_camera_device (PpStage    *self,
                                       const char *device);
void       pp_stage_share_asset_cache (PpStage *self,
                                       PpStage *source);
void       pp_stage_get_asset_store_stats (PpStage           *self,
                                           PpAssetStoreStats *stats);
void       pp_stage_set_asset_texture_budget (PpStage *self,
                                              guint64  bytes);
void       pp_stage_invalidate_asset (PpStage *self,
                                      GFile   *file);

G_END_DECLS
