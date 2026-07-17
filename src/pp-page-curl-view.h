#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define PP_TYPE_PAGE_CURL_VIEW (pp_page_curl_view_get_type ())
G_DECLARE_FINAL_TYPE (PpPageCurlView, pp_page_curl_view, PP, PAGE_CURL_VIEW, GtkGLArea)

GtkWidget *pp_page_curl_view_new (void);

void pp_page_curl_view_set_transition (PpPageCurlView *self,
                                       GdkTexture     *previous,
                                       GdkTexture     *current,
                                       double          previous_period,
                                       double          previous_angle,
                                       double          current_period,
                                       double          current_angle,
                                       gboolean        backwards);
void pp_page_curl_view_clear (PpPageCurlView *self);

G_END_DECLS
