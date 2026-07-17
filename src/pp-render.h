#pragma once

#include "pp-presentation.h"

G_BEGIN_DECLS

typedef struct
{
  float x;
  float y;
  float width;
  float height;
} PpRect;

gboolean pp_render_validate_pixel_layout (int      width,
                                          int      height,
                                          int      rowstride,
                                          int      channels,
                                          gsize    available_bytes,
                                          gsize   *row_bytes,
                                          gsize   *required_bytes,
                                          GError **error);

void   pp_render_get_background_rect (const PpSlide *slide,
                                      float          stage_width,
                                      float          stage_height,
                                      float          background_width,
                                      float          background_height,
                                      PpRect        *rect);
void   pp_render_get_text_rect (const PpSlide *slide,
                                float          stage_width,
                                float          stage_height,
                                float          text_width,
                                float          text_height,
                                PpRect        *rect,
                                float         *scale);
PpRect pp_render_get_shading_rect (float stage_width,
                                   const PpRect *text_rect);
GFile *pp_render_resolve_asset (const PpPresentation *presentation,
                                const char           *asset);
char  *pp_render_find_missing_relative_asset (const PpPresentation *presentation);

G_END_DECLS
