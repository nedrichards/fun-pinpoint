#include "pp-dbus-prototype.h"

#define CONTROL_OBJECT_PATH "/com/nedrichards/pinpoint/Control"

struct _PpDbusPrototype
{
  GDBusConnection *connection;
  char *bus_name;
  guint export_id;
};

static gboolean
request_name (GDBusConnection *connection,
              const char      *bus_name,
              GError         **error)
{
  g_autoptr (GVariant) reply = g_dbus_connection_call_sync (
    connection,
    "org.freedesktop.DBus",
    "/org/freedesktop/DBus",
    "org.freedesktop.DBus",
    "RequestName",
    g_variant_new ("(su)", bus_name, 4u),
    G_VARIANT_TYPE ("(u)"),
    G_DBUS_CALL_FLAGS_NONE,
    -1,
    NULL,
    error);
  guint result = 0;

  if (reply == NULL)
    return FALSE;
  g_variant_get (reply, "(u)", &result);
  if (result != 1)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_EXISTS,
                   "D-Bus name %s is already owned",
                   bus_name);
      return FALSE;
    }
  return TRUE;
}

PpDbusPrototype *
pp_dbus_prototype_new (GDBusConnection *connection,
                       const char      *bus_name,
                       GActionGroup    *actions,
                       GError         **error)
{
  PpDbusPrototype *self;

  g_return_val_if_fail (G_IS_DBUS_CONNECTION (connection), NULL);
  g_return_val_if_fail (g_dbus_is_name (bus_name), NULL);
  g_return_val_if_fail (G_IS_ACTION_GROUP (actions), NULL);
  if (!request_name (connection, bus_name, error))
    return NULL;

  self = g_new0 (PpDbusPrototype, 1);
  self->connection = g_object_ref (connection);
  self->bus_name = g_strdup (bus_name);
  self->export_id = g_dbus_connection_export_action_group (connection,
                                                            CONTROL_OBJECT_PATH,
                                                            actions,
                                                            error);
  if (self->export_id == 0)
    {
      pp_dbus_prototype_free (self);
      return NULL;
    }
  return self;
}

void
pp_dbus_prototype_free (PpDbusPrototype *self)
{
  if (self == NULL)
    return;
  if (self->export_id != 0)
    g_dbus_connection_unexport_action_group (self->connection,
                                             self->export_id);
  if (self->connection != NULL && self->bus_name != NULL)
    g_dbus_connection_call_sync (self->connection,
                                 "org.freedesktop.DBus",
                                 "/org/freedesktop/DBus",
                                 "org.freedesktop.DBus",
                                 "ReleaseName",
                                 g_variant_new ("(s)", self->bus_name),
                                 NULL,
                                 G_DBUS_CALL_FLAGS_NONE,
                                 -1,
                                 NULL,
                                 NULL);
  g_clear_object (&self->connection);
  g_free (self->bus_name);
  g_free (self);
}
