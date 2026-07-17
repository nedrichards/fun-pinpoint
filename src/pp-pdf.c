#include "config.h"

#include "pp-pdf.h"

#include "pp-render.h"
#include "pp-video-thumbnail.h"

#include <cairo-pdf.h>
#include <gdk/gdk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <librsvg/rsvg.h>
#include <pango/pangocairo.h>

#define A4_WIDTH 595.275590551
#define A4_HEIGHT 841.88976378
#define LETTER_WIDTH 612.0
#define LETTER_HEIGHT 792.0

typedef struct
{
  const PpPresentation *presentation;
  cairo_surface_t *surface;
  cairo_t *cr;
  GHashTable *images;
  GHashTable *svgs;
  GHashTable *failed_videos;
  double page_width;
  double page_height;
  double margin;
} PdfRenderer;

static cairo_user_data_key_t pixel_data_key;

static void
destroy_surface (gpointer data)
{
  cairo_surface_destroy (data);
}

static cairo_surface_t *
surface_from_pixbuf (GdkPixbuf  *pixbuf,
                     GError    **error)
{
  int width = gdk_pixbuf_get_width (pixbuf);
  int height = gdk_pixbuf_get_height (pixbuf);
  int source_stride = gdk_pixbuf_get_rowstride (pixbuf);
  int channels = gdk_pixbuf_get_n_channels (pixbuf);
  cairo_format_t format = channels == 4 ? CAIRO_FORMAT_ARGB32 : CAIRO_FORMAT_RGB24;
  int destination_stride = cairo_format_stride_for_width (format, width);
  const guchar *source = gdk_pixbuf_read_pixels (pixbuf);
  gsize destination_size;
  guchar *pixels;
  cairo_surface_t *surface;
  cairo_status_t status;

  if (source == NULL ||
      !pp_render_validate_pixel_layout (width,
                                        height,
                                        source_stride,
                                        channels,
                                        gdk_pixbuf_get_byte_length (pixbuf),
                                        NULL,
                                        NULL,
                                        error) ||
      destination_stride <= 0 ||
      !g_size_checked_mul (&destination_size,
                           (gsize) destination_stride,
                           (gsize) height))
    {
      if (error == NULL || *error == NULL)
        g_set_error_literal (error,
                             G_IO_ERROR,
                             G_IO_ERROR_INVALID_DATA,
                             "The image pixel layout cannot be represented safely");
      return NULL;
    }

  pixels = g_try_malloc0 (destination_size);
  if (pixels == NULL)
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_NO_SPACE,
                           "Unable to allocate the PDF image surface");
      return NULL;
    }
  surface = cairo_image_surface_create_for_data (pixels,
                                                  format,
                                                  width,
                                                  height,
                                                  destination_stride);
  status = cairo_surface_status (surface);
  if (status != CAIRO_STATUS_SUCCESS)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "Unable to create the PDF image surface: %s",
                   cairo_status_to_string (status));
      cairo_surface_destroy (surface);
      g_free (pixels);
      return NULL;
    }

  status = cairo_surface_set_user_data (surface,
                                        &pixel_data_key,
                                        pixels,
                                        g_free);
  if (status != CAIRO_STATUS_SUCCESS)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "Unable to retain the PDF image pixels: %s",
                   cairo_status_to_string (status));
      cairo_surface_destroy (surface);
      g_free (pixels);
      return NULL;
    }

  for (int y = 0; y < height; y++)
    {
      const guchar *p = source + (gsize) y * (gsize) source_stride;
      guchar *q = pixels + (gsize) y * (gsize) destination_stride;

      for (int x = 0; x < width; x++)
        {
          guint alpha = channels == 4 ? p[3] : 255;
          guint red = (p[0] * alpha + 127) / 255;
          guint green = (p[1] * alpha + 127) / 255;
          guint blue = (p[2] * alpha + 127) / 255;

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
          q[0] = blue;
          q[1] = green;
          q[2] = red;
          q[3] = alpha;
#else
          q[0] = alpha;
          q[1] = red;
          q[2] = green;
          q[3] = blue;
#endif
          p += channels;
          q += 4;
        }
    }

  cairo_surface_mark_dirty (surface);
  return surface;
}

static cairo_surface_t *
get_image (PdfRenderer *renderer,
           GFile       *file)
{
  g_autofree char *uri = g_file_get_uri (file);
  g_autofree char *path = NULL;
  g_autoptr (GdkPixbuf) pixbuf = NULL;
  g_autoptr (GError) error = NULL;
  cairo_surface_t *surface = g_hash_table_lookup (renderer->images, uri);

  if (surface != NULL)
    return surface;

  path = g_file_get_path (file);
  if (path == NULL)
    return NULL;
  pixbuf = gdk_pixbuf_new_from_file (path, &error);
  if (pixbuf == NULL)
    {
      g_warning ("Unable to load PDF background: %s", error->message);
      return NULL;
    }

  surface = surface_from_pixbuf (pixbuf, &error);
  if (surface == NULL)
    {
      g_warning ("Unable to prepare PDF background pixels: %s", error->message);
      return NULL;
    }

  if (g_str_has_suffix (path, ".jpg") || g_str_has_suffix (path, ".jpeg"))
    {
      g_autoptr (GError) bytes_error = NULL;
      GBytes *bytes = g_file_load_bytes (file, NULL, NULL, &bytes_error);

      if (bytes != NULL)
        {
          gsize size = 0;
          gconstpointer data = g_bytes_get_data (bytes, &size);
          cairo_status_t mime_status = cairo_surface_set_mime_data (
            surface,
            CAIRO_MIME_TYPE_JPEG,
            data,
            size,
            (cairo_destroy_func_t) g_bytes_unref,
            bytes);

          if (mime_status != CAIRO_STATUS_SUCCESS)
            g_bytes_unref (bytes);
        }
    }
  g_hash_table_insert (renderer->images, g_steal_pointer (&uri), surface);
  return surface;
}

static RsvgHandle *
get_svg (PdfRenderer *renderer,
         GFile       *file)
{
  g_autofree char *uri = g_file_get_uri (file);
  g_autoptr (GError) error = NULL;
  RsvgHandle *handle = g_hash_table_lookup (renderer->svgs, uri);

  if (handle != NULL)
    return handle;

  handle = rsvg_handle_new_from_gfile_sync (file,
                                            RSVG_HANDLE_FLAGS_NONE,
                                            NULL,
                                            &error);
  if (handle == NULL)
    {
      g_warning ("Unable to load SVG background: %s", error->message);
      return NULL;
    }

  g_hash_table_insert (renderer->svgs, g_steal_pointer (&uri), handle);
  return handle;
}

static cairo_surface_t *
get_video_thumbnail (PdfRenderer *renderer,
                     GFile       *file)
{
  g_autofree char *uri = g_file_get_uri (file);
  g_autoptr (GdkPixbuf) pixbuf = NULL;
  g_autoptr (GError) error = NULL;
  cairo_surface_t *surface = g_hash_table_lookup (renderer->images, uri);

  if (surface != NULL)
    return surface;
  if (g_hash_table_contains (renderer->failed_videos, uri))
    return NULL;

  pixbuf = pp_video_thumbnail_new (file, NULL, &error);
  if (pixbuf == NULL)
    {
      g_warning ("Unable to create PDF video thumbnail: %s", error->message);
      g_hash_table_add (renderer->failed_videos, g_steal_pointer (&uri));
      return NULL;
    }

  surface = surface_from_pixbuf (pixbuf, &error);
  if (surface == NULL)
    {
      g_warning ("Unable to prepare PDF video pixels: %s", error->message);
      g_hash_table_add (renderer->failed_videos, g_steal_pointer (&uri));
      return NULL;
    }
  g_hash_table_insert (renderer->images, g_steal_pointer (&uri), surface);
  return surface;
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

static void
set_source_color (cairo_t       *cr,
                  const GdkRGBA *color)
{
  cairo_set_source_rgba (cr, color->red, color->green, color->blue, color->alpha);
}

static void
render_surface_background (PdfRenderer    *renderer,
                           const PpSlide  *slide,
                           cairo_surface_t *surface)
{
  PpRect rect;
  double surface_width = cairo_image_surface_get_width (surface);
  double surface_height = cairo_image_surface_get_height (surface);

  pp_render_get_background_rect (slide,
                                 renderer->page_width,
                                 renderer->page_height,
                                 surface_width,
                                 surface_height,
                                 &rect);
  cairo_save (renderer->cr);
  cairo_rectangle (renderer->cr,
                   0,
                   0,
                   renderer->page_width,
                   renderer->page_height);
  cairo_clip (renderer->cr);
  cairo_translate (renderer->cr, rect.x, rect.y);
  cairo_scale (renderer->cr,
               rect.width / surface_width,
               rect.height / surface_height);
  cairo_set_source_surface (renderer->cr, surface, 0, 0);
  cairo_paint (renderer->cr);
  cairo_restore (renderer->cr);
}

static void
render_background (PdfRenderer  *renderer,
                   const PpSlide *slide)
{
  static const GdkRGBA white = { 1, 1, 1, 1 };
  GdkRGBA stage_color = parse_color (slide->stage_color, &white);

  set_source_color (renderer->cr, &stage_color);
  cairo_paint (renderer->cr);

  if (slide->background_type == PP_BACKGROUND_COLOR)
    {
      GdkRGBA background = parse_color (slide->background, &stage_color);
      set_source_color (renderer->cr, &background);
      cairo_paint (renderer->cr);
    }
  else if (slide->background_type == PP_BACKGROUND_IMAGE)
    {
      g_autoptr (GFile) file = pp_render_resolve_asset (renderer->presentation,
                                                        slide->background);
      cairo_surface_t *image = get_image (renderer, file);

      if (image != NULL)
        render_surface_background (renderer, slide, image);
    }
  else if (slide->background_type == PP_BACKGROUND_SVG)
    {
      g_autoptr (GFile) file = pp_render_resolve_asset (renderer->presentation,
                                                        slide->background);
      RsvgHandle *svg = get_svg (renderer, file);

      if (svg != NULL)
        {
          g_autoptr (GError) error = NULL;
          double intrinsic_width = 0;
          double intrinsic_height = 0;
          PpRect rect;
          RsvgRectangle viewport;

          if (!rsvg_handle_get_intrinsic_size_in_pixels (svg,
                                                         &intrinsic_width,
                                                         &intrinsic_height))
            {
              intrinsic_width = renderer->page_width;
              intrinsic_height = renderer->page_height;
            }
          pp_render_get_background_rect (slide,
                                         renderer->page_width,
                                         renderer->page_height,
                                         intrinsic_width,
                                         intrinsic_height,
                                         &rect);
          viewport = (RsvgRectangle) { rect.x, rect.y, rect.width, rect.height };
          if (!rsvg_handle_render_document (svg, renderer->cr, &viewport, &error))
            g_warning ("Unable to render SVG background: %s", error->message);
        }
    }
  else if (slide->background_type == PP_BACKGROUND_VIDEO)
    {
      g_autoptr (GFile) file = pp_render_resolve_asset (renderer->presentation,
                                                        slide->background);
      cairo_surface_t *thumbnail = get_video_thumbnail (renderer, file);

      if (thumbnail != NULL)
        render_surface_background (renderer, slide, thumbnail);
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
    default:
      return PANGO_ALIGN_LEFT;
    }
}

static void
render_text (PdfRenderer  *renderer,
             const PpSlide *slide)
{
  static const GdkRGBA white = { 1, 1, 1, 1 };
  static const GdkRGBA black = { 0, 0, 0, 1 };
  g_autoptr (PangoLayout) layout = pango_cairo_create_layout (renderer->cr);
  g_autoptr (PangoFontDescription) description = pango_font_description_from_string (slide->font);
  PangoRectangle logical;
  PpRect text_rect;
  PpRect shading_rect;
  GdkRGBA text_color;
  GdkRGBA shading_color;
  float scale;

  if (slide->text == NULL || *slide->text == '\0')
    return;

  pango_layout_set_font_description (layout, description);
  pango_layout_set_alignment (layout, to_pango_alignment (slide->text_align));
  if (slide->use_markup)
    pango_layout_set_markup (layout, slide->text, -1);
  else
    pango_layout_set_text (layout, slide->text, -1);
  pango_layout_get_pixel_extents (layout, NULL, &logical);
  pp_render_get_text_rect (slide,
                           renderer->page_width,
                           renderer->page_height,
                           logical.x + logical.width,
                           logical.y + logical.height,
                           &text_rect,
                           &scale);
  if (text_rect.width <= 0)
    return;

  /* Preserve the 0.1.8 PDF renderer's height-based shading padding. */
  shading_rect = pp_render_get_shading_rect (renderer->page_height, &text_rect);
  shading_color = parse_color (slide->shading_color, &black);
  shading_color.alpha *= CLAMP (slide->shading_opacity, 0.0, 1.0);
  set_source_color (renderer->cr, &shading_color);
  cairo_rectangle (renderer->cr,
                   shading_rect.x,
                   shading_rect.y,
                   shading_rect.width,
                   shading_rect.height);
  cairo_fill (renderer->cr);

  text_color = parse_color (slide->text_color, &white);
  cairo_save (renderer->cr);
  cairo_translate (renderer->cr, text_rect.x, text_rect.y);
  cairo_scale (renderer->cr, scale, scale);
  set_source_color (renderer->cr, &text_color);
  pango_cairo_show_layout (renderer->cr, layout);
  cairo_restore (renderer->cr);
}

static void
render_notes (PdfRenderer  *renderer,
              const PpSlide *slide)
{
  g_autoptr (PangoLayout) layout = pango_cairo_create_layout (renderer->cr);
  g_autoptr (PangoFontDescription) description = pango_font_description_from_string ("Sans");

  pango_layout_set_text (layout, slide->speaker_notes, -1);
  pango_layout_set_font_description (layout, description);
  pango_layout_set_alignment (layout, PANGO_ALIGN_LEFT);
  pango_layout_set_width (layout,
                          (int) ((renderer->page_width - 2 * renderer->margin) *
                                 PANGO_SCALE));
  pango_layout_set_height (layout,
                           (int) ((renderer->page_height - 2 * renderer->margin) *
                                  PANGO_SCALE));
  pango_layout_set_wrap (layout, PANGO_WRAP_WORD_CHAR);
  cairo_save (renderer->cr);
  cairo_translate (renderer->cr, renderer->margin, renderer->margin);
  cairo_set_source_rgb (renderer->cr, 0, 0, 0);
  pango_cairo_show_layout (renderer->cr, layout);
  cairo_restore (renderer->cr);
  cairo_show_page (renderer->cr);
}

gboolean
pp_pdf_export_with_options (const PpPresentation *presentation,
                            GFile                *output,
                            const PpPdfOptions   *options,
                            GError              **error)
{
  g_autofree char *path = NULL;
  PdfRenderer renderer = { 0 };
  cairo_status_t status;

  g_return_val_if_fail (presentation != NULL, FALSE);
  g_return_val_if_fail (G_IS_FILE (output), FALSE);
  g_return_val_if_fail (options != NULL, FALSE);

  path = g_file_get_path (output);
  if (path == NULL)
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_NOT_SUPPORTED,
                           "PDF output must be a local file");
      return FALSE;
    }

  renderer.presentation = presentation;
  renderer.images = g_hash_table_new_full (g_str_hash,
                                            g_str_equal,
                                            g_free,
                                            destroy_surface);
  renderer.svgs = g_hash_table_new_full (g_str_hash,
                                         g_str_equal,
                                         g_free,
                                         g_object_unref);
  renderer.failed_videos = g_hash_table_new_full (g_str_hash,
                                                   g_str_equal,
                                                   g_free,
                                                   NULL);
  pp_pdf_options_get_page_dimensions (options,
                                      &renderer.page_width,
                                      &renderer.page_height);
  renderer.margin = renderer.page_width * 0.05;
  renderer.surface = cairo_pdf_surface_create (path,
                                                renderer.page_width,
                                                renderer.page_height);
  renderer.cr = cairo_create (renderer.surface);

  for (guint i = 0; i < pp_presentation_get_n_slides (presentation); i++)
    {
      const PpSlide *slide = pp_presentation_get_slide (presentation, i);

      render_background (&renderer, slide);
      render_text (&renderer, slide);
      cairo_show_page (renderer.cr);
      if (options->include_speaker_notes && slide->speaker_notes != NULL)
        render_notes (&renderer, slide);
    }

  cairo_destroy (renderer.cr);
  cairo_surface_finish (renderer.surface);
  status = cairo_surface_status (renderer.surface);
  cairo_surface_destroy (renderer.surface);
  g_hash_table_unref (renderer.images);
  g_hash_table_unref (renderer.svgs);
  g_hash_table_unref (renderer.failed_videos);

  if (status != CAIRO_STATUS_SUCCESS)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "Unable to write PDF: %s",
                   cairo_status_to_string (status));
      return FALSE;
    }

  return TRUE;
}

void
pp_pdf_options_get_page_dimensions (const PpPdfOptions *options,
                                    double             *width,
                                    double             *height)
{
  double short_edge;
  double long_edge;

  g_return_if_fail (options != NULL);
  g_return_if_fail (width != NULL);
  g_return_if_fail (height != NULL);

  if (options->page_size == PP_PDF_PAGE_SIZE_LETTER)
    {
      short_edge = LETTER_WIDTH;
      long_edge = LETTER_HEIGHT;
    }
  else
    {
      short_edge = A4_WIDTH;
      long_edge = A4_HEIGHT;
    }

  if (options->orientation == PP_PDF_ORIENTATION_PORTRAIT)
    {
      *width = short_edge;
      *height = long_edge;
    }
  else
    {
      *width = long_edge;
      *height = short_edge;
    }
}

gboolean
pp_pdf_export (const PpPresentation *presentation,
               GFile                *output,
               GError              **error)
{
  const PpPdfOptions options = PP_PDF_OPTIONS_DEFAULT;

  return pp_pdf_export_with_options (presentation, output, &options, error);
}
