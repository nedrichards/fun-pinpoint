#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

typedef enum
{
  PP_GRAVITY_CENTER,
  PP_GRAVITY_TOP,
  PP_GRAVITY_BOTTOM,
  PP_GRAVITY_LEFT,
  PP_GRAVITY_RIGHT,
  PP_GRAVITY_TOP_LEFT,
  PP_GRAVITY_TOP_RIGHT,
  PP_GRAVITY_BOTTOM_LEFT,
  PP_GRAVITY_BOTTOM_RIGHT,
} PpGravity;

typedef enum
{
  PP_TEXT_ALIGN_LEFT,
  PP_TEXT_ALIGN_CENTER,
  PP_TEXT_ALIGN_RIGHT,
} PpTextAlign;

typedef enum
{
  PP_BACKGROUND_NONE,
  PP_BACKGROUND_COLOR,
  PP_BACKGROUND_IMAGE,
  PP_BACKGROUND_VIDEO,
  PP_BACKGROUND_CAMERA,
  PP_BACKGROUND_SVG,
} PpBackgroundType;

typedef enum
{
  PP_BACKGROUND_UNSCALED,
  PP_BACKGROUND_FIT,
  PP_BACKGROUND_FILL,
  PP_BACKGROUND_STRETCH,
} PpBackgroundScale;

typedef enum
{
  PP_TRANSITION_DIRECTION_LEFT,
  PP_TRANSITION_DIRECTION_RIGHT,
  PP_TRANSITION_DIRECTION_UP,
  PP_TRANSITION_DIRECTION_DOWN,
} PpTransitionDirection;

typedef enum
{
  PP_TRANSITION_LAYER_DEFAULT,
  PP_TRANSITION_LAYER_ALL,
  PP_TRANSITION_LAYER_BACKGROUND,
  PP_TRANSITION_LAYER_TEXT,
} PpTransitionLayer;

typedef enum
{
  PP_TRANSITION_MODE_BOTH,
  PP_TRANSITION_MODE_IN,
  PP_TRANSITION_MODE_OUT,
} PpTransitionMode;

typedef struct
{
  int width;
  int height;
} PpResolution;

typedef struct _PpSlide
{
  char *stage_color;
  char *background;
  PpBackgroundType background_type;
  PpBackgroundScale background_scale;
  PpGravity background_position;

  char *text;
  PpGravity text_position;
  char *font;
  char *notes_font;
  char *notes_font_size;
  PpTextAlign text_align;
  char *text_color;
  gboolean use_markup;

  double duration;
  double new_duration;
  char *speaker_notes;
  char *visual_description;

  char *shading_color;
  double shading_opacity;
  char *transition;
  PpTransitionDirection transition_direction;
  PpTransitionLayer transition_layer;
  PpTransitionMode transition_mode;
  guint transition_duration_ms;
  char *transition_easing;
  char *command;

  int camera_framerate;
  PpResolution camera_resolution;
} PpSlide;

typedef struct _PpPresentation PpPresentation;

#define PP_PRESENTATION_ERROR (pp_presentation_error_quark ())

typedef enum
{
  PP_PRESENTATION_ERROR_INVALID,
  PP_PRESENTATION_ERROR_IO,
} PpPresentationError;

GQuark pp_presentation_error_quark (void);

PpPresentation *pp_presentation_load (GFile        *file,
                                      gboolean      ignore_comments,
                                      GCancellable *cancellable,
                                      GError      **error);
PpPresentation *pp_presentation_load_for_pdf (GFile        *file,
                                              gboolean      ignore_comments,
                                              GCancellable *cancellable,
                                              GError      **error);
PpPresentation *pp_presentation_parse (const char  *source,
                                       GFile       *file,
                                       gboolean     ignore_comments,
                                       GError     **error);
PpPresentation *pp_presentation_new_usage (void);
PpPresentation *pp_presentation_ref (PpPresentation *self);
void            pp_presentation_free (PpPresentation *self);

guint           pp_presentation_get_n_slides (const PpPresentation *self);
const PpSlide  *pp_presentation_get_slide (const PpPresentation *self,
                                           guint                 index);
const PpSlide  *pp_presentation_get_defaults (const PpPresentation *self);
GFile          *pp_presentation_get_file (const PpPresentation *self);
const char     *pp_presentation_get_source (const PpPresentation *self);
guint           pp_presentation_first_changed_slide (const PpPresentation *old_presentation,
                                                      const PpPresentation *new_presentation);
char           *pp_presentation_serialize (const PpPresentation *self);
void            pp_presentation_rehearsal_reset (PpPresentation *self);
void            pp_presentation_rehearsal_record (PpPresentation *self,
                                                   guint           index,
                                                   double          seconds);
gboolean        pp_presentation_rehearsal_finish (PpPresentation *self,
                                                  GError        **error);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (PpPresentation, pp_presentation_free)

G_END_DECLS
