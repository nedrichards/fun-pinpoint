#pragma once

#include "pp-control.h"

#include <gio/gio.h>

G_BEGIN_DECLS

typedef struct _PpMpris PpMpris;

PpMpris    *pp_mpris_new          (GDBusConnection *connection,
                                   const char      *application_id,
                                   PpControl       *control,
                                   GError         **error);
const char *pp_mpris_get_bus_name (PpMpris         *self);
void        pp_mpris_sync         (PpMpris         *self);
void        pp_mpris_free         (PpMpris         *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (PpMpris, pp_mpris_free)

G_END_DECLS
