#include "config.h"

#include "pp-asset-monitor.h"

#include "pp-render.h"
#include "pp-transition.h"

struct _PpAssetMonitors
{
  GPtrArray *monitors;
  PpAssetChangedFunc changed;
  gpointer user_data;
};

static void
file_changed_cb (GFileMonitor      *monitor,
                 GFile             *file,
                 GFile             *other_file,
                 GFileMonitorEvent  event_type,
                 gpointer           user_data)
{
  PpAssetMonitors *self = user_data;

  (void) monitor;
  (void) event_type;
  self->changed (file, self->user_data);
  if (other_file != NULL)
    self->changed (other_file, self->user_data);
}

static void
monitor_file (PpAssetMonitors *self,
              GHashTable      *seen,
              GFile           *file)
{
  g_autoptr (GFileMonitor) monitor = NULL;
  g_autoptr (GError) error = NULL;

  if (file == NULL || g_hash_table_contains (seen, file))
    return;
  g_hash_table_add (seen, g_object_ref (file));

  monitor = g_file_monitor_file (file,
                                 G_FILE_MONITOR_WATCH_MOVES,
                                 NULL,
                                 &error);
  if (monitor == NULL)
    {
      g_autofree char *name = g_file_get_parse_name (file);

      g_warning ("Unable to monitor asset %s: %s", name, error->message);
      return;
    }

  g_file_monitor_set_rate_limit (monitor, 100);
  g_signal_connect (monitor,
                    "changed",
                    G_CALLBACK (file_changed_cb),
                    self);
  g_ptr_array_add (self->monitors, g_steal_pointer (&monitor));
}

static void
monitor_asset (PpAssetMonitors      *self,
               GHashTable           *seen,
               const PpPresentation *presentation,
               const char           *asset)
{
  g_autoptr (GFile) file = NULL;

  if (asset == NULL || asset[0] == '\0')
    return;
  file = pp_render_resolve_asset (presentation, asset);
  monitor_file (self, seen, file);
}

PpAssetMonitors *
pp_asset_monitors_new (const PpPresentation *presentation,
                       PpAssetChangedFunc    changed,
                       gpointer              user_data)
{
  g_autoptr (GHashTable) seen = NULL;
  PpAssetMonitors *self;

  g_return_val_if_fail (presentation != NULL, NULL);
  g_return_val_if_fail (changed != NULL, NULL);

  self = g_new0 (PpAssetMonitors, 1);
  self->monitors = g_ptr_array_new_with_free_func (g_object_unref);
  self->changed = changed;
  self->user_data = user_data;
  seen = g_hash_table_new_full ((GHashFunc) g_file_hash,
                                (GEqualFunc) g_file_equal,
                                g_object_unref,
                                NULL);

  for (guint i = 0; i < pp_presentation_get_n_slides (presentation); i++)
    {
      const PpSlide *slide = pp_presentation_get_slide (presentation, i);

      if (slide->background_type == PP_BACKGROUND_IMAGE ||
          slide->background_type == PP_BACKGROUND_VIDEO ||
          slide->background_type == PP_BACKGROUND_SVG)
        monitor_asset (self, seen, presentation, slide->background);
      if (slide->transition != NULL &&
          !pp_transition_is_builtin (slide->transition))
        {
          g_autoptr (GFile) file = pp_legacy_transition_resolve_file (
            presentation, slide->transition);

          monitor_file (self, seen, file);
        }
    }

  return self;
}

guint
pp_asset_monitors_get_count (PpAssetMonitors *self)
{
  g_return_val_if_fail (self != NULL, 0);
  return self->monitors->len;
}

void
pp_asset_monitors_free (PpAssetMonitors *self)
{
  if (self == NULL)
    return;
  g_ptr_array_unref (self->monitors);
  g_free (self);
}
