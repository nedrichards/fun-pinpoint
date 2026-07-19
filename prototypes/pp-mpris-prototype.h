#pragma once

#include "pp-control.h"

#include <gio/gio.h>

G_BEGIN_DECLS

typedef struct _PpMprisPrototype PpMprisPrototype;

PpMprisPrototype *pp_mpris_prototype_new (GDBusConnection *connection,
                                          const char      *bus_name,
                                          PpControl       *control,
                                          GError         **error);
void              pp_mpris_prototype_sync (PpMprisPrototype *self);
void              pp_mpris_prototype_free (PpMprisPrototype *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (PpMprisPrototype, pp_mpris_prototype_free)

G_END_DECLS
