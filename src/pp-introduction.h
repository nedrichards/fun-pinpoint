#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

GFile    *pp_introduction_get_presentation (void);
gboolean  pp_introduction_copy_to_folder   (GFile         *folder,
                                             GFile        **presentation_out,
                                             GCancellable  *cancellable,
                                             GError       **error);

G_END_DECLS
