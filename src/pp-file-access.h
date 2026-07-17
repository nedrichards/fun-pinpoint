#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

GPtrArray *pp_file_access_find_presentations (GFile         *folder,
                                               GCancellable  *cancellable,
                                               GError       **error);

G_END_DECLS
