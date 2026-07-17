#include "config.h"

#include "pp-speaker.h"

#include <math.h>

typedef struct
{
  GtkWidget parent_instance;
  GtkWidget *previous;
  GtkWidget *current;
  GtkWidget *next;
  GtkWidget *notes;
} PpSpeakerContent;

typedef GtkWidgetClass PpSpeakerContentClass;

G_DEFINE_FINAL_TYPE (PpSpeakerContent, pp_speaker_content, GTK_TYPE_WIDGET)

static void
allocate_child (GtkWidget *child,
                int        width,
                int        height,
                float      x,
                float      y)
{
  graphene_point_t point = GRAPHENE_POINT_INIT (x, y);
  GskTransform *transform = gsk_transform_translate (NULL, &point);

  gtk_widget_allocate (child, width, height, -1, transform);
}

static void
pp_speaker_content_measure (GtkWidget      *widget,
                            GtkOrientation  orientation,
                            int             for_size,
                            int            *minimum,
                            int            *natural,
                            int            *minimum_baseline,
                            int            *natural_baseline)
{
  (void) widget;
  (void) for_size;
  if (orientation == GTK_ORIENTATION_HORIZONTAL)
    {
      *minimum = 480;
      *natural = 800;
    }
  else
    {
      *minimum = 360;
      *natural = 600;
    }
  *minimum_baseline = -1;
  *natural_baseline = -1;
}

static void
pp_speaker_content_size_allocate (GtkWidget *widget,
                                  int        width,
                                  int        height,
                                  int        baseline)
{
  PpSpeakerContent *self = (PpSpeakerContent *) widget;
  int preview_height = MAX ((int) round (height * 0.4), 1);
  int preview_width = MAX ((int) round (preview_height * 4.0 / 3.0), 1);
  int notes_x = preview_width + 20;
  int notes_width = MAX (width - preview_width - 40, 1);
  int notes_height = MAX (height - 90, 1);

  (void) baseline;
  allocate_child (self->previous,
                  preview_width,
                  preview_height,
                  0,
                  (float) height * -0.1f - 2.0f);
  allocate_child (self->current,
                  preview_width,
                  preview_height,
                  0,
                  (float) height * 0.3f);
  allocate_child (self->next,
                  preview_width,
                  preview_height,
                  0,
                  (float) height * 0.7f + 2.0f);
  allocate_child (self->notes, notes_width, notes_height, notes_x, 50.0f);
}

static void
pp_speaker_content_snapshot (GtkWidget   *widget,
                             GtkSnapshot *snapshot)
{
  PpSpeakerContent *self = (PpSpeakerContent *) widget;
  int width = gtk_widget_get_width (widget);
  int height = gtk_widget_get_height (widget);
  int preview_height = MAX ((int) round (height * 0.4), 1);
  float preview_width = (float) preview_height * 4.0f / 3.0f + 2.0f;
  const GdkRGBA black = { 0.0, 0.0, 0.0, 1.0 };
  const GdkRGBA light_gray = { 0.867, 0.867, 0.867, 1.0 };
  graphene_rect_t bounds = GRAPHENE_RECT_INIT (0, 0, width, height);
  graphene_rect_t preview_bar = GRAPHENE_RECT_INIT (0,
                                                    0,
                                                    preview_width,
                                                    height);

  gtk_snapshot_append_color (snapshot, &black, &bounds);
  gtk_snapshot_append_color (snapshot, &light_gray, &preview_bar);
  gtk_widget_snapshot_child (widget, self->previous, snapshot);
  gtk_widget_snapshot_child (widget, self->current, snapshot);
  gtk_widget_snapshot_child (widget, self->next, snapshot);
  gtk_widget_snapshot_child (widget, self->notes, snapshot);
}

static void
pp_speaker_content_dispose (GObject *object)
{
  PpSpeakerContent *self = (PpSpeakerContent *) object;

  g_clear_pointer (&self->previous, gtk_widget_unparent);
  g_clear_pointer (&self->current, gtk_widget_unparent);
  g_clear_pointer (&self->next, gtk_widget_unparent);
  g_clear_pointer (&self->notes, gtk_widget_unparent);
  G_OBJECT_CLASS (pp_speaker_content_parent_class)->dispose (object);
}

static void
pp_speaker_content_class_init (PpSpeakerContentClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = pp_speaker_content_dispose;
  widget_class->measure = pp_speaker_content_measure;
  widget_class->size_allocate = pp_speaker_content_size_allocate;
  widget_class->snapshot = pp_speaker_content_snapshot;
}

static void
pp_speaker_content_init (PpSpeakerContent *self)
{
  (void) self;
  gtk_widget_set_overflow (GTK_WIDGET (self), GTK_OVERFLOW_HIDDEN);
}

static GtkWidget *
pp_speaker_content_new (GtkWidget *previous,
                        GtkWidget *current,
                        GtkWidget *next,
                        GtkWidget *notes)
{
  PpSpeakerContent *self = g_object_new (pp_speaker_content_get_type (), NULL);

  self->previous = previous;
  self->current = current;
  self->next = next;
  self->notes = notes;
  gtk_widget_set_parent (previous, GTK_WIDGET (self));
  gtk_widget_set_parent (current, GTK_WIDGET (self));
  gtk_widget_set_parent (next, GTK_WIDGET (self));
  gtk_widget_set_parent (notes, GTK_WIDGET (self));
  return GTK_WIDGET (self);
}

struct _PpSpeaker
{
  GtkApplication *application;
  PpStage *audience_stage;
  GtkWindow *window;
  PpStage *previous_preview;
  PpStage *current_preview;
  PpStage *next_preview;
  GtkLabel *notes;
  GtkLabel *remaining;
  GtkDrawingArea *progress;
  GtkButton *start_button;
  GtkButton *pause_button;
  GtkButton *rehearse_button;
  GtkToggleButton *autoadvance_button;
  GTimer *timer;
  PpPresentation *rehearsal_presentation;
  double slide_started;
  guint timed_slide;
  guint tick_id;
  gboolean running;
  gboolean paused;
  gboolean rehearsing;
  double elapsed;
  double slide_elapsed;
  double total_seconds;
  double slide_plan_start;
  double slide_plan_duration;
  double warning_opacity;
  PpSpeakerFullscreenRequestFunc fullscreen_request;
  gpointer fullscreen_request_data;
};

static void update_previews (PpSpeaker *self);

static void
set_preview (PpStage              *preview,
             const PpPresentation *presentation,
             int                   index)
{
  guint count = pp_presentation_get_n_slides (presentation);

  if (index < 0 || (guint) index >= count)
    {
      gtk_widget_set_visible (GTK_WIDGET (preview), FALSE);
      return;
    }

  gtk_widget_set_visible (GTK_WIDGET (preview), TRUE);
  pp_stage_set_presentation (preview,
                             pp_presentation_ref ((PpPresentation *) presentation),
                             (guint) index);
}

static char *
format_time (double seconds)
{
  int value = (int) floor (seconds + 0.5);

  if (value <= -60)
    return g_strdup_printf ("%dmin", value / 60);
  if (value <= 10)
    return g_strdup_printf ("%ds", value);
  if (value <= 60)
    return g_strdup_printf ("%ds", ((value + 4) / 5) * 5);
  return g_strdup_printf ("%d%smin",
                          value / 60,
                          value % 60 > 30 ? "½" : "");
}

static void
progress_draw_cb (GtkDrawingArea *area,
                  cairo_t        *cr,
                  int             width,
                  int             height,
                  gpointer        user_data)
{
  PpSpeaker *self = user_data;
  double elapsed_fraction = self->total_seconds > 0.0
    ? CLAMP (self->elapsed / self->total_seconds, 0.0, 1.0)
    : 0.0;

  (void) area;
  cairo_set_source_rgb (cr, 0x11 / 255.0, 0x11 / 255.0, 0x11 / 255.0);
  cairo_paint (cr);

  if (self->warning_opacity > 0.0)
    {
      cairo_set_source_rgba (cr, 1.0, 0.0, 0.0, self->warning_opacity);
      cairo_paint (cr);
    }

  cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, 0x55 / 255.0);
  cairo_rectangle (cr,
                   width * elapsed_fraction,
                   0,
                   width * (1.0 - elapsed_fraction),
                   height * 0.84);
  cairo_fill (cr);

  cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, 0x77 / 255.0);
  cairo_rectangle (cr,
                   width * self->slide_plan_start,
                   height * 0.05,
                   width * self->slide_plan_duration,
                   height * 0.7);
  cairo_fill (cr);
}

static gboolean
timer_cb (gpointer user_data)
{
  PpSpeaker *self = user_data;
  const PpPresentation *presentation = pp_stage_get_presentation (self->audience_stage);
  guint index;
  const PpSlide *slide;
  double elapsed;
  double slide_elapsed;
  double total_seconds;
  double warning_time;
  g_autofree char *remaining = NULL;

  if (presentation == NULL)
    return G_SOURCE_CONTINUE;

  index = pp_stage_get_current_slide (self->audience_stage);
  slide = pp_presentation_get_slide (presentation, index);
  elapsed = self->running ? g_timer_elapsed (self->timer, NULL) : 0.0;
  slide_elapsed = MAX (elapsed - self->slide_started, 0.0);
  total_seconds = pp_presentation_get_defaults (presentation)->duration * 60.0;
  remaining = format_time (total_seconds - elapsed);
  gtk_label_set_text (self->remaining, remaining);
  warning_time = MAX (2.0, slide->duration * 0.33);
  if (slide->duration > 0.0 && slide_elapsed >= slide->duration)
    self->warning_opacity = 140.0 / 255.0;
  else if (slide->duration > 0.0 &&
           slide->duration - slide_elapsed < warning_time)
    self->warning_opacity = 96.0 / 255.0;
  else
    self->warning_opacity = 0.0;
  self->elapsed = elapsed;
  self->slide_elapsed = slide_elapsed;
  self->total_seconds = total_seconds;
  gtk_widget_queue_draw (GTK_WIDGET (self->progress));

  if (self->running &&
      !self->paused &&
      gtk_toggle_button_get_active (self->autoadvance_button) &&
      slide->duration > 0.0 &&
      slide_elapsed >= slide->duration)
    {
      if (!pp_stage_next (self->audience_stage))
        gtk_toggle_button_set_active (self->autoadvance_button, FALSE);
    }

  return G_SOURCE_CONTINUE;
}

static void
audience_slide_changed_cb (PpStage *stage,
                           guint    index,
                           gpointer user_data)
{
  PpSpeaker *self = user_data;
  const PpPresentation *presentation = pp_stage_get_presentation (stage);
  double now = self->running ? g_timer_elapsed (self->timer, NULL) : 0.0;

  if (self->rehearsing &&
      presentation != self->rehearsal_presentation)
    {
      g_warning ("Rehearsal cancelled because the presentation was reloaded");
      self->rehearsing = FALSE;
      g_clear_pointer (&self->rehearsal_presentation, pp_presentation_free);
      gtk_button_set_label (self->rehearse_button, "Rehearse");
    }
  else if (self->rehearsing && self->running)
    {
      pp_presentation_rehearsal_record (self->rehearsal_presentation,
                                        self->timed_slide,
                                        MAX (now - self->slide_started, 0.0));
    }

  self->timed_slide = index;
  self->slide_started = now;
  update_previews (self);
}

static void
presentation_ended_cb (PpStage *stage,
                       gpointer user_data)
{
  PpSpeaker *self = user_data;
  g_autoptr (GError) error = NULL;
  double now;

  (void) stage;
  gtk_toggle_button_set_active (self->autoadvance_button, FALSE);
  if (!self->rehearsing || !self->running)
    return;

  now = g_timer_elapsed (self->timer, NULL);
  pp_presentation_rehearsal_record (self->rehearsal_presentation,
                                    self->timed_slide,
                                    MAX (now - self->slide_started, 0.0));
  self->rehearsing = FALSE;
  if (!pp_presentation_rehearsal_finish (self->rehearsal_presentation, &error))
    g_warning ("Unable to save rehearsal timings: %s", error->message);
  else
    g_print ("Saved rehearsal timings\n");
  g_clear_pointer (&self->rehearsal_presentation, pp_presentation_free);
  gtk_button_set_label (self->rehearse_button, "Rehearse");
}

static void
restart_timer (PpSpeaker *self,
               gboolean   rehearse)
{
  const PpPresentation *presentation = pp_stage_get_presentation (self->audience_stage);

  self->rehearsing = FALSE;
  g_clear_pointer (&self->rehearsal_presentation, pp_presentation_free);
  pp_stage_first (self->audience_stage);
  g_timer_start (self->timer);
  self->slide_started = 0.0;
  self->timed_slide = pp_stage_get_current_slide (self->audience_stage);
  self->running = TRUE;
  self->paused = FALSE;
  gtk_button_set_label (self->start_button, "Restart");
  gtk_button_set_label (self->pause_button, "Pause");
  gtk_button_set_label (self->rehearse_button, "Rehearse");

  if (rehearse && presentation != NULL && pp_presentation_get_file (presentation) != NULL)
    {
      self->rehearsal_presentation = pp_presentation_ref ((PpPresentation *) presentation);
      pp_presentation_rehearsal_reset (self->rehearsal_presentation);
      self->rehearsing = TRUE;
      gtk_button_set_label (self->rehearse_button, "Rehearsing…");
    }
  update_previews (self);
}

static void
update_previews (PpSpeaker *self)
{
  const PpPresentation *presentation = pp_stage_get_presentation (self->audience_stage);
  guint index;
  const PpSlide *slide;
  g_autofree char *notes_font = NULL;
  g_autoptr (PangoFontDescription) description = NULL;
  g_autoptr (PangoAttrList) attributes = NULL;
  double plan_total = 0.0;
  double plan_start = 0.0;

  if (presentation == NULL)
    return;

  index = pp_stage_get_current_slide (self->audience_stage);
  for (guint i = 0; i < pp_presentation_get_n_slides (presentation); i++)
    {
      const PpSlide *planned = pp_presentation_get_slide (presentation, i);
      double duration = planned->duration > 0.0 ? planned->duration : 2.0;

      plan_total += duration;
      if (i < index)
        plan_start += duration;
    }
  if (plan_total > 0.0)
    {
      double current_duration =
        pp_presentation_get_slide (presentation, index)->duration;

      if (current_duration <= 0.0)
        current_duration = 2.0;
      self->slide_plan_start = plan_start / plan_total;
      self->slide_plan_duration = current_duration / plan_total;
    }
  gtk_widget_set_sensitive (GTK_WIDGET (self->rehearse_button),
                            pp_presentation_get_file (presentation) != NULL);
  set_preview (self->previous_preview, presentation, (int) index - 1);
  set_preview (self->current_preview, presentation, (int) index);
  set_preview (self->next_preview, presentation, (int) index + 1);

  slide = pp_presentation_get_slide (presentation, index);
  gtk_label_set_text (self->notes,
                      slide->speaker_notes != NULL ? slide->speaker_notes : "");
  notes_font = g_strdup_printf ("%s %s",
                                slide->notes_font,
                                g_str_equal (slide->notes_font_size, "auto")
                                  ? "20px"
                                  : slide->notes_font_size);
  description = pango_font_description_from_string (notes_font);
  attributes = pango_attr_list_new ();
  pango_attr_list_insert (attributes, pango_attr_font_desc_new (description));
  gtk_label_set_attributes (self->notes, attributes);
}

static void
start_cb (GtkButton *button,
          gpointer   user_data)
{
  PpSpeaker *self = user_data;

  (void) button;
  restart_timer (self, FALSE);
}

static void
rehearse_cb (GtkButton *button,
             gpointer   user_data)
{
  PpSpeaker *self = user_data;

  (void) button;
  restart_timer (self, TRUE);
}

static void
pause_cb (GtkButton *button,
          gpointer   user_data)
{
  PpSpeaker *self = user_data;

  (void) button;
  if (!self->running)
    return;

  if (self->paused)
    {
      g_timer_continue (self->timer);
      self->paused = FALSE;
      gtk_button_set_label (self->pause_button, "Pause");
    }
  else
    {
      g_timer_stop (self->timer);
      self->paused = TRUE;
      gtk_button_set_label (self->pause_button, "Continue");
    }
}

static void
fullscreen_cb (GtkButton *button,
               gpointer   user_data)
{
  PpSpeaker *self = user_data;
  gboolean fullscreen = !gtk_window_is_fullscreen (self->window);

  (void) button;
  if (self->fullscreen_request != NULL)
    self->fullscreen_request (self,
                              fullscreen,
                              self->fullscreen_request_data);
  else
    pp_speaker_set_fullscreen (self, fullscreen, NULL);
}

static gboolean
key_pressed_cb (GtkEventControllerKey *controller,
                guint                  keyval,
                guint                  keycode,
                GdkModifierType        state,
                gpointer               user_data)
{
  PpSpeaker *self = user_data;

  (void) controller;
  (void) keycode;
  (void) state;
  switch (keyval)
    {
    case GDK_KEY_Left:
    case GDK_KEY_Up:
    case GDK_KEY_BackSpace:
    case GDK_KEY_Page_Up:
      pp_stage_previous (self->audience_stage);
      return GDK_EVENT_STOP;

    case GDK_KEY_Right:
    case GDK_KEY_Down:
    case GDK_KEY_space:
    case GDK_KEY_Page_Down:
      pp_stage_next (self->audience_stage);
      return GDK_EVENT_STOP;

    case GDK_KEY_F11:
    case GDK_KEY_f:
    case GDK_KEY_F:
      fullscreen_cb (NULL, self);
      return GDK_EVENT_STOP;

    case GDK_KEY_F1:
      gtk_widget_set_visible (GTK_WIDGET (self->window), FALSE);
      return GDK_EVENT_STOP;

    case GDK_KEY_b:
    case GDK_KEY_B:
      pp_stage_set_blank (self->audience_stage,
                          !pp_stage_get_blank (self->audience_stage));
      return GDK_EVENT_STOP;

    case GDK_KEY_h:
    case GDK_KEY_H:
    case GDK_KEY_Home:
      pp_stage_first (self->audience_stage);
      return GDK_EVENT_STOP;

    case GDK_KEY_Escape:
    case GDK_KEY_q:
    case GDK_KEY_Q:
      g_application_quit (G_APPLICATION (self->application));
      return GDK_EVENT_STOP;

    default:
      return GDK_EVENT_PROPAGATE;
    }
}

static void
hide_cb (GtkButton *button,
         gpointer   user_data)
{
  PpSpeaker *self = user_data;

  (void) button;
  gtk_widget_set_visible (GTK_WIDGET (self->window), FALSE);
}

static gboolean
close_request_cb (GtkWindow *window,
                  gpointer   user_data)
{
  PpSpeaker *self = user_data;

  (void) window;
  gtk_widget_set_visible (GTK_WIDGET (self->window), FALSE);
  return TRUE;
}

static void
install_speaker_css (GdkDisplay *display)
{
  static gboolean installed;
  GtkCssProvider *provider;

  if (installed)
    return;
  provider = gtk_css_provider_new ();
  gtk_css_provider_load_from_string (
    provider,
    "window.pinpoint-speaker { background: #000; }"
    ".pinpoint-speaker-toolbar { background: #808080; padding: 0 10px; min-height: 28px; }"
    ".pinpoint-speaker-toolbar button { min-height: 26px; min-width: 0; padding: 0 5px; border-radius: 0; background: transparent; box-shadow: none; color: #000; font: 16px Sans; }"
    ".pinpoint-speaker-toolbar button:hover { background: rgba(255,255,255,0.22); }"
    ".pinpoint-speaker-toolbar button:checked { background: rgba(255,255,255,0.35); }"
    ".pinpoint-speaker-notes, .pinpoint-speaker-notes viewport { background: transparent; color: #fff; }"
    ".pinpoint-speaker-notes-label { color: #fff; }"
    ".pinpoint-speaker-remaining { color: #fff; font: 24px Sans; }"
  );
  gtk_style_context_add_provider_for_display (
    display,
    GTK_STYLE_PROVIDER (provider),
    GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref (provider);
  installed = TRUE;
}

static GtkWidget *
make_preview (PpStage **out_preview)
{
  GtkWidget *preview = pp_stage_new ();

  pp_stage_set_media_enabled (PP_STAGE (preview), FALSE);
  *out_preview = PP_STAGE (preview);
  return preview;
}

PpSpeaker *
pp_speaker_new (GtkApplication *application,
                PpStage         *audience_stage)
{
  PpSpeaker *self;
  GtkWidget *root;
  GtkWidget *toolbar;
  GtkWidget *content;
  GtkWidget *notes_scroll;
  GtkWidget *progress;
  GtkWidget *previous;
  GtkWidget *current;
  GtkWidget *next;
  GtkWidget *button;
  GtkEventController *keys;

  g_return_val_if_fail (GTK_IS_APPLICATION (application), NULL);
  g_return_val_if_fail (PP_IS_STAGE (audience_stage), NULL);

  self = g_new0 (PpSpeaker, 1);
  self->application = application;
  self->audience_stage = g_object_ref (audience_stage);
  self->timer = g_timer_new ();
  g_timer_stop (self->timer);

  self->window = GTK_WINDOW (gtk_application_window_new (application));
  gtk_window_set_title (self->window, "Pinpoint Speaker View");
  gtk_window_set_default_size (self->window, 800, 600);
  gtk_widget_add_css_class (GTK_WIDGET (self->window), "pinpoint-speaker");
  install_speaker_css (gtk_widget_get_display (GTK_WIDGET (self->window)));
  g_signal_connect (self->window,
                    "close-request",
                    G_CALLBACK (close_request_cb),
                    self);
  keys = gtk_event_controller_key_new ();
  g_signal_connect (keys, "key-pressed", G_CALLBACK (key_pressed_cb), self);
  gtk_widget_add_controller (GTK_WIDGET (self->window), keys);

  root = gtk_overlay_new ();
  gtk_window_set_child (self->window, root);
  previous = make_preview (&self->previous_preview);
  current = make_preview (&self->current_preview);
  next = make_preview (&self->next_preview);

  self->notes = GTK_LABEL (gtk_label_new (NULL));
  gtk_label_set_wrap (self->notes, TRUE);
  gtk_label_set_wrap_mode (self->notes, PANGO_WRAP_WORD_CHAR);
  gtk_label_set_xalign (self->notes, 0.0f);
  gtk_label_set_yalign (self->notes, 0.0f);
  gtk_widget_set_hexpand (GTK_WIDGET (self->notes), TRUE);
  gtk_widget_set_vexpand (GTK_WIDGET (self->notes), TRUE);
  gtk_widget_add_css_class (GTK_WIDGET (self->notes),
                            "pinpoint-speaker-notes-label");
  notes_scroll = gtk_scrolled_window_new ();
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (notes_scroll),
                                  GTK_POLICY_NEVER,
                                  GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (notes_scroll),
                                 GTK_WIDGET (self->notes));
  gtk_widget_add_css_class (notes_scroll, "pinpoint-speaker-notes");
  content = pp_speaker_content_new (previous,
                                    current,
                                    next,
                                    notes_scroll);
  gtk_overlay_set_child (GTK_OVERLAY (root), content);

  toolbar = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_widget_set_halign (toolbar, GTK_ALIGN_FILL);
  gtk_widget_set_valign (toolbar, GTK_ALIGN_START);
  gtk_widget_add_css_class (toolbar, "pinpoint-speaker-toolbar");
  gtk_overlay_add_overlay (GTK_OVERLAY (root), toolbar);

  button = gtk_button_new_with_label ("Speaker");
  g_signal_connect (button, "clicked", G_CALLBACK (hide_cb), self);
  gtk_box_append (GTK_BOX (toolbar), button);
  self->start_button = GTK_BUTTON (gtk_button_new_with_label ("Start"));
  g_signal_connect (self->start_button, "clicked", G_CALLBACK (start_cb), self);
  gtk_box_append (GTK_BOX (toolbar), GTK_WIDGET (self->start_button));
  self->pause_button = GTK_BUTTON (gtk_button_new_with_label ("Pause"));
  gtk_widget_set_sensitive (GTK_WIDGET (self->pause_button), TRUE);
  g_signal_connect (self->pause_button, "clicked", G_CALLBACK (pause_cb), self);
  gtk_box_append (GTK_BOX (toolbar), GTK_WIDGET (self->pause_button));
  self->autoadvance_button = GTK_TOGGLE_BUTTON (
    gtk_toggle_button_new_with_label ("Autoadvance"));
  gtk_box_append (GTK_BOX (toolbar), GTK_WIDGET (self->autoadvance_button));
  self->rehearse_button = GTK_BUTTON (gtk_button_new_with_label ("Rehearse"));
  g_signal_connect (self->rehearse_button,
                    "clicked",
                    G_CALLBACK (rehearse_cb),
                    self);
  gtk_box_append (GTK_BOX (toolbar), GTK_WIDGET (self->rehearse_button));
  button = gtk_button_new_with_label ("Fullscreen");
  g_signal_connect (button, "clicked", G_CALLBACK (fullscreen_cb), self);
  gtk_box_append (GTK_BOX (toolbar), button);

  progress = gtk_overlay_new ();
  gtk_widget_set_halign (progress, GTK_ALIGN_FILL);
  gtk_widget_set_valign (progress, GTK_ALIGN_END);
  gtk_widget_set_size_request (progress, -1, 32);
  self->progress = GTK_DRAWING_AREA (gtk_drawing_area_new ());
  gtk_drawing_area_set_draw_func (self->progress,
                                  progress_draw_cb,
                                  self,
                                  NULL);
  gtk_overlay_set_child (GTK_OVERLAY (progress), GTK_WIDGET (self->progress));
  self->remaining = GTK_LABEL (gtk_label_new ("–"));
  gtk_widget_set_halign (GTK_WIDGET (self->remaining), GTK_ALIGN_END);
  gtk_widget_set_valign (GTK_WIDGET (self->remaining), GTK_ALIGN_END);
  gtk_widget_set_margin_end (GTK_WIDGET (self->remaining), 4);
  gtk_widget_set_margin_bottom (GTK_WIDGET (self->remaining), 4);
  gtk_widget_add_css_class (GTK_WIDGET (self->remaining),
                            "pinpoint-speaker-remaining");
  gtk_overlay_add_overlay (GTK_OVERLAY (progress), GTK_WIDGET (self->remaining));
  gtk_overlay_add_overlay (GTK_OVERLAY (root), progress);

  g_signal_connect (audience_stage,
                    "slide-changed",
                    G_CALLBACK (audience_slide_changed_cb),
                    self);
  g_signal_connect (audience_stage,
                    "presentation-ended",
                    G_CALLBACK (presentation_ended_cb),
                    self);
  self->tick_id = g_timeout_add (50, timer_cb, self);
  update_previews (self);
  return self;
}

void
pp_speaker_free (PpSpeaker *self)
{
  if (self == NULL)
    return;

  if (self->tick_id != 0)
    g_source_remove (self->tick_id);
  g_signal_handlers_disconnect_by_data (self->audience_stage, self);
  /* GtkApplicationWindow removal on Wayland expects a toplevel surface even
   * when the optional speaker window has never been shown. Realizing here is
   * harmless and keeps the hidden-window shutdown path well-defined. */
  if (!gtk_widget_get_realized (GTK_WIDGET (self->window)))
    gtk_widget_realize (GTK_WIDGET (self->window));
  gtk_window_destroy (self->window);
  g_clear_object (&self->audience_stage);
  g_clear_pointer (&self->rehearsal_presentation, pp_presentation_free);
  g_timer_destroy (self->timer);
  g_free (self);
}

void
pp_speaker_show (PpSpeaker *self)
{
  g_return_if_fail (self != NULL);
  update_previews (self);
  gtk_window_present (self->window);
}

void
pp_speaker_toggle (PpSpeaker *self)
{
  g_return_if_fail (self != NULL);
  if (gtk_widget_get_visible (GTK_WIDGET (self->window)))
    gtk_widget_set_visible (GTK_WIDGET (self->window), FALSE);
  else
    pp_speaker_show (self);
}

gboolean
pp_speaker_is_visible (PpSpeaker *self)
{
  g_return_val_if_fail (self != NULL, FALSE);
  return gtk_widget_get_visible (GTK_WIDGET (self->window));
}

void
pp_speaker_set_fullscreen (PpSpeaker *self,
                           gboolean   fullscreen,
                           GdkMonitor *monitor)
{
  g_return_if_fail (self != NULL);
  g_return_if_fail (monitor == NULL || GDK_IS_MONITOR (monitor));

  if (fullscreen && monitor != NULL)
    gtk_window_fullscreen_on_monitor (self->window, monitor);
  else if (fullscreen)
    gtk_window_fullscreen (self->window);
  else
    gtk_window_unfullscreen (self->window);
}

void
pp_speaker_set_fullscreen_request_func (
  PpSpeaker                       *self,
  PpSpeakerFullscreenRequestFunc   callback,
  gpointer                         user_data)
{
  g_return_if_fail (self != NULL);
  self->fullscreen_request = callback;
  self->fullscreen_request_data = user_data;
}

void
pp_speaker_start_rehearsal (PpSpeaker *self)
{
  g_return_if_fail (self != NULL);
  restart_timer (self, TRUE);
}
