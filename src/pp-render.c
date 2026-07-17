#include "config.h"

#include "pp-render.h"

gboolean
pp_render_validate_pixel_layout (int      width,
                                 int      height,
                                 int      rowstride,
                                 int      channels,
                                 gsize    available_bytes,
                                 gsize   *row_bytes,
                                 gsize   *required_bytes,
                                 GError **error)
{
  gsize checked_row_bytes;
  gsize preceding_rows;
  gsize checked_required_bytes;

  if (width <= 0 || height <= 0 ||
      (channels != 3 && channels != 4) || rowstride <= 0)
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_INVALID_DATA,
                           "Pixel dimensions, channels, or rowstride are invalid");
      return FALSE;
    }
  if (!g_size_checked_mul (&checked_row_bytes,
                           (gsize) width,
                           (gsize) channels) ||
      checked_row_bytes > (gsize) rowstride ||
      !g_size_checked_mul (&preceding_rows,
                           (gsize) (height - 1),
                           (gsize) rowstride) ||
      !g_size_checked_add (&checked_required_bytes,
                           preceding_rows,
                           checked_row_bytes) ||
      checked_required_bytes > available_bytes)
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_INVALID_DATA,
                           "Pixel row layout exceeds its backing storage");
      return FALSE;
    }

  if (row_bytes != NULL)
    *row_bytes = checked_row_bytes;
  if (required_bytes != NULL)
    *required_bytes = checked_required_bytes;
  return TRUE;
}

void
pp_render_get_background_rect (const PpSlide *slide,
                               float          stage_width,
                               float          stage_height,
                               float          background_width,
                               float          background_height,
                               PpRect        *rect)
{
  float width_scale;
  float height_scale;
  float scale_x;
  float scale_y;

  g_return_if_fail (slide != NULL);
  g_return_if_fail (rect != NULL);

  if (background_width <= 0.0f || background_height <= 0.0f)
    {
      *rect = (PpRect) { 0, 0, 0, 0 };
      return;
    }

  width_scale = stage_width / background_width;
  height_scale = stage_height / background_height;

  switch (slide->background_scale)
    {
    case PP_BACKGROUND_FILL:
      scale_x = scale_y = MAX (width_scale, height_scale);
      break;
    case PP_BACKGROUND_FIT:
      scale_x = scale_y = MIN (width_scale, height_scale);
      break;
    case PP_BACKGROUND_UNSCALED:
      scale_x = scale_y = MIN (MIN (width_scale, height_scale), 1.0f);
      break;
    case PP_BACKGROUND_STRETCH:
      scale_x = width_scale;
      scale_y = height_scale;
      break;
    default:
      g_assert_not_reached ();
    }

  rect->width = background_width * scale_x;
  rect->height = background_height * scale_y;

  switch (slide->background_position)
    {
    case PP_GRAVITY_RIGHT:
    case PP_GRAVITY_TOP_RIGHT:
    case PP_GRAVITY_BOTTOM_RIGHT:
      rect->x = stage_width * 0.95f - rect->width;
      break;
    case PP_GRAVITY_LEFT:
    case PP_GRAVITY_TOP_LEFT:
    case PP_GRAVITY_BOTTOM_LEFT:
      rect->x = stage_width * 0.05f;
      break;
    default:
      rect->x = (stage_width - rect->width) / 2.0f;
      break;
    }

  switch (slide->background_position)
    {
    case PP_GRAVITY_BOTTOM:
    case PP_GRAVITY_BOTTOM_LEFT:
    case PP_GRAVITY_BOTTOM_RIGHT:
      rect->y = stage_height * 0.95f - rect->height;
      break;
    case PP_GRAVITY_TOP:
    case PP_GRAVITY_TOP_LEFT:
    case PP_GRAVITY_TOP_RIGHT:
      rect->y = stage_height * 0.05f;
      break;
    default:
      rect->y = (stage_height - rect->height) / 2.0f;
      break;
    }
}

void
pp_render_get_text_rect (const PpSlide *slide,
                         float          stage_width,
                         float          stage_height,
                         float          text_width,
                         float          text_height,
                         PpRect        *rect,
                         float         *scale)
{
  float text_scale;

  g_return_if_fail (slide != NULL);
  g_return_if_fail (rect != NULL);
  g_return_if_fail (scale != NULL);

  if (text_width <= 0.0f || text_height <= 0.0f)
    {
      *rect = (PpRect) { 0, 0, 0, 0 };
      *scale = 1.0f;
      return;
    }

  text_scale = MIN (stage_width / text_width * 0.8f,
                    stage_height / text_height * 0.8f);
  text_scale = MIN (text_scale, 1.0f);

  rect->width = text_width * text_scale;
  rect->height = text_height * text_scale;

  switch (slide->text_position)
    {
    case PP_GRAVITY_RIGHT:
    case PP_GRAVITY_TOP_RIGHT:
    case PP_GRAVITY_BOTTOM_RIGHT:
      rect->x = stage_width * 0.95f - rect->width;
      break;
    case PP_GRAVITY_LEFT:
    case PP_GRAVITY_TOP_LEFT:
    case PP_GRAVITY_BOTTOM_LEFT:
      rect->x = stage_width * 0.05f;
      break;
    default:
      rect->x = (stage_width - rect->width) / 2.0f;
      break;
    }

  switch (slide->text_position)
    {
    case PP_GRAVITY_BOTTOM:
    case PP_GRAVITY_BOTTOM_LEFT:
    case PP_GRAVITY_BOTTOM_RIGHT:
      rect->y = stage_height * 0.95f - rect->height;
      break;
    case PP_GRAVITY_TOP:
    case PP_GRAVITY_TOP_LEFT:
    case PP_GRAVITY_TOP_RIGHT:
      rect->y = stage_height * 0.05f;
      break;
    default:
      rect->y = (stage_height - rect->height) / 2.0f;
      break;
    }

  *scale = text_scale;
}

PpRect
pp_render_get_shading_rect (float         stage_width,
                            const PpRect *text_rect)
{
  float padding;

  g_return_val_if_fail (text_rect != NULL, ((PpRect) { 0, 0, 0, 0 }));

  padding = stage_width * 0.01f;
  return (PpRect) {
    text_rect->x - padding,
    text_rect->y - padding,
    text_rect->width + padding * 2.0f,
    text_rect->height + padding * 2.0f,
  };
}

GFile *
pp_render_resolve_asset (const PpPresentation *presentation,
                         const char           *asset)
{
  GFile *presentation_file;
  g_autoptr (GFile) parent = NULL;

  g_return_val_if_fail (presentation != NULL, NULL);
  g_return_val_if_fail (asset != NULL, NULL);

  if (g_path_is_absolute (asset))
    return g_file_new_for_path (asset);

  presentation_file = pp_presentation_get_file (presentation);
  if (presentation_file == NULL)
    return g_file_new_for_path (asset);

  parent = g_file_get_parent (presentation_file);
  if (parent == NULL)
    return g_file_new_for_path (asset);

  return g_file_resolve_relative_path (parent, asset);
}

char *
pp_render_find_missing_relative_asset (const PpPresentation *presentation)
{
  guint n_slides;

  g_return_val_if_fail (presentation != NULL, NULL);

  n_slides = pp_presentation_get_n_slides (presentation);
  for (guint i = 0; i < n_slides; i++)
    {
      const PpSlide *slide = pp_presentation_get_slide (presentation, i);
      const char *asset = slide->background;
      g_autofree char *scheme = NULL;
      g_autoptr (GFile) file = NULL;

      if (asset == NULL ||
          (slide->background_type != PP_BACKGROUND_IMAGE &&
           slide->background_type != PP_BACKGROUND_VIDEO &&
           slide->background_type != PP_BACKGROUND_SVG) ||
          g_path_is_absolute (asset))
        continue;

      scheme = g_uri_parse_scheme (asset);
      if (scheme != NULL)
        continue;

      file = pp_render_resolve_asset (presentation, asset);
      if (!g_file_query_exists (file, NULL))
        return g_strdup (asset);
    }

  return NULL;
}
