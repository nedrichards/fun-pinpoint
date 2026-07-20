#include "config.h"

#include "pp-video-thumbnail.h"

#include "pp-render.h"

#include <gst/app/gstappsink.h>
#include <gst/video/video.h>
#include <math.h>
#include <string.h>

#define THUMBNAIL_STATE_TIMEOUT (4 * GST_SECOND)
#define THUMBNAIL_SAMPLE_TIMEOUT GST_SECOND
#define THUMBNAIL_CANCEL_POLL (100 * GST_MSECOND)

static GstStateChangeReturn
wait_for_state (GstElement   *element,
                GCancellable *cancellable,
                GError      **error)
{
  gint64 deadline = g_get_monotonic_time () +
                    THUMBNAIL_STATE_TIMEOUT / (GST_SECOND / G_USEC_PER_SEC);
  GstStateChangeReturn state;

  do
    {
      if (cancellable != NULL &&
          g_cancellable_set_error_if_cancelled (cancellable, error))
        return GST_STATE_CHANGE_FAILURE;
      state = gst_element_get_state (element,
                                     NULL,
                                     NULL,
                                     THUMBNAIL_CANCEL_POLL);
      if (state != GST_STATE_CHANGE_ASYNC)
        return state;
    }
  while (g_get_monotonic_time () < deadline);

  return GST_STATE_CHANGE_ASYNC;
}

static GstSample *
pull_preroll (GstAppSink   *sink,
              GCancellable *cancellable,
              GError      **error)
{
  GstClockTime waited = 0;

  while (waited < THUMBNAIL_SAMPLE_TIMEOUT)
    {
      GstSample *sample;

      if (cancellable != NULL &&
          g_cancellable_set_error_if_cancelled (cancellable, error))
        return NULL;
      sample = gst_app_sink_try_pull_preroll (sink, THUMBNAIL_CANCEL_POLL);
      if (sample != NULL)
        return sample;
      waited += THUMBNAIL_CANCEL_POLL;
    }
  return NULL;
}

static double
pixel_luminance (const guchar *pixel)
{
  return pixel[0] * 0.2126 + pixel[1] * 0.7152 + pixel[2] * 0.0722;
}

static double
score_pixels (const guchar *pixels,
              int           width,
              int           height,
              int           rowstride,
              int           channels,
              gboolean     *acceptable)
{
  int x_step;
  int y_step;
  double sum = 0.0;
  double squared_sum = 0.0;
  double edge_sum = 0.0;
  guint samples = 0;
  guint edges = 0;
  double mean;
  double deviation;
  double detail;
  double exposure;

  x_step = MAX (1, width / 96);
  y_step = MAX (1, height / 54);

  for (int y = 0; y < height; y += y_step)
    for (int x = 0; x < width; x += x_step)
      {
        const guchar *pixel = pixels + (gsize) y * rowstride + x * channels;
        double luminance = pixel_luminance (pixel);

        sum += luminance;
        squared_sum += luminance * luminance;
        samples++;
        if (x >= x_step)
          {
            const guchar *left = pixel - x_step * channels;

            edge_sum += fabs (luminance - pixel_luminance (left));
            edges++;
          }
        if (y >= y_step)
          {
            const guchar *above = pixel - (gsize) y_step * rowstride;

            edge_sum += fabs (luminance - pixel_luminance (above));
            edges++;
          }
      }

  if (samples == 0)
    {
      *acceptable = FALSE;
      return 0.0;
    }

  mean = sum / samples;
  deviation = sqrt (MAX (0.0, squared_sum / samples - mean * mean));
  detail = edges > 0 ? edge_sum / edges : 0.0;
  exposure = 1.0 - fabs (mean - 127.5) / 127.5;
  *acceptable = !((mean < 18.0 && deviation < 18.0) ||
                  (deviation < 7.0 && detail < 6.0));

  return deviation * 4.0 + detail * 3.0 + exposure * 25.0;
}

double
pp_video_thumbnail_score (GdkPixbuf *pixbuf,
                          gboolean  *acceptable)
{
  const guchar *pixels;
  int width;
  int height;
  int rowstride;
  int channels;

  g_return_val_if_fail (GDK_IS_PIXBUF (pixbuf), 0.0);
  g_return_val_if_fail (acceptable != NULL, 0.0);

  pixels = gdk_pixbuf_read_pixels (pixbuf);
  width = gdk_pixbuf_get_width (pixbuf);
  height = gdk_pixbuf_get_height (pixbuf);
  rowstride = gdk_pixbuf_get_rowstride (pixbuf);
  channels = gdk_pixbuf_get_n_channels (pixbuf);
  if (!pp_render_validate_pixel_layout (width,
                                        height,
                                        rowstride,
                                        channels,
                                        gdk_pixbuf_get_byte_length (pixbuf),
                                        NULL,
                                        NULL,
                                        NULL))
    {
      *acceptable = FALSE;
      return 0.0;
    }

  return score_pixels (pixels,
                       width,
                       height,
                       rowstride,
                       channels,
                       acceptable);
}

static void
set_pipeline_error (GstElement  *pipeline,
                    GError     **error,
                    const char  *fallback)
{
  GstBus *bus = gst_element_get_bus (pipeline);
  GstMessage *message = gst_bus_pop_filtered (bus, GST_MESSAGE_ERROR);

  if (message != NULL)
    {
      g_autoptr (GError) gst_error = NULL;
      g_autofree char *debug = NULL;

      gst_message_parse_error (message, &gst_error, &debug);
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "%s",
                   gst_error->message);
      gst_message_unref (message);
    }
  else
    {
      g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, fallback);
    }

  gst_object_unref (bus);
}

static GdkPixbuf *
pixbuf_from_sample (GstSample *sample,
                    GError   **error)
{
  GstCaps *caps = gst_sample_get_caps (sample);
  GstBuffer *buffer = gst_sample_get_buffer (sample);
  GstVideoInfo info;
  GstVideoFrame frame;
  g_autoptr (GBytes) bytes = NULL;
  guchar *pixels;
  int width;
  int height;
  int source_stride;
  int destination_stride;
  const guchar *source_pixels;
  gsize source_offset;
  gsize source_available;
  gsize destination_stride_size;
  gsize destination_size;

  if (caps == NULL || buffer == NULL || !gst_video_info_from_caps (&info, caps))
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_INVALID_DATA,
                           "The video frame has no usable format information");
      return NULL;
    }
  if (GST_VIDEO_INFO_FORMAT (&info) != GST_VIDEO_FORMAT_RGBA ||
      !gst_video_frame_map (&frame, &info, buffer, GST_MAP_READ))
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_INVALID_DATA,
                           "Unable to map the decoded video frame as RGBA");
      return NULL;
    }

  width = GST_VIDEO_FRAME_WIDTH (&frame);
  height = GST_VIDEO_FRAME_HEIGHT (&frame);
  source_stride = GST_VIDEO_FRAME_PLANE_STRIDE (&frame, 0);
  source_pixels = GST_VIDEO_FRAME_PLANE_DATA (&frame, 0);
  source_offset = GST_VIDEO_INFO_PLANE_OFFSET (&info, 0);
  if (source_offset > gst_buffer_get_size (buffer))
    source_available = 0;
  else
    source_available = gst_buffer_get_size (buffer) - source_offset;
  if (!pp_render_validate_pixel_layout (width,
                                        height,
                                        source_stride,
                                        4,
                                        source_available,
                                        NULL,
                                        NULL,
                                        error) ||
      !g_size_checked_mul (&destination_stride_size,
                           (gsize) width,
                           4) ||
      destination_stride_size > G_MAXINT ||
      !g_size_checked_mul (&destination_size,
                           destination_stride_size,
                           (gsize) height))
    {
      if (error == NULL || *error == NULL)
        g_set_error_literal (error,
                             G_IO_ERROR,
                             G_IO_ERROR_INVALID_DATA,
                             "The decoded video frame has an unsafe pixel layout");
      gst_video_frame_unmap (&frame);
      return NULL;
    }
  destination_stride = (int) destination_stride_size;
  pixels = g_try_malloc (destination_size);
  if (pixels == NULL)
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_NO_SPACE,
                           "Unable to allocate the decoded video frame");
      gst_video_frame_unmap (&frame);
      return NULL;
    }
  for (int y = 0; y < height; y++)
    memcpy (pixels + (gsize) y * destination_stride_size,
            source_pixels + (gsize) y * (gsize) source_stride,
            destination_stride_size);
  gst_video_frame_unmap (&frame);

  bytes = g_bytes_new_take (pixels, destination_size);
  return gdk_pixbuf_new_from_bytes (bytes,
                                    GDK_COLORSPACE_RGB,
                                    TRUE,
                                    8,
                                    width,
                                    height,
                                    destination_stride);
}

static gboolean
score_sample (GstSample *sample,
              double    *score,
              gboolean  *acceptable)
{
  GstCaps *caps = gst_sample_get_caps (sample);
  GstBuffer *buffer = gst_sample_get_buffer (sample);
  GstVideoInfo info;
  GstVideoFrame frame;
  const guchar *pixels;
  gsize offset;
  gsize available;
  int width;
  int height;
  int stride;

  if (caps == NULL || buffer == NULL ||
      !gst_video_info_from_caps (&info, caps) ||
      GST_VIDEO_INFO_FORMAT (&info) != GST_VIDEO_FORMAT_RGBA ||
      !gst_video_frame_map (&frame, &info, buffer, GST_MAP_READ))
    return FALSE;

  width = GST_VIDEO_FRAME_WIDTH (&frame);
  height = GST_VIDEO_FRAME_HEIGHT (&frame);
  stride = GST_VIDEO_FRAME_PLANE_STRIDE (&frame, 0);
  pixels = GST_VIDEO_FRAME_PLANE_DATA (&frame, 0);
  offset = GST_VIDEO_INFO_PLANE_OFFSET (&info, 0);
  available = offset <= gst_buffer_get_size (buffer)
    ? gst_buffer_get_size (buffer) - offset
    : 0;
  if (!pp_render_validate_pixel_layout (width,
                                        height,
                                        stride,
                                        4,
                                        available,
                                        NULL,
                                        NULL,
                                        NULL))
    {
      gst_video_frame_unmap (&frame);
      return FALSE;
    }

  *score = score_pixels (pixels,
                         width,
                         height,
                         stride,
                         4,
                         acceptable);
  gst_video_frame_unmap (&frame);
  return TRUE;
}

GdkPixbuf *
pp_video_thumbnail_new (GFile        *file,
                        GCancellable *cancellable,
                        GError      **error)
{
  return pp_video_thumbnail_new_for_size (file, 0, 0, cancellable, error);
}

GdkPixbuf *
pp_video_thumbnail_new_for_size (GFile        *file,
                                 int           max_width,
                                 int           max_height,
                                 GCancellable *cancellable,
                                 GError      **error)
{
  g_autofree char *uri = NULL;
  GstElement *player = NULL;
  GstElement *video_sink = NULL;
  GstElement *audio_sink = NULL;
  GstCaps *caps = NULL;
  GdkPixbuf *pixbuf = NULL;
  GstSample *best_sample = NULL;
  GstSample *fallback_sample = NULL;
  GstStateChangeReturn state;
  gint64 duration = GST_CLOCK_TIME_NONE;
  double best_score = -G_MAXDOUBLE;
  double fallback_score = -G_MAXDOUBLE;

  g_return_val_if_fail (G_IS_FILE (file), NULL);
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable),
                        NULL);

  if (cancellable != NULL && g_cancellable_set_error_if_cancelled (cancellable,
                                                                    error))
    return NULL;
  if (!gst_is_initialized ())
    gst_init (NULL, NULL);

  uri = g_file_get_uri (file);
  player = gst_element_factory_make ("playbin3", NULL);
  if (player == NULL)
    player = gst_element_factory_make ("playbin", NULL);
  video_sink = gst_element_factory_make ("appsink", NULL);
  audio_sink = gst_element_factory_make ("fakesink", NULL);
  if (player == NULL || video_sink == NULL || audio_sink == NULL)
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_NOT_SUPPORTED,
                           "The required GStreamer thumbnail elements are unavailable");
      goto out;
    }

  gst_object_ref_sink (player);
  gst_object_ref_sink (video_sink);
  gst_object_ref_sink (audio_sink);
  caps = gst_caps_new_simple ("video/x-raw",
                              "format", G_TYPE_STRING, "RGBA",
                              "pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1,
                              NULL);
  gst_app_sink_set_caps (GST_APP_SINK (video_sink), caps);
  gst_app_sink_set_max_buffers (GST_APP_SINK (video_sink), 1);
  gst_app_sink_set_drop (GST_APP_SINK (video_sink), TRUE);
  g_object_set (video_sink, "sync", FALSE, NULL);
  g_object_set (player,
                "uri", uri,
                "video-sink", video_sink,
                "audio-sink", audio_sink,
                NULL);
  gst_caps_unref (caps);
  caps = NULL;

  state = gst_element_set_state (player, GST_STATE_PAUSED);
  if (state == GST_STATE_CHANGE_FAILURE ||
      wait_for_state (player, cancellable, error) == GST_STATE_CHANGE_FAILURE)
    {
      if (error == NULL || *error == NULL)
        set_pipeline_error (player, error, "Unable to decode the video");
      goto out;
    }

  if (gst_element_query_duration (player, GST_FORMAT_TIME, &duration) &&
      duration > GST_SECOND)
    {
      static const double fractions[] = { 0.12, 0.38, 0.63, 0.86 };

      for (guint i = 0; i < G_N_ELEMENTS (fractions); i++)
        {
          GstSample *sample;
          gint64 position = CLAMP ((gint64) (duration * fractions[i]),
                                   GST_SECOND / 4,
                                   duration - GST_MSECOND * 100);
          gboolean acceptable;
          double score;

          if (cancellable != NULL &&
              g_cancellable_set_error_if_cancelled (cancellable, error))
            goto out;
          if (!gst_element_seek_simple (player,
                                        GST_FORMAT_TIME,
                                        GST_SEEK_FLAG_FLUSH |
                                        GST_SEEK_FLAG_ACCURATE,
                                        position))
            continue;
          sample = pull_preroll (GST_APP_SINK (video_sink),
                                 cancellable,
                                 error);
          if (sample == NULL)
            {
              if (error != NULL && *error != NULL)
                goto out;
              continue;
            }
          if (!score_sample (sample, &score, &acceptable))
            {
              gst_sample_unref (sample);
              continue;
            }
          if (score > fallback_score)
            {
              g_clear_pointer (&fallback_sample, gst_sample_unref);
              fallback_sample = gst_sample_ref (sample);
              fallback_score = score;
            }
          if (acceptable && score > best_score)
            {
              g_clear_pointer (&best_sample, gst_sample_unref);
              best_sample = gst_sample_ref (sample);
              best_score = score;
            }
          gst_sample_unref (sample);
        }
    }

  if (best_sample != NULL || fallback_sample != NULL)
    {
      pixbuf = pixbuf_from_sample (best_sample != NULL
                                     ? best_sample
                                     : fallback_sample,
                                   error);
      if (pixbuf == NULL)
        goto out;
    }
  if (pixbuf == NULL)
    {
      GstSample *sample;

      if (cancellable != NULL &&
          g_cancellable_set_error_if_cancelled (cancellable, error))
        goto out;
      sample = pull_preroll (GST_APP_SINK (video_sink), cancellable, error);
      if (sample == NULL)
        {
          if (error == NULL || *error == NULL)
            set_pipeline_error (player,
                                error,
                                "Timed out while decoding a video thumbnail");
          goto out;
        }
      pixbuf = pixbuf_from_sample (sample, error);
      gst_sample_unref (sample);
    }

  if (pixbuf != NULL && max_width > 0 && max_height > 0 &&
      (gdk_pixbuf_get_width (pixbuf) > max_width ||
       gdk_pixbuf_get_height (pixbuf) > max_height))
    {
      double scale = MIN ((double) max_width / gdk_pixbuf_get_width (pixbuf),
                          (double) max_height / gdk_pixbuf_get_height (pixbuf));
      int width = MAX (1, (int) floor (gdk_pixbuf_get_width (pixbuf) * scale));
      int height = MAX (1, (int) floor (gdk_pixbuf_get_height (pixbuf) * scale));
      GdkPixbuf *scaled = gdk_pixbuf_scale_simple (pixbuf,
                                                   width,
                                                   height,
                                                   GDK_INTERP_BILINEAR);

      g_clear_object (&pixbuf);
      pixbuf = scaled;
    }

out:
  g_clear_pointer (&best_sample, gst_sample_unref);
  g_clear_pointer (&fallback_sample, gst_sample_unref);
  if (player != NULL)
    {
      gst_element_set_state (player, GST_STATE_NULL);
      gst_object_unref (player);
    }
  if (video_sink != NULL)
    gst_object_unref (video_sink);
  if (audio_sink != NULL)
    gst_object_unref (audio_sink);
  if (caps != NULL)
    gst_caps_unref (caps);
  return pixbuf;
}
