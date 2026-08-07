#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

GPtrArray *pp_file_access_find_presentations (GFile         *folder,
                                               GCancellable  *cancellable,
                                               GError       **error);
char      *pp_file_access_get_display_path    (GFile         *file);

G_END_DECLS
