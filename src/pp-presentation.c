#include "config.h"

#include "pp-presentation.h"

#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct _PpPresentation
{
  gint ref_count;
  GFile *file;
  char *source;
  PpSlide *defaults;
  GPtrArray *slides;
};

G_DEFINE_QUARK (pp-presentation-error-quark, pp_presentation_error)

static void
replace_string (char       **destination,
                const char  *value)
{
  g_free (*destination);
  *destination = g_strdup (value);
}

static PpSlide *
pp_slide_new (void)
{
  PpSlide *slide = g_new0 (PpSlide, 1);

  slide->stage_color = g_strdup ("black");
  slide->background_scale = PP_BACKGROUND_FIT;
  slide->background_position = PP_GRAVITY_CENTER;
  slide->text_position = PP_GRAVITY_CENTER;
  slide->font = g_strdup ("Sans 60px");
  slide->notes_font = g_strdup ("Sans");
  slide->notes_font_size = g_strdup ("20px");
  slide->text_color = g_strdup ("white");
  slide->text_align = PP_TEXT_ALIGN_LEFT;
  slide->use_markup = TRUE;
  slide->duration = 30.0;
  slide->shading_color = g_strdup ("black");
  slide->shading_opacity = 0.66;
  slide->transition = g_strdup ("fade");
  slide->transition_direction = PP_TRANSITION_DIRECTION_LEFT;
  slide->transition_layer = PP_TRANSITION_LAYER_DEFAULT;
  slide->transition_mode = PP_TRANSITION_MODE_BOTH;
  slide->transition_easing = g_strdup ("linear");

  return slide;
}

static PpSlide *
pp_slide_copy (const PpSlide *source)
{
  PpSlide *slide = g_new0 (PpSlide, 1);

  *slide = *source;
  slide->stage_color = g_strdup (source->stage_color);
  slide->background = g_strdup (source->background);
  slide->text = g_strdup (source->text);
  slide->font = g_strdup (source->font);
  slide->notes_font = g_strdup (source->notes_font);
  slide->notes_font_size = g_strdup (source->notes_font_size);
  slide->text_color = g_strdup (source->text_color);
  slide->speaker_notes = g_strdup (source->speaker_notes);
  slide->shading_color = g_strdup (source->shading_color);
  slide->transition = g_strdup (source->transition);
  slide->transition_easing = g_strdup (source->transition_easing);
  slide->command = g_strdup (source->command);

  return slide;
}

static void
pp_slide_free (PpSlide *slide)
{
  if (slide == NULL)
    return;

  g_free (slide->stage_color);
  g_free (slide->background);
  g_free (slide->text);
  g_free (slide->font);
  g_free (slide->notes_font);
  g_free (slide->notes_font_size);
  g_free (slide->text_color);
  g_free (slide->speaker_notes);
  g_free (slide->shading_color);
  g_free (slide->transition);
  g_free (slide->transition_easing);
  g_free (slide->command);
  g_free (slide);
}

static gboolean
parse_resolution (const char   *value,
                  PpResolution *resolution)
{
  if (sscanf (value, "%dx%d", &resolution->width, &resolution->height) != 2)
    {
      resolution->width = 0;
      resolution->height = 0;
      return FALSE;
    }

  return TRUE;
}

static PpTextAlign
parse_text_align (const char *value)
{
  if (g_str_equal (value, "center"))
    return PP_TEXT_ALIGN_CENTER;
  if (g_str_equal (value, "right"))
    return PP_TEXT_ALIGN_RIGHT;
  return PP_TEXT_ALIGN_LEFT;
}

static PpGravity
parse_background_gravity (const char *value)
{
  /* Keep the 0.1.8 parser's observable omission of top and bottom. */
  if (g_str_equal (value, "top-left"))
    return PP_GRAVITY_TOP_LEFT;
  if (g_str_equal (value, "left"))
    return PP_GRAVITY_LEFT;
  if (g_str_equal (value, "bottom-left"))
    return PP_GRAVITY_BOTTOM_LEFT;
  if (g_str_equal (value, "top-right"))
    return PP_GRAVITY_TOP_RIGHT;
  if (g_str_equal (value, "right"))
    return PP_GRAVITY_RIGHT;
  if (g_str_equal (value, "bottom-right"))
    return PP_GRAVITY_BOTTOM_RIGHT;
  return PP_GRAVITY_CENTER;
}

static PpTransitionDirection
parse_transition_direction (const char *value)
{
  if (g_str_equal (value, "right"))
    return PP_TRANSITION_DIRECTION_RIGHT;
  if (g_str_equal (value, "up"))
    return PP_TRANSITION_DIRECTION_UP;
  if (g_str_equal (value, "down"))
    return PP_TRANSITION_DIRECTION_DOWN;
  return PP_TRANSITION_DIRECTION_LEFT;
}

static PpTransitionLayer
parse_transition_layer (const char *value)
{
  if (g_str_equal (value, "default"))
    return PP_TRANSITION_LAYER_DEFAULT;
  if (g_str_equal (value, "background"))
    return PP_TRANSITION_LAYER_BACKGROUND;
  if (g_str_equal (value, "text"))
    return PP_TRANSITION_LAYER_TEXT;
  return PP_TRANSITION_LAYER_ALL;
}

static PpTransitionMode
parse_transition_mode (const char *value)
{
  if (g_str_equal (value, "in"))
    return PP_TRANSITION_MODE_IN;
  if (g_str_equal (value, "out"))
    return PP_TRANSITION_MODE_OUT;
  return PP_TRANSITION_MODE_BOTH;
}

static guint
parse_transition_duration (const char *value)
{
  char *end = NULL;
  guint64 duration;

  if (value == NULL || *value == '\0' || *value == '-')
    return 0;
  duration = g_ascii_strtoull (value, &end, 10);
  if (end == value || *end != '\0')
    return 0;
  return (guint) MIN (duration, G_MAXUINT);
}

static void
parse_setting (PpSlide    *slide,
               const char *setting)
{
  const char *value;

#define PREFIX(name) (g_str_has_prefix (setting, name) && (value = setting + strlen (name), TRUE))

  if (PREFIX ("stage-color="))
    replace_string (&slide->stage_color, value);
  else if (PREFIX ("font="))
    replace_string (&slide->font, value);
  else if (PREFIX ("notes-font="))
    replace_string (&slide->notes_font, value);
  else if (PREFIX ("notes-font-size="))
    replace_string (&slide->notes_font_size, value);
  else if (PREFIX ("text-color="))
    replace_string (&slide->text_color, value);
  else if (PREFIX ("text-align="))
    slide->text_align = parse_text_align (value);
  else if (PREFIX ("shading-color="))
    replace_string (&slide->shading_color, value);
  else if (PREFIX ("shading-opacity="))
    slide->shading_opacity = g_ascii_strtod (value, NULL);
  else if (PREFIX ("duration="))
    slide->duration = g_ascii_strtod (value, NULL);
  else if (PREFIX ("command="))
    replace_string (&slide->command, value);
  else if (PREFIX ("transition="))
    replace_string (&slide->transition, value);
  else if (PREFIX ("transition-direction="))
    slide->transition_direction = parse_transition_direction (value);
  else if (PREFIX ("transition-layer="))
    slide->transition_layer = parse_transition_layer (value);
  else if (PREFIX ("transition-mode="))
    slide->transition_mode = parse_transition_mode (value);
  else if (PREFIX ("transition-duration="))
    slide->transition_duration_ms = parse_transition_duration (value);
  else if (PREFIX ("transition-easing="))
    replace_string (&slide->transition_easing, value);
  else if (PREFIX ("camera-framerate="))
    slide->camera_framerate = (int) g_ascii_strtoll (value, NULL, 10);
  else if (PREFIX ("camera-resolution="))
    parse_resolution (value, &slide->camera_resolution);
  else if (g_str_equal (setting, "fill"))
    slide->background_scale = PP_BACKGROUND_FILL;
  else if (g_str_equal (setting, "fit"))
    slide->background_scale = PP_BACKGROUND_FIT;
  else if (g_str_equal (setting, "stretch"))
    slide->background_scale = PP_BACKGROUND_STRETCH;
  else if (g_str_equal (setting, "unscaled"))
    slide->background_scale = PP_BACKGROUND_UNSCALED;
  else if (PREFIX ("bg-position="))
    slide->background_position = parse_background_gravity (value);
  else if (g_str_equal (setting, "center"))
    slide->text_position = PP_GRAVITY_CENTER;
  else if (g_str_equal (setting, "top"))
    slide->text_position = PP_GRAVITY_TOP;
  else if (g_str_equal (setting, "bottom"))
    slide->text_position = PP_GRAVITY_BOTTOM;
  else if (g_str_equal (setting, "left"))
    slide->text_position = PP_GRAVITY_LEFT;
  else if (g_str_equal (setting, "right"))
    slide->text_position = PP_GRAVITY_RIGHT;
  else if (g_str_equal (setting, "top-left"))
    slide->text_position = PP_GRAVITY_TOP_LEFT;
  else if (g_str_equal (setting, "top-right"))
    slide->text_position = PP_GRAVITY_TOP_RIGHT;
  else if (g_str_equal (setting, "bottom-left"))
    slide->text_position = PP_GRAVITY_BOTTOM_LEFT;
  else if (g_str_equal (setting, "bottom-right"))
    slide->text_position = PP_GRAVITY_BOTTOM_RIGHT;
  else if (g_str_equal (setting, "no-markup"))
    slide->use_markup = FALSE;
  else if (g_str_equal (setting, "markup"))
    slide->use_markup = TRUE;
  else
    replace_string (&slide->background, setting);

#undef PREFIX
}

static void
parse_config (PpSlide    *slide,
              const char *config)
{
  const char *cursor = config;

  while ((cursor = strchr (cursor, '[')) != NULL)
    {
      const char *end = cursor + 1;
      g_autofree char *setting = NULL;

      while (*end != '\0' && *end != ']' && *end != '\n')
        end++;

      if (*end != ']')
        {
          cursor = end;
          continue;
        }

      setting = g_strndup (cursor + 1, end - cursor - 1);
      parse_setting (slide, setting);
      cursor = end + 1;
    }
}

static gboolean
has_suffix (const char *filename,
            const char *suffix)
{
  gsize filename_length = strlen (filename);
  gsize suffix_length = strlen (suffix);

  return filename_length >= suffix_length &&
         g_ascii_strcasecmp (filename + filename_length - suffix_length,
                             suffix) == 0;
}

static gboolean
has_video_suffix (const char *filename)
{
  static const char *const suffixes[] = {
    ".avi", ".ogg", ".ogv", ".mpg", ".flv", ".mpeg", ".mov", ".mp4",
    ".m4v", ".wmv", ".webm", ".mkv", ".3gp", ".gif", ".ts", ".mts",
    ".m2ts", ".m2v", ".mxf", ".vob", ".m3u8", NULL
  };

  for (guint i = 0; suffixes[i] != NULL; i++)
    if (has_suffix (filename, suffixes[i]))
      return TRUE;

  return FALSE;
}

static GFile *
resolve_background_file (PpPresentation *presentation,
                         const char     *background)
{
  g_autofree char *scheme = NULL;
  g_autoptr (GFile) parent = NULL;

  scheme = g_uri_parse_scheme (background);
  if (scheme != NULL)
    {
      if (g_str_equal (scheme, "file"))
        return g_file_new_for_uri (background);
      return NULL;
    }

  if (g_path_is_absolute (background))
    return g_file_new_for_path (background);
  if (presentation->file == NULL)
    return g_file_new_for_path (background);

  parent = g_file_get_parent (presentation->file);
  if (parent == NULL)
    return g_file_new_for_path (background);
  return g_file_resolve_relative_path (parent, background);
}

static char *
guess_background_content_type (PpPresentation *presentation,
                               const char     *background)
{
  guint8 prefix[4096];
  g_autoptr (GFile) file = resolve_background_file (presentation, background);
  g_autoptr (GFileInputStream) stream = NULL;
  gboolean uncertain = TRUE;
  gssize length = 0;
  char *content_type = NULL;

  if (file != NULL)
    {
      stream = g_file_read (file, NULL, NULL);
      if (stream != NULL)
        length = g_input_stream_read (G_INPUT_STREAM (stream),
                                      prefix,
                                      sizeof prefix,
                                      NULL,
                                      NULL);
    }

  if (length > 0)
    content_type = g_content_type_guess (NULL,
                                         prefix,
                                         (gsize) length,
                                         &uncertain);
  if (content_type != NULL && !uncertain)
    return content_type;

  g_clear_pointer (&content_type, g_free);
  return g_content_type_guess (background, NULL, 0, &uncertain);
}

static gboolean
content_type_is_video (const char *content_type)
{
  g_autofree char *mime_type = NULL;

  if (content_type == NULL)
    return FALSE;
  mime_type = g_content_type_get_mime_type (content_type);
  if (mime_type == NULL)
    return FALSE;

  return g_str_has_prefix (mime_type, "video/") ||
         g_str_equal (mime_type, "image/gif") ||
         g_str_equal (mime_type, "application/ogg") ||
         g_str_equal (mime_type, "application/mxf") ||
         g_str_equal (mime_type, "application/vnd.apple.mpegurl") ||
         g_str_equal (mime_type, "application/x-mpegurl");
}

static void
classify_background (PpPresentation *presentation,
                     PpSlide        *slide)
{
  g_autofree char *content_type = NULL;
  g_autofree char *mime_type = NULL;
  GdkRGBA color;

  if (slide->background == NULL || *slide->background == '\0')
    {
      slide->background_type = PP_BACKGROUND_NONE;
      return;
    }

  if (g_ascii_strcasecmp (slide->background, "camera") == 0)
    slide->background_type = PP_BACKGROUND_CAMERA;
  else if (gdk_rgba_parse (&color, slide->background))
    slide->background_type = PP_BACKGROUND_COLOR;
  else
    {
      content_type = guess_background_content_type (presentation,
                                                     slide->background);
      if (content_type != NULL)
        mime_type = g_content_type_get_mime_type (content_type);

      if (g_strcmp0 (mime_type, "image/svg+xml") == 0 ||
          has_suffix (slide->background, ".svg"))
        slide->background_type = PP_BACKGROUND_SVG;
      else if (content_type_is_video (content_type) ||
               has_video_suffix (slide->background))
        slide->background_type = PP_BACKGROUND_VIDEO;
      else
        slide->background_type = PP_BACKGROUND_IMAGE;
    }
}

static void
finish_slide (PpPresentation *presentation,
              PpSlide        *slide,
              GString        *text,
              GString        *notes)
{
  gsize start = 0;
  gsize end = text->len;

  while (start < end && text->str[start] == '\n')
    start++;
  while (end > start && text->str[end - 1] == '\n')
    end--;

  g_free (slide->text);
  slide->text = g_strndup (text->str + start, end - start);
  if (notes->len > 0)
    replace_string (&slide->speaker_notes, notes->str);
  classify_background (presentation, slide);
  g_ptr_array_add (presentation->slides, slide);
}

static PpPresentation *
pp_presentation_parse_with_stage_color (const char  *source,
                                        GFile       *file,
                                        gboolean     ignore_comments,
                                        const char  *stage_color,
                                        GError     **error)
{
  g_autoptr (GString) text = NULL;
  g_autoptr (GString) notes = NULL;
  g_autoptr (GString) settings = NULL;
  PpPresentation *presentation;
  PpSlide *slide;
  gboolean start_of_line = TRUE;
  gboolean got_config = FALSE;
  gsize length;
  gsize position = 0;

  g_return_val_if_fail (source != NULL, NULL);
  g_return_val_if_fail (file == NULL || G_IS_FILE (file), NULL);

  if (!g_utf8_validate (source, -1, NULL))
    {
      g_set_error_literal (error,
                           PP_PRESENTATION_ERROR,
                           PP_PRESENTATION_ERROR_INVALID,
                           "The presentation is not valid UTF-8");
      return NULL;
    }

  presentation = g_new0 (PpPresentation, 1);
  presentation->ref_count = 1;
  presentation->file = file != NULL ? g_object_ref (file) : NULL;
  presentation->source = g_strdup (source);
  presentation->defaults = pp_slide_new ();
  replace_string (&presentation->defaults->stage_color, stage_color);
  presentation->slides = g_ptr_array_new_with_free_func ((GDestroyNotify) pp_slide_free);

  text = g_string_new (NULL);
  notes = g_string_new (NULL);
  settings = g_string_new (NULL);
  slide = pp_slide_copy (presentation->defaults);
  length = strlen (source);

  while (position <= length)
    {
      char current = position < length ? source[position] : '\0';

      if (current == '\\' && position + 1 < length)
        {
          position++;
          g_string_append_c (text, source[position]);
          start_of_line = FALSE;
          position++;
          continue;
        }

      if (current == '\n')
        {
          g_string_append_c (text, current);
          start_of_line = TRUE;
          position++;
          continue;
        }

      if ((current == '-' && start_of_line) || current == '\0')
        {
          PpSlide *next_slide;

          g_string_truncate (settings, 0);
          if (current != '\0')
            {
              while (position < length && source[position] != '\n')
                g_string_append_c (settings, source[position++]);
              if (position < length && source[position] == '\n')
                position++;
            }
          else
            {
              position++;
            }

          next_slide = pp_slide_copy (presentation->defaults);
          parse_config (next_slide, settings->str);

          if (!got_config)
            {
              parse_config (presentation->defaults, text->str);
              pp_slide_free (slide);
              pp_slide_free (next_slide);
              slide = pp_slide_copy (presentation->defaults);
              parse_config (slide, settings->str);
              got_config = TRUE;
            }
          else
            {
              finish_slide (presentation, slide, text, notes);
              slide = next_slide;
              next_slide = NULL;
            }

          g_string_truncate (text, 0);
          g_string_truncate (notes, 0);
          start_of_line = TRUE;

          if (current == '\0')
            break;
          continue;
        }

      if (current == '#' && start_of_line)
        {
          position++;
          while (position < length && source[position] != '\n')
            {
              if (!ignore_comments)
                g_string_append_c (notes, source[position]);
              position++;
            }
          if (!ignore_comments)
            g_string_append_c (notes, '\n');
          if (position < length && source[position] == '\n')
            position++;
          start_of_line = TRUE;
          continue;
        }

      start_of_line = FALSE;
      g_string_append_c (text, current);
      position++;
    }

  if (!got_config || presentation->slides->len == 0)
    {
      pp_slide_free (slide);
      g_set_error_literal (error,
                           PP_PRESENTATION_ERROR,
                           PP_PRESENTATION_ERROR_INVALID,
                           "The presentation does not contain a slide separator");
      pp_presentation_free (presentation);
      return NULL;
    }
  else
    {
      /* The EOF path allocates a notional following slide just like 0.1.8. */
      pp_slide_free (slide);
    }

  return presentation;
}

PpPresentation *
pp_presentation_parse (const char  *source,
                       GFile       *file,
                       gboolean     ignore_comments,
                       GError     **error)
{
  return pp_presentation_parse_with_stage_color (source,
                                                  file,
                                                  ignore_comments,
                                                  "black",
                                                  error);
}

static PpPresentation *
pp_presentation_load_with_stage_color (GFile        *file,
                                       gboolean      ignore_comments,
                                       GCancellable *cancellable,
                                       const char   *stage_color,
                                       GError      **error)
{
  g_autofree char *contents = NULL;
  gsize length = 0;
  g_autoptr (GError) local_error = NULL;

  g_return_val_if_fail (G_IS_FILE (file), NULL);

  if (!g_file_load_contents (file, cancellable, &contents, &length, NULL, &local_error))
    {
      g_set_error (error,
                   PP_PRESENTATION_ERROR,
                   PP_PRESENTATION_ERROR_IO,
                   "Unable to read presentation: %s",
                   local_error->message);
      return NULL;
    }

  return pp_presentation_parse_with_stage_color (contents,
                                                  file,
                                                  ignore_comments,
                                                  stage_color,
                                                  error);
}

PpPresentation *
pp_presentation_load (GFile        *file,
                      gboolean      ignore_comments,
                      GCancellable *cancellable,
                      GError      **error)
{
  return pp_presentation_load_with_stage_color (file,
                                                 ignore_comments,
                                                 cancellable,
                                                 "black",
                                                 error);
}

PpPresentation *
pp_presentation_load_for_pdf (GFile        *file,
                              gboolean      ignore_comments,
                              GCancellable *cancellable,
                              GError      **error)
{
  return pp_presentation_load_with_stage_color (file,
                                                 ignore_comments,
                                                 cancellable,
                                                 "white",
                                                 error);
}

PpPresentation *
pp_presentation_new_usage (void)
{
  const char *source =
    "[no-markup][transition=sheet][#c01c28]\n"
    "--\n"
    "usage: pinpoint [options] <presentation.pin>\n";

  return pp_presentation_parse (source, NULL, FALSE, NULL);
}

void
pp_presentation_free (PpPresentation *self)
{
  if (self == NULL)
    return;

  if (!g_atomic_int_dec_and_test (&self->ref_count))
    return;

  g_clear_object (&self->file);
  g_free (self->source);
  pp_slide_free (self->defaults);
  g_ptr_array_unref (self->slides);
  g_free (self);
}

PpPresentation *
pp_presentation_ref (PpPresentation *self)
{
  g_return_val_if_fail (self != NULL, NULL);
  g_atomic_int_inc (&self->ref_count);
  return self;
}

guint
pp_presentation_get_n_slides (const PpPresentation *self)
{
  g_return_val_if_fail (self != NULL, 0);
  return self->slides->len;
}

const PpSlide *
pp_presentation_get_slide (const PpPresentation *self,
                           guint                 index)
{
  g_return_val_if_fail (self != NULL, NULL);
  if (index >= self->slides->len)
    return NULL;
  return g_ptr_array_index (self->slides, index);
}

const PpSlide *
pp_presentation_get_defaults (const PpPresentation *self)
{
  g_return_val_if_fail (self != NULL, NULL);
  return self->defaults;
}

GFile *
pp_presentation_get_file (const PpPresentation *self)
{
  g_return_val_if_fail (self != NULL, NULL);
  return self->file;
}

const char *
pp_presentation_get_source (const PpPresentation *self)
{
  g_return_val_if_fail (self != NULL, NULL);
  return self->source;
}

guint
pp_presentation_first_changed_slide (const PpPresentation *old_presentation,
                                     const PpPresentation *new_presentation)
{
  const char *old_source;
  const char *new_source;
  guint separators = 0;
  gboolean start_of_line = TRUE;
  gsize position = 0;

  g_return_val_if_fail (old_presentation != NULL, 0);
  g_return_val_if_fail (new_presentation != NULL, 0);

  old_source = old_presentation->source;
  new_source = new_presentation->source;

  while (old_source[position] != '\0' &&
         new_source[position] != '\0' &&
         old_source[position] == new_source[position])
    {
      if (start_of_line && old_source[position] == '-')
        separators++;
      start_of_line = old_source[position] == '\n';
      position++;
    }

  return separators > 0 ? separators - 1 : 0;
}

static gboolean
strings_differ (const char *a,
                const char *b)
{
  return g_strcmp0 (a, b) != 0;
}

static void
append_string_setting (GString    *output,
                       const char *separator,
                       const char *name,
                       const char *value,
                       const char *reference)
{
  if (strings_differ (value, reference) && value != NULL)
    g_string_append_printf (output, "%s[%s%s]", separator, name, value);
}

static void
append_gravity (GString    *output,
                const char *separator,
                const char *name,
                PpGravity   gravity)
{
  const char *value = NULL;

  switch (gravity)
    {
    case PP_GRAVITY_TOP:         value = "top"; break;
    case PP_GRAVITY_BOTTOM:      value = "bottom"; break;
    case PP_GRAVITY_LEFT:        value = "left"; break;
    case PP_GRAVITY_RIGHT:       value = "right"; break;
    case PP_GRAVITY_TOP_LEFT:    value = "top-left"; break;
    case PP_GRAVITY_TOP_RIGHT:   value = "top-right"; break;
    case PP_GRAVITY_BOTTOM_LEFT: value = "bottom-left"; break;
    case PP_GRAVITY_BOTTOM_RIGHT:value = "bottom-right"; break;
    case PP_GRAVITY_CENTER:      break;
    }

  if (value != NULL)
    g_string_append_printf (output, "%s[%s%s]", separator, name, value);
}

static const char *
transition_direction_name (PpTransitionDirection direction)
{
  static const char *const values[] = {
    [PP_TRANSITION_DIRECTION_LEFT] = "left",
    [PP_TRANSITION_DIRECTION_RIGHT] = "right",
    [PP_TRANSITION_DIRECTION_UP] = "up",
    [PP_TRANSITION_DIRECTION_DOWN] = "down",
  };

  return values[direction];
}

static const char *
transition_layer_name (PpTransitionLayer layer)
{
  static const char *const values[] = {
    [PP_TRANSITION_LAYER_DEFAULT] = "default",
    [PP_TRANSITION_LAYER_ALL] = "all",
    [PP_TRANSITION_LAYER_BACKGROUND] = "background",
    [PP_TRANSITION_LAYER_TEXT] = "text",
  };

  return values[layer];
}

static const char *
transition_mode_name (PpTransitionMode mode)
{
  static const char *const values[] = {
    [PP_TRANSITION_MODE_BOTH] = "both",
    [PP_TRANSITION_MODE_IN] = "in",
    [PP_TRANSITION_MODE_OUT] = "out",
  };

  return values[mode];
}

static void
append_slide_config (GString       *output,
                     const PpSlide *slide,
                     const PpSlide *reference,
                     const char    *separator)
{
  append_string_setting (output, separator, "stage-color=",
                         slide->stage_color, reference->stage_color);
  append_string_setting (output, separator, "",
                         slide->background, reference->background);

  if (slide->background_scale != reference->background_scale)
    {
      static const char *const values[] = {
        [PP_BACKGROUND_UNSCALED] = "unscaled",
        [PP_BACKGROUND_FIT] = "fit",
        [PP_BACKGROUND_FILL] = "fill",
        [PP_BACKGROUND_STRETCH] = "stretch",
      };

      g_string_append_printf (output, "%s[%s]", separator,
                              values[slide->background_scale]);
    }

  if (slide->background_position != reference->background_position)
    append_gravity (output, separator, "bg-position=",
                    slide->background_position);

  if (slide->text_align != reference->text_align)
    {
      static const char *const values[] = {
        [PP_TEXT_ALIGN_LEFT] = "left",
        [PP_TEXT_ALIGN_CENTER] = "center",
        [PP_TEXT_ALIGN_RIGHT] = "right",
      };

      g_string_append_printf (output, "%s[text-align=%s]", separator,
                              values[slide->text_align]);
    }

  if (slide->text_position != reference->text_position)
    append_gravity (output, separator, "", slide->text_position);

  append_string_setting (output, separator, "font=",
                         slide->font, reference->font);
  append_string_setting (output, separator, "text-color=",
                         slide->text_color, reference->text_color);
  append_string_setting (output, separator, "shading-color=",
                         slide->shading_color, reference->shading_color);
  if (slide->shading_opacity != reference->shading_opacity)
    g_string_append_printf (output, "%s[shading-opacity=%f]", separator,
                            slide->shading_opacity);
  append_string_setting (output, separator, "transition=",
                         slide->transition, reference->transition);
  if (slide->transition_direction != reference->transition_direction)
    g_string_append_printf (
      output,
      "%s[transition-direction=%s]",
      separator,
      transition_direction_name (slide->transition_direction));
  if (slide->transition_layer != reference->transition_layer)
    g_string_append_printf (output,
                            "%s[transition-layer=%s]",
                            separator,
                            transition_layer_name (slide->transition_layer));
  if (slide->transition_mode != reference->transition_mode)
    g_string_append_printf (output,
                            "%s[transition-mode=%s]",
                            separator,
                            transition_mode_name (slide->transition_mode));
  if (slide->transition_duration_ms != reference->transition_duration_ms)
    g_string_append_printf (output,
                            "%s[transition-duration=%u]",
                            separator,
                            slide->transition_duration_ms);
  append_string_setting (output, separator, "transition-easing=",
                         slide->transition_easing,
                         reference->transition_easing);
  append_string_setting (output, separator, "command=",
                         slide->command, reference->command);
  if (slide->duration != 0.0 && slide->duration != reference->duration)
    g_string_append_printf (output, "%s[duration=%f]", separator,
                            slide->duration);
  if (slide->camera_framerate != reference->camera_framerate)
    g_string_append_printf (output, "%s[camera-framerate=%d]", separator,
                            slide->camera_framerate);

  /* Keep the missing separator and two-axis comparison from 0.1.8. */
  if (slide->camera_resolution.width != reference->camera_resolution.width &&
      slide->camera_resolution.height != reference->camera_resolution.height)
    g_string_append_printf (output, "[camera-resolution=%dx%d]",
                            slide->camera_resolution.width,
                            slide->camera_resolution.height);

  if (slide->use_markup != reference->use_markup)
    g_string_append_printf (output, "%s[%s]", separator,
                            slide->use_markup ? "markup" : "no-markup");
}

char *
pp_presentation_serialize (const PpPresentation *self)
{
  g_autoptr (GString) output = NULL;
  PpSlide *built_in_defaults;

  g_return_val_if_fail (self != NULL, NULL);

  output = g_string_new ("#!/usr/bin/env pinpoint\n");
  built_in_defaults = pp_slide_new ();
  append_slide_config (output, self->defaults, built_in_defaults, "\n");
  pp_slide_free (built_in_defaults);

  for (guint i = 0; i < self->slides->len; i++)
    {
      const PpSlide *slide = g_ptr_array_index (self->slides, i);

      g_string_append (output, "\n--");
      append_slide_config (output, slide, self->defaults, " ");
      g_string_append_c (output, '\n');
      g_string_append_printf (output, "%s\n", slide->text);

      if (slide->speaker_notes != NULL)
        {
          g_string_append_c (output, '#');
          for (const char *p = slide->speaker_notes; *p != '\0'; p++)
            {
              g_string_append_c (output, *p);
              if (*p == '\n' && p[1] != '\0')
                g_string_append_c (output, '#');
            }
        }
    }

  return g_string_free (g_steal_pointer (&output), FALSE);
}

void
pp_presentation_rehearsal_reset (PpPresentation *self)
{
  g_return_if_fail (self != NULL);

  for (guint i = 0; i < self->slides->len; i++)
    {
      PpSlide *slide = g_ptr_array_index (self->slides, i);
      slide->new_duration = 0.0;
    }
}

void
pp_presentation_rehearsal_record (PpPresentation *self,
                                  guint           index,
                                  double          seconds)
{
  PpSlide *slide;

  g_return_if_fail (self != NULL);
  g_return_if_fail (index < self->slides->len);
  g_return_if_fail (seconds >= 0.0);

  slide = g_ptr_array_index (self->slides, index);
  slide->new_duration += seconds;
}

gboolean
pp_presentation_rehearsal_finish (PpPresentation *self,
                                  GError        **error)
{
  g_autofree char *serialized = NULL;
  gsize length;

  g_return_val_if_fail (self != NULL, FALSE);
  g_return_val_if_fail (self->file != NULL, FALSE);

  for (guint i = 0; i < self->slides->len; i++)
    {
      PpSlide *slide = g_ptr_array_index (self->slides, i);
      slide->duration = slide->new_duration;
    }

  serialized = pp_presentation_serialize (self);
  length = strlen (serialized);
  if (!g_file_replace_contents (self->file,
                                serialized,
                                length,
                                NULL,
                                FALSE,
                                G_FILE_CREATE_REPLACE_DESTINATION,
                                NULL,
                                NULL,
                                error))
    return FALSE;

  g_free (self->source);
  self->source = g_steal_pointer (&serialized);
  return TRUE;
}
