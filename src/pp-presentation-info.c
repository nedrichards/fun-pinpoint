#include "pp-presentation-info.h"

#include "pp-file-access.h"
#include "pp-presentation.h"

struct _PpPresentationInfo
{
  char *name;
  char *details;
  gboolean presentable;
};

PpPresentationInfo *
pp_presentation_info_new (GFile    *file,
                          gboolean  ignore_comments)
{
  g_autoptr (PpPresentation) presentation = NULL;
  g_autoptr (GError) error = NULL;
  g_autoptr (GString) details = NULL;
  PpPresentationInfo *self;
  guint visual_assets = 0;
  guint videos = 0;
  guint notes = 0;
  guint n_slides;

  g_return_val_if_fail (G_IS_FILE (file), NULL);
  self = g_new0 (PpPresentationInfo, 1);
  self->name = pp_file_access_get_display_path (file);
  presentation = pp_presentation_load (file,
                                       ignore_comments,
                                       NULL,
                                       &error);
  if (presentation == NULL)
    {
      self->details = g_strdup_printf ("Cannot open: %s", error->message);
      return self;
    }

  n_slides = pp_presentation_get_n_slides (presentation);
  details = g_string_new (NULL);
  g_string_append_printf (details,
                          "%u %s",
                          n_slides,
                          n_slides == 1 ? "slide" : "slides");
  for (guint i = 0; i < n_slides; i++)
    {
      const PpSlide *slide = pp_presentation_get_slide (presentation, i);

      if (slide->background_type == PP_BACKGROUND_IMAGE ||
          slide->background_type == PP_BACKGROUND_SVG)
        visual_assets++;
      else if (slide->background_type == PP_BACKGROUND_VIDEO)
        videos++;
      if (slide->speaker_notes != NULL && slide->speaker_notes[0] != '\0')
        notes++;
    }
  if (visual_assets > 0)
    g_string_append_printf (details,
                            " · %u visual %s",
                            visual_assets,
                            visual_assets == 1 ? "asset" : "assets");
  if (videos > 0)
    g_string_append_printf (details,
                            " · %u %s",
                            videos,
                            videos == 1 ? "video" : "videos");
  if (notes > 0)
    g_string_append (details, " · speaker notes");
  self->details = g_string_free (g_steal_pointer (&details), FALSE);
  self->presentable = TRUE;
  return self;
}

void
pp_presentation_info_free (PpPresentationInfo *self)
{
  if (self == NULL)
    return;
  g_free (self->name);
  g_free (self->details);
  g_free (self);
}

const char *
pp_presentation_info_get_name (const PpPresentationInfo *self)
{
  g_return_val_if_fail (self != NULL, NULL);
  return self->name;
}

const char *
pp_presentation_info_get_details (const PpPresentationInfo *self)
{
  g_return_val_if_fail (self != NULL, NULL);
  return self->details;
}

gboolean
pp_presentation_info_is_presentable (const PpPresentationInfo *self)
{
  g_return_val_if_fail (self != NULL, FALSE);
  return self->presentable;
}
