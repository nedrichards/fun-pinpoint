#include "config.h"

#include "pp-pdf.h"

#include "pp-render.h"
#include "pp-video-thumbnail.h"

#include <cairo-pdf.h>
#include <errno.h>
#include <gdk/gdk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <librsvg/rsvg.h>
#include <pango/pangocairo.h>
#include <fcntl.h>
#include <math.h>
#include <unistd.h>

#define A4_WIDTH 595.275590551
#define A4_HEIGHT 841.88976378
#define LETTER_WIDTH 612.0
#define LETTER_HEIGHT 792.0
#define PDF_RASTER_PIXELS_PER_POINT 2.0

typedef struct
{
  cairo_surface_t *surface;
  double intrinsic_width;
  double intrinsic_height;
} PdfRaster;

typedef struct
{
  const PpPresentation *presentation;
  cairo_surface_t *surface;
  cairo_t *cr;
  char *raster_uri;
  PdfRaster raster;
  GHashTable *svgs;
  GHashTable *failed_videos;
  GCancellable *cancellable;
  PpPdfProgressCallback progress_callback;
  gpointer progress_data;
  double page_width;
  double page_height;
  double margin;
} PdfRenderer;

static cairo_user_data_key_t pixel_data_key;

static void
propagate_or_clear_error (GError  **destination,
                          GError   *error)
{
  if (destination != NULL)
    g_propagate_error (destination, error);
  else
    g_error_free (error);
}

static void
clear_raster (PdfRenderer *renderer)
{
  g_clear_pointer (&renderer->raster_uri, g_free);
  g_clear_pointer (&renderer->raster.surface, cairo_surface_destroy);
  renderer->raster.intrinsic_width = 0;
  renderer->raster.intrinsic_height = 0;
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

static PdfRaster *
get_image (PdfRenderer *renderer,
           GFile       *file,
           GError     **error)
{
  g_autofree char *uri = g_file_get_uri (file);
  g_autofree char *path = NULL;
  g_autoptr (GFileInputStream) stream = NULL;
  g_autoptr (GdkPixbuf) pixbuf = NULL;
  g_autoptr (GError) local_error = NULL;
  cairo_surface_t *surface;
  int intrinsic_width = 0;
  int intrinsic_height = 0;
  int max_width = (int) ceil (renderer->page_width * PDF_RASTER_PIXELS_PER_POINT);
  int max_height = (int) ceil (renderer->page_height * PDF_RASTER_PIXELS_PER_POINT);

  if (g_strcmp0 (renderer->raster_uri, uri) == 0)
    return &renderer->raster;

  path = g_file_get_path (file);
  if (path == NULL)
    return NULL;
  gdk_pixbuf_get_file_info (path, &intrinsic_width, &intrinsic_height);
  stream = g_file_read (file, renderer->cancellable, &local_error);
  if (stream != NULL)
    pixbuf = gdk_pixbuf_new_from_stream_at_scale (G_INPUT_STREAM (stream),
                                                  max_width,
                                                  max_height,
                                                  TRUE,
                                                  renderer->cancellable,
                                                  &local_error);
  if (pixbuf == NULL)
    {
      if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        propagate_or_clear_error (error, g_steal_pointer (&local_error));
      else
        g_warning ("Unable to load PDF background: %s", local_error->message);
      return NULL;
    }

  surface = surface_from_pixbuf (pixbuf, &local_error);
  if (surface == NULL)
    {
      g_warning ("Unable to prepare PDF background pixels: %s", local_error->message);
      return NULL;
    }

  if (intrinsic_width == gdk_pixbuf_get_width (pixbuf) &&
      intrinsic_height == gdk_pixbuf_get_height (pixbuf) &&
      (g_str_has_suffix (path, ".jpg") || g_str_has_suffix (path, ".jpeg")))
    {
      g_autoptr (GError) bytes_error = NULL;
      GBytes *bytes = g_file_load_bytes (file,
                                         renderer->cancellable,
                                         NULL,
                                         &bytes_error);

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
      else if (g_error_matches (bytes_error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        {
          cairo_surface_destroy (surface);
          propagate_or_clear_error (error, g_steal_pointer (&bytes_error));
          return NULL;
        }
    }
  clear_raster (renderer);
  renderer->raster_uri = g_steal_pointer (&uri);
  renderer->raster.surface = surface;
  renderer->raster.intrinsic_width = intrinsic_width > 0
    ? intrinsic_width
    : gdk_pixbuf_get_width (pixbuf);
  renderer->raster.intrinsic_height = intrinsic_height > 0
    ? intrinsic_height
    : gdk_pixbuf_get_height (pixbuf);
  return &renderer->raster;
}

static RsvgHandle *
get_svg (PdfRenderer *renderer,
         GFile       *file,
         GError     **error)
{
  g_autofree char *uri = g_file_get_uri (file);
  g_autoptr (GError) local_error = NULL;
  RsvgHandle *handle = g_hash_table_lookup (renderer->svgs, uri);

  if (handle != NULL)
    return handle;

  handle = rsvg_handle_new_from_gfile_sync (file,
                                            RSVG_HANDLE_FLAGS_NONE,
                                            renderer->cancellable,
                                            &local_error);
  if (handle == NULL)
    {
      if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        propagate_or_clear_error (error, g_steal_pointer (&local_error));
      else
        g_warning ("Unable to load SVG background: %s", local_error->message);
      return NULL;
    }

  g_hash_table_insert (renderer->svgs, g_steal_pointer (&uri), handle);
  return handle;
}

static PdfRaster *
get_video_thumbnail (PdfRenderer *renderer,
                     GFile       *file,
                     GError     **error)
{
  g_autofree char *uri = g_file_get_uri (file);
  g_autoptr (GdkPixbuf) pixbuf = NULL;
  g_autoptr (GError) local_error = NULL;
  cairo_surface_t *surface;
  int max_width = (int) ceil (renderer->page_width * PDF_RASTER_PIXELS_PER_POINT);
  int max_height = (int) ceil (renderer->page_height * PDF_RASTER_PIXELS_PER_POINT);

  if (g_strcmp0 (renderer->raster_uri, uri) == 0)
    return &renderer->raster;
  if (g_hash_table_contains (renderer->failed_videos, uri))
    return NULL;

  pixbuf = pp_video_thumbnail_new_for_size (file,
                                            max_width,
                                            max_height,
                                            renderer->cancellable,
                                            &local_error);
  if (pixbuf == NULL)
    {
      if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        {
          propagate_or_clear_error (error, g_steal_pointer (&local_error));
          return NULL;
        }
      g_warning ("Unable to create PDF video thumbnail: %s", local_error->message);
      g_hash_table_add (renderer->failed_videos, g_steal_pointer (&uri));
      return NULL;
    }

  surface = surface_from_pixbuf (pixbuf, &local_error);
  if (surface == NULL)
    {
      g_warning ("Unable to prepare PDF video pixels: %s", local_error->message);
      g_hash_table_add (renderer->failed_videos, g_steal_pointer (&uri));
      return NULL;
    }
  clear_raster (renderer);
  renderer->raster_uri = g_steal_pointer (&uri);
  renderer->raster.surface = surface;
  renderer->raster.intrinsic_width = gdk_pixbuf_get_width (pixbuf);
  renderer->raster.intrinsic_height = gdk_pixbuf_get_height (pixbuf);
  return &renderer->raster;
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
render_surface_background (PdfRenderer   *renderer,
                           const PpSlide *slide,
                           PdfRaster     *raster)
{
  PpRect rect;
  double surface_width = cairo_image_surface_get_width (raster->surface);
  double surface_height = cairo_image_surface_get_height (raster->surface);

  pp_render_get_background_rect (slide,
                                 renderer->page_width,
                                 renderer->page_height,
                                 raster->intrinsic_width,
                                 raster->intrinsic_height,
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
  cairo_set_source_surface (renderer->cr, raster->surface, 0, 0);
  cairo_paint (renderer->cr);
  cairo_restore (renderer->cr);
}

static gboolean
render_background (PdfRenderer  *renderer,
                   const PpSlide *slide,
                   GError       **error)
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
      PdfRaster *image = get_image (renderer, file, error);

      if (image != NULL)
        render_surface_background (renderer, slide, image);
      else if (error != NULL && *error != NULL)
        return FALSE;
    }
  else if (slide->background_type == PP_BACKGROUND_SVG)
    {
      g_autoptr (GFile) file = pp_render_resolve_asset (renderer->presentation,
                                                        slide->background);
      RsvgHandle *svg = get_svg (renderer, file, error);

      if (svg != NULL)
        {
          g_autoptr (GError) render_error = NULL;
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
          if (!rsvg_handle_render_document (svg,
                                            renderer->cr,
                                            &viewport,
                                            &render_error))
            g_warning ("Unable to render SVG background: %s",
                       render_error->message);
        }
      else if (error != NULL && *error != NULL)
        return FALSE;
    }
  else if (slide->background_type == PP_BACKGROUND_VIDEO)
    {
      g_autoptr (GFile) file = pp_render_resolve_asset (renderer->presentation,
                                                        slide->background);
      PdfRaster *thumbnail = get_video_thumbnail (renderer, file, error);

      if (thumbnail != NULL)
        render_surface_background (renderer, slide, thumbnail);
      else if (error != NULL && *error != NULL)
        return FALSE;
    }
  return TRUE;
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
pp_pdf_export_with_options_full (const PpPresentation *presentation,
                                 GFile                *output,
                                 const PpPdfOptions   *options,
                                 GCancellable         *cancellable,
                                 PpPdfProgressCallback progress_callback,
                                 gpointer              progress_data,
                                 GError              **error)
{
  g_autofree char *path = NULL;
  g_autofree char *directory = NULL;
  g_autofree char *temporary_path = NULL;
  g_autoptr (GFile) parent = NULL;
  g_autoptr (GFile) temporary = NULL;
  PdfRenderer renderer = { 0 };
  cairo_status_t status;
  guint n_slides;
  int temporary_fd;
  gboolean success = FALSE;
  gboolean render_complete = FALSE;

  g_return_val_if_fail (presentation != NULL, FALSE);
  g_return_val_if_fail (G_IS_FILE (output), FALSE);
  g_return_val_if_fail (options != NULL, FALSE);
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable),
                        FALSE);

  if (cancellable != NULL &&
      g_cancellable_set_error_if_cancelled (cancellable, error))
    return FALSE;

  path = g_file_get_path (output);
  if (path == NULL)
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_NOT_SUPPORTED,
                           "PDF output must be a local file");
      return FALSE;
    }

  parent = g_file_get_parent (output);
  directory = parent != NULL ? g_file_get_path (parent) : NULL;
  if (directory == NULL)
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_NOT_SUPPORTED,
                           "PDF output must have a local parent folder");
      return FALSE;
    }
  temporary_path = g_build_filename (directory,
                                     ".pinpoint-pdf-XXXXXX",
                                     NULL);
  temporary_fd = g_mkstemp_full (temporary_path, O_RDWR | O_CLOEXEC, 0666);
  if (temporary_fd < 0)
    {
      int saved_errno = errno;

      g_set_error (error,
                   G_IO_ERROR,
                   g_io_error_from_errno (saved_errno),
                   "Unable to create temporary PDF: %s",
                   g_strerror (saved_errno));
      return FALSE;
    }
  close (temporary_fd);
  temporary = g_file_new_for_path (temporary_path);

  renderer.presentation = presentation;
  renderer.svgs = g_hash_table_new_full (g_str_hash,
                                         g_str_equal,
                                         g_free,
                                         g_object_unref);
  renderer.failed_videos = g_hash_table_new_full (g_str_hash,
                                                   g_str_equal,
                                                   g_free,
                                                   NULL);
  renderer.cancellable = cancellable;
  renderer.progress_callback = progress_callback;
  renderer.progress_data = progress_data;
  pp_pdf_options_get_page_dimensions (options,
                                      &renderer.page_width,
                                      &renderer.page_height);
  renderer.margin = renderer.page_width * 0.05;
  renderer.surface = cairo_pdf_surface_create (temporary_path,
                                                renderer.page_width,
                                                renderer.page_height);
  renderer.cr = cairo_create (renderer.surface);
  n_slides = pp_presentation_get_n_slides (presentation);
  if (progress_callback != NULL)
    progress_callback (0, n_slides, progress_data);

  for (guint i = 0; i < n_slides; i++)
    {
      const PpSlide *slide = pp_presentation_get_slide (presentation, i);

      if ((cancellable != NULL &&
           g_cancellable_set_error_if_cancelled (cancellable, error)) ||
          !render_background (&renderer, slide, error))
        goto out;
      render_text (&renderer, slide);
      cairo_show_page (renderer.cr);
      if (options->include_speaker_notes && slide->speaker_notes != NULL)
        render_notes (&renderer, slide);
      if (progress_callback != NULL)
        progress_callback (i + 1, n_slides, progress_data);
    }

  if (cancellable != NULL &&
      g_cancellable_set_error_if_cancelled (cancellable, error))
    goto out;
  render_complete = TRUE;

out:
  cairo_destroy (renderer.cr);
  cairo_surface_finish (renderer.surface);
  status = cairo_surface_status (renderer.surface);
  cairo_surface_destroy (renderer.surface);
  clear_raster (&renderer);
  g_hash_table_unref (renderer.svgs);
  g_hash_table_unref (renderer.failed_videos);

  if (!render_complete || (error != NULL && *error != NULL))
    goto cleanup;
  if (status != CAIRO_STATUS_SUCCESS)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "Unable to write PDF: %s",
                   cairo_status_to_string (status));
      goto cleanup;
    }

  if (!g_file_move (temporary,
                    output,
                    G_FILE_COPY_OVERWRITE,
                    cancellable,
                    NULL,
                    NULL,
                    error))
    goto cleanup;
  success = TRUE;

cleanup:
  if (!success)
    g_file_delete (temporary, NULL, NULL);
  return success;
}

gboolean
pp_pdf_export_with_options (const PpPresentation *presentation,
                            GFile                *output,
                            const PpPdfOptions   *options,
                            GError              **error)
{
  return pp_pdf_export_with_options_full (presentation,
                                          output,
                                          options,
                                          NULL,
                                          NULL,
                                          NULL,
                                          error);
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
