#include "config.h"

#include "pp-stage.h"

#include "pp-page-curl-view.h"
#include "pp-render.h"
#include "pp-transition.h"

#include <gio/gunixfdlist.h>
#include <gst/gst.h>
#include <librsvg/rsvg.h>
#include <math.h>
#include <unistd.h>

#define PORTAL_BUS_NAME "org.freedesktop.portal.Desktop"
#define PORTAL_OBJECT_PATH "/org/freedesktop/portal/desktop"
#define CAMERA_INTERFACE "org.freedesktop.portal.Camera"
#define REQUEST_INTERFACE "org.freedesktop.portal.Request"
#define DEFAULT_TEXTURE_CACHE_BYTES (64 * 1024 * 1024)

typedef struct
{
  GstElement *player;
  GstElement *video_sink;
  GdkPaintable *paintable;
  char *label;
  guint bus_watch_id;
  int fd;
  gboolean playing;
  gboolean failed;
  gboolean diagnostics_reported;
  gboolean offload_reported;
} PpMedia;

typedef struct
{
  float width;
  float height;
  guint pango_serial;
  int scale_factor;
  PpRect rect;
  GskRenderNode *node;
} PpCachedText;

typedef struct
{
  char *uri;
  float width;
  float height;
  GskRenderNode *node;
} PpCachedSvg;

typedef struct
{
  GFile *file;
  char *uri;
} PpResolvedAsset;

typedef struct
{
  GdkTexture *texture;
  guint64 last_used;
  guint64 bytes;
} PpCachedTexture;

typedef struct
{
  GFile *file;
  RsvgHandle *handle;
  double intrinsic_width;
  double intrinsic_height;
} PpCachedSvgSource;

typedef struct
{
  guint64 token;
  guint64 last_used;
} PpPendingTexture;

typedef struct
{
  struct _PpAssetCache *cache;
  GFile *file;
  char *uri;
  guint64 token;
} PpTextureTask;

typedef struct _PpAssetCache
{
  gint ref_count;
  GHashTable *resolved;
  GHashTable *textures;
  GHashTable *pending_textures;
  GHashTable *failed_textures;
  GHashTable *svg_sources;
  GPtrArray *stages;
  PpPresentation *presentation;
  GCancellable *load_cancellable;
  guint64 use_serial;
  guint64 load_serial;
  guint64 texture_bytes;
  guint64 texture_budget;
} PpAssetCache;

typedef PpTransitionLayerState LayerState;
typedef PpTransitionState TransitionState;

struct _PpStage
{
  GtkWidget parent_instance;

  PpPresentation *presentation;
  PpAssetCache *assets;
  GHashTable *text_nodes;
  GHashTable *svg_nodes;
  GHashTable *media;
  GHashTable *legacy_transitions;
  GHashTable *failed_transitions;
  guint current_slide;
  guint previous_slide;
  gboolean blank;
  gboolean media_enabled;
  gboolean audio_enabled;
  char *accessible_context;

  gboolean transitioning;
  gboolean backwards;
  gint64 transition_started;
  guint transition_duration_ms;
  guint tick_id;

  PpPageCurlView *curl_view;
  GtkWidget *media_offload;
  GtkPicture *media_picture;
  PpMedia *offloaded_media;
  const PpSlide *offloaded_slide;
  gboolean media_offload_allocated;
  GdkTexture *curl_previous_texture;
  GdkTexture *curl_current_texture;
  float curl_texture_width;
  float curl_texture_height;
  int curl_texture_scale;

  GDBusConnection *portal_connection;
  guint camera_idle_id;
  guint camera_response_subscription;
  char *camera_request_path;
  char *camera_device;
  gboolean camera_request_pending;
  gboolean camera_request_attempted;
  gboolean renderer_reported;
};

G_DEFINE_FINAL_TYPE (PpStage, pp_stage, GTK_TYPE_WIDGET)

enum
{
  SLIDE_CHANGED,
  PRESENTATION_ENDED,
  N_SIGNALS,
};

static guint signals[N_SIGNALS];

static gboolean presentations_share_file (const PpPresentation *a,
                                           const PpPresentation *b);

static void
resolved_asset_free (PpResolvedAsset *asset)
{
  if (asset == NULL)
    return;
  g_clear_object (&asset->file);
  g_free (asset->uri);
  g_free (asset);
}

static void
cached_texture_free (PpCachedTexture *cached)
{
  if (cached == NULL)
    return;
  g_clear_object (&cached->texture);
  g_free (cached);
}

static void
cached_svg_source_free (PpCachedSvgSource *cached)
{
  if (cached == NULL)
    return;
  g_clear_object (&cached->file);
  g_clear_object (&cached->handle);
  g_free (cached);
}

static PpAssetCache *
asset_cache_new (void)
{
  PpAssetCache *cache = g_new0 (PpAssetCache, 1);

  cache->ref_count = 1;
  cache->resolved = g_hash_table_new_full (
    g_str_hash,
    g_str_equal,
    g_free,
    (GDestroyNotify) resolved_asset_free);
  cache->textures = g_hash_table_new_full (
    g_str_hash,
    g_str_equal,
    g_free,
    (GDestroyNotify) cached_texture_free);
  cache->pending_textures = g_hash_table_new_full (g_str_hash,
                                                    g_str_equal,
                                                    g_free,
                                                    g_free);
  cache->failed_textures = g_hash_table_new_full (g_str_hash,
                                                   g_str_equal,
                                                   g_free,
                                                   NULL);
  cache->svg_sources = g_hash_table_new_full (
    g_str_hash,
    g_str_equal,
    g_free,
    (GDestroyNotify) cached_svg_source_free);
  cache->stages = g_ptr_array_new ();
  cache->load_cancellable = g_cancellable_new ();
  cache->texture_budget = DEFAULT_TEXTURE_CACHE_BYTES;
  return cache;
}

static PpAssetCache *
asset_cache_ref (PpAssetCache *cache)
{
  g_atomic_int_inc (&cache->ref_count);
  return cache;
}

static void
asset_cache_unref (PpAssetCache *cache)
{
  if (cache == NULL || !g_atomic_int_dec_and_test (&cache->ref_count))
    return;
  g_cancellable_cancel (cache->load_cancellable);
  g_clear_pointer (&cache->presentation, pp_presentation_free);
  g_hash_table_unref (cache->resolved);
  g_hash_table_unref (cache->textures);
  g_hash_table_unref (cache->pending_textures);
  g_hash_table_unref (cache->failed_textures);
  g_hash_table_unref (cache->svg_sources);
  g_ptr_array_unref (cache->stages);
  g_object_unref (cache->load_cancellable);
  g_free (cache);
}

static void
asset_cache_cancel_loads (PpAssetCache *cache)
{
  g_cancellable_cancel (cache->load_cancellable);
  g_hash_table_remove_all (cache->pending_textures);
  g_clear_object (&cache->load_cancellable);
  cache->load_cancellable = g_cancellable_new ();
}

static void
asset_cache_clear_decoded (PpAssetCache *cache)
{
  cache->texture_bytes = 0;
  g_hash_table_remove_all (cache->resolved);
  g_hash_table_remove_all (cache->textures);
  g_hash_table_remove_all (cache->failed_textures);
  g_hash_table_remove_all (cache->svg_sources);
}

static void
asset_cache_prepare_presentation (PpAssetCache  *cache,
                                  PpPresentation *presentation)
{
  gboolean keep_decoded;

  if (cache->presentation == presentation)
    return;
  keep_decoded = presentations_share_file (cache->presentation, presentation);
  asset_cache_cancel_loads (cache);
  if (!keep_decoded)
    asset_cache_clear_decoded (cache);
  else
    g_hash_table_remove_all (cache->failed_textures);
  g_clear_pointer (&cache->presentation, pp_presentation_free);
  cache->presentation = pp_presentation_ref (presentation);
}

static void
asset_cache_attach_stage (PpAssetCache *cache,
                          PpStage      *stage)
{
  if (g_ptr_array_find (cache->stages, stage, NULL))
    return;
  g_ptr_array_add (cache->stages, stage);
}

static void
asset_cache_detach_stage (PpAssetCache *cache,
                          PpStage      *stage)
{
  g_ptr_array_remove (cache->stages, stage);
  if (cache->stages->len == 0)
    asset_cache_cancel_loads (cache);
}

static PpResolvedAsset *
resolve_asset (PpStage              *self,
               const PpPresentation *presentation,
               const char           *asset)
{
  PpResolvedAsset *resolved = g_hash_table_lookup (self->assets->resolved,
                                                    asset);

  if (resolved != NULL)
    return resolved;

  resolved = g_new0 (PpResolvedAsset, 1);
  resolved->file = pp_render_resolve_asset (presentation, asset);
  if (resolved->file == NULL)
    {
      g_free (resolved);
      return NULL;
    }
  resolved->uri = g_file_get_uri (resolved->file);
  g_hash_table_insert (self->assets->resolved, g_strdup (asset), resolved);
  return resolved;
}

static void
trim_texture_cache (PpAssetCache    *cache,
                    PpCachedTexture *keep)
{
  while (cache->texture_bytes > cache->texture_budget)
    {
      GHashTableIter iter;
      gpointer key;
      gpointer value;
      gpointer oldest_key = NULL;
      guint64 oldest_use = G_MAXUINT64;

      g_hash_table_iter_init (&iter, cache->textures);
      while (g_hash_table_iter_next (&iter, &key, &value))
        {
          PpCachedTexture *candidate = value;

          if (candidate != keep && candidate->last_used < oldest_use)
            {
              oldest_key = key;
              oldest_use = candidate->last_used;
            }
        }
      if (oldest_key == NULL)
        break;
      {
        PpCachedTexture *oldest = g_hash_table_lookup (cache->textures,
                                                       oldest_key);

        cache->texture_bytes -= oldest->bytes;
      }
      g_hash_table_remove (cache->textures, oldest_key);
    }
}

static void
remove_cached_texture (PpAssetCache *cache,
                       const char   *uri)
{
  PpCachedTexture *cached = g_hash_table_lookup (cache->textures, uri);

  if (cached == NULL)
    return;
  cache->texture_bytes -= cached->bytes;
  g_hash_table_remove (cache->textures, uri);
}

static guint64
texture_decoded_bytes (GdkTexture *texture)
{
  guint64 width = (guint64) gdk_texture_get_width (texture);
  guint64 height = (guint64) gdk_texture_get_height (texture);

  if (width > G_MAXUINT64 / MAX (height, 1) / 4)
    return G_MAXUINT64;
  return width * height * 4;
}

static void
queue_asset_redraws (PpAssetCache *cache)
{
  for (guint i = 0; i < cache->stages->len; i++)
    gtk_widget_queue_draw (GTK_WIDGET (g_ptr_array_index (cache->stages, i)));
}

static void
texture_task_free (PpTextureTask *task)
{
  if (task == NULL)
    return;
  asset_cache_unref (task->cache);
  g_clear_object (&task->file);
  g_free (task->uri);
  g_free (task);
}

static void
load_texture_thread (GTask        *task,
                     gpointer      source_object,
                     gpointer      task_data,
                     GCancellable *cancellable)
{
  PpTextureTask *load = task_data;
  g_autoptr (GError) error = NULL;
  GdkTexture *texture;

  (void) source_object;
  (void) cancellable;
  if (g_task_return_error_if_cancelled (task))
    return;
  texture = gdk_texture_new_from_file (load->file, &error);
  if (texture == NULL)
    {
      g_task_return_error (task, g_steal_pointer (&error));
      return;
    }
  if (g_task_return_error_if_cancelled (task))
    {
      g_object_unref (texture);
      return;
    }
  g_task_return_pointer (task, texture, g_object_unref);
}

static void
texture_loaded_cb (GObject      *source_object,
                   GAsyncResult *result,
                   gpointer      user_data)
{
  GTask *task = G_TASK (result);
  PpTextureTask *load = g_task_get_task_data (task);
  PpAssetCache *cache = load->cache;
  PpPendingTexture *pending;
  g_autoptr (GError) error = NULL;
  GdkTexture *texture;

  (void) source_object;
  (void) user_data;
  texture = g_task_propagate_pointer (task, &error);
  pending = g_hash_table_lookup (cache->pending_textures, load->uri);
  if (pending == NULL || pending->token != load->token)
    {
      g_clear_object (&texture);
      return;
    }
  if (texture == NULL)
    {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        {
          g_warning ("Unable to load background %s: %s",
                     load->uri,
                     error->message);
          g_hash_table_add (cache->failed_textures, g_strdup (load->uri));
        }
      g_hash_table_remove (cache->pending_textures, load->uri);
      return;
    }
  {
    PpCachedTexture *cached = g_new0 (PpCachedTexture, 1);

    cached->texture = texture;
    cached->last_used = pending->last_used;
    cached->bytes = texture_decoded_bytes (texture);
    cache->texture_bytes += cached->bytes;
    g_hash_table_insert (cache->textures, g_strdup (load->uri), cached);
    g_hash_table_remove (cache->pending_textures, load->uri);
    trim_texture_cache (cache, cached);
  }
  queue_asset_redraws (cache);
}

static GdkTexture *
request_texture (PpStage              *self,
                 const PpPresentation *presentation,
                 const char           *asset,
                 int                   priority)
{
  PpResolvedAsset *resolved = resolve_asset (self, presentation, asset);
  PpCachedTexture *cached;
  PpPendingTexture *pending;
  PpTextureTask *load;
  GTask *task;

  if (resolved == NULL)
    return NULL;
  cached = g_hash_table_lookup (self->assets->textures, resolved->uri);
  if (cached != NULL)
    {
      cached->last_used = ++self->assets->use_serial;
      return cached->texture;
    }
  if (g_hash_table_contains (self->assets->failed_textures, resolved->uri))
    return NULL;
  pending = g_hash_table_lookup (self->assets->pending_textures, resolved->uri);
  if (pending != NULL)
    {
      if (priority <= G_PRIORITY_DEFAULT)
        pending->last_used = ++self->assets->use_serial;
      return NULL;
    }

  pending = g_new0 (PpPendingTexture, 1);
  pending->token = ++self->assets->load_serial;
  pending->last_used = ++self->assets->use_serial;
  g_hash_table_insert (self->assets->pending_textures,
                       g_strdup (resolved->uri),
                       pending);
  load = g_new0 (PpTextureTask, 1);
  load->cache = asset_cache_ref (self->assets);
  load->file = g_object_ref (resolved->file);
  load->uri = g_strdup (resolved->uri);
  load->token = pending->token;
  task = g_task_new (NULL,
                     self->assets->load_cancellable,
                     texture_loaded_cb,
                     NULL);
  g_task_set_task_data (task, load, (GDestroyNotify) texture_task_free);
  g_task_set_priority (task, priority);
  g_task_set_return_on_cancel (task, TRUE);
  g_task_run_in_thread (task, load_texture_thread);
  g_object_unref (task);
  return NULL;
}

static gboolean
presentations_share_file (const PpPresentation *a,
                          const PpPresentation *b)
{
  GFile *a_file;
  GFile *b_file;

  if (a == NULL || b == NULL)
    return FALSE;
  a_file = pp_presentation_get_file (a);
  b_file = pp_presentation_get_file (b);
  return a_file != NULL && b_file != NULL && g_file_equal (a_file, b_file);
}

static char *
slide_accessible_text (const PpSlide *slide)
{
  char *text = NULL;

  if (slide->text == NULL || slide->text[0] == '\0')
    return NULL;
  if (slide->use_markup &&
      pango_parse_markup (slide->text, -1, 0, NULL, &text, NULL, NULL))
    return g_strstrip (text);
  g_clear_pointer (&text, g_free);
  return g_strstrip (g_strdup (slide->text));
}

static void
update_accessibility (PpStage *self)
{
  const char *context = self->accessible_context != NULL
    ? self->accessible_context
    : "Presentation slide";
  g_autofree char *label = NULL;
  g_autofree char *description = NULL;

  if (self->presentation == NULL)
    {
      label = g_strdup (context);
      description = g_strdup ("No presentation loaded");
    }
  else
    {
      const PpSlide *slide = pp_presentation_get_slide (self->presentation,
                                                        self->current_slide);
      guint count = pp_presentation_get_n_slides (self->presentation);

      label = g_strdup_printf ("%s %u of %u",
                               context,
                               self->current_slide + 1,
                               count);
      description = self->blank
        ? g_strdup ("Blank screen")
        : slide_accessible_text (slide);
      if (description == NULL || description[0] == '\0')
        {
          g_clear_pointer (&description, g_free);
          description = g_strdup ("Slide has no audience text");
        }
    }

  gtk_accessible_update_property (
    GTK_ACCESSIBLE (self),
    GTK_ACCESSIBLE_PROPERTY_LABEL, label,
    GTK_ACCESSIBLE_PROPERTY_DESCRIPTION, description,
    -1);
}

static void
cached_text_free (PpCachedText *cached)
{
  if (cached == NULL)
    return;
  g_clear_pointer (&cached->node, gsk_render_node_unref);
  g_free (cached);
}

static void
cached_svg_free (PpCachedSvg *cached)
{
  if (cached == NULL)
    return;
  g_free (cached->uri);
  g_clear_pointer (&cached->node, gsk_render_node_unref);
  g_free (cached);
}

static void
report_media_pipeline (PpMedia *media)
{
  g_autoptr (GstCaps) caps = NULL;
  g_autoptr (GString) elements = g_string_new (NULL);
  g_autofree char *caps_text = NULL;
  GstPad *pad;
  GstIterator *iterator;
  GValue item = G_VALUE_INIT;
  gboolean done = FALSE;

  if (media->diagnostics_reported || media->video_sink == NULL)
    return;
  pad = gst_element_get_static_pad (media->video_sink, "sink");
  if (pad != NULL)
    {
      caps = gst_pad_get_current_caps (pad);
      gst_object_unref (pad);
    }
  if (caps == NULL)
    return;

  iterator = gst_bin_iterate_recurse (GST_BIN (media->player));
  while (!done)
    switch (gst_iterator_next (iterator, &item))
      {
      case GST_ITERATOR_OK:
        {
          GstElement *element = g_value_get_object (&item);
          GstElementFactory *factory = gst_element_get_factory (element);

          if (factory != NULL)
            {
              if (elements->len > 0)
                g_string_append (elements, ", ");
              g_string_append (elements,
                               gst_plugin_feature_get_name (
                                 GST_PLUGIN_FEATURE (factory)));
            }
          g_value_reset (&item);
          break;
        }
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (iterator);
        g_string_truncate (elements, 0);
        break;
      case GST_ITERATOR_DONE:
      case GST_ITERATOR_ERROR:
        done = TRUE;
        break;
      }
  if (G_IS_VALUE (&item))
    g_value_unset (&item);
  gst_iterator_free (iterator);

  caps_text = gst_caps_to_string (caps);
  g_log ("pinpoint-media",
         G_LOG_LEVEL_DEBUG,
         "Media pipeline “%s” negotiated %s; selected elements [%s]",
         media->label,
         caps_text,
         elements->str);
  media->diagnostics_reported = TRUE;
}

static void
report_render_backend (PpStage *self)
{
  GtkNative *native;
  GskRenderer *renderer;
  GdkDisplay *display;

  if (self->renderer_reported)
    return;
  native = gtk_widget_get_native (GTK_WIDGET (self));
  renderer = native != NULL ? gtk_native_get_renderer (native) : NULL;
  if (renderer == NULL)
    return;
  if (!g_str_equal (G_OBJECT_TYPE_NAME (renderer), "GskGLRenderer") &&
      !g_str_equal (G_OBJECT_TYPE_NAME (renderer), "GskNglRenderer") &&
      !g_str_equal (G_OBJECT_TYPE_NAME (renderer), "GskVulkanRenderer"))
    g_error ("Pinpoint requires an OpenGL or Vulkan GSK renderer; got %s",
             G_OBJECT_TYPE_NAME (renderer));
  display = gtk_widget_get_display (GTK_WIDGET (self));
  g_log ("pinpoint-media",
         G_LOG_LEVEL_DEBUG,
         "Display backend %s; GSK renderer %s; scale factor %d",
         G_OBJECT_TYPE_NAME (display),
         G_OBJECT_TYPE_NAME (renderer),
         gtk_widget_get_scale_factor (GTK_WIDGET (self)));
  self->renderer_reported = TRUE;
}

static gboolean
media_bus_cb (GstBus     *bus,
              GstMessage *message,
              gpointer    user_data)
{
  PpMedia *media = user_data;

  (void) bus;
  if (GST_MESSAGE_TYPE (message) == GST_MESSAGE_ERROR)
    {
      g_autoptr (GError) error = NULL;
      g_autofree char *debug = NULL;

      gst_message_parse_error (message, &error, &debug);
      if (!media->failed)
        g_warning ("Unable to play video background: %s", error->message);
      media->failed = TRUE;
      if (GST_IS_ELEMENT (media->player))
        gst_element_set_state (media->player, GST_STATE_NULL);
      media->playing = FALSE;
      media->bus_watch_id = 0;
      return G_SOURCE_REMOVE;
    }
  if (GST_MESSAGE_TYPE (message) == GST_MESSAGE_ASYNC_DONE)
    report_media_pipeline (media);
  else if (GST_MESSAGE_TYPE (message) == GST_MESSAGE_STATE_CHANGED &&
           GST_MESSAGE_SRC (message) == GST_OBJECT (media->player))
    {
      GstState new_state;

      gst_message_parse_state_changed (message, NULL, &new_state, NULL);
      if (new_state >= GST_STATE_PAUSED)
        report_media_pipeline (media);
    }

  return G_SOURCE_CONTINUE;
}

static void
media_free (PpMedia *media)
{
  if (media == NULL)
    return;

  if (media->bus_watch_id != 0 &&
      g_main_context_find_source_by_id (NULL, media->bus_watch_id) != NULL)
    g_source_remove (media->bus_watch_id);
  if (GST_IS_ELEMENT (media->player))
    {
      gst_element_set_state (media->player, GST_STATE_NULL);
      gst_element_get_state (media->player,
                             NULL,
                             NULL,
                             2 * GST_SECOND);
    }
  g_clear_object (&media->paintable);
  gst_clear_object (&media->video_sink);
  gst_clear_object (&media->player);
  g_clear_pointer (&media->label, g_free);
  if (media->fd >= 0)
    close (media->fd);
  g_free (media);
}

static void
disable_media_offload (PpStage *self)
{
  if (self->media_offload == NULL)
    return;

  gtk_widget_set_visible (self->media_offload, FALSE);
  gtk_picture_set_paintable (self->media_picture, NULL);
  self->offloaded_media = NULL;
  self->offloaded_slide = NULL;
  self->media_offload_allocated = FALSE;
}

static gboolean
configure_media_offload (PpStage       *self,
                         PpMedia       *media,
                         const PpSlide *slide)
{
  if (self->transitioning || media == NULL || media->paintable == NULL)
    return FALSE;

  if (self->offloaded_media != media || self->offloaded_slide != slide)
    {
      self->offloaded_media = media;
      self->offloaded_slide = slide;
      self->media_offload_allocated = FALSE;
      gtk_picture_set_paintable (self->media_picture, media->paintable);
      gtk_widget_set_visible (self->media_offload, TRUE);
      gtk_widget_queue_allocate (GTK_WIDGET (self));
    }

  if (!media->offload_reported)
    {
      g_log ("pinpoint-media",
             G_LOG_LEVEL_DEBUG,
             "Media background “%s” is configured for GtkGraphicsOffload; "
             "GTK will fall back to GSK when direct compositor offload is "
             "not possible",
             media->label);
      media->offload_reported = TRUE;
    }

  return self->media_offload_allocated;
}

static void
stop_all_media (PpStage *self)
{
  GHashTableIter iter;
  gpointer value;

  disable_media_offload (self);
  g_hash_table_iter_init (&iter, self->media);
  while (g_hash_table_iter_next (&iter, NULL, &value))
    {
      PpMedia *media = value;

      if (media->playing && GST_IS_ELEMENT (media->player))
        {
          gst_element_set_state (media->player, GST_STATE_NULL);
          media->playing = FALSE;
        }
    }
}

static LayerState
layer_state_identity (void)
{
  return (LayerState) {
    .scale_x = 1.0f,
    .scale_y = 1.0f,
    .opacity = 1.0f,
  };
}

static TransitionState
transition_state_identity (void)
{
  LayerState identity = layer_state_identity ();

  return (TransitionState) {
    .actor = identity,
    .background = identity,
    .midground = identity,
    .foreground = identity,
  };
}

static PpLegacyTransition *
lookup_legacy_transition (PpStage    *self,
                          const char *name)
{
  g_autoptr (GError) error = NULL;
  PpLegacyTransition *transition;

  transition = g_hash_table_lookup (self->legacy_transitions, name);
  if (transition != NULL)
    return transition;
  if (g_hash_table_contains (self->failed_transitions, name))
    return NULL;

  transition = pp_legacy_transition_load (self->presentation, name, &error);
  if (transition == NULL)
    {
      g_warning ("Unable to load legacy transition “%s”: %s; using fade",
                 name,
                 error->message);
      g_hash_table_add (self->failed_transitions, g_strdup (name));
      return NULL;
    }
  g_hash_table_insert (self->legacy_transitions,
                       g_strdup (name),
                       transition);
  return transition;
}

static guint
transition_duration (PpStage       *self,
                     const PpSlide *slide,
                     gboolean       incoming,
                     gboolean       backwards)
{
  PpLegacyTransition *transition;
  const char *name = slide->transition;

  if (name == NULL || *name == '\0')
    return 0;
  if ((incoming && slide->transition_mode == PP_TRANSITION_MODE_OUT) ||
      (!incoming && slide->transition_mode == PP_TRANSITION_MODE_IN))
    return 0;
  if (slide->transition_duration_ms > 0)
    return slide->transition_duration_ms;
  if (g_str_equal (name, "fade") ||
      g_str_equal (name, "spin") ||
      g_str_equal (name, "spin-bg") ||
      g_str_equal (name, "spin-text"))
    return 800;
  if (g_str_equal (name, "page-curl"))
    return 2000;
  if (pp_transition_is_builtin (name))
    return 1000;

  transition = lookup_legacy_transition (self, name);
  if (transition != NULL)
    return pp_legacy_transition_get_duration (transition,
                                              incoming,
                                              backwards);
  return 1000;
}

static gboolean
transition_side_enabled (const PpSlide *slide,
                         gboolean       incoming)
{
  return !((incoming && slide->transition_mode == PP_TRANSITION_MODE_OUT) ||
           (!incoming && slide->transition_mode == PP_TRANSITION_MODE_IN));
}

static guint
transition_target_layers (TransitionState   *state,
                          PpTransitionLayer  target,
                          LayerState        *layers[2])
{
  if (target == PP_TRANSITION_LAYER_BACKGROUND)
    {
      layers[0] = &state->background;
      return 1;
    }
  if (target == PP_TRANSITION_LAYER_TEXT)
    {
      layers[0] = &state->midground;
      layers[1] = &state->foreground;
      return 2;
    }

  layers[0] = &state->actor;
  return 1;
}

static void
set_spin_state (LayerState *state,
                gboolean    incoming,
                double      progress,
                float       direction)
{
  if (incoming)
    {
      state->angle = (float) (-360.0 * direction * (1.0 - progress));
      state->scale_x = state->scale_y = (float) (0.01 + 0.99 * progress);
      state->opacity = (float) progress;
    }
  else
    {
      state->angle = (float) (360.0 * direction * progress);
      state->scale_x = state->scale_y = (float) (1.0 + 3.0 * progress);
      state->opacity = (float) (1.0 - progress);
    }
}

static TransitionState
calculate_transition (PpStage       *self,
                      const PpSlide *slide,
                      gboolean       incoming,
                      gboolean       backwards,
                      double         progress)
{
  TransitionState state = transition_state_identity ();
  LayerState *targets[2] = { NULL, NULL };
  const char *name = slide->transition;
  guint n_targets;
  float direction = backwards ? -1.0f : 1.0f;

  if (name == NULL || *name == '\0' ||
      !transition_side_enabled (slide, incoming))
    return state;

  if (!pp_transition_is_builtin (name))
    {
      PpLegacyTransition *transition = lookup_legacy_transition (self, name);

      if (transition != NULL)
        {
          pp_legacy_transition_calculate (transition,
                                          incoming,
                                          backwards,
                                          progress,
                                          &state);
          return state;
        }
      state.actor.opacity = incoming ? (float) progress
                                     : (float) (1.0 - progress);
      return state;
    }

  progress = pp_transition_apply_easing (slide->transition_easing,
                                         CLAMP (progress, 0.0, 1.0));
  n_targets = transition_target_layers (&state,
                                        slide->transition_layer,
                                        targets);

  if (g_str_equal (name, "fade"))
    {
      for (guint i = 0; i < n_targets; i++)
        targets[i]->opacity = incoming ? (float) progress
                                       : (float) (1.0 - progress);
    }
  else if (g_str_equal (name, "slide"))
    {
      float axis_direction = direction;
      float offset;

      if (slide->transition_direction == PP_TRANSITION_DIRECTION_RIGHT ||
          slide->transition_direction == PP_TRANSITION_DIRECTION_DOWN)
        axis_direction *= -1.0f;
      offset = incoming
        ? axis_direction * (float) ((1.0 - progress) * 1024.0)
        : -axis_direction * (float) (progress * 1024.0);
      for (guint i = 0; i < n_targets; i++)
        {
          if (slide->transition_direction == PP_TRANSITION_DIRECTION_LEFT ||
              slide->transition_direction == PP_TRANSITION_DIRECTION_RIGHT)
            targets[i]->x = offset;
          else
            targets[i]->y = offset;
        }
    }
  else if (g_str_equal (name, "zoom") || g_str_equal (name, "scale"))
    {
      float scale = incoming
        ? (float) (0.8 + 0.2 * progress)
        : (float) (1.0 + 0.2 * progress);
      float opacity = incoming ? (float) progress
                               : (float) (1.0 - progress);

      for (guint i = 0; i < n_targets; i++)
        {
          targets[i]->scale_x = targets[i]->scale_y = scale;
          targets[i]->opacity = opacity;
        }
    }
  else if (g_str_equal (name, "flip"))
    {
      float axis_direction = direction;
      float angle;
      float opacity = incoming ? (float) progress
                               : (float) (1.0 - progress);

      if (slide->transition_direction == PP_TRANSITION_DIRECTION_RIGHT ||
          slide->transition_direction == PP_TRANSITION_DIRECTION_DOWN)
        axis_direction *= -1.0f;
      angle = incoming
        ? axis_direction * (float) (90.0 * (1.0 - progress))
        : -axis_direction * (float) (90.0 * progress);
      for (guint i = 0; i < n_targets; i++)
        {
          if (slide->transition_direction == PP_TRANSITION_DIRECTION_LEFT ||
              slide->transition_direction == PP_TRANSITION_DIRECTION_RIGHT)
            targets[i]->angle_y = angle;
          else
            targets[i]->angle_x = angle;
          targets[i]->opacity = opacity;
        }
    }
  else if (g_str_equal (name, "spin") &&
           slide->transition_layer != PP_TRANSITION_LAYER_DEFAULT)
    {
      float spin_direction = direction;

      if (slide->transition_direction == PP_TRANSITION_DIRECTION_RIGHT ||
          slide->transition_direction == PP_TRANSITION_DIRECTION_DOWN)
        spin_direction *= -1.0f;
      for (guint i = 0; i < n_targets; i++)
        set_spin_state (targets[i], incoming, progress, spin_direction);
    }
  else if (g_str_has_prefix (name, "page-curl"))
    {
      state.actor.opacity = incoming ? (float) progress : (float) (1.0 - progress);
    }
  else if (g_str_equal (name, "slide-left") || g_str_equal (name, "action"))
    {
      state.actor.x = incoming
        ? direction * (float) ((1.0 - progress) * 1024.0)
        : -direction * (float) (progress * 1024.0);
    }
  else if (g_str_equal (name, "slide-up"))
    {
      state.actor.y = incoming
        ? direction * (float) ((1.0 - progress) * 1024.0)
        : -direction * (float) (progress * 1024.0);
    }
  else if (g_str_equal (name, "slide-in-left"))
    {
      if (incoming)
        state.actor.x = direction * (float) ((1.0 - progress) * 1024.0);
      state.actor.opacity = incoming ? (float) progress : (float) (1.0 - progress);
    }
  else if (g_str_equal (name, "text-slide-left"))
    {
      float offset = incoming
        ? direction * (float) ((1.0 - progress) * 1024.0)
        : -direction * (float) (progress * 1024.0);
      float opacity = incoming ? (float) progress : (float) (1.0 - progress);

      state.foreground.x = offset;
      state.midground.x = offset;
      state.midground.opacity = opacity;
      state.background.opacity = opacity;
    }
  else if (g_str_equal (name, "text-slide-up") ||
           g_str_equal (name, "text-slide-down"))
    {
      float transition_direction = g_str_equal (name, "text-slide-down") ? -1.0f : 1.0f;
      float offset = incoming
        ? transition_direction * direction * (float) ((1.0 - progress) * 1024.0)
        : -transition_direction * direction * (float) (progress * 1024.0);
      float opacity = incoming ? (float) progress : (float) (1.0 - progress);

      state.foreground.y = offset;
      state.midground.y = offset;
      state.background.opacity = opacity;
    }
  else if (g_str_equal (name, "spin-bg"))
    {
      set_spin_state (&state.actor, incoming, progress, 1.0f);
    }
  else if (g_str_equal (name, "spin"))
    {
      set_spin_state (&state.background, incoming, progress, 1.0f);
      state.actor.opacity = incoming ? (float) progress : (float) (1.0 - progress);
    }
  else if (g_str_equal (name, "spin-text"))
    {
      set_spin_state (&state.foreground, incoming, progress, 1.0f);
      state.midground = state.foreground;
      state.actor.opacity = incoming ? (float) progress : (float) (1.0 - progress);
    }
  else if (g_str_equal (name, "sheet") || g_str_equal (name, "swing"))
    {
      state.actor.opacity = incoming ? (float) progress : (float) (1.0 - progress);
      if (g_str_equal (name, "sheet"))
        {
          if (!backwards)
            {
              state.actor.angle_x = incoming
                ? (float) (90.0 * (1.0 - progress))
                : 0.0f;
              state.actor.y = incoming ? 0.0f : (float) (progress * 1024.0);
            }
          else
            {
              state.actor.angle_x = incoming ? 0.0f : (float) (90.0 * progress);
              state.actor.y = incoming
                ? (float) ((1.0 - progress) * 1024.0)
                : 0.0f;
            }
        }
      else if (!backwards)
        {
          state.actor.angle_x = incoming
            ? (float) (90.0 * (1.0 - progress))
            : (float) (-90.0 * progress);
        }
      else
        {
          state.actor.angle_x = incoming
            ? (float) (-90.0 * (1.0 - progress))
            : (float) (90.0 * progress);
        }
    }
  return state;
}

static void
snapshot_layer_begin (GtkSnapshot       *snapshot,
                      const LayerState  *state,
                      float              width,
                      float              height)
{
  graphene_point_t point;
  graphene_vec3_t x_axis;
  graphene_vec3_t y_axis;

  gtk_snapshot_save (snapshot);
  graphene_point_init (&point, state->x, state->y);
  gtk_snapshot_translate (snapshot, &point);

  graphene_point_init (&point, width / 2.0f, height / 2.0f);
  gtk_snapshot_translate (snapshot, &point);
  if (state->angle_x != 0.0f || state->angle_y != 0.0f)
    gtk_snapshot_perspective (snapshot, MAX (width, height) * 2.0f);
  if (state->angle_x != 0.0f)
    {
      graphene_vec3_init (&x_axis, 1.0f, 0.0f, 0.0f);
      gtk_snapshot_rotate_3d (snapshot, state->angle_x, &x_axis);
    }
  if (state->angle_y != 0.0f)
    {
      graphene_vec3_init (&y_axis, 0.0f, 1.0f, 0.0f);
      gtk_snapshot_rotate_3d (snapshot, state->angle_y, &y_axis);
    }
  gtk_snapshot_rotate (snapshot, state->angle);
  gtk_snapshot_scale (snapshot, state->scale_x, state->scale_y);
  graphene_point_init (&point, -width / 2.0f, -height / 2.0f);
  gtk_snapshot_translate (snapshot, &point);
  if (state->opacity < 1.0f)
    gtk_snapshot_push_opacity (snapshot, CLAMP (state->opacity, 0.0f, 1.0f));
}

static void
snapshot_layer_end (GtkSnapshot      *snapshot,
                    const LayerState *state)
{
  if (state->opacity < 1.0f)
    gtk_snapshot_pop (snapshot);
  gtk_snapshot_restore (snapshot);
}

static GdkTexture *
load_texture (PpStage              *self,
              const PpPresentation *presentation,
              const char           *asset)
{
  return request_texture (self, presentation, asset, G_PRIORITY_DEFAULT);
}

static void
prefetch_raster_slide (PpStage *self,
                       guint    slide_index,
                       int      priority)
{
  const PpSlide *slide;

  if (self->presentation == NULL ||
      slide_index >= pp_presentation_get_n_slides (self->presentation))
    return;
  slide = pp_presentation_get_slide (self->presentation, slide_index);
  if (slide->background_type == PP_BACKGROUND_IMAGE)
    request_texture (self,
                     self->presentation,
                     slide->background,
                     priority);
}

static void
prefetch_raster_working_set (PpStage *self)
{
  if (self->presentation == NULL)
    return;
  if (self->current_slide > 0)
    prefetch_raster_slide (self,
                           self->current_slide - 1,
                           G_PRIORITY_DEFAULT_IDLE);
  prefetch_raster_slide (self,
                         self->current_slide + 1,
                         G_PRIORITY_DEFAULT_IDLE);
  prefetch_raster_slide (self,
                         self->current_slide,
                         G_PRIORITY_DEFAULT);
}

static PpMedia *
load_media (PpStage              *self,
            const PpPresentation *presentation,
            const char           *asset)
{
  PpResolvedAsset *resolved = resolve_asset (self, presentation, asset);
  GstElement *sink;
  GstElement *audio_sink = NULL;
  GstBus *bus;
  PpMedia *media;

  if (resolved == NULL)
    return NULL;
  media = g_hash_table_lookup (self->media, resolved->uri);

  if (media != NULL)
    return media;

  sink = gst_element_factory_make ("gtk4paintablesink", NULL);
  if (sink == NULL)
    {
      g_warning ("The gtk4paintablesink GStreamer element is unavailable");
      return NULL;
    }
  if (g_object_is_floating (sink))
    gst_object_ref_sink (sink);

  media = g_new0 (PpMedia, 1);
  media->fd = -1;
  media->label = g_strdup (asset);
  media->player = gst_element_factory_make ("playbin3", NULL);
  if (media->player == NULL)
    media->player = gst_element_factory_make ("playbin", NULL);
  if (media->player == NULL)
    {
      g_warning ("The GStreamer playback element is unavailable");
      gst_object_unref (sink);
      g_free (media);
      return NULL;
    }
  if (g_object_is_floating (media->player))
    gst_object_ref_sink (media->player);

  media->video_sink = gst_object_ref (sink);
  g_object_get (sink, "paintable", &media->paintable, NULL);
  if (!self->audio_enabled)
    {
      audio_sink = gst_element_factory_make ("fakesink", NULL);
      if (audio_sink != NULL)
        {
          if (g_object_is_floating (audio_sink))
            gst_object_ref_sink (audio_sink);
          g_object_set (audio_sink, "sync", FALSE, NULL);
        }
    }
  g_object_set (media->player,
                "uri", resolved->uri,
                "video-sink", sink,
                "audio-sink", audio_sink,
                NULL);
  gst_object_unref (sink);
  if (audio_sink != NULL)
    gst_object_unref (audio_sink);

  if (media->paintable == NULL)
    {
      g_warning ("The GTK 4 video sink did not provide a paintable");
      media_free (media);
      return NULL;
    }

  g_signal_connect_object (media->paintable,
                           "invalidate-contents",
                           G_CALLBACK (gtk_widget_queue_draw),
                           self,
                           G_CONNECT_SWAPPED);
  g_signal_connect_object (media->paintable,
                           "invalidate-size",
                           G_CALLBACK (gtk_widget_queue_draw),
                           self,
                           G_CONNECT_SWAPPED);
  bus = gst_element_get_bus (media->player);
  media->bus_watch_id = gst_bus_add_watch (bus, media_bus_cb, media);
  gst_object_unref (bus);
  g_hash_table_insert (self->media, g_strdup (resolved->uri), media);

  return media;
}

static PpMedia *
create_camera_media (PpStage *self,
                     int      fd)
{
  GstElement *source;
  GstElement *sink;
  GstBus *bus;
  PpMedia *media;

  source = gst_element_factory_make ("pipewiresrc", NULL);
  sink = gst_element_factory_make ("gtk4paintablesink", NULL);
  if (source == NULL || sink == NULL)
    {
      g_warning ("The PipeWire camera GStreamer elements are unavailable");
      if (source != NULL)
        gst_object_unref (source);
      if (sink != NULL)
        gst_object_unref (sink);
      close (fd);
      return NULL;
    }

  media = g_new0 (PpMedia, 1);
  media->fd = fd;
  media->label = g_strdup ("camera");
  media->player = gst_pipeline_new ("pinpoint-camera");
  if (g_object_is_floating (media->player))
    gst_object_ref_sink (media->player);
  g_object_set (source, "fd", fd, NULL);
  media->video_sink = gst_object_ref (sink);
  g_object_get (sink, "paintable", &media->paintable, NULL);
  gst_bin_add_many (GST_BIN (media->player), source, sink, NULL);
  if (!gst_element_link (source, sink))
    {
      g_warning ("Unable to link the PipeWire camera pipeline");
      media_free (media);
      return NULL;
    }

  if (media->paintable == NULL)
    {
      g_warning ("The GTK 4 camera sink did not provide a paintable");
      media_free (media);
      return NULL;
    }

  g_signal_connect_object (media->paintable,
                           "invalidate-contents",
                           G_CALLBACK (gtk_widget_queue_draw),
                           self,
                           G_CONNECT_SWAPPED);
  g_signal_connect_object (media->paintable,
                           "invalidate-size",
                           G_CALLBACK (gtk_widget_queue_draw),
                           self,
                           G_CONNECT_SWAPPED);
  bus = gst_element_get_bus (media->player);
  media->bus_watch_id = gst_bus_add_watch (bus, media_bus_cb, media);
  gst_object_unref (bus);
  g_hash_table_insert (self->media, g_strdup ("camera"), media);
  gtk_widget_queue_draw (GTK_WIDGET (self));
  return media;
}

static void
camera_response_cb (GDBusConnection *connection,
                    const char      *sender_name,
                    const char      *object_path,
                    const char      *interface_name,
                    const char      *signal_name,
                    GVariant        *parameters,
                    gpointer         user_data)
{
  PpStage *self = user_data;
  g_autoptr (GVariant) results = NULL;
  g_autoptr (GVariant) reply = NULL;
  g_autoptr (GUnixFDList) fd_list = NULL;
  g_autoptr (GError) error = NULL;
  guint response;
  int fd_index;
  int fd;

  (void) sender_name;
  (void) object_path;
  (void) interface_name;
  (void) signal_name;

  if (self->camera_response_subscription != 0)
    {
      g_dbus_connection_signal_unsubscribe (connection,
                                             self->camera_response_subscription);
      self->camera_response_subscription = 0;
    }
  self->camera_request_pending = FALSE;
  g_clear_pointer (&self->camera_request_path, g_free);
  g_variant_get (parameters, "(u@a{sv})", &response, &results);
  if (response != 0)
    {
      if (response != 1)
        g_warning ("Camera portal request failed with response %u", response);
      return;
    }

  reply = g_dbus_connection_call_with_unix_fd_list_sync (
    connection,
    PORTAL_BUS_NAME,
    PORTAL_OBJECT_PATH,
    CAMERA_INTERFACE,
    "OpenPipeWireRemote",
    g_variant_new ("(a{sv})", NULL),
    G_VARIANT_TYPE ("(h)"),
    G_DBUS_CALL_FLAGS_NONE,
    -1,
    NULL,
    &fd_list,
    NULL,
    &error);
  if (reply == NULL)
    {
      g_warning ("Unable to open the camera PipeWire remote: %s", error->message);
      return;
    }

  g_variant_get (reply, "(h)", &fd_index);
  fd = g_unix_fd_list_get (fd_list, fd_index, &error);
  if (fd < 0)
    {
      g_warning ("Unable to receive the camera PipeWire descriptor: %s",
                 error->message);
      return;
    }
  create_camera_media (self, fd);
}

static gboolean
request_camera_idle_cb (gpointer user_data)
{
  PpStage *self = user_data;
  GtkRoot *root;
  const PpSlide *slide;
  g_autoptr (GVariant) reply = NULL;
  g_autoptr (GError) error = NULL;
  GVariantBuilder options;
  const char *unique_name;
  g_autofree char *sender = NULL;
  g_autofree char *token = NULL;
  const char *returned_path;

  self->camera_idle_id = 0;
  slide = self->presentation != NULL
    ? pp_presentation_get_slide (self->presentation, self->current_slide)
    : NULL;
  if (!self->media_enabled ||
      slide == NULL ||
      slide->background_type != PP_BACKGROUND_CAMERA)
    {
      self->camera_request_pending = FALSE;
      return G_SOURCE_REMOVE;
    }

  root = gtk_widget_get_root (GTK_WIDGET (self));
  if (GTK_IS_WINDOW (root) && !gtk_window_is_active (GTK_WINDOW (root)))
    {
      self->camera_idle_id = g_timeout_add_full (G_PRIORITY_DEFAULT,
                                                 100,
                                                 request_camera_idle_cb,
                                                 g_object_ref (self),
                                                 g_object_unref);
      return G_SOURCE_REMOVE;
    }

  self->camera_request_attempted = TRUE;
  self->portal_connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
  if (self->portal_connection == NULL)
    {
      g_warning ("Unable to connect to the camera portal: %s", error->message);
      self->camera_request_pending = FALSE;
      return G_SOURCE_REMOVE;
    }

  unique_name = g_dbus_connection_get_unique_name (self->portal_connection);
  sender = g_strdup (unique_name != NULL && unique_name[0] == ':'
                       ? unique_name + 1
                       : "pinpoint");
  for (char *p = sender; *p != '\0'; p++)
    if (!g_ascii_isalnum (*p))
      *p = '_';
  token = g_strdup_printf ("pinpoint_%08x", g_random_int ());
  self->camera_request_path = g_strdup_printf (
    "/org/freedesktop/portal/desktop/request/%s/%s", sender, token);
  self->camera_response_subscription = g_dbus_connection_signal_subscribe (
    self->portal_connection,
    PORTAL_BUS_NAME,
    REQUEST_INTERFACE,
    "Response",
    self->camera_request_path,
    NULL,
    G_DBUS_SIGNAL_FLAGS_NONE,
    camera_response_cb,
    self,
    NULL);

  g_variant_builder_init (&options, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&options, "{sv}", "handle_token",
                         g_variant_new_string (token));
  reply = g_dbus_connection_call_sync (self->portal_connection,
                                       PORTAL_BUS_NAME,
                                       PORTAL_OBJECT_PATH,
                                       CAMERA_INTERFACE,
                                       "AccessCamera",
                                       g_variant_new ("(a{sv})", &options),
                                       G_VARIANT_TYPE ("(o)"),
                                       G_DBUS_CALL_FLAGS_NONE,
                                       -1,
                                       NULL,
                                       &error);
  if (reply == NULL)
    {
      g_warning ("Unable to request camera access: %s", error->message);
      g_dbus_connection_signal_unsubscribe (self->portal_connection,
                                             self->camera_response_subscription);
      self->camera_response_subscription = 0;
      self->camera_request_pending = FALSE;
      g_clear_pointer (&self->camera_request_path, g_free);
      return G_SOURCE_REMOVE;
    }

  g_variant_get (reply, "(&o)", &returned_path);
  if (!g_str_equal (returned_path, self->camera_request_path))
    g_warning ("Camera portal returned an unexpected request path");
  return G_SOURCE_REMOVE;
}

static PpMedia *
load_camera (PpStage *self)
{
  PpMedia *media = g_hash_table_lookup (self->media, "camera");

  if (media == NULL &&
      !self->camera_request_pending &&
      !self->camera_request_attempted)
    {
      self->camera_request_pending = TRUE;
      self->camera_idle_id = g_idle_add_full (G_PRIORITY_DEFAULT_IDLE,
                                              request_camera_idle_cb,
                                              g_object_ref (self),
                                              g_object_unref);
    }
  return media;
}

static GdkRGBA
parse_color (const char    *value,
             const GdkRGBA *fallback)
{
  GdkRGBA color;

  if (value == NULL || !gdk_rgba_parse (&color, value))
    return *fallback;
  return color;
}

static PpCachedSvgSource *
load_svg_source (PpStage              *self,
                 const PpPresentation *presentation,
                 const char           *asset)
{
  PpResolvedAsset *resolved = resolve_asset (self, presentation, asset);
  PpCachedSvgSource *source;
  g_autoptr (GError) error = NULL;

  if (resolved == NULL)
    return NULL;
  source = g_hash_table_lookup (self->assets->svg_sources, resolved->uri);
  if (source != NULL)
    return source;
  source = g_new0 (PpCachedSvgSource, 1);
  source->file = g_object_ref (resolved->file);
  source->handle = rsvg_handle_new_from_gfile_sync (resolved->file,
                                                    RSVG_HANDLE_FLAGS_NONE,
                                                    NULL,
                                                    &error);
  if (source->handle == NULL)
    {
      g_warning ("Unable to load SVG background: %s", error->message);
      cached_svg_source_free (source);
      return NULL;
    }
  if (!rsvg_handle_get_intrinsic_size_in_pixels (source->handle,
                                                 &source->intrinsic_width,
                                                 &source->intrinsic_height) ||
      source->intrinsic_width <= 0.0 || source->intrinsic_height <= 0.0)
    {
      source->intrinsic_width = 0.0;
      source->intrinsic_height = 0.0;
    }
  g_hash_table_insert (self->assets->svg_sources,
                       g_strdup (resolved->uri),
                       source);
  return source;
}

static GskRenderNode *
load_svg_node (PpStage              *self,
               const PpPresentation *presentation,
               const PpSlide        *slide,
               float                 width,
               float                 height)
{
  PpCachedSvg *cached = g_hash_table_lookup (self->svg_nodes, slide);
  PpResolvedAsset *resolved;
  PpCachedSvgSource *source;
  g_autoptr (GError) error = NULL;
  GtkSnapshot *svg_snapshot;
  GskRenderNode *node;
  cairo_t *cr;
  double intrinsic_width = 0.0;
  double intrinsic_height = 0.0;
  PpRect rect;
  graphene_rect_t bounds;
  RsvgRectangle viewport;
  gboolean rendered;

  if (cached != NULL && cached->width == width && cached->height == height)
    return cached->node;

  resolved = resolve_asset (self, presentation, slide->background);
  if (resolved == NULL)
    return NULL;
  source = load_svg_source (self, presentation, slide->background);
  if (source == NULL)
    return NULL;
  intrinsic_width = source->intrinsic_width;
  intrinsic_height = source->intrinsic_height;
  if (intrinsic_width <= 0.0 || intrinsic_height <= 0.0)
    {
      intrinsic_width = width;
      intrinsic_height = height;
    }
  pp_render_get_background_rect (slide,
                                 width,
                                 height,
                                 intrinsic_width,
                                 intrinsic_height,
                                 &rect);
  bounds = GRAPHENE_RECT_INIT (rect.x, rect.y, rect.width, rect.height);
  viewport = (RsvgRectangle) { 0, 0, intrinsic_width, intrinsic_height };
  svg_snapshot = gtk_snapshot_new ();
  cr = gtk_snapshot_append_cairo (svg_snapshot, &bounds);
  cairo_save (cr);
  cairo_translate (cr, rect.x, rect.y);
  cairo_scale (cr,
               rect.width / intrinsic_width,
               rect.height / intrinsic_height);
  rendered = rsvg_handle_render_document (source->handle,
                                          cr,
                                          &viewport,
                                          &error);
  cairo_restore (cr);
  cairo_destroy (cr);
  node = gtk_snapshot_free_to_node (svg_snapshot);
  if (!rendered)
    {
      g_warning ("Unable to render SVG background: %s", error->message);
      g_clear_pointer (&node, gsk_render_node_unref);
      return NULL;
    }

  cached = g_new0 (PpCachedSvg, 1);
  cached->uri = g_strdup (resolved->uri);
  cached->width = width;
  cached->height = height;
  cached->node = node;
  g_hash_table_replace (self->svg_nodes, (gpointer) slide, cached);
  return cached->node;
}

static void
snapshot_background (PpStage              *self,
                     GtkSnapshot          *snapshot,
                     const PpPresentation *presentation,
                     const PpSlide        *slide,
                     gboolean              active,
                     float                 width,
                     float                 height)
{
  static const GdkRGBA black = { 0, 0, 0, 1 };
  GdkRGBA stage_color = parse_color (slide->stage_color, &black);
  graphene_rect_t bounds = GRAPHENE_RECT_INIT (0, 0, width, height);

  gtk_snapshot_append_color (snapshot, &stage_color, &bounds);

  if (slide->background_type == PP_BACKGROUND_COLOR)
    {
      GdkRGBA background = parse_color (slide->background, &stage_color);
      gtk_snapshot_append_color (snapshot, &background, &bounds);
    }
  else if ((slide->background_type == PP_BACKGROUND_VIDEO ||
            slide->background_type == PP_BACKGROUND_CAMERA) &&
           self->media_enabled)
    {
      PpMedia *media = slide->background_type == PP_BACKGROUND_CAMERA
        ? load_camera (self)
        : load_media (self, presentation, slide->background);

      if (media != NULL)
        {
          int intrinsic_width;
          int intrinsic_height;
          PpRect rect;
          graphene_point_t point;

          if (active && !media->playing && !media->failed)
            {
              gst_element_set_state (media->player, GST_STATE_PLAYING);
              media->playing = TRUE;
            }
          else if (!active && media->playing)
            {
              gst_element_set_state (media->player, GST_STATE_NULL);
              media->playing = FALSE;
            }

          intrinsic_width = gdk_paintable_get_intrinsic_width (media->paintable);
          intrinsic_height = gdk_paintable_get_intrinsic_height (media->paintable);
          if (intrinsic_width <= 0 || intrinsic_height <= 0)
            {
              intrinsic_width = (int) width;
              intrinsic_height = (int) height;
            }
          pp_render_get_background_rect (slide,
                                         width,
                                         height,
                                         intrinsic_width,
                                         intrinsic_height,
                                         &rect);
          if (active && configure_media_offload (self, media, slide))
            gtk_widget_snapshot_child (GTK_WIDGET (self),
                                       self->media_offload,
                                       snapshot);
          else
            {
              gtk_snapshot_push_clip (snapshot, &bounds);
              gtk_snapshot_save (snapshot);
              graphene_point_init (&point, rect.x, rect.y);
              gtk_snapshot_translate (snapshot, &point);
              gdk_paintable_snapshot (media->paintable,
                                      GDK_SNAPSHOT (snapshot),
                                      rect.width,
                                      rect.height);
              gtk_snapshot_restore (snapshot);
              gtk_snapshot_pop (snapshot);
            }
        }
    }
  else if (slide->background_type == PP_BACKGROUND_IMAGE)
    {
      GdkTexture *texture = load_texture (self, presentation, slide->background);

      if (texture != NULL)
        {
          PpRect rect;
          graphene_rect_t texture_bounds;
          GskScalingFilter filter;

          pp_render_get_background_rect (slide,
                                         width,
                                         height,
                                         gdk_texture_get_width (texture),
                                         gdk_texture_get_height (texture),
                                         &rect);
          texture_bounds = GRAPHENE_RECT_INIT (rect.x,
                                                rect.y,
                                                rect.width,
                                                rect.height);
          filter = rect.width < gdk_texture_get_width (texture) ||
                   rect.height < gdk_texture_get_height (texture)
            ? GSK_SCALING_FILTER_TRILINEAR
            : GSK_SCALING_FILTER_LINEAR;
          gtk_snapshot_push_clip (snapshot, &bounds);
          gtk_snapshot_append_scaled_texture (snapshot,
                                              texture,
                                              filter,
                                              &texture_bounds);
          gtk_snapshot_pop (snapshot);
        }
    }
  else if (slide->background_type == PP_BACKGROUND_SVG)
    {
      GskRenderNode *node = load_svg_node (self,
                                           presentation,
                                           slide,
                                           width,
                                           height);

      if (node != NULL)
        {
          gtk_snapshot_push_clip (snapshot, &bounds);
          gtk_snapshot_append_node (snapshot, node);
          gtk_snapshot_pop (snapshot);
        }
    }
}

static PangoAlignment
to_pango_alignment (PpTextAlign alignment)
{
  switch (alignment)
    {
    case PP_TEXT_ALIGN_CENTER:
      return PANGO_ALIGN_CENTER;
    case PP_TEXT_ALIGN_RIGHT:
      return PANGO_ALIGN_RIGHT;
    case PP_TEXT_ALIGN_LEFT:
    default:
      return PANGO_ALIGN_LEFT;
    }
}

static PpCachedText *
get_cached_text (PpStage       *self,
                 const PpSlide *slide,
                 float          width,
                 float          height)
{
  static const GdkRGBA white = { 1, 1, 1, 1 };
  PpCachedText *cached = g_hash_table_lookup (self->text_nodes, slide);
  PangoContext *context = gtk_widget_get_pango_context (GTK_WIDGET (self));
  guint pango_serial = pango_context_get_serial (context);
  int scale_factor = gtk_widget_get_scale_factor (GTK_WIDGET (self));
  g_autoptr (PangoLayout) layout = NULL;
  g_autoptr (PangoFontDescription) description = NULL;
  PangoRectangle logical;
  GtkSnapshot *text_snapshot;
  GdkRGBA text_color;
  graphene_point_t point;
  float scale;

  if (cached != NULL && cached->width == width && cached->height == height &&
      cached->pango_serial == pango_serial &&
      cached->scale_factor == scale_factor)
    return cached;

  cached = g_new0 (PpCachedText, 1);
  cached->width = width;
  cached->height = height;
  cached->pango_serial = pango_serial;
  cached->scale_factor = scale_factor;
  layout = gtk_widget_create_pango_layout (GTK_WIDGET (self), NULL);
  description = pango_font_description_from_string (slide->font);
  pango_layout_set_font_description (layout, description);
  pango_layout_set_alignment (layout, to_pango_alignment (slide->text_align));
  if (slide->use_markup)
    pango_layout_set_markup (layout, slide->text, -1);
  else
    pango_layout_set_text (layout, slide->text, -1);

  pango_layout_get_pixel_extents (layout, NULL, &logical);
  pp_render_get_text_rect (slide,
                           width,
                           height,
                           logical.x + logical.width,
                           logical.y + logical.height,
                           &cached->rect,
                           &scale);
  text_color = parse_color (slide->text_color, &white);

  text_snapshot = gtk_snapshot_new ();
  graphene_point_init (&point, cached->rect.x, cached->rect.y);
  gtk_snapshot_translate (text_snapshot, &point);
  gtk_snapshot_scale (text_snapshot, scale, scale);
  gtk_snapshot_append_layout (text_snapshot, layout, &text_color);
  cached->node = gtk_snapshot_free_to_node (text_snapshot);
  g_hash_table_replace (self->text_nodes, (gpointer) slide, cached);
  return cached;
}

static void
snapshot_slide (PpStage              *self,
                GtkSnapshot          *snapshot,
                const PpPresentation *presentation,
                const PpSlide        *slide,
                const TransitionState *transition,
                gboolean              active,
                float                 width,
                float                 height)
{
  static const GdkRGBA black = { 0, 0, 0, 1 };
  PpCachedText *text = NULL;
  PpRect shading_rect;
  GdkRGBA shading_color;
  graphene_rect_t bounds;

  snapshot_layer_begin (snapshot, &transition->actor, width, height);

  snapshot_layer_begin (snapshot, &transition->background, width, height);
  snapshot_background (self, snapshot, presentation, slide, active, width, height);
  snapshot_layer_end (snapshot, &transition->background);

  if (slide->text != NULL && *slide->text != '\0')
    text = get_cached_text (self, slide, width, height);

  if (text != NULL && text->rect.width > 0.0f)
    {
      snapshot_layer_begin (snapshot, &transition->midground, width, height);
      shading_rect = pp_render_get_shading_rect (width, &text->rect);
      shading_color = parse_color (slide->shading_color, &black);
      shading_color.alpha *= CLAMP (slide->shading_opacity, 0.0, 1.0);
      bounds = GRAPHENE_RECT_INIT (shading_rect.x,
                                   shading_rect.y,
                                   shading_rect.width,
                                   shading_rect.height);
      gtk_snapshot_append_color (snapshot, &shading_color, &bounds);
      snapshot_layer_end (snapshot, &transition->midground);
    }

  snapshot_layer_begin (snapshot, &transition->foreground, width, height);
  if (text != NULL && text->node != NULL)
    gtk_snapshot_append_node (snapshot, text->node);
  snapshot_layer_end (snapshot, &transition->foreground);

  snapshot_layer_end (snapshot, &transition->actor);
}

static gboolean
is_page_curl (const char *name)
{
  return g_str_equal (name, "page-curl") ||
         g_str_equal (name, "page-curl-both");
}

static gboolean
uses_page_curl (const PpSlide *slide,
                gboolean       incoming)
{
  return transition_side_enabled (slide, incoming) &&
         is_page_curl (slide->transition);
}

static double
page_curl_period (const char *name,
                  gboolean    incoming,
                  gboolean    backwards,
                  double      progress)
{
  if (!is_page_curl (name))
    return 0.0;
  if (g_str_equal (name, "page-curl-both"))
    return incoming ? 1.0 - progress : progress;
  if (backwards)
    return incoming ? 1.0 - progress : 0.0;
  return incoming ? 0.0 : progress;
}

static GskRenderNode *
snapshot_slide_to_node (PpStage               *self,
                        const PpSlide         *slide,
                        const TransitionState *transition,
                        gboolean               active,
                        float                  width,
                        float                  height,
                        int                    output_scale)
{
  GtkSnapshot *slide_snapshot = gtk_snapshot_new ();

  if (output_scale > 1)
    gtk_snapshot_scale (slide_snapshot, output_scale, output_scale);
  snapshot_slide (self,
                  slide_snapshot,
                  self->presentation,
                  slide,
                  transition,
                  active,
                  width,
                  height);
  return gtk_snapshot_free_to_node (slide_snapshot);
}

static void
snapshot_page_curl_transition (PpStage       *self,
                               GtkSnapshot   *snapshot,
                               const PpSlide *previous,
                               const PpSlide *current,
                               double         progress,
                               float          width,
                               float          height)
{
  double previous_progress = pp_transition_apply_easing (
    previous->transition_easing, progress);
  double current_progress = pp_transition_apply_easing (
    current->transition_easing, progress);
  double previous_period = uses_page_curl (previous, FALSE)
    ? page_curl_period (previous->transition,
                        FALSE,
                        self->backwards,
                        previous_progress)
    : 0.0;
  double current_period = uses_page_curl (current, TRUE)
    ? page_curl_period (current->transition,
                        TRUE,
                        self->backwards,
                        current_progress)
    : 0.0;
  double previous_angle = g_str_equal (previous->transition, "page-curl-both")
    ? 15.0
    : 0.0;
  double current_angle = g_str_equal (current->transition, "page-curl-both")
    ? 15.0
    : 0.0;
  int output_scale = gtk_widget_get_scale_factor (GTK_WIDGET (self));

  if (self->curl_previous_texture == NULL ||
      self->curl_current_texture == NULL ||
      self->curl_texture_width != width ||
      self->curl_texture_height != height ||
      self->curl_texture_scale != output_scale)
    {
      GtkNative *native = gtk_widget_get_native (GTK_WIDGET (self));
      GskRenderer *renderer = native != NULL
        ? gtk_native_get_renderer (native)
        : NULL;
      TransitionState identity = transition_state_identity ();
      graphene_rect_t viewport = GRAPHENE_RECT_INIT (0,
                                                      0,
                                                      width * output_scale,
                                                      height * output_scale);
      GskRenderNode *previous_node;
      GskRenderNode *current_node;

      if (renderer == NULL)
        return;

      g_clear_object (&self->curl_previous_texture);
      g_clear_object (&self->curl_current_texture);
      previous_node = snapshot_slide_to_node (self,
                                              previous,
                                              &identity,
                                              FALSE,
                                              width,
                                              height,
                                              output_scale);
      current_node = snapshot_slide_to_node (self,
                                             current,
                                             &identity,
                                             TRUE,
                                             width,
                                             height,
                                             output_scale);
      self->curl_previous_texture = gsk_renderer_render_texture (renderer,
                                                                 previous_node,
                                                                 &viewport);
      self->curl_current_texture = gsk_renderer_render_texture (renderer,
                                                                current_node,
                                                                &viewport);
      self->curl_texture_width = width;
      self->curl_texture_height = height;
      self->curl_texture_scale = output_scale;
      gsk_render_node_unref (previous_node);
      gsk_render_node_unref (current_node);
    }

  pp_page_curl_view_set_transition (self->curl_view,
                                    self->curl_previous_texture,
                                    self->curl_current_texture,
                                    previous_period,
                                    previous_angle,
                                    current_period,
                                    current_angle,
                                    self->backwards);
  gtk_widget_snapshot_child (GTK_WIDGET (self),
                             GTK_WIDGET (self->curl_view),
                             snapshot);
}

static void
clear_page_curl (PpStage *self)
{
  g_clear_object (&self->curl_previous_texture);
  g_clear_object (&self->curl_current_texture);
  self->curl_texture_width = 0.0f;
  self->curl_texture_height = 0.0f;
  self->curl_texture_scale = 0;
  if (self->curl_view != NULL)
    pp_page_curl_view_clear (self->curl_view);
}

static gboolean
tick_cb (GtkWidget     *widget,
         GdkFrameClock *frame_clock,
         gpointer       user_data)
{
  PpStage *self = PP_STAGE (widget);
  gint64 elapsed;

  (void) user_data;
  elapsed = gdk_frame_clock_get_frame_time (frame_clock) - self->transition_started;
  if (elapsed >= (gint64) self->transition_duration_ms * 1000)
    {
      self->transitioning = FALSE;
      self->tick_id = 0;
      clear_page_curl (self);
      gtk_widget_queue_draw (widget);
      return G_SOURCE_REMOVE;
    }

  gtk_widget_queue_draw (widget);
  return G_SOURCE_CONTINUE;
}

static void
pp_stage_snapshot (GtkWidget   *widget,
                   GtkSnapshot *snapshot)
{
  PpStage *self = PP_STAGE (widget);
  float width = gtk_widget_get_width (widget);
  float height = gtk_widget_get_height (widget);
  static const GdkRGBA black = { 0, 0, 0, 1 };

  report_render_backend (self);

  if (self->blank || self->presentation == NULL)
    {
      graphene_rect_t bounds = GRAPHENE_RECT_INIT (0, 0, width, height);
      gtk_snapshot_append_color (snapshot, &black, &bounds);
      return;
    }

  if (self->transitioning)
    {
      GdkFrameClock *clock = gtk_widget_get_frame_clock (widget);
      gint64 now = clock != NULL ? gdk_frame_clock_get_frame_time (clock) : g_get_monotonic_time ();
      double progress = (double) (now - self->transition_started) /
                        ((double) self->transition_duration_ms * 1000.0);
      const PpSlide *previous = pp_presentation_get_slide (self->presentation,
                                                           self->previous_slide);
      const PpSlide *current = pp_presentation_get_slide (self->presentation,
                                                          self->current_slide);
      TransitionState outgoing;
      TransitionState incoming;

      progress = CLAMP (progress, 0.0, 1.0);
      if (uses_page_curl (previous, FALSE) ||
          uses_page_curl (current, TRUE))
        {
          snapshot_page_curl_transition (self,
                                         snapshot,
                                         previous,
                                         current,
                                         progress,
                                         width,
                                         height);
          return;
        }
      outgoing = calculate_transition (self,
                                       previous,
                                       FALSE,
                                       self->backwards,
                                       progress);
      incoming = calculate_transition (self,
                                       current,
                                       TRUE,
                                       self->backwards,
                                       progress);
      if (!transition_side_enabled (current, TRUE) &&
          transition_side_enabled (previous, FALSE))
        {
          /* An exit-only transition must remain above the new static slide. */
          snapshot_slide (self,
                          snapshot,
                          self->presentation,
                          current,
                          &incoming,
                          TRUE,
                          width,
                          height);
          snapshot_slide (self,
                          snapshot,
                          self->presentation,
                          previous,
                          &outgoing,
                          FALSE,
                          width,
                          height);
        }
      else
        {
          snapshot_slide (self,
                          snapshot,
                          self->presentation,
                          previous,
                          &outgoing,
                          FALSE,
                          width,
                          height);
          snapshot_slide (self,
                          snapshot,
                          self->presentation,
                          current,
                          &incoming,
                          TRUE,
                          width,
                          height);
        }
    }
  else
    {
      TransitionState identity = transition_state_identity ();
      const PpSlide *current = pp_presentation_get_slide (self->presentation,
                                                          self->current_slide);
      snapshot_slide (self,
                      snapshot,
                      self->presentation,
                      current,
                      &identity,
                      TRUE,
                      width,
                      height);
    }
}

static int
align_offload_value (int    value,
                     double surface_scale,
                     int    minimum)
{
  /* Wayland offload requires integral device coordinates. Fractional scales
   * used by GTK have small rational denominators, so a nearby logical value
   * preserves the authored geometry while making the compositor candidate
   * eligible. */
  for (int delta = 0; delta <= 4; delta++)
    {
      int candidates[] = { value - delta, value + delta };

      for (guint i = 0; i < G_N_ELEMENTS (candidates); i++)
        if (candidates[i] >= minimum &&
            fabs (candidates[i] * surface_scale -
                  round (candidates[i] * surface_scale)) < 0.001)
          return candidates[i];
    }

  return MAX (minimum, value);
}

static void
pp_stage_size_allocate (GtkWidget *widget,
                        int        width,
                        int        height,
                        int        baseline)
{
  PpStage *self = PP_STAGE (widget);

  if (gtk_widget_get_visible (self->media_offload) &&
      self->offloaded_media != NULL &&
      self->offloaded_slide != NULL)
    {
      int intrinsic_width = gdk_paintable_get_intrinsic_width (
        self->offloaded_media->paintable);
      int intrinsic_height = gdk_paintable_get_intrinsic_height (
        self->offloaded_media->paintable);
      PpRect rect;
      GtkNative *native = gtk_widget_get_native (widget);
      GdkSurface *surface = native != NULL
        ? gtk_native_get_surface (native)
        : NULL;
      double surface_scale = surface != NULL
        ? gdk_surface_get_scale (surface)
        : 1.0;
      int allocated_x;
      int allocated_y;
      int allocated_width;
      int allocated_height;
      graphene_point_t point;
      GskTransform *transform;

      if (intrinsic_width <= 0 || intrinsic_height <= 0)
        {
          intrinsic_width = width;
          intrinsic_height = height;
        }
      pp_render_get_background_rect (self->offloaded_slide,
                                     width,
                                     height,
                                     intrinsic_width,
                                     intrinsic_height,
                                     &rect);
      allocated_x = (int) roundf (rect.x);
      allocated_y = (int) roundf (rect.y);
      allocated_width = MAX (1, (int) roundf (rect.width));
      allocated_height = MAX (1, (int) roundf (rect.height));
      allocated_x = align_offload_value (allocated_x,
                                         surface_scale,
                                         G_MININT);
      allocated_y = align_offload_value (allocated_y,
                                         surface_scale,
                                         G_MININT);
      allocated_width = align_offload_value (allocated_width,
                                             surface_scale,
                                             1);
      allocated_height = align_offload_value (allocated_height,
                                              surface_scale,
                                              1);
      graphene_point_init (&point, allocated_x, allocated_y);
      transform = gsk_transform_translate (NULL, &point);
      gtk_widget_allocate (self->media_offload,
                           allocated_width,
                           allocated_height,
                           baseline,
                           transform);
      self->media_offload_allocated = TRUE;
    }

  gtk_widget_allocate (GTK_WIDGET (self->curl_view),
                       width,
                       height,
                       baseline,
                       NULL);
}

static void
pp_stage_dispose (GObject *object)
{
  PpStage *self = PP_STAGE (object);

  if (self->tick_id != 0)
    {
      gtk_widget_remove_tick_callback (GTK_WIDGET (self), self->tick_id);
      self->tick_id = 0;
    }
  clear_page_curl (self);
  if (self->camera_idle_id != 0)
    {
      g_source_remove (self->camera_idle_id);
      self->camera_idle_id = 0;
    }
  if (self->camera_response_subscription != 0)
    {
      if (self->camera_request_path != NULL)
        g_dbus_connection_call (self->portal_connection,
                                PORTAL_BUS_NAME,
                                self->camera_request_path,
                                REQUEST_INTERFACE,
                                "Close",
                                NULL,
                                NULL,
                                G_DBUS_CALL_FLAGS_NONE,
                                -1,
                                NULL,
                                NULL,
                                NULL);
      g_dbus_connection_signal_unsubscribe (self->portal_connection,
                                             self->camera_response_subscription);
      self->camera_response_subscription = 0;
    }
  disable_media_offload (self);
  g_clear_pointer (&self->presentation, pp_presentation_free);
  if (self->assets != NULL)
    {
      asset_cache_detach_stage (self->assets, self);
      g_clear_pointer (&self->assets, asset_cache_unref);
    }
  g_clear_pointer (&self->text_nodes, g_hash_table_unref);
  g_clear_pointer (&self->svg_nodes, g_hash_table_unref);
  g_clear_pointer (&self->media, g_hash_table_unref);
  g_clear_pointer (&self->legacy_transitions, g_hash_table_unref);
  g_clear_pointer (&self->failed_transitions, g_hash_table_unref);
  g_clear_object (&self->portal_connection);
  g_clear_pointer (&self->camera_request_path, g_free);
  g_clear_pointer (&self->camera_device, g_free);
  g_clear_pointer (&self->accessible_context, g_free);
  if (self->curl_view != NULL)
    {
      gtk_widget_unparent (GTK_WIDGET (self->curl_view));
      self->curl_view = NULL;
    }
  if (self->media_offload != NULL)
    {
      gtk_widget_unparent (self->media_offload);
      self->media_offload = NULL;
      self->media_picture = NULL;
    }

  G_OBJECT_CLASS (pp_stage_parent_class)->dispose (object);
}

static void
pp_stage_class_init (PpStageClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = pp_stage_dispose;
  widget_class->size_allocate = pp_stage_size_allocate;
  widget_class->snapshot = pp_stage_snapshot;
  gtk_widget_class_set_css_name (widget_class, "pinpoint-stage");
  gtk_widget_class_set_accessible_role (widget_class,
                                        GTK_ACCESSIBLE_ROLE_GROUP);

  signals[SLIDE_CHANGED] =
    g_signal_new ("slide-changed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL,
                  NULL,
                  NULL,
                  G_TYPE_NONE,
                  1,
                  G_TYPE_UINT);
  signals[PRESENTATION_ENDED] =
    g_signal_new ("presentation-ended",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL,
                  NULL,
                  NULL,
                  G_TYPE_NONE,
                  0);
}

static void
pp_stage_init (PpStage *self)
{
  self->assets = asset_cache_new ();
  asset_cache_attach_stage (self->assets, self);
  self->text_nodes = g_hash_table_new_full (g_direct_hash,
                                            g_direct_equal,
                                            NULL,
                                            (GDestroyNotify) cached_text_free);
  self->svg_nodes = g_hash_table_new_full (g_direct_hash,
                                           g_direct_equal,
                                           NULL,
                                           (GDestroyNotify) cached_svg_free);
  self->media = g_hash_table_new_full (g_str_hash,
                                       g_str_equal,
                                       g_free,
                                       (GDestroyNotify) media_free);
  self->legacy_transitions = g_hash_table_new_full (
    g_str_hash,
    g_str_equal,
    g_free,
    (GDestroyNotify) pp_legacy_transition_free);
  self->failed_transitions = g_hash_table_new_full (g_str_hash,
                                                     g_str_equal,
                                                     g_free,
                                                     NULL);
  self->curl_view = PP_PAGE_CURL_VIEW (pp_page_curl_view_new ());
  gtk_widget_set_visible (GTK_WIDGET (self->curl_view), FALSE);
  gtk_widget_set_parent (GTK_WIDGET (self->curl_view), GTK_WIDGET (self));
  self->media_picture = GTK_PICTURE (gtk_picture_new ());
  gtk_picture_set_can_shrink (self->media_picture, TRUE);
  gtk_picture_set_content_fit (self->media_picture, GTK_CONTENT_FIT_FILL);
  self->media_offload = gtk_graphics_offload_new (
    GTK_WIDGET (self->media_picture));
  gtk_widget_set_visible (self->media_offload, FALSE);
  gtk_widget_set_parent (self->media_offload, GTK_WIDGET (self));
  self->media_enabled = TRUE;
  self->audio_enabled = TRUE;
  self->accessible_context = g_strdup ("Presentation slide");
  gtk_widget_set_focusable (GTK_WIDGET (self), TRUE);
  gtk_widget_set_overflow (GTK_WIDGET (self), GTK_OVERFLOW_HIDDEN);
  gtk_accessible_update_property (
    GTK_ACCESSIBLE (self),
    GTK_ACCESSIBLE_PROPERTY_KEY_SHORTCUTS,
    "Left Right Up Down PageUp PageDown Space F11 F1 B H Home Enter Tab Escape Q",
    GTK_ACCESSIBLE_PROPERTY_HELP_TEXT,
    "Use the arrow or page keys to change slide; F11 for fullscreen; F1 for "
    "speaker view; B to blank; H for the first slide; and Escape to quit.",
    -1);
  update_accessibility (self);
}

GtkWidget *
pp_stage_new (void)
{
  return g_object_new (PP_TYPE_STAGE, NULL);
}

void
pp_stage_set_presentation (PpStage        *self,
                           PpPresentation *presentation,
                           guint           initial_slide)
{
  guint count;

  g_return_if_fail (PP_IS_STAGE (self));
  g_return_if_fail (presentation != NULL);

  count = pp_presentation_get_n_slides (presentation);
  g_return_if_fail (count > 0);
  asset_cache_prepare_presentation (self->assets, presentation);

  if (self->tick_id != 0)
    {
      gtk_widget_remove_tick_callback (GTK_WIDGET (self), self->tick_id);
      self->tick_id = 0;
    }
  self->transitioning = FALSE;
  clear_page_curl (self);
  stop_all_media (self);
  g_clear_pointer (&self->presentation, pp_presentation_free);
  self->presentation = presentation;
  self->current_slide = MIN (initial_slide, count - 1);
  self->previous_slide = self->current_slide;
  g_hash_table_remove_all (self->text_nodes);
  g_hash_table_remove_all (self->svg_nodes);
  g_hash_table_remove_all (self->media);
  g_hash_table_remove_all (self->legacy_transitions);
  g_hash_table_remove_all (self->failed_transitions);
  prefetch_raster_working_set (self);
  update_accessibility (self);
  gtk_widget_queue_draw (GTK_WIDGET (self));
  g_signal_emit (self, signals[SLIDE_CHANGED], 0, self->current_slide);
}

const PpPresentation *
pp_stage_get_presentation (PpStage *self)
{
  g_return_val_if_fail (PP_IS_STAGE (self), NULL);
  return self->presentation;
}

guint
pp_stage_get_current_slide (PpStage *self)
{
  g_return_val_if_fail (PP_IS_STAGE (self), 0);
  return self->current_slide;
}

void
pp_stage_set_slide (PpStage *self,
                    guint    slide)
{
  g_return_if_fail (PP_IS_STAGE (self));
  g_return_if_fail (self->presentation != NULL);
  g_return_if_fail (slide < pp_presentation_get_n_slides (self->presentation));

  if (self->current_slide == slide && !self->transitioning)
    return;
  if (self->tick_id != 0)
    {
      gtk_widget_remove_tick_callback (GTK_WIDGET (self), self->tick_id);
      self->tick_id = 0;
    }
  self->transitioning = FALSE;
  clear_page_curl (self);
  stop_all_media (self);
  self->current_slide = slide;
  self->previous_slide = slide;
  prefetch_raster_working_set (self);
  update_accessibility (self);
  gtk_widget_queue_draw (GTK_WIDGET (self));
  g_signal_emit (self, signals[SLIDE_CHANGED], 0, self->current_slide);
}

static gboolean
go_to_slide (PpStage  *self,
             guint     target,
             gboolean  backwards)
{
  const PpSlide *old_slide;
  const PpSlide *new_slide;
  GdkFrameClock *clock;

  if (self->presentation == NULL || target == self->current_slide)
    return FALSE;
  if (target >= pp_presentation_get_n_slides (self->presentation))
    return FALSE;

  self->previous_slide = self->current_slide;
  self->current_slide = target;
  prefetch_raster_working_set (self);
  clear_page_curl (self);
  stop_all_media (self);
  self->backwards = backwards;
  old_slide = pp_presentation_get_slide (self->presentation, self->previous_slide);
  new_slide = pp_presentation_get_slide (self->presentation, self->current_slide);
  self->transition_duration_ms = MAX (
    transition_duration (self,
                         old_slide,
                         FALSE,
                         backwards),
    transition_duration (self,
                         new_slide,
                         TRUE,
                         backwards));
  {
    static gsize reduced_motion_reported;
    GtkSettings *settings = gtk_widget_get_settings (GTK_WIDGET (self));
    GtkReducedMotion reduced_motion = GTK_REDUCED_MOTION_NO_PREFERENCE;
    gboolean animations_enabled = TRUE;

    g_object_get (settings,
                  "gtk-enable-animations", &animations_enabled,
                  "gtk-interface-reduced-motion", &reduced_motion,
                  NULL);
    if (!animations_enabled || reduced_motion == GTK_REDUCED_MOTION_REDUCE)
      {
        self->transition_duration_ms = 0;
        if (g_once_init_enter (&reduced_motion_reported))
          {
            g_message ("Reduced motion is enabled; slide transitions will "
                       "complete immediately");
            g_once_init_leave (&reduced_motion_reported, 1);
          }
      }
  }
  self->transitioning = self->transition_duration_ms > 0;
  update_accessibility (self);
  if (uses_page_curl (old_slide, FALSE) || uses_page_curl (new_slide, TRUE))
    gtk_widget_set_visible (GTK_WIDGET (self->curl_view), TRUE);

  if (self->tick_id != 0)
    gtk_widget_remove_tick_callback (GTK_WIDGET (self), self->tick_id);
  self->tick_id = 0;

  if (self->transitioning)
    {
      clock = gtk_widget_get_frame_clock (GTK_WIDGET (self));
      self->transition_started = clock != NULL
        ? gdk_frame_clock_get_frame_time (clock)
        : g_get_monotonic_time ();
      self->tick_id = gtk_widget_add_tick_callback (GTK_WIDGET (self), tick_cb, NULL, NULL);
    }

  gtk_widget_queue_draw (GTK_WIDGET (self));
  g_signal_emit (self, signals[SLIDE_CHANGED], 0, self->current_slide);
  return TRUE;
}

gboolean
pp_stage_next (PpStage *self)
{
  g_return_val_if_fail (PP_IS_STAGE (self), FALSE);
  if (self->presentation != NULL &&
      self->current_slide + 1 >= pp_presentation_get_n_slides (self->presentation))
    {
      g_signal_emit (self, signals[PRESENTATION_ENDED], 0);
      return FALSE;
    }
  return go_to_slide (self, self->current_slide + 1, FALSE);
}

gboolean
pp_stage_previous (PpStage *self)
{
  g_return_val_if_fail (PP_IS_STAGE (self), FALSE);
  if (self->current_slide == 0)
    return FALSE;
  return go_to_slide (self, self->current_slide - 1, TRUE);
}

void
pp_stage_first (PpStage *self)
{
  g_return_if_fail (PP_IS_STAGE (self));
  go_to_slide (self, 0, TRUE);
}

void
pp_stage_set_blank (PpStage  *self,
                    gboolean  blank)
{
  g_return_if_fail (PP_IS_STAGE (self));
  blank = !!blank;
  if (self->blank == blank)
    return;
  self->blank = blank;
  if (blank)
    disable_media_offload (self);
  update_accessibility (self);
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

void
pp_stage_set_accessible_context (PpStage    *self,
                                 const char *context)
{
  g_return_if_fail (PP_IS_STAGE (self));
  g_return_if_fail (context != NULL && context[0] != '\0');

  if (g_strcmp0 (self->accessible_context, context) == 0)
    return;
  g_free (self->accessible_context);
  self->accessible_context = g_strdup (context);
  update_accessibility (self);
}

gboolean
pp_stage_get_blank (PpStage *self)
{
  g_return_val_if_fail (PP_IS_STAGE (self), FALSE);
  return self->blank;
}

gboolean
pp_stage_is_transitioning (PpStage *self)
{
  g_return_val_if_fail (PP_IS_STAGE (self), FALSE);
  return self->transitioning || self->tick_id != 0;
}

gboolean
pp_stage_is_media_offload_configured (PpStage *self)
{
  g_return_val_if_fail (PP_IS_STAGE (self), FALSE);
  return self->offloaded_media != NULL &&
         self->media_offload_allocated &&
         gtk_widget_get_visible (self->media_offload);
}

void
pp_stage_set_media_enabled (PpStage  *self,
                            gboolean  enabled)
{
  g_return_if_fail (PP_IS_STAGE (self));
  enabled = !!enabled;
  if (self->media_enabled == enabled)
    return;
  self->media_enabled = enabled;
  if (!enabled)
    stop_all_media (self);
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

void
pp_stage_set_audio_enabled (PpStage  *self,
                            gboolean  enabled)
{
  g_return_if_fail (PP_IS_STAGE (self));
  enabled = !!enabled;
  if (self->audio_enabled == enabled)
    return;
  self->audio_enabled = enabled;
  stop_all_media (self);
  g_hash_table_remove_all (self->media);
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

void
pp_stage_set_camera_device (PpStage    *self,
                            const char *device)
{
  g_return_if_fail (PP_IS_STAGE (self));
  g_free (self->camera_device);
  self->camera_device = g_strdup (device);
}

void
pp_stage_share_asset_cache (PpStage *self,
                            PpStage *source)
{
  g_return_if_fail (PP_IS_STAGE (self));
  g_return_if_fail (PP_IS_STAGE (source));
  g_return_if_fail (self != source);

  if (self->assets == source->assets)
    return;
  asset_cache_detach_stage (self->assets, self);
  asset_cache_unref (self->assets);
  self->assets = asset_cache_ref (source->assets);
  asset_cache_attach_stage (self->assets, self);
}

void
pp_stage_get_asset_store_stats (PpStage          *self,
                                PpAssetStoreStats *stats)
{
  g_return_if_fail (PP_IS_STAGE (self));
  g_return_if_fail (stats != NULL);

  *stats = (PpAssetStoreStats) {
    .texture_count = g_hash_table_size (self->assets->textures),
    .pending_texture_count = g_hash_table_size (
      self->assets->pending_textures),
    .texture_bytes = self->assets->texture_bytes,
    .texture_budget = self->assets->texture_budget,
    .svg_source_count = g_hash_table_size (self->assets->svg_sources),
  };
}

void
pp_stage_set_asset_texture_budget (PpStage *self,
                                   guint64  bytes)
{
  g_return_if_fail (PP_IS_STAGE (self));
  g_return_if_fail (bytes > 0);

  self->assets->texture_budget = bytes;
  trim_texture_cache (self->assets, NULL);
}

void
pp_stage_invalidate_asset (PpStage *self,
                           GFile   *file)
{
  GHashTableIter iter;
  gpointer key;
  gpointer value;
  g_autofree char *uri = NULL;

  g_return_if_fail (PP_IS_STAGE (self));
  g_return_if_fail (G_IS_FILE (file));

  uri = g_file_get_uri (file);
  remove_cached_texture (self->assets, uri);
  g_hash_table_remove (self->assets->pending_textures, uri);
  g_hash_table_remove (self->assets->failed_textures, uri);
  g_hash_table_remove (self->assets->svg_sources, uri);
  g_hash_table_remove (self->media, uri);
  g_hash_table_iter_init (&iter, self->svg_nodes);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      PpCachedSvg *cached = value;

      if (g_str_equal (cached->uri, uri))
        g_hash_table_iter_remove (&iter);
    }
  g_hash_table_iter_init (&iter, self->legacy_transitions);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      g_autoptr (GFile) transition_file =
        pp_legacy_transition_resolve_file (self->presentation, key);

      if (transition_file != NULL && g_file_equal (transition_file, file))
        g_hash_table_iter_remove (&iter);
    }
  g_hash_table_iter_init (&iter, self->failed_transitions);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      g_autoptr (GFile) transition_file =
        pp_legacy_transition_resolve_file (self->presentation, key);

      if (transition_file != NULL && g_file_equal (transition_file, file))
        g_hash_table_iter_remove (&iter);
    }
  prefetch_raster_working_set (self);
  gtk_widget_queue_draw (GTK_WIDGET (self));
}
