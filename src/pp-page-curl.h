#pragma once

#include <glib.h>

G_BEGIN_DECLS

enum
{
  PP_PAGE_CURL_TILES = 32,
  PP_PAGE_CURL_VERTEX_COUNT = (PP_PAGE_CURL_TILES + 1) *
                              (PP_PAGE_CURL_TILES + 1),
  PP_PAGE_CURL_TRIANGLE_COUNT = PP_PAGE_CURL_TILES *
                                PP_PAGE_CURL_TILES * 2,
  PP_PAGE_CURL_INDEX_COUNT = PP_PAGE_CURL_TRIANGLE_COUNT * 3,
};

typedef struct
{
  float x;
  float y;
  float z;
  float shade;
} PpPageCurlVertex;

typedef struct
{
  float x;
  float y;
  float u;
  float v;
  float shade;
} PpPageCurlMeshVertex;

void pp_page_curl_deform_vertex (float             width,
                                 float             height,
                                 double            period,
                                 double            angle,
                                 float             radius,
                                 PpPageCurlVertex *vertex);
void pp_page_curl_build_mesh    (float                    width,
                                 float                    height,
                                 double                   period,
                                 double                   angle,
                                 PpPageCurlMeshVertex    *vertices,
                                 guint                   *indices);

G_END_DECLS
