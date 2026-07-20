#pragma once

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gio/gio.h>

G_BEGIN_DECLS

double     pp_video_thumbnail_score (GdkPixbuf *pixbuf,
                                     gboolean  *acceptable);
GdkPixbuf *pp_video_thumbnail_new (GFile        *file,
                                   GCancellable *cancellable,
                                   GError      **error);
GdkPixbuf *pp_video_thumbnail_new_for_size (GFile        *file,
                                            int           max_width,
                                            int           max_height,
                                            GCancellable *cancellable,
                                            GError      **error);

G_END_DECLS
