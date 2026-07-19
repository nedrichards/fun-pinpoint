#include "pp-display-selection.h"

gboolean
pp_display_selection_contains (const PpDisplayCandidate *candidates,
                               guint                     count,
                               gconstpointer             display)
{
  if (display == NULL)
    return FALSE;
  for (guint i = 0; i < count; i++)
    if (candidates[i].display == display)
      return TRUE;
  return FALSE;
}

gconstpointer
pp_display_selection_presenter (const PpDisplayCandidate *candidates,
                                guint                     count,
                                gconstpointer             audience,
                                gconstpointer             preferred,
                                gconstpointer             window_display)
{
  if (preferred != audience &&
      pp_display_selection_contains (candidates, count, preferred))
    return preferred;

  for (guint i = 0; i < count; i++)
    if (candidates[i].display != audience && candidates[i].builtin)
      return candidates[i].display;

  if (window_display != audience &&
      pp_display_selection_contains (candidates, count, window_display))
    return window_display;

  for (guint i = 0; i < count; i++)
    if (candidates[i].display != audience)
      return candidates[i].display;
  return NULL;
}

gconstpointer
pp_display_selection_audience (const PpDisplayCandidate *candidates,
                               guint                     count,
                               gconstpointer             presenter,
                               gconstpointer             preferred)
{
  if (preferred != presenter &&
      pp_display_selection_contains (candidates, count, preferred))
    return preferred;

  for (guint i = 0; i < count; i++)
    if (candidates[i].display != presenter && !candidates[i].builtin)
      return candidates[i].display;

  for (guint i = 0; i < count; i++)
    if (candidates[i].display != presenter)
      return candidates[i].display;
  return NULL;
}

void
pp_display_selection_validate (const PpDisplayCandidate *candidates,
                               guint                     count,
                               gconstpointer            *presenter,
                               gconstpointer            *audience)
{
  g_return_if_fail (presenter != NULL);
  g_return_if_fail (audience != NULL);

  if (count < 2)
    {
      *presenter = NULL;
      *audience = NULL;
      return;
    }

  if (!pp_display_selection_contains (candidates, count, *presenter))
    *presenter = NULL;
  if (!pp_display_selection_contains (candidates, count, *audience))
    *audience = NULL;
  if (*presenter != NULL && *presenter == *audience)
    *audience = NULL;
}
