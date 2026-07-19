#pragma once

#include "pp-presentation.h"

G_BEGIN_DECLS

typedef struct _PpAssetMonitors PpAssetMonitors;

typedef void (*PpAssetChangedFunc) (GFile    *file,
                                    gpointer user_data);

PpAssetMonitors *pp_asset_monitors_new       (const PpPresentation *presentation,
                                               PpAssetChangedFunc    changed,
                                               gpointer              user_data);
guint            pp_asset_monitors_get_count (PpAssetMonitors      *self);
void             pp_asset_monitors_free      (PpAssetMonitors      *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (PpAssetMonitors, pp_asset_monitors_free)

G_END_DECLS
