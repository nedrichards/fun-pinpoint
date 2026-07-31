#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

typedef struct _PpPresentationInfo PpPresentationInfo;

PpPresentationInfo *pp_presentation_info_new            (GFile                    *file,
                                                         gboolean                  ignore_comments);
void                pp_presentation_info_free           (PpPresentationInfo       *self);
const char         *pp_presentation_info_get_name       (const PpPresentationInfo *self);
const char         *pp_presentation_info_get_details    (const PpPresentationInfo *self);
gboolean            pp_presentation_info_is_presentable (const PpPresentationInfo *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (PpPresentationInfo, pp_presentation_info_free)

G_END_DECLS
