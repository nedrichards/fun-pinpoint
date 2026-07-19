#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef struct
{
  gconstpointer display;
  gboolean builtin;
} PpDisplayCandidate;

gboolean      pp_display_selection_contains (
  const PpDisplayCandidate *candidates,
  guint                     count,
  gconstpointer             display);
gconstpointer pp_display_selection_presenter (
  const PpDisplayCandidate *candidates,
  guint                     count,
  gconstpointer             audience,
  gconstpointer             preferred,
  gconstpointer             window_display);
gconstpointer pp_display_selection_audience (
  const PpDisplayCandidate *candidates,
  guint                     count,
  gconstpointer             presenter,
  gconstpointer             preferred);
void          pp_display_selection_validate (
  const PpDisplayCandidate *candidates,
  guint                     count,
  gconstpointer            *presenter,
  gconstpointer            *audience);

G_END_DECLS
