#include "config.h"

#include "pp-page-curl.h"

#include <math.h>

static void
project_vertex (PpPageCurlVertex *vertex,
                float             width,
                float             height)
{
  float focal_length = MAX (width, height) * 2.0f;
  float scale = focal_length / MAX (focal_length - vertex->z, 1.0f);

  vertex->x = width / 2.0f + (vertex->x - width / 2.0f) * scale;
  vertex->y = height / 2.0f + (vertex->y - height / 2.0f) * scale;
}

float
pp_page_curl_radius (float width,
                     float height)
{
  g_return_val_if_fail (width > 0.0f, 0.0f);
  g_return_val_if_fail (height > 0.0f, 0.0f);

  return MIN (width, height) * (50.0f / 360.0f);
}

static void
deform_vertex_with_rotation (float             width,
                             float             height,
                             double            period,
                             float             cosine,
                             float             sine,
                             float             radius,
                             PpPageCurlVertex *vertex)
{
  float cx = (float) (1.0 - period) * width;
  float cy = (float) (1.0 - period) * height;
  float dx = vertex->x - cx;
  float dy = vertex->y - cy;
  float rx = (dx * cosine) + (dy * sine) - radius;
  float ry = (-dx * sine) + (dy * cosine);
  float turn_angle = 0.0f;

  vertex->z = 0.0f;
  vertex->shade = 1.0f;
  if (rx > radius * -2.0f)
    {
      turn_angle = (rx / radius * (float) G_PI_2) - (float) G_PI_2;
      vertex->shade = ((sinf (turn_angle) * 96.0f) + 159.0f) / 255.0f;
    }

  if (rx > 0.0f)
    {
      float small_radius = radius - MIN (radius,
                                         (turn_angle * 10.0f) / (float) G_PI);

      rx = (small_radius * cosf (turn_angle)) + radius;
      vertex->x = (rx * cosine) - (ry * sine) + cx;
      vertex->y = (rx * sine) + (ry * cosine) + cy;
      vertex->z = (small_radius * sinf (turn_angle)) + radius;
    }
}

void
pp_page_curl_deform_vertex (float             width,
                            float             height,
                            double            period,
                            double            angle,
                            float             radius,
                            PpPageCurlVertex *vertex)
{
  float radians;

  g_return_if_fail (vertex != NULL);
  g_return_if_fail (period >= 0.0 && period <= 1.0);
  g_return_if_fail (radius > 0.0f);

  vertex->z = 0.0f;
  vertex->shade = 1.0f;
  if (period == 0.0)
    return;

  radians = (float) (angle * (G_PI / 180.0));
  deform_vertex_with_rotation (width,
                               height,
                               period,
                               cosf (radians),
                               sinf (radians),
                               radius,
                               vertex);
}

void
pp_page_curl_build_mesh (float                 width,
                         float                 height,
                         double                period,
                         double                angle,
                         PpPageCurlMeshVertex *vertices,
                         guint                *indices)
{
  guint index = 0;
  float radians = (float) (angle * (G_PI / 180.0));
  float cosine = cosf (radians);
  float sine = sinf (radians);

  g_return_if_fail (width > 0.0f);
  g_return_if_fail (height > 0.0f);
  g_return_if_fail (period >= 0.0 && period <= 1.0);
  g_return_if_fail (vertices != NULL);
  g_return_if_fail (indices != NULL);

  for (guint y = 0; y <= PP_PAGE_CURL_TILES; y++)
    for (guint x = 0; x <= PP_PAGE_CURL_TILES; x++)
      {
        guint vertex_index = y * (PP_PAGE_CURL_TILES + 1) + x;
        float source_x = width * (float) x / PP_PAGE_CURL_TILES;
        float source_y = height * (float) y / PP_PAGE_CURL_TILES;
        PpPageCurlVertex destination = {
          .x = source_x,
          .y = source_y,
          .shade = 1.0f,
        };

        if (period > 0.0)
          {
            deform_vertex_with_rotation (width,
                                         height,
                                         period,
                                         cosine,
                                         sine,
                                         pp_page_curl_radius (width, height),
                                         &destination);
            project_vertex (&destination, width, height);
          }
        vertices[vertex_index] = (PpPageCurlMeshVertex) {
          .x = destination.x / width * 2.0f - 1.0f,
          .y = 1.0f - destination.y / height * 2.0f,
          .z = -destination.z / (MAX (width, height) * 2.0f),
          .u = (float) x / PP_PAGE_CURL_TILES,
          .v = (float) y / PP_PAGE_CURL_TILES,
          .shade = destination.shade,
        };
      }

  for (guint y = 0; y < PP_PAGE_CURL_TILES; y++)
    for (guint x = 0; x < PP_PAGE_CURL_TILES; x++)
      {
        guint top_left = y * (PP_PAGE_CURL_TILES + 1) + x;
        guint top_right = top_left + 1;
        guint bottom_left = top_left + PP_PAGE_CURL_TILES + 1;
        guint bottom_right = bottom_left + 1;

        indices[index++] = top_left;
        indices[index++] = top_right;
        indices[index++] = bottom_right;
        indices[index++] = top_left;
        indices[index++] = bottom_right;
        indices[index++] = bottom_left;
      }
}
