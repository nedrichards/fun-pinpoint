#include "pp-mpris-prototype.h"

#define MPRIS_OBJECT_PATH "/org/mpris/MediaPlayer2"
#define MPRIS_ROOT_INTERFACE "org.mpris.MediaPlayer2"
#define MPRIS_PLAYER_INTERFACE "org.mpris.MediaPlayer2.Player"

struct _PpMprisPrototype
{
  GDBusConnection *connection;
  PpControl *control;
  GDBusNodeInfo *node_info;
  char *bus_name;
  guint root_id;
  guint player_id;
};

static const char introspection_xml[] =
  "<node>"
  " <interface name='org.mpris.MediaPlayer2'>"
  "  <method name='Raise'/>"
  "  <method name='Quit'/>"
  "  <property name='CanQuit' type='b' access='read'/>"
  "  <property name='CanRaise' type='b' access='read'/>"
  "  <property name='HasTrackList' type='b' access='read'/>"
  "  <property name='Identity' type='s' access='read'/>"
  "  <property name='DesktopEntry' type='s' access='read'/>"
  "  <property name='SupportedUriSchemes' type='as' access='read'/>"
  "  <property name='SupportedMimeTypes' type='as' access='read'/>"
  "  <property name='Fullscreen' type='b' access='readwrite'/>"
  "  <property name='CanSetFullscreen' type='b' access='read'/>"
  " </interface>"
  " <interface name='org.mpris.MediaPlayer2.Player'>"
  "  <method name='Next'/>"
  "  <method name='Previous'/>"
  "  <method name='Pause'/>"
  "  <method name='PlayPause'/>"
  "  <method name='Stop'/>"
  "  <method name='Play'/>"
  "  <method name='Seek'><arg direction='in' type='x' name='Offset'/></method>"
  "  <method name='SetPosition'>"
  "   <arg direction='in' type='o' name='TrackId'/>"
  "   <arg direction='in' type='x' name='Position'/>"
  "  </method>"
  "  <method name='OpenUri'><arg direction='in' type='s' name='Uri'/></method>"
  "  <signal name='Seeked'><arg type='x' name='Position'/></signal>"
  "  <property name='PlaybackStatus' type='s' access='read'/>"
  "  <property name='LoopStatus' type='s' access='readwrite'/>"
  "  <property name='Rate' type='d' access='readwrite'/>"
  "  <property name='Shuffle' type='b' access='readwrite'/>"
  "  <property name='Metadata' type='a{sv}' access='read'/>"
  "  <property name='Volume' type='d' access='readwrite'/>"
  "  <property name='Position' type='x' access='read'/>"
  "  <property name='MinimumRate' type='d' access='read'/>"
  "  <property name='MaximumRate' type='d' access='read'/>"
  "  <property name='CanGoNext' type='b' access='read'/>"
  "  <property name='CanGoPrevious' type='b' access='read'/>"
  "  <property name='CanPlay' type='b' access='read'/>"
  "  <property name='CanPause' type='b' access='read'/>"
  "  <property name='CanSeek' type='b' access='read'/>"
  "  <property name='CanControl' type='b' access='read'/>"
  " </interface>"
  "</node>";

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
                   "MPRIS name %s is already owned",
                   bus_name);
      return FALSE;
    }
  return TRUE;
}

static GVariant *
metadata (PpMprisPrototype *self)
{
  GVariantBuilder builder;
  guint index = pp_control_get_slide_index (self->control);
  guint count = pp_control_get_slide_count (self->control);
  g_autofree char *track_id = g_strdup_printf (
    "/com/nedrichards/pinpoint/Slide/s%u",
    index + 1);
  g_autofree char *title = g_strdup_printf ("Slide %u of %u",
                                            index + 1,
                                            count);

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));
  g_variant_builder_add (&builder,
                         "{sv}",
                         "mpris:trackid",
                         g_variant_new_object_path (track_id));
  g_variant_builder_add (&builder,
                         "{sv}",
                         "xesam:title",
                         g_variant_new_string (title));
  g_variant_builder_add (&builder,
                         "{sv}",
                         "xesam:album",
                         g_variant_new_string ("Pinpoint presentation"));
  return g_variant_builder_end (&builder);
}

static void
not_supported (GDBusMethodInvocation *invocation,
               const char            *method_name)
{
  g_dbus_method_invocation_return_error (invocation,
                                         G_DBUS_ERROR,
                                         G_DBUS_ERROR_NOT_SUPPORTED,
                                         "%s is not meaningful for this "
                                         "presentation prototype",
                                         method_name);
}

static void
method_call_cb (GDBusConnection       *connection,
                const char            *sender,
                const char            *object_path,
                const char            *interface_name,
                const char            *method_name,
                GVariant              *parameters,
                GDBusMethodInvocation *invocation,
                gpointer               user_data)
{
  PpMprisPrototype *self = user_data;

  (void) connection;
  (void) sender;
  (void) object_path;
  (void) parameters;
  if (g_str_equal (interface_name, MPRIS_PLAYER_INTERFACE) &&
      g_str_equal (method_name, "Next"))
    {
      if (pp_control_can_go_next (self->control))
        pp_control_activate (self->control, PP_CONTROL_ACTION_NEXT);
      g_dbus_method_invocation_return_value (invocation, NULL);
    }
  else if (g_str_equal (interface_name, MPRIS_PLAYER_INTERFACE) &&
           g_str_equal (method_name, "Previous"))
    {
      if (pp_control_can_go_previous (self->control))
        pp_control_activate (self->control, PP_CONTROL_ACTION_PREVIOUS);
      g_dbus_method_invocation_return_value (invocation, NULL);
    }
  else
    not_supported (invocation, method_name);
}

static GVariant *
get_property_cb (GDBusConnection  *connection,
                 const char       *sender,
                 const char       *object_path,
                 const char       *interface_name,
                 const char       *property_name,
                 GError          **error,
                 gpointer          user_data)
{
  PpMprisPrototype *self = user_data;
  const char *empty[] = { NULL };

  (void) connection;
  (void) sender;
  (void) object_path;
  (void) error;
  if (g_str_equal (interface_name, MPRIS_ROOT_INTERFACE))
    {
      if (g_str_equal (property_name, "Identity"))
        return g_variant_new_string ("Pinpoint Prototype");
      if (g_str_equal (property_name, "DesktopEntry"))
        return g_variant_new_string ("com.nedrichards.pinpoint");
      if (g_str_equal (property_name, "SupportedUriSchemes") ||
          g_str_equal (property_name, "SupportedMimeTypes"))
        return g_variant_new_strv (empty, 0);
      if (g_str_equal (property_name, "Fullscreen"))
        return g_variant_new_boolean (pp_control_get_fullscreen (self->control));
      if (g_str_equal (property_name, "CanSetFullscreen"))
        return g_variant_new_boolean (TRUE);
      return g_variant_new_boolean (FALSE);
    }

  if (g_str_equal (property_name, "PlaybackStatus"))
    return g_variant_new_string ("Stopped");
  if (g_str_equal (property_name, "LoopStatus"))
    return g_variant_new_string ("None");
  if (g_str_equal (property_name, "Metadata"))
    return metadata (self);
  if (g_str_equal (property_name, "CanGoNext"))
    return g_variant_new_boolean (pp_control_can_go_next (self->control));
  if (g_str_equal (property_name, "CanGoPrevious"))
    return g_variant_new_boolean (pp_control_can_go_previous (self->control));
  if (g_str_equal (property_name, "CanControl"))
    return g_variant_new_boolean (TRUE);
  if (g_str_equal (property_name, "Rate") ||
      g_str_equal (property_name, "Volume") ||
      g_str_equal (property_name, "MinimumRate") ||
      g_str_equal (property_name, "MaximumRate"))
    return g_variant_new_double (1.0);
  if (g_str_equal (property_name, "Position"))
    return g_variant_new_int64 (0);
  return g_variant_new_boolean (FALSE);
}

static gboolean
set_property_cb (GDBusConnection  *connection,
                 const char       *sender,
                 const char       *object_path,
                 const char       *interface_name,
                 const char       *property_name,
                 GVariant         *value,
                 GError          **error,
                 gpointer          user_data)
{
  PpMprisPrototype *self = user_data;

  (void) connection;
  (void) sender;
  (void) object_path;
  if (g_str_equal (interface_name, MPRIS_ROOT_INTERFACE) &&
      g_str_equal (property_name, "Fullscreen"))
    {
      gboolean requested = g_variant_get_boolean (value);

      if (requested != pp_control_get_fullscreen (self->control))
        pp_control_activate (self->control, PP_CONTROL_ACTION_FULLSCREEN);
      return TRUE;
    }
  g_set_error (error,
               G_DBUS_ERROR,
               G_DBUS_ERROR_NOT_SUPPORTED,
               "%s is read-only in this presentation prototype",
               property_name);
  return FALSE;
}

static const GDBusInterfaceVTable interface_vtable = {
  .method_call = method_call_cb,
  .get_property = get_property_cb,
  .set_property = set_property_cb,
};

static void
emit_properties (PpMprisPrototype *self,
                 const char       *interface_name,
                 GVariantBuilder  *changed)
{
  GVariantBuilder invalidated;

  g_variant_builder_init (&invalidated, G_VARIANT_TYPE ("as"));
  g_dbus_connection_emit_signal (
    self->connection,
    NULL,
    MPRIS_OBJECT_PATH,
    "org.freedesktop.DBus.Properties",
    "PropertiesChanged",
    g_variant_new ("(s@a{sv}@as)",
                   interface_name,
                   g_variant_builder_end (changed),
                   g_variant_builder_end (&invalidated)),
    NULL);
}

PpMprisPrototype *
pp_mpris_prototype_new (GDBusConnection *connection,
                        const char      *bus_name,
                        PpControl       *control,
                        GError         **error)
{
  PpMprisPrototype *self;

  g_return_val_if_fail (G_IS_DBUS_CONNECTION (connection), NULL);
  g_return_val_if_fail (g_dbus_is_name (bus_name), NULL);
  g_return_val_if_fail (PP_IS_CONTROL (control), NULL);
  if (!request_name (connection, bus_name, error))
    return NULL;

  self = g_new0 (PpMprisPrototype, 1);
  self->connection = g_object_ref (connection);
  self->control = g_object_ref (control);
  self->bus_name = g_strdup (bus_name);
  self->node_info = g_dbus_node_info_new_for_xml (introspection_xml, error);
  if (self->node_info == NULL)
    goto error;
  self->root_id = g_dbus_connection_register_object (
    connection,
    MPRIS_OBJECT_PATH,
    self->node_info->interfaces[0],
    &interface_vtable,
    self,
    NULL,
    error);
  if (self->root_id == 0)
    goto error;
  self->player_id = g_dbus_connection_register_object (
    connection,
    MPRIS_OBJECT_PATH,
    self->node_info->interfaces[1],
    &interface_vtable,
    self,
    NULL,
    error);
  if (self->player_id == 0)
    goto error;
  return self;

error:
  pp_mpris_prototype_free (self);
  return NULL;
}

void
pp_mpris_prototype_sync (PpMprisPrototype *self)
{
  GVariantBuilder player;
  GVariantBuilder root;

  g_return_if_fail (self != NULL);
  g_variant_builder_init (&player, G_VARIANT_TYPE ("a{sv}"));
  g_variant_builder_add (&player, "{sv}", "Metadata", metadata (self));
  g_variant_builder_add (&player,
                         "{sv}",
                         "CanGoNext",
                         g_variant_new_boolean (
                           pp_control_can_go_next (self->control)));
  g_variant_builder_add (&player,
                         "{sv}",
                         "CanGoPrevious",
                         g_variant_new_boolean (
                           pp_control_can_go_previous (self->control)));
  emit_properties (self, MPRIS_PLAYER_INTERFACE, &player);

  g_variant_builder_init (&root, G_VARIANT_TYPE ("a{sv}"));
  g_variant_builder_add (&root,
                         "{sv}",
                         "Fullscreen",
                         g_variant_new_boolean (
                           pp_control_get_fullscreen (self->control)));
  emit_properties (self, MPRIS_ROOT_INTERFACE, &root);
}

void
pp_mpris_prototype_free (PpMprisPrototype *self)
{
  if (self == NULL)
    return;
  if (self->player_id != 0)
    g_dbus_connection_unregister_object (self->connection, self->player_id);
  if (self->root_id != 0)
    g_dbus_connection_unregister_object (self->connection, self->root_id);
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
  g_clear_pointer (&self->node_info, g_dbus_node_info_unref);
  g_clear_object (&self->control);
  g_clear_object (&self->connection);
  g_free (self->bus_name);
  g_free (self);
}
