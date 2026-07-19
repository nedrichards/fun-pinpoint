#include "config.h"

#include "pp-transition.h"

#include "pp-render.h"

#include <json-glib/json-glib.h>
#include <math.h>

typedef enum
{
  LEGACY_STATE_PRE,
  LEGACY_STATE_SHOW,
  LEGACY_STATE_POST,
  LEGACY_STATE_COUNT,
} LegacyState;

struct _PpLegacyTransition
{
  PpTransitionState states[LEGACY_STATE_COUNT];
  guint durations[LEGACY_STATE_COUNT];
  char *easing[LEGACY_STATE_COUNT];
};

static PpTransitionLayerState
layer_identity (void)
{
  return (PpTransitionLayerState) {
    .scale_x = 1.0f,
    .scale_y = 1.0f,
    .opacity = 1.0f,
  };
}

static PpTransitionState
state_identity (void)
{
  PpTransitionLayerState identity = layer_identity ();

  return (PpTransitionState) {
    .actor = identity,
    .background = identity,
    .midground = identity,
    .foreground = identity,
  };
}

gboolean
pp_transition_is_builtin (const char *name)
{
  static const char *const names[] = {
    "fade", "slide-left", "slide-up", "slide-in-left",
    "text-slide-left", "text-slide-up", "text-slide-down",
    "spin", "spin-bg", "spin-text", "sheet", "swing",
    "page-curl", "page-curl-both", "action",
    "slide", "zoom", "scale", "flip", NULL,
  };

  for (guint i = 0; names[i] != NULL; i++)
    if (g_str_equal (name, names[i]))
      return TRUE;
  return FALSE;
}

static gboolean
state_from_name (const char *name,
                 LegacyState *state)
{
  if (g_str_equal (name, "pre"))
    *state = LEGACY_STATE_PRE;
  else if (g_str_equal (name, "show"))
    *state = LEGACY_STATE_SHOW;
  else if (g_str_equal (name, "post"))
    *state = LEGACY_STATE_POST;
  else
    return FALSE;
  return TRUE;
}

static PpTransitionLayerState *
layer_from_name (PpTransitionState *state,
                 const char        *name)
{
  if (g_str_equal (name, "actor"))
    return &state->actor;
  if (g_str_equal (name, "background"))
    return &state->background;
  if (g_str_equal (name, "midground"))
    return &state->midground;
  if (g_str_equal (name, "foreground"))
    return &state->foreground;
  return NULL;
}

static gboolean
set_layer_property (PpTransitionLayerState *layer,
                    const char             *property,
                    double                  value)
{
  if (g_str_equal (property, "x"))
    layer->x = (float) value;
  else if (g_str_equal (property, "y"))
    layer->y = (float) value;
  else if (g_str_equal (property, "scale-x"))
    layer->scale_x = (float) value;
  else if (g_str_equal (property, "scale-y"))
    layer->scale_y = (float) value;
  else if (g_str_equal (property, "rotation-angle-z"))
    layer->angle = (float) value;
  else if (g_str_equal (property, "rotation-angle-x"))
    layer->angle_x = (float) value;
  else if (g_str_equal (property, "rotation-angle-y"))
    layer->angle_y = (float) value;
  else if (g_str_equal (property, "opacity"))
    layer->opacity = (float) CLAMP (value / 255.0, 0.0, 1.0);
  else
    return FALSE;
  return TRUE;
}

static guint
parse_transition_object (PpLegacyTransition *transition,
                         JsonObject         *object,
                         guint               default_duration,
                         guint              *unsupported)
{
  JsonArray *keys;
  const char *target;
  LegacyState state;
  guint recognized = 0;

  if (!json_object_has_member (object, "target"))
    return 0;
  target = json_object_get_string_member (object, "target");
  if (target == NULL || !state_from_name (target, &state))
    return 0;

  transition->durations[state] = json_object_has_member (object, "duration")
    ? (guint) MAX (json_object_get_int_member (object, "duration"), 0)
    : default_duration;
  if (!json_object_has_member (object, "keys"))
    return 0;
  keys = json_object_get_array_member (object, "keys");
  if (keys == NULL)
    return 0;

  for (guint i = 0; i < json_array_get_length (keys); i++)
    {
      JsonArray *key = json_array_get_array_element (keys, i);
      PpTransitionLayerState *layer;
      const char *layer_name;
      const char *property;
      const char *easing;
      JsonNode *value_node;

      if (key == NULL || json_array_get_length (key) < 4)
        {
          (*unsupported)++;
          continue;
        }
      layer_name = json_array_get_string_element (key, 0);
      property = json_array_get_string_element (key, 1);
      easing = json_array_get_string_element (key, 2);
      value_node = json_array_get_element (key, 3);
      layer = layer_from_name (&transition->states[state], layer_name);
      if (layer == NULL || value_node == NULL ||
          !JSON_NODE_HOLDS_VALUE (value_node))
        {
          (*unsupported)++;
          continue;
        }
      if (set_layer_property (layer,
                              property,
                              json_node_get_double (value_node)))
        {
          recognized++;
          if (transition->easing[state] == NULL && easing != NULL)
            transition->easing[state] = g_strdup (easing);
        }
      else
        {
          (*unsupported)++;
        }
    }
  return recognized;
}

GFile *
pp_legacy_transition_resolve_file (const PpPresentation *presentation,
                                   const char           *name)
{
  g_autofree char *filename = NULL;

  g_return_val_if_fail (presentation != NULL, NULL);
  g_return_val_if_fail (name != NULL, NULL);

  filename = g_str_has_suffix (name, ".json")
    ? g_strdup (name)
    : g_strconcat (name, ".json", NULL);
  return pp_render_resolve_asset (presentation, filename);
}

PpLegacyTransition *
pp_legacy_transition_load (const PpPresentation *presentation,
                           const char           *name,
                           GError              **error)
{
  g_autofree char *contents = NULL;
  gsize contents_length = 0;
  g_autoptr (GFile) file = NULL;
  g_autoptr (JsonParser) parser = NULL;
  JsonNode *root;
  JsonArray *objects;
  JsonObject *state_object = NULL;
  PpLegacyTransition *transition;
  guint default_duration;
  guint recognized = 0;
  guint unsupported = 0;

  g_return_val_if_fail (presentation != NULL, NULL);
  g_return_val_if_fail (name != NULL, NULL);

  file = pp_legacy_transition_resolve_file (presentation, name);
  parser = json_parser_new ();
  if (!g_file_load_contents (file,
                             NULL,
                             &contents,
                             &contents_length,
                             NULL,
                             error) ||
      !json_parser_load_from_data (parser,
                                   contents,
                                   (gssize) contents_length,
                                   error))
    return NULL;
  root = json_parser_get_root (parser);
  if (root == NULL || !JSON_NODE_HOLDS_ARRAY (root))
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_INVALID_DATA,
                           "Legacy transition JSON must contain a top-level array");
      return NULL;
    }

  objects = json_node_get_array (root);
  for (guint i = 0; i < json_array_get_length (objects); i++)
    {
      JsonObject *object = json_array_get_object_element (objects, i);
      const char *type;

      if (object == NULL || !json_object_has_member (object, "type"))
        continue;
      type = json_object_get_string_member (object, "type");
      if (g_strcmp0 (type, "ClutterState") == 0)
        {
          state_object = object;
          break;
        }
      if (json_object_has_member (object, "effects"))
        unsupported++;
    }
  if (state_object == NULL ||
      !json_object_has_member (state_object, "transitions"))
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_INVALID_DATA,
                           "Legacy transition JSON has no ClutterState transitions");
      return NULL;
    }

  transition = g_new0 (PpLegacyTransition, 1);
  for (guint i = 0; i < LEGACY_STATE_COUNT; i++)
    transition->states[i] = state_identity ();
  default_duration = json_object_has_member (state_object, "duration")
    ? (guint) MAX (json_object_get_int_member (state_object, "duration"), 0)
    : 1000;
  for (guint i = 0; i < LEGACY_STATE_COUNT; i++)
    transition->durations[i] = default_duration;

  objects = json_object_get_array_member (state_object, "transitions");
  for (guint i = 0; i < json_array_get_length (objects); i++)
    {
      JsonObject *object = json_array_get_object_element (objects, i);

      if (object != NULL)
        recognized += parse_transition_object (transition,
                                               object,
                                               default_duration,
                                               &unsupported);
    }
  if (recognized == 0)
    {
      pp_legacy_transition_free (transition);
      g_set_error_literal (
        error,
        G_IO_ERROR,
        G_IO_ERROR_NOT_SUPPORTED,
        "The transition has no supported actor, background, midground, or foreground properties");
      return NULL;
    }
  if (unsupported > 0)
    g_warning ("Legacy transition “%s” ignores %u unsupported custom actor, effect, or property entr%s",
               name,
               unsupported,
               unsupported == 1 ? "y" : "ies");
  return transition;
}

void
pp_legacy_transition_free (PpLegacyTransition *transition)
{
  if (transition == NULL)
    return;
  for (guint i = 0; i < LEGACY_STATE_COUNT; i++)
    g_free (transition->easing[i]);
  g_free (transition);
}

guint
pp_legacy_transition_get_duration (PpLegacyTransition *transition,
                                   gboolean            incoming,
                                   gboolean            backwards)
{
  g_return_val_if_fail (transition != NULL, 0);

  if (incoming)
    return transition->durations[LEGACY_STATE_SHOW];
  return transition->durations[backwards ? LEGACY_STATE_PRE
                                          : LEGACY_STATE_POST];
}

double
pp_transition_apply_easing (const char *name,
                            double      progress)
{
  g_autofree char *normalized = NULL;
  GString *buffer;

  if (name == NULL || g_ascii_strcasecmp (name, "linear") == 0)
    return progress;
  buffer = g_string_new (NULL);
  for (const char *p = name; *p != '\0'; p++)
    if (g_ascii_isalnum (*p))
      g_string_append_c (buffer, g_ascii_tolower (*p));
  normalized = g_string_free (buffer, FALSE);

  if (g_str_equal (normalized, "easeinquad"))
    return progress * progress;
  if (g_str_equal (normalized, "easeoutquad"))
    return 1.0 - (1.0 - progress) * (1.0 - progress);
  if (g_str_equal (normalized, "easeinoutquad"))
    return progress < 0.5
      ? 2.0 * progress * progress
      : 1.0 - pow (-2.0 * progress + 2.0, 2.0) / 2.0;
  if (g_str_equal (normalized, "easein") ||
      g_str_equal (normalized, "easeincubic"))
    return progress * progress * progress;
  if (g_str_equal (normalized, "easeout") ||
      g_str_equal (normalized, "easeoutcubic"))
    return 1.0 - pow (1.0 - progress, 3.0);
  if (g_str_equal (normalized, "easeinout") ||
      g_str_equal (normalized, "easeinoutcubic"))
    return progress < 0.5
      ? 4.0 * progress * progress * progress
      : 1.0 - pow (-2.0 * progress + 2.0, 3.0) / 2.0;
  if (g_str_equal (normalized, "easeoutquint"))
    return 1.0 - pow (1.0 - progress, 5.0);
  return progress;
}

static PpTransitionLayerState
interpolate_layer (const PpTransitionLayerState *from,
                   const PpTransitionLayerState *to,
                   double                        progress)
{
#define INTERPOLATE(member) \
  result.member = (float) (from->member + (to->member - from->member) * progress)
  PpTransitionLayerState result;

  INTERPOLATE (x);
  INTERPOLATE (y);
  INTERPOLATE (scale_x);
  INTERPOLATE (scale_y);
  INTERPOLATE (angle);
  INTERPOLATE (angle_x);
  INTERPOLATE (angle_y);
  INTERPOLATE (opacity);
  return result;
#undef INTERPOLATE
}

void
pp_legacy_transition_calculate (PpLegacyTransition *transition,
                                gboolean            incoming,
                                gboolean            backwards,
                                double              progress,
                                PpTransitionState  *state)
{
  LegacyState from_state;
  LegacyState to_state;
  const PpTransitionState *from;
  const PpTransitionState *to;

  g_return_if_fail (transition != NULL);
  g_return_if_fail (state != NULL);

  if (incoming)
    {
      from_state = backwards ? LEGACY_STATE_POST : LEGACY_STATE_PRE;
      to_state = LEGACY_STATE_SHOW;
    }
  else
    {
      from_state = LEGACY_STATE_SHOW;
      to_state = backwards ? LEGACY_STATE_PRE : LEGACY_STATE_POST;
    }
  progress = pp_transition_apply_easing (transition->easing[to_state],
                                         CLAMP (progress, 0.0, 1.0));
  from = &transition->states[from_state];
  to = &transition->states[to_state];
  state->actor = interpolate_layer (&from->actor, &to->actor, progress);
  state->background = interpolate_layer (&from->background,
                                         &to->background,
                                         progress);
  state->midground = interpolate_layer (&from->midground,
                                        &to->midground,
                                        progress);
  state->foreground = interpolate_layer (&from->foreground,
                                         &to->foreground,
                                         progress);
}
