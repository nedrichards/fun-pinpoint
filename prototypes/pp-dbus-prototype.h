#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

typedef struct _PpDbusPrototype PpDbusPrototype;

PpDbusPrototype *pp_dbus_prototype_new (GDBusConnection *connection,
                                        const char      *bus_name,
                                        GActionGroup    *actions,
                                        GError         **error);
void             pp_dbus_prototype_free (PpDbusPrototype *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (PpDbusPrototype, pp_dbus_prototype_free)

G_END_DECLS
