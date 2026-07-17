#pragma once

#include "pp-presentation.h"

G_BEGIN_DECLS

typedef struct
{
  float x;
  float y;
  float scale_x;
  float scale_y;
  float angle;
  float angle_x;
  float angle_y;
  float opacity;
} PpTransitionLayerState;

typedef struct
{
  PpTransitionLayerState actor;
  PpTransitionLayerState background;
  PpTransitionLayerState midground;
  PpTransitionLayerState foreground;
} PpTransitionState;

typedef struct _PpLegacyTransition PpLegacyTransition;

gboolean            pp_transition_is_builtin            (const char           *name);
double              pp_transition_apply_easing          (const char           *name,
                                                         double                progress);
PpLegacyTransition *pp_legacy_transition_load           (const PpPresentation *presentation,
                                                         const char           *name,
                                                         GError              **error);
void                pp_legacy_transition_free           (PpLegacyTransition  *transition);
guint               pp_legacy_transition_get_duration   (PpLegacyTransition  *transition,
                                                         gboolean             incoming,
                                                         gboolean             backwards);
void                pp_legacy_transition_calculate      (PpLegacyTransition  *transition,
                                                         gboolean             incoming,
                                                         gboolean             backwards,
                                                         double               progress,
                                                         PpTransitionState   *state);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (PpLegacyTransition, pp_legacy_transition_free)

G_END_DECLS
