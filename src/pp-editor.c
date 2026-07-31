#include "config.h"

#include "pp-editor-module.h"
#include "pp-presentation.h"
#include "pp-source.h"
#include "pp-stage.h"

#include <adwaita.h>
#include <gtksourceview/gtksource.h>
#include <string.h>

#define REPARSE_DELAY_MS 200

typedef struct
{
  PpEditorHost host;
  GtkWidget *root;
  GtkSourceBuffer *buffer;
  GtkSourceView *source_view;
  GtkListBox *outline;
  GtkScrolledWindow *outline_scroll;
  PpStage *preview;
  GtkLabel *status;
  GtkButton *save_button;
  GtkPopover *completion;
  GtkTextTag *warning_tag;
  GtkTextTag *error_tag;
  AdwStyleManager *style_manager;
  gulong style_changed_id;
  GFile *file;
  GFileMonitor *monitor;
  char *etag;
  PpSourceAnalysis *analysis;
  guint reparse_id;
  guint outline_scroll_id;
  guint outline_scroll_index;
  guint outline_scroll_attempts;
  guint external_reload_id;
  guint portal_poll_id;
  gboolean conflict_visible;
  gboolean close_quits;
  gboolean close_after_save;
  gboolean syncing_slide;
} PpEditor;

typedef struct
{
  PpEditor *editor;
  char *updated_source;
} TimingPrompt;

static void editor_reparse (PpEditor *self);
static void start_file_monitor (PpEditor *self);
static void save_as (PpEditor *self);

static void
add_editor_resource_paths (GtkSourceLanguageManager    *languages,
                           GtkSourceStyleSchemeManager *schemes)
{
  static const char language_resource[] =
    "resource:///com/nedrichards/pinpoint/gtksourceview-5/language-specs";
  static const char style_resource[] =
    "resource:///com/nedrichards/pinpoint/gtksourceview-5/styles";
  const char *const *language_paths;
  const char *const *scheme_paths;
  g_auto (GStrv) updated_language_paths = NULL;
  g_auto (GStrv) updated_scheme_paths = NULL;
  guint n_language_paths = 0;
  guint n_scheme_paths = 0;
  gboolean add_language_resource = TRUE;
  gboolean add_style_resource = TRUE;

  language_paths = gtk_source_language_manager_get_search_path (languages);
  while (language_paths[n_language_paths] != NULL)
    {
      if (g_str_equal (language_paths[n_language_paths], language_resource))
        add_language_resource = FALSE;
      n_language_paths++;
    }
  if (add_language_resource)
    {
      updated_language_paths = g_new0 (char *, n_language_paths + 2);
      for (guint i = 0; i < n_language_paths; i++)
        updated_language_paths[i] = g_strdup (language_paths[i]);
      updated_language_paths[n_language_paths] = g_strdup (language_resource);
      gtk_source_language_manager_set_search_path (
        languages, (const char *const *) updated_language_paths);
    }

  scheme_paths = gtk_source_style_scheme_manager_get_search_path (schemes);
  while (scheme_paths[n_scheme_paths] != NULL)
    {
      if (g_str_equal (scheme_paths[n_scheme_paths], style_resource))
        add_style_resource = FALSE;
      n_scheme_paths++;
    }
  if (add_style_resource)
    {
      updated_scheme_paths = g_new0 (char *, n_scheme_paths + 2);
      for (guint i = 0; i < n_scheme_paths; i++)
        updated_scheme_paths[i] = g_strdup (scheme_paths[i]);
      updated_scheme_paths[n_scheme_paths] = g_strdup (style_resource);
      gtk_source_style_scheme_manager_set_search_path (
        schemes, (const char *const *) updated_scheme_paths);
    }
}

static void
update_editor_style (PpEditor *self)
{
  GtkSourceStyleSchemeManager *schemes =
    gtk_source_style_scheme_manager_get_default ();
  gboolean dark = adw_style_manager_get_dark (self->style_manager);
  GtkSourceStyleScheme *scheme = gtk_source_style_scheme_manager_get_scheme (
    schemes, dark ? "Pinpoint-dark" : "Pinpoint");
  GdkRGBA warning = dark
    ? (GdkRGBA) { 0.97, 0.89, 0.36, 1.0 }
    : (GdkRGBA) { 0.78, 0.27, 0.0, 1.0 };
  GdkRGBA error = dark
    ? (GdkRGBA) { 1.0, 0.48, 0.39, 1.0 }
    : (GdkRGBA) { 0.75, 0.11, 0.16, 1.0 };

  if (scheme != NULL)
    gtk_source_buffer_set_style_scheme (self->buffer, scheme);
  g_object_set (self->warning_tag, "underline-rgba", &warning, NULL);
  g_object_set (self->error_tag, "underline-rgba", &error, NULL);
}

static void
style_changed_cb (AdwStyleManager *manager,
                  GParamSpec      *pspec,
                  gpointer         user_data)
{
  (void) manager;
  (void) pspec;
  update_editor_style (user_data);
}

static void
timing_prompt_free (gpointer data)
{
  TimingPrompt *prompt = data;

  g_free (prompt->updated_source);
  g_free (prompt);
}

static void
timing_response_cb (AdwAlertDialog *dialog,
                    const char     *response,
                    gpointer        user_data)
{
  TimingPrompt *prompt = user_data;
  GtkTextBuffer *buffer = GTK_TEXT_BUFFER (prompt->editor->buffer);
  GtkTextIter start;
  GtkTextIter end;

  (void) dialog;
  if (!g_str_equal (response, "apply"))
    return;
  gtk_text_buffer_get_bounds (buffer, &start, &end);
  gtk_text_buffer_begin_user_action (buffer);
  gtk_text_buffer_delete (buffer, &start, &end);
  gtk_text_buffer_insert (buffer, &start, prompt->updated_source, -1);
  gtk_text_buffer_end_user_action (buffer);
  gtk_widget_grab_focus (GTK_WIDGET (prompt->editor->source_view));
}

static char *
buffer_text (PpEditor *self)
{
  GtkTextIter start;
  GtkTextIter end;

  gtk_text_buffer_get_bounds (GTK_TEXT_BUFFER (self->buffer), &start, &end);
  return gtk_text_buffer_get_text (GTK_TEXT_BUFFER (self->buffer),
                                   &start,
                                   &end,
                                   TRUE);
}

static gsize
iter_byte_offset (PpEditor         *self,
                  const GtkTextIter *iter)
{
  GtkTextIter start;
  g_autofree char *prefix = NULL;

  gtk_text_buffer_get_start_iter (GTK_TEXT_BUFFER (self->buffer), &start);
  prefix = gtk_text_buffer_get_text (GTK_TEXT_BUFFER (self->buffer),
                                     &start,
                                     iter,
                                     TRUE);
  return strlen (prefix);
}

static int
byte_to_character_offset (const char *source,
                          gsize       offset)
{
  return (int) g_utf8_pointer_to_offset (source, source + offset);
}

static void
show_error (PpEditor   *self,
            const char *title,
            const char *message)
{
  AdwAlertDialog *dialog = ADW_ALERT_DIALOG (adw_alert_dialog_new (title, message));

  adw_alert_dialog_add_response (dialog, "close", "Close");
  adw_dialog_present (ADW_DIALOG (dialog), self->root);
}

static gboolean
save_to_file (PpEditor *self,
              GFile    *file)
{
  g_autofree char *source = buffer_text (self);
  g_autofree char *new_etag = NULL;
  g_autoptr (GError) error = NULL;

  if (!g_file_replace_contents (file,
                                source,
                                strlen (source),
                                self->file != NULL && g_file_equal (file, self->file)
                                  ? self->etag : NULL,
                                FALSE,
                                G_FILE_CREATE_REPLACE_DESTINATION,
                                &new_etag,
                                NULL,
                                &error))
    {
      show_error (self, "Unable to Save Presentation", error->message);
      self->close_after_save = FALSE;
      return FALSE;
    }

  g_set_object (&self->file, file);
  g_free (self->etag);
  self->etag = g_steal_pointer (&new_etag);
  gtk_text_buffer_set_modified (GTK_TEXT_BUFFER (self->buffer), FALSE);
  gtk_widget_set_sensitive (GTK_WIDGET (self->save_button), FALSE);
  start_file_monitor (self);
  if (self->close_after_save)
    {
      self->close_after_save = FALSE;
      self->host.close (self->close_quits, self->host.user_data);
    }
  return TRUE;
}

static gboolean
reload_external_file (PpEditor *self)
{
  g_autofree char *source = NULL;
  g_autofree char *etag = NULL;
  g_autofree char *existing = NULL;
  g_autoptr (GError) error = NULL;

  if (self->file == NULL ||
      !g_file_load_contents (self->file, NULL, &source, NULL, &etag, &error))
    {
      if (error != NULL)
        show_error (self, "Unable to Reload Presentation", error->message);
      return FALSE;
    }
  existing = buffer_text (self);
  g_free (self->etag);
  self->etag = g_steal_pointer (&etag);
  if (g_str_equal (existing, source))
    return TRUE;

  gtk_text_buffer_set_text (GTK_TEXT_BUFFER (self->buffer), source, -1);
  gtk_text_buffer_set_modified (GTK_TEXT_BUFFER (self->buffer), FALSE);
  gtk_widget_set_sensitive (GTK_WIDGET (self->save_button), FALSE);
  return TRUE;
}

static void
external_conflict_response_cb (AdwAlertDialog *dialog,
                               const char     *response,
                               gpointer        user_data)
{
  PpEditor *self = user_data;

  (void) dialog;
  self->conflict_visible = FALSE;
  if (g_str_equal (response, "reload"))
    reload_external_file (self);
  else if (g_str_equal (response, "save-as"))
    save_as (self);
}

static void
show_external_conflict (PpEditor *self)
{
  AdwAlertDialog *dialog;

  if (self->conflict_visible)
    return;
  self->conflict_visible = TRUE;
  dialog = ADW_ALERT_DIALOG (adw_alert_dialog_new (
    "Presentation Changed on Disk",
    "Keep the edits in this window, reload the external version, or save this version under a new name."));
  adw_alert_dialog_add_responses (dialog,
                                  "keep", "Keep Editing",
                                  "save-as", "Save As…",
                                  "reload", "Reload",
                                  NULL);
  adw_alert_dialog_set_response_appearance (dialog,
                                            "reload",
                                            ADW_RESPONSE_DESTRUCTIVE);
  adw_alert_dialog_set_default_response (dialog, "keep");
  adw_alert_dialog_set_close_response (dialog, "keep");
  g_signal_connect (dialog,
                    "response",
                    G_CALLBACK (external_conflict_response_cb),
                    self);
  adw_dialog_present (ADW_DIALOG (dialog), self->root);
}

static gboolean
external_reload_cb (gpointer user_data)
{
  PpEditor *self = user_data;

  self->external_reload_id = 0;
  if (gtk_text_buffer_get_modified (GTK_TEXT_BUFFER (self->buffer)))
    show_external_conflict (self);
  else
    reload_external_file (self);
  return G_SOURCE_REMOVE;
}

static void
editor_file_changed_cb (GFileMonitor      *monitor,
                        GFile             *file,
                        GFile             *other_file,
                        GFileMonitorEvent  event_type,
                        gpointer           user_data)
{
  PpEditor *self = user_data;

  (void) monitor;
  (void) event_type;
  if (self->file == NULL ||
      (!g_file_equal (file, self->file) &&
       (other_file == NULL || !g_file_equal (other_file, self->file))))
    return;
  if (self->external_reload_id != 0)
    g_source_remove (self->external_reload_id);
  self->external_reload_id = g_timeout_add (REPARSE_DELAY_MS,
                                             external_reload_cb,
                                             self);
}

static gboolean
portal_file_poll_cb (gpointer user_data)
{
  PpEditor *self = user_data;
  g_autoptr (GFileInfo) info = NULL;
  const char *etag;

  if (self->file == NULL)
    return G_SOURCE_CONTINUE;
  info = g_file_query_info (self->file,
                            G_FILE_ATTRIBUTE_ETAG_VALUE,
                            G_FILE_QUERY_INFO_NONE,
                            NULL,
                            NULL);
  if (info == NULL)
    return G_SOURCE_CONTINUE;
  etag = g_file_info_get_attribute_string (info, G_FILE_ATTRIBUTE_ETAG_VALUE);
  if (etag != NULL && g_strcmp0 (etag, self->etag) != 0 &&
      self->external_reload_id == 0)
    self->external_reload_id = g_timeout_add (REPARSE_DELAY_MS,
                                               external_reload_cb,
                                               self);
  return G_SOURCE_CONTINUE;
}

static void
start_file_monitor (PpEditor *self)
{
  g_autoptr (GFile) parent = NULL;
  g_autoptr (GFile) portal_root = NULL;

  g_clear_object (&self->monitor);
  if (self->portal_poll_id != 0)
    {
      g_source_remove (self->portal_poll_id);
      self->portal_poll_id = 0;
    }
  if (self->file == NULL)
    return;
  parent = g_file_get_parent (self->file);
  if (parent == NULL)
    return;
  self->monitor = g_file_monitor_directory (parent,
                                             G_FILE_MONITOR_WATCH_MOVES,
                                             NULL,
                                             NULL);
  if (self->monitor != NULL)
    g_signal_connect (self->monitor,
                      "changed",
                      G_CALLBACK (editor_file_changed_cb),
                      self);
  portal_root = g_file_new_build_filename (g_get_user_runtime_dir (),
                                            "doc",
                                            NULL);
  if (g_file_has_prefix (self->file, portal_root))
    self->portal_poll_id = g_timeout_add (500, portal_file_poll_cb, self);
}

static void
save_dialog_finished_cb (GObject      *source,
                         GAsyncResult *result,
                         gpointer      user_data)
{
  PpEditor *self = user_data;
  g_autoptr (GError) error = NULL;
  g_autoptr (GFile) file = gtk_file_dialog_save_finish (GTK_FILE_DIALOG (source),
                                                        result,
                                                        &error);

  if (file != NULL)
    save_to_file (self, file);
  else
    {
      self->close_after_save = FALSE;
      if (!g_error_matches (error, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_DISMISSED) &&
          !g_error_matches (error, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_CANCELLED))
        show_error (self, "Unable to Choose a File", error->message);
    }
}

static void
save_as (PpEditor *self)
{
  GtkFileDialog *dialog = gtk_file_dialog_new ();
  GtkRoot *root = gtk_widget_get_root (self->root);

  gtk_file_dialog_set_title (dialog, "Save Pinpoint Presentation");
  gtk_file_dialog_set_initial_name (dialog, "presentation.pin");
  gtk_file_dialog_save (dialog,
                        GTK_IS_WINDOW (root) ? GTK_WINDOW (root) : NULL,
                        NULL,
                        save_dialog_finished_cb,
                        self);
  g_object_unref (dialog);
}

static void
save (PpEditor *self)
{
  if (self->file == NULL)
    save_as (self);
  else
    save_to_file (self, self->file);
}

static void
save_clicked_cb (GtkButton *button,
                 gpointer   user_data)
{
  (void) button;
  save (user_data);
}

static void
launch (PpEditor *self,
        gboolean  rehearse)
{
  g_autofree char *source = buffer_text (self);
  guint initial_slide = 0;
  GtkTextIter insert;

  gtk_text_buffer_get_iter_at_mark (GTK_TEXT_BUFFER (self->buffer),
                                    &insert,
                                    gtk_text_buffer_get_insert (GTK_TEXT_BUFFER (self->buffer)));
  if (self->analysis != NULL && !rehearse)
    initial_slide = pp_source_analysis_find_slide (self->analysis,
                                                   iter_byte_offset (self, &insert));
  self->host.launch (source,
                     self->file,
                     initial_slide,
                     rehearse,
                     self->host.user_data);
}

static void
present_clicked_cb (GtkButton *button,
                    gpointer   user_data)
{
  (void) button;
  launch (user_data, FALSE);
}

static void
rehearse_clicked_cb (GtkButton *button,
                     gpointer   user_data)
{
  (void) button;
  launch (user_data, TRUE);
}

static void
close_confirmed_cb (AdwAlertDialog *dialog,
                    const char     *response,
                    gpointer        user_data)
{
  PpEditor *self = user_data;

  (void) dialog;
  if (g_str_equal (response, "save"))
    {
      self->close_after_save = TRUE;
      save (self);
      return;
    }
  if (g_str_equal (response, "discard"))
    self->host.close (self->close_quits, self->host.user_data);
}

static void
request_close (PpEditor *self,
               gboolean  quit)
{
  self->close_quits = quit;
  if (!gtk_text_buffer_get_modified (GTK_TEXT_BUFFER (self->buffer)))
    {
      self->host.close (self->close_quits, self->host.user_data);
      return;
    }

  AdwAlertDialog *dialog = ADW_ALERT_DIALOG (adw_alert_dialog_new (
    "Save Changes?", "Unsaved changes to this presentation will be lost."));
  adw_alert_dialog_add_responses (dialog,
                                  "cancel", "Cancel",
                                  "discard", "Discard",
                                  "save", "Save",
                                  NULL);
  adw_alert_dialog_set_response_appearance (dialog,
                                            "discard",
                                            ADW_RESPONSE_DESTRUCTIVE);
  adw_alert_dialog_set_response_appearance (dialog,
                                            "save",
                                            ADW_RESPONSE_SUGGESTED);
  adw_alert_dialog_set_default_response (dialog, "save");
  adw_alert_dialog_set_close_response (dialog, "cancel");
  g_signal_connect (dialog, "response", G_CALLBACK (close_confirmed_cb), self);
  adw_dialog_present (ADW_DIALOG (dialog), self->root);
}

static void
back_clicked_cb (GtkButton *button,
                 gpointer   user_data)
{
  (void) button;
  request_close (user_data, FALSE);
}

static gboolean
scroll_outline_to_selected (PpEditor *self)
{
  GtkListBoxRow *row = gtk_list_box_get_row_at_index (
    self->outline, (int) self->outline_scroll_index);
  GtkAdjustment *adjustment;
  graphene_rect_t bounds;
  double value;
  double page_size;
  double target;

  if (row == NULL ||
      !gtk_widget_compute_bounds (GTK_WIDGET (row),
                                  GTK_WIDGET (self->outline),
                                  &bounds))
    return FALSE;
  adjustment = gtk_scrolled_window_get_vadjustment (self->outline_scroll);
  value = gtk_adjustment_get_value (adjustment);
  page_size = gtk_adjustment_get_page_size (adjustment);
  if (page_size <= 0.0 || bounds.size.height <= 0.0)
    return FALSE;
  target = value;
  if (bounds.origin.y < value)
    target = bounds.origin.y;
  else if (bounds.origin.y + bounds.size.height > value + page_size)
    target = bounds.origin.y + bounds.size.height - page_size;
  gtk_adjustment_set_value (
    adjustment,
    CLAMP (target,
           gtk_adjustment_get_lower (adjustment),
           MAX (gtk_adjustment_get_lower (adjustment),
                gtk_adjustment_get_upper (adjustment) - page_size)));
  return TRUE;
}

static gboolean
scroll_outline_to_selected_cb (gpointer user_data)
{
  PpEditor *self = user_data;

  if (!scroll_outline_to_selected (self) &&
      self->outline_scroll_attempts++ < 5)
    return G_SOURCE_CONTINUE;
  self->outline_scroll_id = 0;
  return G_SOURCE_REMOVE;
}

static void
queue_outline_scroll (PpEditor *self,
                      guint     index)
{
  self->outline_scroll_index = index;
  self->outline_scroll_attempts = 0;
  if (self->outline_scroll_id != 0)
    return;
  self->outline_scroll_id = g_timeout_add (16,
                                           scroll_outline_to_selected_cb,
                                           self);
  g_source_set_name_by_id (self->outline_scroll_id,
                           "pinpoint-editor-outline-scroll");
}

static void
select_outline_slide (PpEditor *self,
                      guint     index)
{
  GtkListBoxRow *row = gtk_list_box_get_row_at_index (self->outline,
                                                       (int) index);
  gboolean was_syncing = self->syncing_slide;

  if (row == NULL)
    return;
  if (gtk_list_box_get_selected_row (self->outline) != row)
    {
      self->syncing_slide = TRUE;
      gtk_list_box_select_row (self->outline, row);
      self->syncing_slide = was_syncing;
    }
  queue_outline_scroll (self, index);
}

static void
set_preview_slide_if_available (PpEditor *self,
                                guint     index)
{
  const PpPresentation *presentation = pp_stage_get_presentation (self->preview);

  if (presentation != NULL &&
      index < pp_presentation_get_n_slides (presentation))
    pp_stage_set_slide (self->preview, index);
}

static void
outline_selected_cb (GtkListBox    *outline,
                     GtkListBoxRow *row,
                     gpointer       user_data)
{
  PpEditor *self = user_data;
  int row_index;
  guint index;
  const PpSourceSlide *slide;
  GtkTextIter iter;
  g_autofree char *source = NULL;

  (void) outline;
  if (self->syncing_slide || row == NULL || self->analysis == NULL)
    return;
  row_index = gtk_list_box_row_get_index (row);
  if (row_index < 0)
    return;
  index = (guint) row_index;
  if (index >= pp_source_analysis_get_n_slides (self->analysis))
    return;
  slide = pp_source_analysis_get_slide (self->analysis, index);
  source = buffer_text (self);
  gtk_text_buffer_get_iter_at_offset (GTK_TEXT_BUFFER (self->buffer),
                                      &iter,
                                      byte_to_character_offset (source,
                                                                MIN (slide->separator_end + 1,
                                                                     slide->end)));
  self->syncing_slide = TRUE;
  gtk_text_buffer_place_cursor (GTK_TEXT_BUFFER (self->buffer), &iter);
  gtk_text_view_scroll_to_iter (GTK_TEXT_VIEW (self->source_view),
                                &iter, 0.15, FALSE, 0.0, 0.0);
  set_preview_slide_if_available (self, index);
  self->syncing_slide = FALSE;
  gtk_widget_grab_focus (GTK_WIDGET (self->source_view));
}

static void
rebuild_outline (PpEditor *self,
                 guint     selected_slide)
{
  guint count = self->analysis != NULL
    ? pp_source_analysis_get_n_slides (self->analysis) : 0;
  guint existing = 0;

  while (gtk_list_box_get_row_at_index (self->outline, (int) existing) != NULL)
    existing++;
  while (existing > count)
    {
      GtkListBoxRow *row = gtk_list_box_get_row_at_index (self->outline,
                                                           (int) existing - 1);

      gtk_list_box_remove (self->outline, GTK_WIDGET (row));
      existing--;
    }
  if (self->analysis == NULL)
    return;

  for (guint i = 0; i < count; i++)
    {
      const PpSourceSlide *slide = pp_source_analysis_get_slide (self->analysis, i);
      GtkListBoxRow *row = gtk_list_box_get_row_at_index (self->outline,
                                                           (int) i);
      GtkLabel *number;
      GtkLabel *title;
      g_autofree char *number_text = g_strdup_printf ("%u", i + 1);

      if (row == NULL)
        {
          GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
          GtkWidget *number_widget = gtk_label_new (NULL);
          GtkWidget *title_widget = gtk_label_new (NULL);

          row = GTK_LIST_BOX_ROW (gtk_list_box_row_new ());
          number = GTK_LABEL (number_widget);
          title = GTK_LABEL (title_widget);
          gtk_label_set_xalign (number, 1.0f);
          gtk_label_set_width_chars (number, 3);
          gtk_widget_add_css_class (number_widget, "dim-label");
          gtk_label_set_xalign (title, 0.0f);
          gtk_label_set_wrap (title, TRUE);
          gtk_label_set_wrap_mode (title, PANGO_WRAP_WORD_CHAR);
          gtk_label_set_lines (title, 2);
          gtk_label_set_ellipsize (title, PANGO_ELLIPSIZE_END);
          gtk_widget_set_hexpand (title_widget, TRUE);
          gtk_widget_set_margin_start (box, 8);
          gtk_widget_set_margin_end (box, 8);
          gtk_widget_set_margin_top (box, 5);
          gtk_widget_set_margin_bottom (box, 5);
          gtk_box_append (GTK_BOX (box), number_widget);
          gtk_box_append (GTK_BOX (box), title_widget);
          gtk_list_box_row_set_child (row, box);
          g_object_set_data (G_OBJECT (row), "pinpoint-slide-number", number);
          g_object_set_data (G_OBJECT (row), "pinpoint-slide-title", title);
          gtk_list_box_append (self->outline, GTK_WIDGET (row));
        }
      else
        {
          number = g_object_get_data (G_OBJECT (row), "pinpoint-slide-number");
          title = g_object_get_data (G_OBJECT (row), "pinpoint-slide-title");
        }
      gtk_label_set_text (number, number_text);
      gtk_label_set_text (title, slide->title);
      gtk_widget_set_tooltip_text (GTK_WIDGET (row), slide->title);
    }
  select_outline_slide (self, selected_slide);
}

static void
apply_diagnostics (PpEditor *self)
{
  GtkTextIter start;
  GtkTextIter end;
  guint count = self->analysis != NULL
    ? pp_source_analysis_get_n_diagnostics (self->analysis) : 0;
  g_autofree char *source = buffer_text (self);

  gtk_text_buffer_get_bounds (GTK_TEXT_BUFFER (self->buffer), &start, &end);
  gtk_text_buffer_remove_tag (GTK_TEXT_BUFFER (self->buffer), self->warning_tag,
                              &start, &end);
  gtk_text_buffer_remove_tag (GTK_TEXT_BUFFER (self->buffer), self->error_tag,
                              &start, &end);
  for (guint i = 0; i < count; i++)
    {
      const PpSourceDiagnostic *diagnostic =
        pp_source_analysis_get_diagnostic (self->analysis, i);
      GtkTextIter diagnostic_start;
      GtkTextIter diagnostic_end;

      gtk_text_buffer_get_iter_at_offset (GTK_TEXT_BUFFER (self->buffer),
                                          &diagnostic_start,
                                          byte_to_character_offset (source,
                                                                    diagnostic->start));
      gtk_text_buffer_get_iter_at_offset (GTK_TEXT_BUFFER (self->buffer),
                                          &diagnostic_end,
                                          byte_to_character_offset (source,
                                                                    diagnostic->end));
      gtk_text_buffer_apply_tag (GTK_TEXT_BUFFER (self->buffer),
                                 diagnostic->severity == PP_SOURCE_DIAGNOSTIC_ERROR
                                   ? self->error_tag : self->warning_tag,
                                 &diagnostic_start,
                                 &diagnostic_end);
    }
  if (count > 0)
    gtk_widget_set_tooltip_text (GTK_WIDGET (self->status),
      pp_source_analysis_get_diagnostic (self->analysis, 0)->message);
  else
    gtk_widget_set_tooltip_text (GTK_WIDGET (self->status), NULL);
}

static void
editor_reparse (PpEditor *self)
{
  g_autofree char *source = buffer_text (self);
  g_autoptr (PpPresentation) presentation = NULL;
  g_autoptr (GError) error = NULL;
  GtkTextIter insert;
  guint slide = 0;
  guint diagnostics;
  g_autofree char *status = NULL;

  g_clear_pointer (&self->analysis, pp_source_analysis_free);
  self->analysis = pp_source_analyze (source, self->file);
  gtk_text_buffer_get_iter_at_mark (GTK_TEXT_BUFFER (self->buffer),
                                    &insert,
                                    gtk_text_buffer_get_insert (GTK_TEXT_BUFFER (self->buffer)));
  slide = pp_source_analysis_find_slide (self->analysis,
                                         iter_byte_offset (self, &insert));
  rebuild_outline (self, slide);
  apply_diagnostics (self);
  presentation = pp_presentation_parse (source, self->file, FALSE, &error);
  diagnostics = pp_source_analysis_get_n_diagnostics (self->analysis);
  if (presentation != NULL)
    {
      guint count = pp_presentation_get_n_slides (presentation);

      self->syncing_slide = TRUE;
      pp_stage_set_presentation (self->preview,
                                 g_steal_pointer (&presentation),
                                 MIN (slide, count - 1));
      self->syncing_slide = FALSE;
      status = g_strdup_printf ("%u slide%s · %u problem%s",
                                count, count == 1 ? "" : "s",
                                diagnostics, diagnostics == 1 ? "" : "s");
    }
  else
    status = g_strdup_printf ("Preview paused · %s", error->message);
  gtk_label_set_text (self->status, status);
}

static gboolean
reparse_cb (gpointer user_data)
{
  PpEditor *self = user_data;

  self->reparse_id = 0;
  editor_reparse (self);
  return G_SOURCE_REMOVE;
}

static void
buffer_changed_cb (GtkTextBuffer *buffer,
                   gpointer       user_data)
{
  PpEditor *self = user_data;

  (void) buffer;
  if (self->reparse_id != 0)
    g_source_remove (self->reparse_id);
  self->reparse_id = g_timeout_add (REPARSE_DELAY_MS, reparse_cb, self);
  gtk_widget_set_sensitive (GTK_WIDGET (self->save_button), TRUE);
}

static void
cursor_moved_cb (GtkTextBuffer *buffer,
                 GtkTextIter   *location,
                 GtkTextMark   *mark,
                 gpointer       user_data)
{
  PpEditor *self = user_data;
  guint slide;

  if (self->syncing_slide || self->analysis == NULL ||
      mark != gtk_text_buffer_get_insert (buffer))
    return;
  slide = pp_source_analysis_find_slide (self->analysis,
                                         iter_byte_offset (self, location));
  select_outline_slide (self, slide);
  self->syncing_slide = TRUE;
  set_preview_slide_if_available (self, slide);
  self->syncing_slide = FALSE;
}

static void
preview_slide_changed_cb (PpStage *stage,
                          guint    index,
                          gpointer user_data)
{
  PpEditor *self = user_data;
  const PpSourceSlide *slide;
  GtkTextIter iter;
  g_autofree char *source = NULL;

  (void) stage;
  if (self->syncing_slide || self->analysis == NULL ||
      index >= pp_source_analysis_get_n_slides (self->analysis))
    return;
  slide = pp_source_analysis_get_slide (self->analysis, index);
  source = buffer_text (self);
  gtk_text_buffer_get_iter_at_offset (GTK_TEXT_BUFFER (self->buffer),
                                      &iter,
                                      byte_to_character_offset (source,
                                                                MIN (slide->separator_end + 1,
                                                                     slide->end)));
  self->syncing_slide = TRUE;
  gtk_text_buffer_place_cursor (GTK_TEXT_BUFFER (self->buffer), &iter);
  gtk_text_view_scroll_to_iter (GTK_TEXT_VIEW (self->source_view),
                                &iter, 0.15, FALSE, 0.0, 0.0);
  select_outline_slide (self, index);
  self->syncing_slide = FALSE;
}

static void
completion_clicked_cb (GtkButton *button,
                       gpointer   user_data)
{
  PpEditor *self = user_data;
  const char *text = g_object_get_data (G_OBJECT (button), "insertion");
  gboolean needs_value = GPOINTER_TO_INT (
    g_object_get_data (G_OBJECT (button), "needs-value"));
  GtkTextIter insert;

  gtk_text_buffer_get_iter_at_mark (GTK_TEXT_BUFFER (self->buffer),
                                    &insert,
                                    gtk_text_buffer_get_insert (GTK_TEXT_BUFFER (self->buffer)));
  gtk_text_buffer_insert (GTK_TEXT_BUFFER (self->buffer), &insert, text, -1);
  if (needs_value)
    gtk_text_iter_backward_char (&insert);
  gtk_text_buffer_place_cursor (GTK_TEXT_BUFFER (self->buffer), &insert);
  gtk_popover_popdown (self->completion);
}

static void
add_completion (PpEditor   *self,
                GtkListBox *list,
                const char *label,
                const char *insertion,
                gboolean    needs_value)
{
  GtkWidget *button = gtk_button_new_with_label (label);

  gtk_button_set_has_frame (GTK_BUTTON (button), FALSE);
  gtk_widget_set_halign (button, GTK_ALIGN_FILL);
  g_object_set_data_full (G_OBJECT (button),
                          "insertion",
                          g_strdup (insertion),
                          g_free);
  g_object_set_data (G_OBJECT (button),
                     "needs-value",
                     GINT_TO_POINTER (needs_value));
  g_signal_connect (button, "clicked", G_CALLBACK (completion_clicked_cb), self);
  gtk_list_box_append (list, button);
}

static void
show_completion (PpEditor *self)
{
  GtkListBox *list = GTK_LIST_BOX (gtk_list_box_new ());
  GtkWidget *scroll = gtk_scrolled_window_new ();
  const char *const *names = pp_source_setting_names ();

  for (guint i = 0; names[i] != NULL; i++)
    {
      const char *const *values = pp_source_setting_values (names[i]);

      if (values != NULL)
        for (guint j = 0; values[j] != NULL; j++)
          {
            g_autofree char *setting = g_strconcat (names[i], values[j], NULL);
            g_autofree char *insertion = g_strdup_printf ("[%s]", setting);
            add_completion (self, list, setting, insertion, FALSE);
          }
      else
        {
          g_autofree char *insertion = g_strdup_printf ("[%s]", names[i]);
          add_completion (self,
                          list,
                          names[i],
                          insertion,
                          g_str_has_suffix (names[i], "="));
        }
    }
  add_completion (self, list, "New slide", "\n--\n", FALSE);
  add_completion (self, list, "Speaker note", "\n#", FALSE);
  add_completion (self, list, "Visual description", "\n#@alt:", FALSE);

  if (self->file != NULL)
    {
      g_autoptr (GFile) parent = g_file_get_parent (self->file);
      g_autoptr (GFileEnumerator) enumerator = parent != NULL
        ? g_file_enumerate_children (parent,
                                     G_FILE_ATTRIBUTE_STANDARD_NAME ","
                                     G_FILE_ATTRIBUTE_STANDARD_TYPE,
                                     G_FILE_QUERY_INFO_NONE,
                                     NULL,
                                     NULL)
        : NULL;
      if (enumerator != NULL)
        {
          GFileInfo *info;

          while ((info = g_file_enumerator_next_file (enumerator, NULL, NULL)) != NULL)
            {
              const char *name = g_file_info_get_name (info);

              if (g_file_info_get_file_type (info) == G_FILE_TYPE_REGULAR &&
                  !g_str_has_suffix (name, ".pin"))
                {
                  g_autofree char *label = g_strdup_printf ("Asset: %s", name);
                  g_autofree char *insertion = g_strdup_printf ("[%s]", name);
                  add_completion (self, list, label, insertion, FALSE);
                }
              g_object_unref (info);
            }
        }
    }
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll),
                                  GTK_POLICY_NEVER,
                                  GTK_POLICY_AUTOMATIC);
  gtk_widget_set_size_request (scroll, 280, 320);
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scroll),
                                 GTK_WIDGET (list));
  gtk_popover_set_child (self->completion, scroll);
  gtk_popover_popup (self->completion);
}

static gboolean
editor_key_pressed_cb (GtkEventControllerKey *controller,
                       guint                  keyval,
                       guint                  keycode,
                       GdkModifierType        state,
                       gpointer               user_data)
{
  PpEditor *self = user_data;
  gboolean control = (state & GDK_CONTROL_MASK) != 0;
  gboolean shift = (state & GDK_SHIFT_MASK) != 0;

  (void) controller;
  (void) keycode;
  if (control && keyval == GDK_KEY_space)
    show_completion (self);
  else if (control && keyval == GDK_KEY_s)
    shift ? save_as (self) : save (self);
  else if (control && keyval == GDK_KEY_Return)
    launch (self, FALSE);
  else if (control && shift && (keyval == GDK_KEY_r || keyval == GDK_KEY_R))
    launch (self, TRUE);
  else if ((state & GDK_ALT_MASK) != 0 && keyval == GDK_KEY_Up)
    pp_stage_previous (self->preview);
  else if ((state & GDK_ALT_MASK) != 0 && keyval == GDK_KEY_Down)
    pp_stage_next (self->preview);
  else if (keyval == GDK_KEY_F6)
    {
      GtkRoot *root = gtk_widget_get_root (self->root);
      GtkWidget *focus = root != NULL ? gtk_root_get_focus (root) : NULL;

      if (focus == GTK_WIDGET (self->source_view))
        gtk_widget_grab_focus (GTK_WIDGET (self->preview));
      else if (focus == GTK_WIDGET (self->outline) ||
               (focus != NULL &&
                gtk_widget_is_ancestor (focus, GTK_WIDGET (self->outline))))
        gtk_widget_grab_focus (GTK_WIDGET (self->source_view));
      else
        gtk_widget_grab_focus (GTK_WIDGET (self->outline));
    }
  else
    return GDK_EVENT_PROPAGATE;
  return GDK_EVENT_STOP;
}

static void
editor_free (gpointer data)
{
  PpEditor *self = data;

  if (self->reparse_id != 0)
    g_source_remove (self->reparse_id);
  if (self->outline_scroll_id != 0)
    g_source_remove (self->outline_scroll_id);
  if (self->external_reload_id != 0)
    g_source_remove (self->external_reload_id);
  if (self->portal_poll_id != 0)
    g_source_remove (self->portal_poll_id);
  g_clear_object (&self->monitor);
  if (self->style_manager != NULL && self->style_changed_id != 0)
    g_signal_handler_disconnect (self->style_manager, self->style_changed_id);
  g_clear_object (&self->file);
  g_clear_pointer (&self->analysis, pp_source_analysis_free);
  g_free (self->etag);
  g_free (self);
}

static GtkWidget *
create_editor (PpEditor *self)
{
  AdwToolbarView *toolbar_view = ADW_TOOLBAR_VIEW (adw_toolbar_view_new ());
  AdwHeaderBar *header = ADW_HEADER_BAR (adw_header_bar_new ());
  GtkWidget *back = gtk_button_new_from_icon_name ("go-previous-symbolic");
  GtkWidget *present = gtk_button_new_with_label ("Present");
  GtkWidget *rehearse = gtk_button_new_with_label ("Rehearse");
  GtkWidget *outer = gtk_paned_new (GTK_ORIENTATION_HORIZONTAL);
  GtkWidget *outline_scroll = gtk_scrolled_window_new ();
  GtkWidget *work = gtk_paned_new (GTK_ORIENTATION_HORIZONTAL);
  GtkWidget *source_scroll = gtk_scrolled_window_new ();
  GtkWidget *preview_frame;
  GtkWidget *status_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  GtkWidget *title = gtk_label_new ("Compose Presentation");
  GtkEventController *keys = gtk_event_controller_key_new ();
  GtkSourceLanguageManager *languages;
  GtkSourceStyleSchemeManager *schemes;
  GtkSourceLanguage *language;

  gtk_source_init ();
  languages = gtk_source_language_manager_get_default ();
  schemes = gtk_source_style_scheme_manager_get_default ();
  add_editor_resource_paths (languages, schemes);
  language = gtk_source_language_manager_get_language (languages, "pinpoint");

  self->root = GTK_WIDGET (toolbar_view);
  self->buffer = GTK_SOURCE_BUFFER (gtk_source_buffer_new (NULL));
  if (language != NULL)
    gtk_source_buffer_set_language (self->buffer, language);
  self->source_view = GTK_SOURCE_VIEW (
    gtk_source_view_new_with_buffer (self->buffer));
  gtk_source_view_set_show_line_numbers (self->source_view, TRUE);
  gtk_source_view_set_highlight_current_line (self->source_view, TRUE);
  gtk_source_view_set_auto_indent (self->source_view, TRUE);
  gtk_source_view_set_smart_backspace (self->source_view, TRUE);
  gtk_text_view_set_monospace (GTK_TEXT_VIEW (self->source_view), TRUE);
  gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (self->source_view), GTK_WRAP_WORD_CHAR);
  gtk_widget_set_hexpand (GTK_WIDGET (self->source_view), TRUE);
  gtk_widget_set_vexpand (GTK_WIDGET (self->source_view), TRUE);

  self->warning_tag = gtk_text_buffer_create_tag (GTK_TEXT_BUFFER (self->buffer),
                                                   "pinpoint-warning",
                                                   "underline", PANGO_UNDERLINE_ERROR,
                                                   NULL);
  self->error_tag = gtk_text_buffer_create_tag (GTK_TEXT_BUFFER (self->buffer),
                                                 "pinpoint-error",
                                                 "underline", PANGO_UNDERLINE_ERROR,
                                                 NULL);
  self->style_manager = adw_style_manager_get_default ();
  self->style_changed_id = g_signal_connect (self->style_manager,
                                              "notify::dark",
                                              G_CALLBACK (style_changed_cb),
                                              self);
  update_editor_style (self);
  self->outline = GTK_LIST_BOX (gtk_list_box_new ());
  self->outline_scroll = GTK_SCROLLED_WINDOW (outline_scroll);
  gtk_list_box_set_selection_mode (self->outline, GTK_SELECTION_SINGLE);
  gtk_widget_add_css_class (GTK_WIDGET (self->outline), "navigation-sidebar");
  gtk_widget_set_size_request (GTK_WIDGET (self->outline), 220, -1);
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (outline_scroll),
                                 GTK_WIDGET (self->outline));
  gtk_widget_add_css_class (outline_scroll, "sidebar");
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (source_scroll),
                                 GTK_WIDGET (self->source_view));

  self->preview = PP_STAGE (pp_stage_new ());
  pp_stage_set_audio_enabled (self->preview, FALSE);
  pp_stage_set_camera_enabled (self->preview, FALSE);
  pp_stage_set_accessible_context (self->preview, "Editor preview");
  g_signal_connect (self->preview,
                    "slide-changed",
                    G_CALLBACK (preview_slide_changed_cb),
                    self);
  preview_frame = gtk_aspect_frame_new (0.5f, 0.5f, 16.0f / 9.0f, FALSE);
  gtk_aspect_frame_set_child (GTK_ASPECT_FRAME (preview_frame),
                              GTK_WIDGET (self->preview));
  gtk_widget_set_hexpand (preview_frame, TRUE);
  gtk_widget_set_vexpand (preview_frame, TRUE);
  gtk_widget_add_css_class (preview_frame, "card");
  gtk_widget_set_margin_start (preview_frame, 12);
  gtk_widget_set_margin_end (preview_frame, 12);
  gtk_widget_set_margin_top (preview_frame, 12);
  gtk_widget_set_margin_bottom (preview_frame, 12);

  gtk_paned_set_start_child (GTK_PANED (work), source_scroll);
  gtk_paned_set_end_child (GTK_PANED (work), preview_frame);
  gtk_paned_set_resize_start_child (GTK_PANED (work), TRUE);
  gtk_paned_set_resize_end_child (GTK_PANED (work), TRUE);
  gtk_paned_set_position (GTK_PANED (work), 620);
  gtk_paned_set_start_child (GTK_PANED (outer), outline_scroll);
  gtk_paned_set_end_child (GTK_PANED (outer), work);
  gtk_paned_set_resize_start_child (GTK_PANED (outer), FALSE);
  gtk_paned_set_shrink_start_child (GTK_PANED (outer), TRUE);
  gtk_paned_set_position (GTK_PANED (outer), 230);

  gtk_widget_set_tooltip_text (back, "Back to presentation setup");
  g_signal_connect (back, "clicked", G_CALLBACK (back_clicked_cb), self);
  adw_header_bar_pack_start (header, back);
  adw_header_bar_set_title_widget (header, title);
  self->save_button = GTK_BUTTON (gtk_button_new_with_label ("Save"));
  gtk_widget_set_sensitive (GTK_WIDGET (self->save_button), FALSE);
  g_signal_connect (self->save_button, "clicked", G_CALLBACK (save_clicked_cb), self);
  g_signal_connect (present, "clicked", G_CALLBACK (present_clicked_cb), self);
  g_signal_connect (rehearse, "clicked", G_CALLBACK (rehearse_clicked_cb), self);
  gtk_widget_add_css_class (present, "suggested-action");
  adw_header_bar_pack_end (header, GTK_WIDGET (self->save_button));
  adw_header_bar_pack_end (header, rehearse);
  adw_header_bar_pack_end (header, present);
  adw_toolbar_view_add_top_bar (toolbar_view, GTK_WIDGET (header));

  self->status = GTK_LABEL (gtk_label_new ("Ready"));
  gtk_widget_add_css_class (GTK_WIDGET (self->status), "dim-label");
  gtk_label_set_xalign (self->status, 0.0f);
  gtk_widget_set_margin_start (GTK_WIDGET (self->status), 8);
  gtk_widget_set_margin_end (GTK_WIDGET (self->status), 8);
  gtk_widget_set_margin_top (GTK_WIDGET (self->status), 4);
  gtk_widget_set_margin_bottom (GTK_WIDGET (self->status), 4);
  gtk_box_append (GTK_BOX (status_box), GTK_WIDGET (self->status));
  gtk_box_append (GTK_BOX (status_box), outer);
  gtk_orientable_set_orientation (GTK_ORIENTABLE (status_box), GTK_ORIENTATION_VERTICAL);
  adw_toolbar_view_set_content (toolbar_view, status_box);

  self->completion = GTK_POPOVER (gtk_popover_new ());
  gtk_widget_set_parent (GTK_WIDGET (self->completion), GTK_WIDGET (self->source_view));
  gtk_popover_set_position (self->completion, GTK_POS_BOTTOM);
  g_signal_connect (keys, "key-pressed", G_CALLBACK (editor_key_pressed_cb), self);
  gtk_widget_add_controller (GTK_WIDGET (self->source_view), keys);
  g_signal_connect (self->buffer, "changed", G_CALLBACK (buffer_changed_cb), self);
  g_signal_connect (self->buffer, "mark-set", G_CALLBACK (cursor_moved_cb), self);
  g_signal_connect (self->outline,
                    "row-selected",
                    G_CALLBACK (outline_selected_cb),
                    self);
  return self->root;
}

G_MODULE_EXPORT guint
pp_editor_module_get_abi (void)
{
  return PP_EDITOR_MODULE_ABI;
}

G_MODULE_EXPORT GtkWidget *
pp_editor_module_create (const PpEditorHost *host,
                         GFile              *file)
{
  static const char starter[] =
    "[stage-color=#241f31] [font=Sans Bold 64px]\n"
    "-- [transition=fade]\n"
    "Your excellent presentation\n"
    "#Speaker notes go here\n";
  PpEditor *self;
  GtkWidget *widget;
  g_autofree char *source = NULL;
  g_autoptr (GError) error = NULL;

  g_return_val_if_fail (host != NULL, NULL);
  g_return_val_if_fail (host->abi_version == PP_EDITOR_MODULE_ABI, NULL);
  g_return_val_if_fail (GTK_IS_APPLICATION (host->application), NULL);
  g_return_val_if_fail (file == NULL || G_IS_FILE (file), NULL);

  self = g_new0 (PpEditor, 1);
  self->host = *host;
  self->file = file != NULL ? g_object_ref (file) : NULL;
  widget = create_editor (self);
  g_object_set_data_full (G_OBJECT (widget), "pinpoint-editor", self, editor_free);

  if (file != NULL &&
      !g_file_load_contents (file, NULL, &source, NULL, &self->etag, &error))
    {
      show_error (self, "Unable to Open Presentation", error->message);
      source = g_strdup (starter);
    }
  else if (source == NULL)
    source = g_strdup (starter);
  gtk_text_buffer_set_text (GTK_TEXT_BUFFER (self->buffer), source, -1);
  gtk_text_buffer_set_modified (GTK_TEXT_BUFFER (self->buffer), FALSE);
  gtk_widget_set_sensitive (GTK_WIDGET (self->save_button), FALSE);
  if (self->reparse_id != 0)
    {
      g_source_remove (self->reparse_id);
      self->reparse_id = 0;
    }
  editor_reparse (self);
  start_file_monitor (self);
  return widget;
}

G_MODULE_EXPORT void
pp_editor_module_apply_durations (GtkWidget    *editor,
                                  const double *durations,
                                  guint         n_durations)
{
  PpEditor *self;
  TimingPrompt *prompt;
  g_autofree char *source = NULL;
  g_autoptr (GError) error = NULL;
  AdwAlertDialog *dialog;

  g_return_if_fail (GTK_IS_WIDGET (editor));
  g_return_if_fail (durations != NULL || n_durations == 0);
  self = g_object_get_data (G_OBJECT (editor), "pinpoint-editor");
  g_return_if_fail (self != NULL);
  source = buffer_text (self);
  prompt = g_new0 (TimingPrompt, 1);
  prompt->editor = self;
  prompt->updated_source = pp_source_apply_durations (source,
                                                      durations,
                                                      n_durations,
                                                      &error);
  if (prompt->updated_source == NULL)
    {
      show_error (self, "Unable to Apply Rehearsal Timings", error->message);
      timing_prompt_free (prompt);
      return;
    }

  dialog = ADW_ALERT_DIALOG (adw_alert_dialog_new (
    "Apply Rehearsal Timings?",
    "This updates each slide's duration in the editor as one undoable change. It will not save automatically."));
  adw_alert_dialog_add_responses (dialog,
                                  "discard", "Discard",
                                  "apply", "Apply Timings",
                                  NULL);
  adw_alert_dialog_set_response_appearance (dialog,
                                            "apply",
                                            ADW_RESPONSE_SUGGESTED);
  adw_alert_dialog_set_default_response (dialog, "apply");
  adw_alert_dialog_set_close_response (dialog, "discard");
  g_object_set_data_full (G_OBJECT (dialog),
                          "pinpoint-timing-prompt",
                          prompt,
                          timing_prompt_free);
  g_signal_connect (dialog, "response", G_CALLBACK (timing_response_cb), prompt);
  adw_dialog_present (ADW_DIALOG (dialog), self->root);
}

G_MODULE_EXPORT void
pp_editor_module_request_close (GtkWidget *editor,
                                gboolean   quit)
{
  PpEditor *self;

  g_return_if_fail (GTK_IS_WIDGET (editor));
  self = g_object_get_data (G_OBJECT (editor), "pinpoint-editor");
  g_return_if_fail (self != NULL);
  request_close (self, quit);
}
