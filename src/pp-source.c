#include "pp-source.h"

#include "pp-presentation.h"

#include <gtk/gtk.h>
#include <math.h>
#include <string.h>

struct _PpSourceAnalysis
{
  GPtrArray *slides;
  GPtrArray *diagnostics;
};

static const char *const setting_names[] = {
  "stage-color=", "font=", "notes-font=", "notes-font-size=",
  "text-color=", "text-align=", "shading-color=", "shading-opacity=",
  "duration=", "command=", "transition=", "transition-direction=",
  "transition-layer=", "transition-mode=", "transition-duration=",
  "transition-easing=", "camera-framerate=", "camera-resolution=",
  "bg-position=", "fill", "fit", "stretch", "unscaled", "center",
  "top", "bottom", "left", "right", "top-left", "top-right",
  "bottom-left", "bottom-right", "no-markup", "markup", NULL
};

static const char *const align_values[] = { "left", "center", "right", NULL };
static const char *const gravity_values[] = {
  "center", "top-left", "left", "bottom-left", "top-right", "right",
  "bottom-right", NULL
};
static const char *const direction_values[] = { "left", "right", "up", "down", NULL };
static const char *const layer_values[] = { "default", "all", "background", "text", NULL };
static const char *const mode_values[] = { "both", "in", "out", NULL };
static const char *const easing_values[] = {
  "linear", "ease-in", "ease-out", "ease-in-out", NULL
};

static void
source_slide_free (PpSourceSlide *slide)
{
  g_free (slide->title);
  g_free (slide);
}

static void
source_diagnostic_free (PpSourceDiagnostic *diagnostic)
{
  g_free (diagnostic->message);
  g_free (diagnostic);
}

static void
add_diagnostic (PpSourceAnalysis          *self,
                gsize                      start,
                gsize                      end,
                PpSourceDiagnosticSeverity severity,
                const char                *message)
{
  PpSourceDiagnostic *diagnostic = g_new0 (PpSourceDiagnostic, 1);

  diagnostic->start = start;
  diagnostic->end = MAX (end, start + 1);
  diagnostic->severity = severity;
  diagnostic->message = g_strdup (message);
  g_ptr_array_add (self->diagnostics, diagnostic);
}

static gboolean
setting_is_known (const char *setting)
{
  for (guint i = 0; setting_names[i] != NULL; i++)
    {
      if (g_str_has_suffix (setting_names[i], "="))
        {
          if (g_str_has_prefix (setting, setting_names[i]))
            return TRUE;
        }
      else if (g_str_equal (setting, setting_names[i]))
        return TRUE;
    }
  return FALSE;
}

static gboolean
number_is_valid (const char *value,
                 gboolean    integer)
{
  char *end = NULL;
  double parsed;

  if (value == NULL || *value == '\0')
    return FALSE;
  parsed = integer ? (double) g_ascii_strtoll (value, &end, 10)
                   : g_ascii_strtod (value, &end);
  return end != value && *end == '\0' && isfinite (parsed) && parsed >= 0.0;
}

static gboolean
value_is_one_of (const char         *value,
                 const char *const *values)
{
  for (guint i = 0; values[i] != NULL; i++)
    if (g_str_equal (value, values[i]))
      return TRUE;
  return FALSE;
}

static void
analyze_setting (PpSourceAnalysis *self,
                 const char       *setting,
                 gsize             start,
                 gsize             end)
{
  const char *equals = strchr (setting, '=');

  if (equals != NULL && !setting_is_known (setting))
    {
      add_diagnostic (self, start, end, PP_SOURCE_DIAGNOSTIC_WARNING,
                      "Unknown setting; Pinpoint will treat it as a background");
      return;
    }
  if (g_str_has_prefix (setting, "text-align=") &&
      !value_is_one_of (setting + strlen ("text-align="), align_values))
    add_diagnostic (self, start, end, PP_SOURCE_DIAGNOSTIC_WARNING,
                    "Text alignment must be left, center, or right");
  else if (g_str_has_prefix (setting, "transition-direction=") &&
           !value_is_one_of (setting + strlen ("transition-direction="), direction_values))
    add_diagnostic (self, start, end, PP_SOURCE_DIAGNOSTIC_WARNING,
                    "Unknown transition direction");
  else if (g_str_has_prefix (setting, "transition-layer=") &&
           !value_is_one_of (setting + strlen ("transition-layer="), layer_values))
    add_diagnostic (self, start, end, PP_SOURCE_DIAGNOSTIC_WARNING,
                    "Unknown transition layer");
  else if (g_str_has_prefix (setting, "transition-mode=") &&
           !value_is_one_of (setting + strlen ("transition-mode="), mode_values))
    add_diagnostic (self, start, end, PP_SOURCE_DIAGNOSTIC_WARNING,
                    "Unknown transition mode");
  else if (g_str_has_prefix (setting, "duration=") &&
      !number_is_valid (setting + strlen ("duration="), FALSE))
    add_diagnostic (self, start, end, PP_SOURCE_DIAGNOSTIC_WARNING,
                    "Duration must be a non-negative number of seconds");
  else if (g_str_has_prefix (setting, "shading-opacity=") &&
           !number_is_valid (setting + strlen ("shading-opacity="), FALSE))
    add_diagnostic (self, start, end, PP_SOURCE_DIAGNOSTIC_WARNING,
                    "Shading opacity must be a non-negative number");
  else if (g_str_has_prefix (setting, "transition-duration=") &&
           !number_is_valid (setting + strlen ("transition-duration="), TRUE))
    add_diagnostic (self, start, end, PP_SOURCE_DIAGNOSTIC_WARNING,
                    "Transition duration must be milliseconds as a non-negative integer");
  else if (g_str_has_prefix (setting, "camera-framerate=") &&
           !number_is_valid (setting + strlen ("camera-framerate="), TRUE))
    add_diagnostic (self, start, end, PP_SOURCE_DIAGNOSTIC_WARNING,
                    "Camera frame rate must be a non-negative integer");
}

static char *
slide_title (const char *source,
             gsize       start,
             gsize       end,
             guint       index)
{
  gsize cursor = start;

  while (cursor < end)
    {
      gsize line_end = cursor;
      g_autofree char *line = NULL;
      g_autofree char *plain = NULL;

      while (line_end < end && source[line_end] != '\n')
        line_end++;
      line = g_strndup (source + cursor, line_end - cursor);
      g_strstrip (line);
      if (line[0] != '\0' && line[0] != '#')
        {
          const char *visible = line[0] == '\\' ? line + 1 : line;

          if (!pango_parse_markup (visible, -1, 0, NULL, &plain, NULL, NULL))
            plain = g_strdup (visible);
          g_strstrip (plain);
          if (plain[0] != '\0')
            return g_utf8_substring (plain, 0, MIN (60, g_utf8_strlen (plain, -1)));
        }
      cursor = line_end < end ? line_end + 1 : end;
    }
  return g_strdup_printf ("Slide %u", index + 1);
}

PpSourceAnalysis *
pp_source_analyze (const char *source,
                   GFile      *file)
{
  PpSourceAnalysis *self;
  gsize length;
  gsize cursor = 0;
  PpSourceSlide *current = NULL;

  g_return_val_if_fail (source != NULL, NULL);
  g_return_val_if_fail (file == NULL || G_IS_FILE (file), NULL);

  self = g_new0 (PpSourceAnalysis, 1);
  self->slides = g_ptr_array_new_with_free_func ((GDestroyNotify) source_slide_free);
  self->diagnostics = g_ptr_array_new_with_free_func ((GDestroyNotify) source_diagnostic_free);
  length = strlen (source);
  if (!g_utf8_validate (source, -1, NULL))
    {
      add_diagnostic (self, 0, length, PP_SOURCE_DIAGNOSTIC_ERROR,
                      "The presentation is not valid UTF-8");
      return self;
    }

  while (cursor < length)
    {
      gsize line_end = cursor;
      gsize scan;

      while (line_end < length && source[line_end] != '\n')
        line_end++;
      if (source[cursor] == '-')
        {
          if (current != NULL)
            current->end = cursor;
          current = g_new0 (PpSourceSlide, 1);
          current->start = cursor;
          current->separator_end = line_end;
          current->end = length;
          g_ptr_array_add (self->slides, current);
        }

      scan = cursor;
      while (scan < line_end)
        {
          const char *open = memchr (source + scan, '[', line_end - scan);
          const char *close;
          g_autofree char *setting = NULL;

          if (open == NULL)
            break;
          close = memchr (open + 1, ']', source + line_end - open - 1);
          if (close == NULL)
            {
              add_diagnostic (self, open - source, line_end,
                              PP_SOURCE_DIAGNOSTIC_WARNING,
                              "Setting is missing a closing bracket");
              break;
            }
          setting = g_strndup (open + 1, close - open - 1);
          analyze_setting (self, setting, open - source, close - source + 1);
          scan = close - source + 1;
        }
      cursor = line_end < length ? line_end + 1 : length;
    }

  for (guint i = 0; i < self->slides->len; i++)
    {
      PpSourceSlide *slide = g_ptr_array_index (self->slides, i);
      gsize body = slide->separator_end < length ? slide->separator_end + 1 : length;

      slide->title = slide_title (source, body, slide->end, i);
    }
  if (self->slides->len == 0)
    add_diagnostic (self, 0, MAX (length, 1), PP_SOURCE_DIAGNOSTIC_ERROR,
                    "The presentation does not contain a slide separator");
  return self;
}

void
pp_source_analysis_free (PpSourceAnalysis *self)
{
  if (self == NULL)
    return;
  g_ptr_array_unref (self->slides);
  g_ptr_array_unref (self->diagnostics);
  g_free (self);
}

guint
pp_source_analysis_get_n_slides (const PpSourceAnalysis *self)
{
  g_return_val_if_fail (self != NULL, 0);
  return self->slides->len;
}

const PpSourceSlide *
pp_source_analysis_get_slide (const PpSourceAnalysis *self,
                              guint                   index)
{
  g_return_val_if_fail (self != NULL, NULL);
  g_return_val_if_fail (index < self->slides->len, NULL);
  return g_ptr_array_index (self->slides, index);
}

guint
pp_source_analysis_find_slide (const PpSourceAnalysis *self,
                               gsize                   offset)
{
  g_return_val_if_fail (self != NULL, 0);

  for (guint i = 0; i < self->slides->len; i++)
    {
      const PpSourceSlide *slide = g_ptr_array_index (self->slides, i);

      if (offset >= slide->start && offset < slide->end)
        return i;
    }
  return self->slides->len > 0 ? self->slides->len - 1 : 0;
}

guint
pp_source_analysis_get_n_diagnostics (const PpSourceAnalysis *self)
{
  g_return_val_if_fail (self != NULL, 0);
  return self->diagnostics->len;
}

const PpSourceDiagnostic *
pp_source_analysis_get_diagnostic (const PpSourceAnalysis *self,
                                   guint                   index)
{
  g_return_val_if_fail (self != NULL, NULL);
  g_return_val_if_fail (index < self->diagnostics->len, NULL);
  return g_ptr_array_index (self->diagnostics, index);
}

const char *const *
pp_source_setting_names (void)
{
  return setting_names;
}

const char *const *
pp_source_setting_values (const char *name)
{
  if (g_str_equal (name, "text-align="))
    return align_values;
  if (g_str_equal (name, "bg-position="))
    return gravity_values;
  if (g_str_equal (name, "transition-direction="))
    return direction_values;
  if (g_str_equal (name, "transition-layer="))
    return layer_values;
  if (g_str_equal (name, "transition-mode="))
    return mode_values;
  if (g_str_equal (name, "transition-easing="))
    return easing_values;
  return NULL;
}

static char *
format_duration (double duration)
{
  char buffer[G_ASCII_DTOSTR_BUF_SIZE];
  char *end;

  g_ascii_formatd (buffer, sizeof buffer, "%.3f", duration);
  end = buffer + strlen (buffer) - 1;
  while (end > buffer && *end == '0')
    *end-- = '\0';
  if (end > buffer && *end == '.')
    *end = '\0';
  return g_strdup (buffer);
}

char *
pp_source_apply_durations (const char   *source,
                           const double *durations,
                           guint         n_durations,
                           GError      **error)
{
  g_autoptr (PpSourceAnalysis) analysis = NULL;
  g_autoptr (GString) output = NULL;

  g_return_val_if_fail (source != NULL, NULL);
  g_return_val_if_fail (durations != NULL || n_durations == 0, NULL);

  analysis = pp_source_analyze (source, NULL);
  if (analysis->slides->len != n_durations)
    {
      g_set_error (error, PP_PRESENTATION_ERROR, PP_PRESENTATION_ERROR_INVALID,
                   "Expected %u rehearsal timings, received %u",
                   analysis->slides->len, n_durations);
      return NULL;
    }
  output = g_string_new (source);
  for (guint i = n_durations; i > 0; i--)
    {
      const PpSourceSlide *slide = g_ptr_array_index (analysis->slides, i - 1);
      g_autofree char *duration = format_duration (durations[i - 1]);
      const char *line = source + slide->start;
      const char *limit = source + slide->separator_end;
      const char *token = g_strstr_len (line, limit - line, "[duration=");

      if (token != NULL)
        {
          const char *value = token + strlen ("[duration=");
          const char *close = memchr (value, ']', limit - value);

          if (close != NULL)
            {
              gsize value_offset = value - source;
              g_string_erase (output, value_offset, close - value);
              g_string_insert (output, value_offset, duration);
              continue;
            }
        }
      {
        g_autofree char *setting = g_strdup_printf (" [duration=%s]", duration);
        g_string_insert (output, slide->separator_end, setting);
      }
    }
  return g_string_free (g_steal_pointer (&output), FALSE);
}
