#include "config.h"

#include "pp-asset-monitor.h"
#include "pp-control.h"
#include "pp-display-selection.h"
#include "pp-file-access.h"
#include "pp-introduction.h"
#include "pp-presentation.h"
#include "pp-pdf.h"
#include "pp-render.h"
#include "pp-speaker.h"
#include "pp-stage.h"

#include <adwaita.h>
#include <gio/gfiledescriptorbased.h>
#include <gio/gunixfdlist.h>
#include <glib-unix.h>
#include <gtk/gtk.h>
#include <gst/gst.h>
#include <stdlib.h>

#define PORTAL_BUS_NAME "org.freedesktop.portal.Desktop"
#define PORTAL_OBJECT_PATH "/org/freedesktop/portal/desktop"
#define OPEN_URI_INTERFACE "org.freedesktop.portal.OpenURI"

typedef struct
{
  GtkApplication *application;
  GtkWindow *window;
  PpStage *stage;
  PpControl *control;
  GtkOverlay *overlay;
  GtkEntry *command_entry;
  GtkStack *view_stack;
  AdwSwitchRow *setup_fullscreen;
  AdwSwitchRow *setup_speaker;
  AdwSwitchRow *setup_ignore_comments;
  AdwComboRow *setup_audience_monitor;
  GPtrArray *setup_monitor_choices;
  PpSpeaker *speaker;
  GFile *file;
  GFile *presentation_folder;
  GFileMonitor *monitor;
  PpAssetMonitors *asset_monitors;
  guint reload_id;
  guint hide_cursor_id;
  guint sigint_id;
  guint sigterm_id;
  guint inhibit_cookie;
  gboolean ignore_comments;
  gboolean fullscreen;
  gboolean maximized;
  gboolean speaker_mode;
  gboolean rehearse;
  gboolean exporting_pdf;
  gboolean bundled_read_only;
  gboolean presenting;
  PpPdfOptions pdf_options;
  char *camera_device;
  GListModel *monitors;
  gulong monitors_changed_id;
  GdkMonitor *presenter_monitor;
  GdkMonitor *audience_monitor;
  gboolean updating_monitor_choices;
} Pinpoint;

static void set_fullscreen (Pinpoint *pinpoint,
                            gboolean  fullscreen);
static void set_speaker_visible (Pinpoint *pinpoint,
                                 gboolean  visible);
static void set_presenting (Pinpoint *pinpoint,
                            gboolean  presenting);
static void open_presentation_folder_dialog (Pinpoint *pinpoint);
static void start_monitor (Pinpoint *pinpoint);

static gboolean
termination_signal_cb (gpointer user_data)
{
  Pinpoint *pinpoint = user_data;

  g_application_quit (G_APPLICATION (pinpoint->application));
  return G_SOURCE_CONTINUE;
}

static gboolean
main_window_close_request_cb (GtkWindow *window,
                              gpointer   user_data)
{
  Pinpoint *pinpoint = user_data;

  (void) window;
  g_application_quit (G_APPLICATION (pinpoint->application));
  return TRUE;
}

static void
application_shutdown_cb (GApplication *application,
                         gpointer      user_data)
{
  Pinpoint *pinpoint = user_data;

  (void) application;
  set_presenting (pinpoint, FALSE);
}

static void
folder_retry_response_cb (AdwAlertDialog *dialog,
                          const char     *response,
                          gpointer        user_data)
{
  (void) dialog;

  if (g_str_equal (response, "choose"))
    open_presentation_folder_dialog (user_data);
}

static void
show_folder_problem (Pinpoint   *pinpoint,
                     const char *heading,
                     const char *body,
                     gboolean    allow_retry)
{
  AdwAlertDialog *dialog = ADW_ALERT_DIALOG (
    adw_alert_dialog_new (heading, body));

  adw_alert_dialog_add_response (dialog,
                                 "cancel",
                                 allow_retry ? "Cancel" : "Close");
  adw_alert_dialog_set_close_response (dialog, "cancel");
  if (allow_retry)
    {
      adw_alert_dialog_add_response (dialog, "choose", "Choose Folder");
      adw_alert_dialog_set_response_appearance (dialog,
                                                "choose",
                                                ADW_RESPONSE_SUGGESTED);
      adw_alert_dialog_set_default_response (dialog, "choose");
      g_signal_connect (dialog,
                        "response",
                        G_CALLBACK (folder_retry_response_cb),
                        pinpoint);
    }
  adw_dialog_present (ADW_DIALOG (dialog), GTK_WIDGET (pinpoint->window));
}

static void
show_missing_asset_recovery (Pinpoint   *pinpoint,
                             const char *asset)
{
  g_autofree char *body = g_strdup_printf (
    "Pinpoint can open the presentation, but it cannot access the relative "
    "asset “%s”. Choose the folder containing the presentation to grant "
    "access to its images, videos, and SVGs.",
    asset);

  show_folder_problem (pinpoint,
                       "Presentation Assets Are Unavailable",
                       body,
                       TRUE);
}

static void
dismiss_command_entry (Pinpoint *pinpoint)
{
  const char *command = gtk_editable_get_text (GTK_EDITABLE (pinpoint->command_entry));

  gtk_widget_set_opacity (GTK_WIDGET (pinpoint->command_entry), 0.33);
  gtk_widget_set_visible (GTK_WIDGET (pinpoint->command_entry), command[0] != '\0');
  gtk_widget_set_can_target (GTK_WIDGET (pinpoint->command_entry), FALSE);
  gtk_widget_grab_focus (GTK_WIDGET (pinpoint->stage));
}

static void
run_current_command (Pinpoint *pinpoint)
{
  const char *command = gtk_editable_get_text (GTK_EDITABLE (pinpoint->command_entry));
  g_autoptr (GSubprocessLauncher) launcher = NULL;
  g_autoptr (GSubprocess) process = NULL;
  g_autoptr (GError) error = NULL;

  if (command[0] == '\0')
    return;

  launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_NONE);
  process = g_subprocess_launcher_spawn (launcher,
                                         &error,
                                         "/bin/sh",
                                         "-c",
                                         command,
                                         NULL);
  if (process == NULL)
    g_warning ("Unable to run slide command: %s", error->message);
}

static void
command_activate_cb (GtkEntry *entry,
                     gpointer  user_data)
{
  Pinpoint *pinpoint = user_data;

  (void) entry;
  run_current_command (pinpoint);
  dismiss_command_entry (pinpoint);
}

static gboolean
command_key_pressed_cb (GtkEventControllerKey *controller,
                        guint                  keyval,
                        guint                  keycode,
                        GdkModifierType        state,
                        gpointer               user_data)
{
  Pinpoint *pinpoint = user_data;

  (void) controller;
  (void) keycode;
  (void) state;
  if (keyval == GDK_KEY_Escape || keyval == GDK_KEY_Tab)
    {
      dismiss_command_entry (pinpoint);
      return GDK_EVENT_STOP;
    }
  return GDK_EVENT_PROPAGATE;
}

static void
slide_changed_cb (PpStage *stage,
                  guint    index,
                  gpointer user_data)
{
  Pinpoint *pinpoint = user_data;
  const PpPresentation *presentation = pp_stage_get_presentation (stage);
  const PpSlide *slide = pp_presentation_get_slide (presentation, index);
  gboolean text_at_bottom;

  pp_control_set_slide (pinpoint->control,
                        index,
                        pp_presentation_get_n_slides (presentation));

  gtk_editable_set_text (GTK_EDITABLE (pinpoint->command_entry),
                         slide->command != NULL ? slide->command : "");
  gtk_widget_set_visible (GTK_WIDGET (pinpoint->command_entry),
                          slide->command != NULL && slide->command[0] != '\0');
  gtk_widget_set_opacity (GTK_WIDGET (pinpoint->command_entry), 0.33);
  gtk_widget_set_can_target (GTK_WIDGET (pinpoint->command_entry), FALSE);

  text_at_bottom = slide->text_position == PP_GRAVITY_BOTTOM ||
                   slide->text_position == PP_GRAVITY_BOTTOM_LEFT ||
                   slide->text_position == PP_GRAVITY_BOTTOM_RIGHT;
  gtk_widget_set_valign (GTK_WIDGET (pinpoint->command_entry),
                         text_at_bottom ? GTK_ALIGN_START : GTK_ALIGN_END);
}

static void
set_window_title (Pinpoint *pinpoint)
{
  g_autofree char *basename = NULL;
  g_autofree char *title = NULL;

  if (pinpoint->file == NULL)
    {
      gtk_window_set_title (pinpoint->window, "Pinpoint");
      return;
    }

  basename = g_file_get_basename (pinpoint->file);
  title = g_strdup_printf ("%s — Pinpoint", basename);
  gtk_window_set_title (pinpoint->window, title);
}

static gboolean
load_presentation (Pinpoint *pinpoint,
                   gboolean  reload)
{
  g_autoptr (PpPresentation) presentation = NULL;
  g_autoptr (GError) error = NULL;
  g_autofree char *missing_asset = NULL;
  guint initial_slide = 0;

  if (pinpoint->file == NULL)
    presentation = pp_presentation_new_usage ();
  else
    presentation = pp_presentation_load (pinpoint->file,
                                         pinpoint->ignore_comments,
                                         NULL,
                                         &error);

  if (presentation == NULL)
    {
      g_warning ("Unable to load presentation: %s", error->message);
      if (!reload && pinpoint->window != NULL)
        show_folder_problem (pinpoint,
                             "Unable to Open Presentation",
                             error->message,
                             TRUE);
      return FALSE;
    }

  if (!reload && pinpoint->presentation_folder == NULL)
    {
      missing_asset = pp_render_find_missing_relative_asset (presentation);
      if (missing_asset != NULL)
        {
          show_missing_asset_recovery (pinpoint, missing_asset);
          return FALSE;
        }
    }

  if (reload && pp_stage_get_presentation (pinpoint->stage) != NULL)
    initial_slide = pp_presentation_first_changed_slide (
      pp_stage_get_presentation (pinpoint->stage), presentation);

  pp_stage_set_presentation (pinpoint->stage,
                             g_steal_pointer (&presentation),
                             initial_slide);
  set_window_title (pinpoint);
  return TRUE;
}

static gboolean
reload_cb (gpointer user_data)
{
  Pinpoint *pinpoint = user_data;

  pinpoint->reload_id = 0;
  if (load_presentation (pinpoint, TRUE))
    start_monitor (pinpoint);
  return G_SOURCE_REMOVE;
}

static void
file_changed_cb (GFileMonitor      *monitor,
                 GFile             *file,
                 GFile             *other_file,
                 GFileMonitorEvent  event_type,
                 gpointer           user_data)
{
  Pinpoint *pinpoint = user_data;

  (void) monitor;
  (void) file;
  (void) other_file;
  (void) event_type;

  if (pinpoint->reload_id != 0)
    g_source_remove (pinpoint->reload_id);
  pinpoint->reload_id = g_timeout_add (200, reload_cb, pinpoint);
}

static void
asset_changed_cb (GFile    *file,
                  gpointer  user_data)
{
  Pinpoint *pinpoint = user_data;

  pp_stage_invalidate_asset (pinpoint->stage, file);
  if (pinpoint->speaker != NULL)
    pp_speaker_invalidate_asset (pinpoint->speaker, file);
}

static void
start_monitor (Pinpoint *pinpoint)
{
  g_autoptr (GError) error = NULL;
  const PpPresentation *presentation;

  g_clear_object (&pinpoint->monitor);
  g_clear_pointer (&pinpoint->asset_monitors, pp_asset_monitors_free);
  presentation = pp_stage_get_presentation (pinpoint->stage);
  if (presentation != NULL && !pinpoint->bundled_read_only)
    pinpoint->asset_monitors = pp_asset_monitors_new (presentation,
                                                       asset_changed_cb,
                                                       pinpoint);
  if (pinpoint->file == NULL || pinpoint->bundled_read_only)
    return;

  pinpoint->monitor = g_file_monitor_file (pinpoint->file,
                                           G_FILE_MONITOR_NONE,
                                           NULL,
                                           &error);
  if (pinpoint->monitor == NULL)
    {
      g_warning ("Unable to monitor presentation: %s", error->message);
      return;
    }

  g_signal_connect (pinpoint->monitor,
                    "changed",
                    G_CALLBACK (file_changed_cb),
                    pinpoint);
}

static gboolean
start_loaded_presentation (Pinpoint *pinpoint)
{
  if (!load_presentation (pinpoint, FALSE))
    return FALSE;

  start_monitor (pinpoint);
  gtk_stack_set_visible_child_name (pinpoint->view_stack, "presentation");
  set_presenting (pinpoint, TRUE);
  set_speaker_visible (pinpoint, pinpoint->speaker_mode);
  set_fullscreen (pinpoint, pinpoint->fullscreen);
  if (pinpoint->rehearse)
    {
      g_print ("Running in rehearsal mode; finish the final slide to save timings\n");
      pp_speaker_start_rehearsal (pinpoint->speaker);
    }
  gtk_widget_grab_focus (GTK_WIDGET (pinpoint->stage));
  return TRUE;
}

static void
open_file_portal_called_cb (GObject      *source,
                            GAsyncResult *result,
                            gpointer      user_data)
{
  Pinpoint *pinpoint = user_data;
  g_autoptr (GUnixFDList) output_fds = NULL;
  g_autoptr (GVariant) reply = NULL;
  g_autoptr (GError) error = NULL;

  reply = g_dbus_connection_call_with_unix_fd_list_finish (
    G_DBUS_CONNECTION (source),
    &output_fds,
    result,
    &error);
  if (reply == NULL)
    show_folder_problem (pinpoint,
                         "Unable to Open File",
                         error->message,
                         FALSE);
}

static void
open_file_with_portal (Pinpoint   *pinpoint,
                       GFile      *file,
                       const char *method)
{
  g_autoptr (GFileInputStream) stream = NULL;
  g_autoptr (GDBusConnection) connection = NULL;
  g_autoptr (GUnixFDList) fd_list = NULL;
  g_autoptr (GError) error = NULL;
  int fd_index;

  stream = g_file_read (file, NULL, &error);
  if (stream == NULL || !G_IS_FILE_DESCRIPTOR_BASED (stream))
    {
      show_folder_problem (
        pinpoint,
        "Unable to Open File",
        error != NULL ? error->message : "The exported file is not local.",
        FALSE);
      return;
    }

  fd_list = g_unix_fd_list_new ();
  fd_index = g_unix_fd_list_append (
    fd_list,
    g_file_descriptor_based_get_fd (G_FILE_DESCRIPTOR_BASED (stream)),
    &error);
  if (fd_index < 0)
    {
      show_folder_problem (pinpoint,
                           "Unable to Open File",
                           error->message,
                           FALSE);
      return;
    }

  connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
  if (connection == NULL)
    {
      show_folder_problem (pinpoint,
                           "Unable to Open File",
                           error->message,
                           FALSE);
      return;
    }

  g_dbus_connection_call_with_unix_fd_list (
    connection,
    PORTAL_BUS_NAME,
    PORTAL_OBJECT_PATH,
    OPEN_URI_INTERFACE,
    method,
    g_variant_new ("(sha{sv})", "", fd_index, NULL),
    G_VARIANT_TYPE ("(o)"),
    G_DBUS_CALL_FLAGS_NONE,
    -1,
    fd_list,
    NULL,
    open_file_portal_called_cb,
    pinpoint);
}

static void
export_success_response_cb (AdwAlertDialog *dialog,
                            const char     *response,
                            gpointer        user_data)
{
  Pinpoint *pinpoint = user_data;
  GFile *output = g_object_get_data (G_OBJECT (dialog), "pinpoint-output");

  if (g_str_equal (response, "open"))
    open_file_with_portal (pinpoint, output, "OpenFile");
  else if (g_str_equal (response, "show"))
    open_file_with_portal (pinpoint, output, "OpenDirectory");
}

static void
show_export_success (Pinpoint *pinpoint,
                     GFile    *output)
{
  g_autofree char *basename = g_file_get_basename (output);
  g_autofree char *body = g_strdup_printf ("“%s” was exported successfully.",
                                           basename);
  AdwAlertDialog *dialog = ADW_ALERT_DIALOG (
    adw_alert_dialog_new ("PDF Exported", body));

  adw_alert_dialog_add_response (dialog, "close", "Close");
  adw_alert_dialog_add_response (dialog, "show", "Show in Files");
  adw_alert_dialog_add_response (dialog, "open", "Open PDF");
  adw_alert_dialog_set_close_response (dialog, "close");
  adw_alert_dialog_set_default_response (dialog, "open");
  adw_alert_dialog_set_response_appearance (dialog,
                                            "open",
                                            ADW_RESPONSE_SUGGESTED);
  adw_alert_dialog_set_prefer_wide_layout (dialog, TRUE);
  g_object_set_data_full (G_OBJECT (dialog),
                          "pinpoint-output",
                          g_object_ref (output),
                          g_object_unref);
  g_signal_connect (dialog,
                    "response",
                    G_CALLBACK (export_success_response_cb),
                    pinpoint);
  adw_dialog_present (ADW_DIALOG (dialog), GTK_WIDGET (pinpoint->window));
}

static void
pdf_export_finished_cb (GObject      *source,
                        GAsyncResult *result,
                        gpointer      user_data)
{
  Pinpoint *pinpoint = user_data;
  GFile *output = pp_pdf_export_file_get_output (result);
  g_autoptr (GError) error = NULL;

  (void) source;
  pinpoint->exporting_pdf = FALSE;
  if (!pp_pdf_export_file_finish (result, &error))
    show_folder_problem (pinpoint, "Unable to Export PDF", error->message, FALSE);
  else
    show_export_success (pinpoint, output);
  g_application_release (G_APPLICATION (pinpoint->application));
}

static void
pdf_save_finished_cb (GObject      *source,
                      GAsyncResult *result,
                      gpointer      user_data)
{
  Pinpoint *pinpoint = user_data;
  g_autoptr (GFile) output = NULL;
  g_autoptr (GError) error = NULL;

  output = gtk_file_dialog_save_finish (GTK_FILE_DIALOG (source),
                                         result,
                                         &error);
  if (output == NULL)
    {
      pinpoint->exporting_pdf = FALSE;
      if (!g_error_matches (error, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_DISMISSED) &&
          !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        show_folder_problem (pinpoint,
                             "Unable to Export PDF",
                             error->message,
                             FALSE);
      return;
    }

  g_application_hold (G_APPLICATION (pinpoint->application));
  pp_pdf_export_file_async (pinpoint->file,
                            pinpoint->ignore_comments,
                            output,
                            &pinpoint->pdf_options,
                            NULL,
                            pdf_export_finished_cb,
                            pinpoint);
}

static void
choose_pdf_destination (Pinpoint *pinpoint)
{
  g_autoptr (GtkFileDialog) dialog = gtk_file_dialog_new ();
  g_autoptr (GtkFileFilter) filter = gtk_file_filter_new ();
  g_autoptr (GListStore) filters = g_list_store_new (GTK_TYPE_FILE_FILTER);
  g_autofree char *basename = g_file_get_basename (pinpoint->file);
  g_autofree char *stem = NULL;
  g_autofree char *output_name = NULL;
  gsize length = strlen (basename);

  stem = length > 4 && g_str_has_suffix (basename, ".pin")
    ? g_strndup (basename, length - 4)
    : g_strdup (basename);
  output_name = g_strconcat (stem, ".pdf", NULL);
  gtk_file_dialog_set_title (dialog, "Export Presentation to PDF");
  gtk_file_dialog_set_accept_label (dialog, "Export");
  gtk_file_dialog_set_initial_name (dialog, output_name);
  gtk_file_filter_set_name (filter, "PDF documents");
  gtk_file_filter_add_mime_type (filter, "application/pdf");
  gtk_file_filter_add_pattern (filter, "*.pdf");
  g_list_store_append (filters, filter);
  gtk_file_dialog_set_filters (dialog, G_LIST_MODEL (filters));
  gtk_file_dialog_set_default_filter (dialog, filter);
  gtk_file_dialog_save (dialog,
                        pinpoint->window,
                        NULL,
                        pdf_save_finished_cb,
                        pinpoint);
}

static void
pdf_options_response_cb (AdwAlertDialog *dialog,
                         const char     *response,
                         gpointer        user_data)
{
  Pinpoint *pinpoint = user_data;
  AdwComboRow *paper_size = g_object_get_data (G_OBJECT (dialog),
                                                "pinpoint-paper-size");
  AdwComboRow *orientation = g_object_get_data (G_OBJECT (dialog),
                                                "pinpoint-orientation");
  AdwSwitchRow *speaker_notes = g_object_get_data (G_OBJECT (dialog),
                                                    "pinpoint-speaker-notes");
  AdwSwitchRow *comment_notes = g_object_get_data (G_OBJECT (dialog),
                                                    "pinpoint-comment-notes");

  if (!g_str_equal (response, "export"))
    {
      pinpoint->exporting_pdf = FALSE;
      return;
    }

  pinpoint->pdf_options.page_size = adw_combo_row_get_selected (paper_size);
  pinpoint->pdf_options.orientation = adw_combo_row_get_selected (orientation);
  pinpoint->pdf_options.include_speaker_notes =
    adw_switch_row_get_active (speaker_notes);
  pinpoint->ignore_comments = !adw_switch_row_get_active (comment_notes);
  adw_switch_row_set_active (pinpoint->setup_ignore_comments,
                             pinpoint->ignore_comments);
  choose_pdf_destination (pinpoint);
}

static void
export_selected_presentation (Pinpoint *pinpoint)
{
  const char *paper_sizes[] = { "A4", "US Letter", NULL };
  const char *orientations[] = { "Landscape", "Portrait", NULL };
  g_autoptr (GtkStringList) paper_size_model = gtk_string_list_new (paper_sizes);
  g_autoptr (GtkStringList) orientation_model = gtk_string_list_new (orientations);
  g_autofree char *basename = g_file_get_basename (pinpoint->file);
  g_autofree char *body = g_strdup_printf (
    "Choose how “%s” should be exported.", basename);
  AdwAlertDialog *dialog = ADW_ALERT_DIALOG (
    adw_alert_dialog_new ("Export to PDF", body));
  GtkWidget *group = adw_preferences_group_new ();
  AdwComboRow *paper_size = ADW_COMBO_ROW (adw_combo_row_new ());
  AdwComboRow *orientation = ADW_COMBO_ROW (adw_combo_row_new ());
  AdwSwitchRow *speaker_notes = ADW_SWITCH_ROW (adw_switch_row_new ());
  AdwSwitchRow *comment_notes = ADW_SWITCH_ROW (adw_switch_row_new ());

  adw_preferences_group_set_title (ADW_PREFERENCES_GROUP (group),
                                   "Document");
  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (paper_size),
                                 "Paper Size");
  adw_combo_row_set_model (paper_size, G_LIST_MODEL (paper_size_model));
  adw_combo_row_set_selected (paper_size, pinpoint->pdf_options.page_size);
  adw_preferences_group_add (ADW_PREFERENCES_GROUP (group),
                             GTK_WIDGET (paper_size));

  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (orientation),
                                 "Orientation");
  adw_combo_row_set_model (orientation, G_LIST_MODEL (orientation_model));
  adw_combo_row_set_selected (orientation,
                              pinpoint->pdf_options.orientation);
  adw_preferences_group_add (ADW_PREFERENCES_GROUP (group),
                             GTK_WIDGET (orientation));

  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (speaker_notes),
                                 "Speaker Notes");
  adw_action_row_set_subtitle (
    ADW_ACTION_ROW (speaker_notes),
    "Add a separate notes page after each slide that has notes");
  adw_switch_row_set_active (speaker_notes,
                             pinpoint->pdf_options.include_speaker_notes);
  adw_preferences_group_add (ADW_PREFERENCES_GROUP (group),
                             GTK_WIDGET (speaker_notes));

  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (comment_notes),
                                 "Comment Notes");
  adw_action_row_set_subtitle (ADW_ACTION_ROW (comment_notes),
                               "Treat comment lines as speaker notes");
  adw_switch_row_set_active (comment_notes, !pinpoint->ignore_comments);
  adw_preferences_group_add (ADW_PREFERENCES_GROUP (group),
                             GTK_WIDGET (comment_notes));
  g_object_bind_property (speaker_notes,
                          "active",
                          comment_notes,
                          "sensitive",
                          G_BINDING_SYNC_CREATE);

  adw_alert_dialog_set_extra_child (dialog, group);
  adw_alert_dialog_add_response (dialog, "cancel", "Cancel");
  adw_alert_dialog_add_response (dialog, "export", "Continue");
  adw_alert_dialog_set_close_response (dialog, "cancel");
  adw_alert_dialog_set_default_response (dialog, "export");
  adw_alert_dialog_set_response_appearance (dialog,
                                            "export",
                                            ADW_RESPONSE_SUGGESTED);
  adw_alert_dialog_set_prefer_wide_layout (dialog, TRUE);
  g_object_set_data (G_OBJECT (dialog), "pinpoint-paper-size", paper_size);
  g_object_set_data (G_OBJECT (dialog), "pinpoint-orientation", orientation);
  g_object_set_data (G_OBJECT (dialog),
                     "pinpoint-speaker-notes",
                     speaker_notes);
  g_object_set_data (G_OBJECT (dialog),
                     "pinpoint-comment-notes",
                     comment_notes);
  g_signal_connect (dialog,
                    "response",
                    G_CALLBACK (pdf_options_response_cb),
                    pinpoint);
  adw_dialog_present (ADW_DIALOG (dialog), GTK_WIDGET (pinpoint->window));
}

static void
use_selected_presentation (Pinpoint *pinpoint)
{
  if (pinpoint->exporting_pdf)
    {
      export_selected_presentation (pinpoint);
      return;
    }

  if (!start_loaded_presentation (pinpoint))
    {
      g_clear_object (&pinpoint->file);
      set_window_title (pinpoint);
    }
}

static void
presentation_choose_clicked_cb (GtkButton *button,
                                gpointer   user_data)
{
  Pinpoint *pinpoint = user_data;
  GFile *file = g_object_get_data (G_OBJECT (button), "pinpoint-file");
  GtkWidget *dialog = gtk_widget_get_ancestor (GTK_WIDGET (button),
                                               ADW_TYPE_DIALOG);

  if (dialog != NULL)
    adw_dialog_close (ADW_DIALOG (dialog));
  g_set_object (&pinpoint->file, file);
  use_selected_presentation (pinpoint);
}

static void
show_presentation_choice (Pinpoint *pinpoint,
                          GPtrArray *files)
{
  AdwDialog *dialog = adw_dialog_new ();
  GtkWidget *toolbar = adw_toolbar_view_new ();
  GtkWidget *header = adw_header_bar_new ();
  GtkWidget *scrolled = gtk_scrolled_window_new ();
  GtkWidget *clamp = adw_clamp_new ();
  GtkWidget *group = adw_preferences_group_new ();

  adw_dialog_set_title (dialog, "Choose a Presentation");
  adw_dialog_set_content_width (dialog, 520);
  adw_dialog_set_content_height (dialog, MIN (180 + (int) files->len * 56, 560));
  adw_toolbar_view_add_top_bar (ADW_TOOLBAR_VIEW (toolbar), header);
  adw_toolbar_view_set_content (ADW_TOOLBAR_VIEW (toolbar), scrolled);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled),
                                  GTK_POLICY_NEVER,
                                  GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled), clamp);
  adw_clamp_set_maximum_size (ADW_CLAMP (clamp), 480);
  adw_clamp_set_child (ADW_CLAMP (clamp), group);
  gtk_widget_set_margin_start (group, 18);
  gtk_widget_set_margin_end (group, 18);
  gtk_widget_set_margin_top (group, 18);
  gtk_widget_set_margin_bottom (group, 18);
  adw_preferences_group_set_description (
    ADW_PREFERENCES_GROUP (group),
    "This folder contains more than one .pin file.");

  for (guint i = 0; i < files->len; i++)
    {
      GFile *file = g_ptr_array_index (files, i);
      g_autofree char *name = g_file_get_basename (file);
      GtkWidget *row = adw_action_row_new ();
      GtkWidget *choose = gtk_button_new_from_icon_name ("go-next-symbolic");

      adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), name);
      gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (row), TRUE);
      gtk_widget_add_css_class (choose, "flat");
      gtk_widget_set_valign (choose, GTK_ALIGN_CENTER);
      gtk_widget_set_tooltip_text (choose, "Open Presentation");
      adw_action_row_add_suffix (ADW_ACTION_ROW (row), choose);
      adw_action_row_set_activatable_widget (ADW_ACTION_ROW (row), choose);
      g_object_set_data_full (G_OBJECT (choose),
                              "pinpoint-file",
                              g_object_ref (file),
                              g_object_unref);
      g_signal_connect (choose,
                        "clicked",
                        G_CALLBACK (presentation_choose_clicked_cb),
                        pinpoint);
      adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), row);
    }

  adw_dialog_set_child (dialog, toolbar);
  adw_dialog_present (dialog, GTK_WIDGET (pinpoint->window));
}

static void
folder_dialog_selected_cb (GObject      *source,
                           GAsyncResult *result,
                           gpointer      user_data)
{
  Pinpoint *pinpoint = user_data;
  g_autoptr (GFile) folder = NULL;
  g_autoptr (GPtrArray) files = NULL;
  g_autoptr (GError) error = NULL;

  folder = gtk_file_dialog_select_folder_finish (GTK_FILE_DIALOG (source),
                                                  result,
                                                  &error);
  if (folder == NULL)
    {
      if (g_error_matches (error, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_DISMISSED) ||
          g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        pinpoint->exporting_pdf = FALSE;
      if (!g_error_matches (error, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_DISMISSED) &&
          !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        show_folder_problem (pinpoint,
                             "Unable to Open Folder",
                             error->message,
                             TRUE);
      return;
    }

  files = pp_file_access_find_presentations (folder, NULL, &error);
  if (files == NULL)
    {
      show_folder_problem (pinpoint,
                           "Unable to Read Folder",
                           error->message,
                           TRUE);
      return;
    }

  g_set_object (&pinpoint->presentation_folder, folder);
  if (files->len == 0)
    {
      show_folder_problem (pinpoint,
                           "No Pinpoint Presentations Found",
                           "Choose a folder containing at least one .pin file.",
                           TRUE);
      return;
    }

  if (files->len > 1)
    {
      show_presentation_choice (pinpoint, files);
      return;
    }

  g_set_object (&pinpoint->file, g_ptr_array_index (files, 0));
  use_selected_presentation (pinpoint);
}

static void
open_presentation_folder_dialog (Pinpoint *pinpoint)
{
  g_autoptr (GtkFileDialog) dialog = NULL;

  dialog = gtk_file_dialog_new ();
  gtk_file_dialog_set_title (dialog, "Choose Presentation Folder");
  gtk_file_dialog_set_accept_label (dialog, "Choose Folder");
  gtk_file_dialog_select_folder (dialog,
                                 pinpoint->window,
                                 NULL,
                                 folder_dialog_selected_cb,
                                 pinpoint);
}

static void
apply_setup_options (Pinpoint *pinpoint,
                     gboolean  rehearse)
{
  pinpoint->fullscreen =
    adw_switch_row_get_active (pinpoint->setup_fullscreen);
  pinpoint->speaker_mode =
    rehearse || adw_switch_row_get_active (pinpoint->setup_speaker);
  pinpoint->ignore_comments =
    adw_switch_row_get_active (pinpoint->setup_ignore_comments);
  pinpoint->rehearse = rehearse;
  pinpoint->exporting_pdf = FALSE;
}

static void
open_for_mode (Pinpoint *pinpoint,
               gboolean  rehearse)
{
  apply_setup_options (pinpoint, rehearse);
  pinpoint->bundled_read_only = FALSE;
  open_presentation_folder_dialog (pinpoint);
}

static void
present_clicked_cb (GtkButton *button,
                    gpointer   user_data)
{
  (void) button;
  open_for_mode (user_data, FALSE);
}

static void
rehearse_clicked_cb (GtkButton *button,
                     gpointer   user_data)
{
  (void) button;
  open_for_mode (user_data, TRUE);
}

static void
audience_monitor_selected_cb (AdwComboRow *row,
                              GParamSpec  *pspec,
                              gpointer     user_data)
{
  Pinpoint *pinpoint = user_data;
  guint selected;

  (void) pspec;
  if (pinpoint->updating_monitor_choices)
    return;

  selected = adw_combo_row_get_selected (row);
  if (selected == 0 ||
      pinpoint->setup_monitor_choices == NULL ||
      selected > pinpoint->setup_monitor_choices->len)
    g_clear_object (&pinpoint->audience_monitor);
  else
    g_set_object (&pinpoint->audience_monitor,
                  g_ptr_array_index (pinpoint->setup_monitor_choices,
                                     selected - 1));
  g_clear_object (&pinpoint->presenter_monitor);
}

static void
update_monitor_choices (Pinpoint *pinpoint)
{
  g_autoptr (GtkStringList) names = gtk_string_list_new (NULL);
  guint count = g_list_model_get_n_items (pinpoint->monitors);
  guint selected = 0;

  pinpoint->updating_monitor_choices = TRUE;
  g_clear_pointer (&pinpoint->setup_monitor_choices, g_ptr_array_unref);
  pinpoint->setup_monitor_choices =
    g_ptr_array_new_with_free_func (g_object_unref);
  gtk_string_list_append (names, "Automatic");

  for (guint i = 0; i < count; i++)
    {
      GdkMonitor *monitor = g_list_model_get_item (pinpoint->monitors, i);
      const char *description = gdk_monitor_get_description (monitor);
      const char *connector = gdk_monitor_get_connector (monitor);
      g_autofree char *label = NULL;

      if (description != NULL && connector != NULL)
        label = g_strdup_printf ("%s (%s)", description, connector);
      else if (description != NULL)
        label = g_strdup (description);
      else if (connector != NULL)
        label = g_strdup (connector);
      else
        label = g_strdup_printf ("Display %u", i + 1);
      gtk_string_list_append (names, label);
      g_ptr_array_add (pinpoint->setup_monitor_choices, monitor);
      if (monitor == pinpoint->audience_monitor)
        selected = i + 1;
    }

  adw_combo_row_set_model (pinpoint->setup_audience_monitor,
                           G_LIST_MODEL (names));
  adw_combo_row_set_selected (pinpoint->setup_audience_monitor, selected);
  gtk_widget_set_visible (GTK_WIDGET (pinpoint->setup_audience_monitor),
                          count > 1);
  pinpoint->updating_monitor_choices = FALSE;
}

static const char *
get_application_icon_name (Pinpoint *pinpoint)
{
  const char *flatpak_id = g_getenv ("FLATPAK_ID");
  const char *application_id = g_application_get_application_id (
    G_APPLICATION (pinpoint->application));
  GtkIconTheme *theme = gtk_icon_theme_get_for_display (
    gtk_widget_get_display (GTK_WIDGET (pinpoint->window)));
  const char *candidates[] = {
    flatpak_id,
    application_id,
    PINPOINT_APPLICATION_ID,
    NULL,
  };

  for (guint i = 0; i < G_N_ELEMENTS (candidates) - 1; i++)
    if (candidates[i] != NULL &&
        candidates[i][0] != '\0' &&
        gtk_icon_theme_has_icon (theme, candidates[i]))
      return candidates[i];

  return PINPOINT_APPLICATION_ID;
}

static void
view_bundled_introduction (Pinpoint *pinpoint)
{
  g_autoptr (GFile) presentation = pp_introduction_get_presentation ();
  g_autoptr (GFile) folder = g_file_get_parent (presentation);

  apply_setup_options (pinpoint, FALSE);
  pinpoint->bundled_read_only = TRUE;
  g_set_object (&pinpoint->file, presentation);
  g_set_object (&pinpoint->presentation_folder, folder);
  if (!start_loaded_presentation (pinpoint))
    {
      pinpoint->bundled_read_only = FALSE;
      g_clear_object (&pinpoint->file);
      g_clear_object (&pinpoint->presentation_folder);
      set_window_title (pinpoint);
    }
}

static void
view_introduction_action_cb (GSimpleAction *action,
                             GVariant      *parameter,
                             gpointer       user_data)
{
  (void) action;
  (void) parameter;
  view_bundled_introduction (user_data);
}

static void
introduction_saved_response_cb (AdwAlertDialog *dialog,
                                const char     *response,
                                gpointer        user_data)
{
  Pinpoint *pinpoint = user_data;
  GFile *presentation = g_object_get_data (G_OBJECT (dialog),
                                            "pinpoint-presentation");

  if (g_str_equal (response, "show"))
    {
      open_file_with_portal (pinpoint, presentation, "OpenDirectory");
      return;
    }
  if (g_str_equal (response, "open"))
    {
      g_autoptr (GFile) folder = g_file_get_parent (presentation);

      apply_setup_options (pinpoint, FALSE);
      pinpoint->bundled_read_only = FALSE;
      g_set_object (&pinpoint->file, presentation);
      g_set_object (&pinpoint->presentation_folder, folder);
      if (!start_loaded_presentation (pinpoint))
        {
          g_clear_object (&pinpoint->file);
          g_clear_object (&pinpoint->presentation_folder);
          set_window_title (pinpoint);
        }
    }
}

static void
show_introduction_saved (Pinpoint *pinpoint,
                         GFile    *presentation)
{
  g_autoptr (GFile) folder = g_file_get_parent (presentation);
  g_autofree char *folder_name = g_file_get_basename (folder);
  g_autofree char *body = g_strdup_printf (
    "The presentation, media, attribution, and image assets were copied to “%s”.",
    folder_name);
  AdwAlertDialog *dialog = ADW_ALERT_DIALOG (
    adw_alert_dialog_new ("Editable Copy Saved", body));

  adw_alert_dialog_add_response (dialog, "close", "Close");
  adw_alert_dialog_add_response (dialog, "show", "Show in Files");
  adw_alert_dialog_add_response (dialog, "open", "Open Copy");
  adw_alert_dialog_set_close_response (dialog, "close");
  adw_alert_dialog_set_default_response (dialog, "open");
  adw_alert_dialog_set_response_appearance (dialog,
                                            "open",
                                            ADW_RESPONSE_SUGGESTED);
  adw_alert_dialog_set_prefer_wide_layout (dialog, TRUE);
  g_object_set_data_full (G_OBJECT (dialog),
                          "pinpoint-presentation",
                          g_object_ref (presentation),
                          g_object_unref);
  g_signal_connect (dialog,
                    "response",
                    G_CALLBACK (introduction_saved_response_cb),
                    pinpoint);
  adw_dialog_present (ADW_DIALOG (dialog), GTK_WIDGET (pinpoint->window));
}

static void
introduction_folder_selected_cb (GObject      *source,
                                 GAsyncResult *result,
                                 gpointer      user_data)
{
  Pinpoint *pinpoint = user_data;
  g_autoptr (GFile) folder = NULL;
  g_autoptr (GFile) presentation = NULL;
  g_autoptr (GError) error = NULL;

  folder = gtk_file_dialog_select_folder_finish (GTK_FILE_DIALOG (source),
                                                  result,
                                                  &error);
  if (folder == NULL)
    {
      if (!g_error_matches (error, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_DISMISSED) &&
          !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        show_folder_problem (pinpoint,
                             "Unable to Save Introduction",
                             error->message,
                             FALSE);
      return;
    }

  if (!pp_introduction_copy_to_folder (folder,
                                       &presentation,
                                       NULL,
                                       &error))
    {
      g_autofree char *body = NULL;

      body = g_error_matches (error, G_IO_ERROR, G_IO_ERROR_EXISTS)
        ? g_strdup_printf ("%s. Choose an empty folder so existing work is not overwritten.",
                           error->message)
        : g_strdup (error->message);
      show_folder_problem (pinpoint,
                           "Unable to Save Introduction",
                           body,
                           FALSE);
      return;
    }
  show_introduction_saved (pinpoint, presentation);
}

static void
save_introduction_action_cb (GSimpleAction *action,
                             GVariant      *parameter,
                             gpointer       user_data)
{
  Pinpoint *pinpoint = user_data;
  g_autoptr (GtkFileDialog) dialog = gtk_file_dialog_new ();

  (void) action;
  (void) parameter;
  gtk_file_dialog_set_title (dialog, "Save Editable Introduction");
  gtk_file_dialog_set_accept_label (dialog, "Save Copy Here");
  gtk_file_dialog_select_folder (dialog,
                                 pinpoint->window,
                                 NULL,
                                 introduction_folder_selected_cb,
                                 pinpoint);
}

static void
export_pdf_action_cb (GSimpleAction *action,
                      GVariant      *parameter,
                      gpointer       user_data)
{
  Pinpoint *pinpoint = user_data;

  (void) action;
  (void) parameter;
  pinpoint->ignore_comments =
    adw_switch_row_get_active (pinpoint->setup_ignore_comments);
  pinpoint->exporting_pdf = TRUE;
  open_presentation_folder_dialog (pinpoint);
}

static void
about_action_cb (GSimpleAction *action,
                 GVariant      *parameter,
                 gpointer       user_data)
{
  static const char *original_authors[] = {
    "Øyvind Kolås",
    "Damien Lespiau",
    "Emmanuele Bassi",
    "Neil Roberts",
    "Nick Richards",
    "Daniel G. Siegel",
    "Jussi Kukkonen",
    "Chris Lord",
    "Will Thompson",
    "Andoni Morales Alastruey",
    "Vladimír Kincl",
    "Antonio Terceiro",
    "Gary Ching-Pang Lin",
    "Lionel Landwerlin",
    "Christoph Fischer",
    "Douglas Bagnall",
    NULL,
  };
  Pinpoint *pinpoint = user_data;
  AdwAboutDialog *dialog = ADW_ABOUT_DIALOG (adw_about_dialog_new ());

  (void) action;
  (void) parameter;
  adw_about_dialog_set_application_name (dialog, "Pinpoint");
  adw_about_dialog_set_application_icon (dialog,
                                         get_application_icon_name (pinpoint));
  adw_about_dialog_set_developer_name (dialog, "Nick Richards");
  adw_about_dialog_set_version (dialog, PINPOINT_VERSION);
  adw_about_dialog_set_comments (
    dialog,
    PINPOINT_TAGLINE ". Write concise plain-text slides in the editor of "
    "your choice.");
  adw_about_dialog_set_website (dialog,
                                "https://github.com/nedrichards/fun-pinpoint");
  adw_about_dialog_set_issue_url (
    dialog,
    "https://github.com/nedrichards/fun-pinpoint/issues");
  adw_about_dialog_set_copyright (
    dialog,
    "Copyright © 2026 Nick Richards");
  adw_about_dialog_set_license_type (dialog, GTK_LICENSE_LGPL_2_1);
  adw_about_dialog_add_legal_section (
    dialog,
    "Original Pinpoint codebase (inspiration)",
    "Copyright © 2010 Intel Corporation and the original Pinpoint "
    "contributors",
    GTK_LICENSE_LGPL_2_1,
    NULL);
  adw_about_dialog_add_legal_section (
    dialog,
    "Big Buck Bunny excerpt",
    "Copyright © 2008 Blender Foundation",
    GTK_LICENSE_CUSTOM,
    "Creative Commons Attribution 3.0\n\n"
    "Source: https://commons.wikimedia.org/wiki/"
    "File:Big_Buck_Bunny_medium.ogv\n"
    "Attribution: https://www.bigbuckbunny.org/\n"
    "License: https://creativecommons.org/licenses/by/3.0/\n\n"
    "The bundled introduction uses a short excerpt, trimmed and re-encoded "
    "as VP9/Opus WebM.");
  adw_about_dialog_add_credit_section (dialog,
                                       "Original Pinpoint Authors (inspiration)",
                                       original_authors);
  adw_dialog_present (ADW_DIALOG (dialog), GTK_WIDGET (pinpoint->window));
}

static GtkWidget *
create_setup_view (Pinpoint *pinpoint)
{
  static const GActionEntry actions[] = {
    { .name = "view-introduction", .activate = view_introduction_action_cb },
    { .name = "save-introduction", .activate = save_introduction_action_cb },
    { .name = "export-pdf", .activate = export_pdf_action_cb },
    { .name = "about", .activate = about_action_cb },
  };
  const char *icon_name = get_application_icon_name (pinpoint);
  g_autoptr (GMenu) menu = g_menu_new ();
  GtkWidget *toolbar = adw_toolbar_view_new ();
  GtkWidget *header = adw_header_bar_new ();
  GtkWidget *menu_button = gtk_menu_button_new ();
  GtkWidget *scrolled = gtk_scrolled_window_new ();
  GtkWidget *clamp = adw_clamp_new ();
  GtkWidget *content = gtk_box_new (GTK_ORIENTATION_VERTICAL, 18);
  GtkWidget *hero = gtk_box_new (GTK_ORIENTATION_VERTICAL, 10);
  GtkWidget *hero_icon = gtk_image_new_from_icon_name (icon_name);
  GtkWidget *hero_title = gtk_label_new (PINPOINT_TAGLINE);
  GtkWidget *hero_description = gtk_label_new (
    "Write concise slides in your text editor, then tune them live.");
  GtkWidget *learn_group = adw_preferences_group_new ();
  GtkWidget *learn_row = adw_action_row_new ();
  GtkWidget *learn_icon = gtk_image_new_from_icon_name (
    "help-contents-symbolic");
  GtkWidget *view_introduction = gtk_button_new_with_label ("View");
  GtkWidget *save_introduction = gtk_button_new_from_icon_name (
    "document-save-symbolic");
  GtkWidget *group = adw_preferences_group_new ();
  GtkWidget *buttons = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
  GtkWidget *present = gtk_button_new_with_label ("Present…");
  GtkWidget *rehearse = gtk_button_new_with_label ("Rehearse…");
  GtkExpression *expression;

  g_action_map_add_action_entries (G_ACTION_MAP (pinpoint->window),
                                   actions,
                                   G_N_ELEMENTS (actions),
                                   pinpoint);
  g_menu_append (menu, "Export to PDF…", "win.export-pdf");
  g_menu_append (menu, "About Pinpoint", "win.about");
  gtk_menu_button_set_icon_name (GTK_MENU_BUTTON (menu_button),
                                 "open-menu-symbolic");
  gtk_menu_button_set_menu_model (GTK_MENU_BUTTON (menu_button),
                                  G_MENU_MODEL (menu));
  gtk_widget_set_tooltip_text (menu_button, "Main Menu");
  gtk_accessible_update_property (GTK_ACCESSIBLE (menu_button),
                                  GTK_ACCESSIBLE_PROPERTY_LABEL,
                                  "Main Menu",
                                  -1);
  adw_header_bar_pack_end (ADW_HEADER_BAR (header), menu_button);
  adw_toolbar_view_add_top_bar (ADW_TOOLBAR_VIEW (toolbar), header);
  adw_toolbar_view_set_content (ADW_TOOLBAR_VIEW (toolbar), scrolled);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled),
                                  GTK_POLICY_NEVER,
                                  GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled), clamp);
  adw_clamp_set_maximum_size (ADW_CLAMP (clamp), 640);
  adw_clamp_set_child (ADW_CLAMP (clamp), content);
  gtk_widget_set_margin_start (content, 24);
  gtk_widget_set_margin_end (content, 24);
  gtk_widget_set_margin_top (content, 12);
  gtk_widget_set_margin_bottom (content, 18);

  gtk_widget_set_halign (hero, GTK_ALIGN_CENTER);
  gtk_widget_set_margin_top (hero, 6);
  gtk_image_set_pixel_size (GTK_IMAGE (hero_icon), 128);
  gtk_widget_add_css_class (hero_icon, "accent");
  gtk_widget_add_css_class (hero_title, "title-1");
  gtk_widget_add_css_class (hero_description, "dim-label");
  gtk_label_set_wrap (GTK_LABEL (hero_description), TRUE);
  gtk_label_set_justify (GTK_LABEL (hero_description), GTK_JUSTIFY_CENTER);
  gtk_box_append (GTK_BOX (hero), hero_icon);
  gtk_box_append (GTK_BOX (hero), hero_title);
  gtk_box_append (GTK_BOX (hero), hero_description);
  gtk_box_append (GTK_BOX (content), hero);

  adw_preferences_group_set_title (ADW_PREFERENCES_GROUP (learn_group),
                                   "See How Pinpoint Works");
  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (learn_row),
                                 "Introduction, Made with Pinpoint");
  adw_action_row_set_subtitle (
    ADW_ACTION_ROW (learn_row),
    "Explore the plain-text format, visuals, and controls");
  gtk_widget_add_css_class (learn_icon, "dim-label");
  adw_action_row_add_prefix (ADW_ACTION_ROW (learn_row), learn_icon);
  gtk_actionable_set_action_name (GTK_ACTIONABLE (view_introduction),
                                  "win.view-introduction");
  gtk_widget_set_valign (view_introduction, GTK_ALIGN_CENTER);
  gtk_widget_add_css_class (view_introduction, "pill");
  adw_action_row_add_suffix (ADW_ACTION_ROW (learn_row), view_introduction);
  gtk_actionable_set_action_name (GTK_ACTIONABLE (save_introduction),
                                  "win.save-introduction");
  gtk_widget_set_valign (save_introduction, GTK_ALIGN_CENTER);
  gtk_widget_add_css_class (save_introduction, "flat");
  gtk_widget_set_tooltip_text (save_introduction, "Save an Editable Copy");
  gtk_accessible_update_property (GTK_ACCESSIBLE (save_introduction),
                                  GTK_ACCESSIBLE_PROPERTY_LABEL,
                                  "Save an Editable Copy",
                                  -1);
  adw_action_row_add_suffix (ADW_ACTION_ROW (learn_row), save_introduction);
  adw_preferences_group_add (ADW_PREFERENCES_GROUP (learn_group), learn_row);
  gtk_box_append (GTK_BOX (content), learn_group);

  adw_preferences_group_set_title (ADW_PREFERENCES_GROUP (group),
                                   "Your Presentation");
  pinpoint->setup_fullscreen = ADW_SWITCH_ROW (adw_switch_row_new ());
  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (pinpoint->setup_fullscreen),
                                 "Start Fullscreen");
  adw_action_row_set_subtitle (ADW_ACTION_ROW (pinpoint->setup_fullscreen),
                               "Use the audience display for the presentation");
  adw_switch_row_set_active (pinpoint->setup_fullscreen, pinpoint->fullscreen);
  adw_preferences_group_add (ADW_PREFERENCES_GROUP (group),
                             GTK_WIDGET (pinpoint->setup_fullscreen));

  pinpoint->setup_speaker = ADW_SWITCH_ROW (adw_switch_row_new ());
  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (pinpoint->setup_speaker),
                                 "Show Speaker View");
  adw_action_row_set_subtitle (ADW_ACTION_ROW (pinpoint->setup_speaker),
                               "Show notes, previews, timing, and controls");
  adw_switch_row_set_active (pinpoint->setup_speaker, pinpoint->speaker_mode);
  adw_preferences_group_add (ADW_PREFERENCES_GROUP (group),
                             GTK_WIDGET (pinpoint->setup_speaker));

  pinpoint->setup_ignore_comments = ADW_SWITCH_ROW (adw_switch_row_new ());
  adw_preferences_row_set_title (
    ADW_PREFERENCES_ROW (pinpoint->setup_ignore_comments),
    "Ignore Comments");
  adw_action_row_set_subtitle (
    ADW_ACTION_ROW (pinpoint->setup_ignore_comments),
    "Do not display comment lines as speaker notes");
  adw_switch_row_set_active (pinpoint->setup_ignore_comments,
                             pinpoint->ignore_comments);
  adw_preferences_group_add (ADW_PREFERENCES_GROUP (group),
                             GTK_WIDGET (pinpoint->setup_ignore_comments));

  pinpoint->setup_audience_monitor = ADW_COMBO_ROW (adw_combo_row_new ());
  adw_preferences_row_set_title (
    ADW_PREFERENCES_ROW (pinpoint->setup_audience_monitor),
    "Audience Display");
  adw_action_row_set_subtitle (
    ADW_ACTION_ROW (pinpoint->setup_audience_monitor),
    "Choose where fullscreen slides appear");
  expression = gtk_property_expression_new (GTK_TYPE_STRING_OBJECT,
                                            NULL,
                                            "string");
  adw_combo_row_set_expression (pinpoint->setup_audience_monitor, expression);
  gtk_expression_unref (expression);
  adw_preferences_group_add (ADW_PREFERENCES_GROUP (group),
                             GTK_WIDGET (pinpoint->setup_audience_monitor));
  g_signal_connect (pinpoint->setup_audience_monitor,
                    "notify::selected",
                    G_CALLBACK (audience_monitor_selected_cb),
                    pinpoint);
  gtk_box_append (GTK_BOX (content), group);

  gtk_box_set_homogeneous (GTK_BOX (buttons), TRUE);
  gtk_widget_set_hexpand (present, TRUE);
  gtk_widget_add_css_class (present, "suggested-action");
  gtk_widget_add_css_class (present, "pill");
  gtk_widget_set_hexpand (rehearse, TRUE);
  gtk_widget_add_css_class (rehearse, "pill");
  gtk_box_append (GTK_BOX (buttons), rehearse);
  gtk_box_append (GTK_BOX (buttons), present);
  gtk_box_append (GTK_BOX (content), buttons);
  g_signal_connect (present, "clicked", G_CALLBACK (present_clicked_cb), pinpoint);
  g_signal_connect (rehearse, "clicked", G_CALLBACK (rehearse_clicked_cb), pinpoint);
  return toolbar;
}

static gboolean
monitor_is_builtin (GdkMonitor *monitor)
{
  const char *connector = gdk_monitor_get_connector (monitor);

  return connector != NULL &&
         (g_str_has_prefix (connector, "eDP") ||
          g_str_has_prefix (connector, "LVDS") ||
          g_str_has_prefix (connector, "DSI"));
}

static GdkMonitor *
monitor_at_window (GtkWindow *window)
{
  GdkSurface *surface = gtk_native_get_surface (GTK_NATIVE (window));
  GdkDisplay *display;

  if (surface == NULL)
    return NULL;
  display = gtk_widget_get_display (GTK_WIDGET (window));
  return gdk_display_get_monitor_at_surface (display, surface);
}

static PpDisplayCandidate *
collect_monitor_candidates (Pinpoint *pinpoint,
                            guint    *count)
{
  PpDisplayCandidate *candidates;

  *count = g_list_model_get_n_items (pinpoint->monitors);
  candidates = g_new0 (PpDisplayCandidate, *count);
  for (guint i = 0; i < *count; i++)
    {
      GdkMonitor *monitor = g_list_model_get_item (pinpoint->monitors, i);

      candidates[i].display = monitor;
      candidates[i].builtin = monitor_is_builtin (monitor);
    }
  return candidates;
}

static void
free_monitor_candidates (PpDisplayCandidate *candidates,
                         guint               count)
{
  for (guint i = 0; i < count; i++)
    g_object_unref ((gpointer) candidates[i].display);
  g_free (candidates);
}

static GdkMonitor *
get_presenter_monitor (Pinpoint *pinpoint)
{
  guint count;
  PpDisplayCandidate *candidates = collect_monitor_candidates (pinpoint,
                                                                &count);
  gconstpointer selected = pp_display_selection_presenter (
    candidates,
    count,
    pinpoint->audience_monitor,
    pinpoint->presenter_monitor,
    monitor_at_window (pinpoint->window));

  g_set_object (&pinpoint->presenter_monitor, (gpointer) selected);
  free_monitor_candidates (candidates, count);
  return pinpoint->presenter_monitor;
}

static GdkMonitor *
get_audience_monitor (Pinpoint  *pinpoint,
                      GdkMonitor *presenter_monitor)
{
  guint count;
  PpDisplayCandidate *candidates = collect_monitor_candidates (pinpoint,
                                                                &count);
  gconstpointer selected = pp_display_selection_audience (
    candidates,
    count,
    presenter_monitor,
    pinpoint->audience_monitor);
  GdkMonitor *result = selected != NULL
    ? g_object_ref ((gpointer) selected)
    : NULL;

  free_monitor_candidates (candidates, count);
  return result;
}

static void
set_presenting (Pinpoint *pinpoint,
                gboolean  presenting)
{
  presenting = !!presenting;
  if (pinpoint->presenting == presenting)
    return;

  pinpoint->presenting = presenting;
  if (pinpoint->control != NULL)
    pp_control_set_presenting (pinpoint->control, presenting);
  if (presenting)
    {
      if (pinpoint->inhibit_cookie == 0)
        pinpoint->inhibit_cookie = gtk_application_inhibit (
          pinpoint->application,
          pinpoint->window,
          GTK_APPLICATION_INHIBIT_IDLE,
          "Presenting slides");
      if (pinpoint->inhibit_cookie == 0)
        g_warning ("Unable to inhibit screen blanking and locking");
    }
  else if (pinpoint->inhibit_cookie != 0)
    {
      gtk_application_uninhibit (pinpoint->application,
                                 pinpoint->inhibit_cookie);
      pinpoint->inhibit_cookie = 0;
    }
}

static void
set_fullscreen (Pinpoint *pinpoint,
                gboolean  fullscreen)
{
  gboolean speaker_visible;
  guint monitor_count;

  fullscreen = !!fullscreen;
  speaker_visible = pp_speaker_is_visible (pinpoint->speaker);
  monitor_count = g_list_model_get_n_items (pinpoint->monitors);
  pp_control_set_swap_displays_available (pinpoint->control,
                                          fullscreen &&
                                          speaker_visible &&
                                          monitor_count > 1);

  pinpoint->fullscreen = fullscreen;
  pp_control_set_fullscreen (pinpoint->control, fullscreen);
  if (fullscreen)
    {
      if (speaker_visible && monitor_count > 1)
        {
          GdkMonitor *presenter_monitor = get_presenter_monitor (pinpoint);
          g_autoptr (GdkMonitor) audience_monitor =
            get_audience_monitor (pinpoint, presenter_monitor);

          if (audience_monitor != NULL && presenter_monitor != NULL)
            {
              gtk_window_fullscreen_on_monitor (pinpoint->window,
                                                audience_monitor);
              pp_speaker_set_fullscreen (pinpoint->speaker,
                                         TRUE,
                                         presenter_monitor);
            }
          else
            gtk_window_fullscreen (pinpoint->window);
        }
      else
        {
          gtk_window_fullscreen (pinpoint->window);
          if (speaker_visible)
            pp_speaker_set_fullscreen (pinpoint->speaker, FALSE, NULL);
        }
    }
  else
    {
      gtk_window_unfullscreen (pinpoint->window);
      pp_speaker_set_fullscreen (pinpoint->speaker, FALSE, NULL);
    }
}

static void
set_speaker_visible (Pinpoint *pinpoint,
                     gboolean  visible)
{
  visible = !!visible;
  pp_speaker_set_visible (pinpoint->speaker, visible);
  pinpoint->speaker_mode = visible;
  pp_control_set_speaker (pinpoint->control, visible);
  if (pinpoint->fullscreen)
    set_fullscreen (pinpoint, TRUE);
}

static void
swap_displays (Pinpoint *pinpoint)
{
  GdkMonitor *presenter_monitor;
  g_autoptr (GdkMonitor) audience_monitor = NULL;

  if (!pinpoint->fullscreen ||
      !pp_speaker_is_visible (pinpoint->speaker) ||
      g_list_model_get_n_items (pinpoint->monitors) < 2)
    return;

  presenter_monitor = get_presenter_monitor (pinpoint);
  audience_monitor = get_audience_monitor (pinpoint, presenter_monitor);
  if (presenter_monitor == NULL || audience_monitor == NULL)
    return;

  g_set_object (&pinpoint->audience_monitor, presenter_monitor);
  g_set_object (&pinpoint->presenter_monitor, audience_monitor);
  update_monitor_choices (pinpoint);
  set_fullscreen (pinpoint, TRUE);
}

static void
control_command_cb (PpControl       *control,
                    guint            command,
                    gboolean         requested_state,
                    gpointer         user_data)
{
  Pinpoint *pinpoint = user_data;

  (void) control;
  switch ((PpControlCommand) command)
    {
    case PP_CONTROL_COMMAND_NEXT:
      pp_stage_next (pinpoint->stage);
      break;
    case PP_CONTROL_COMMAND_PREVIOUS:
      pp_stage_previous (pinpoint->stage);
      break;
    case PP_CONTROL_COMMAND_FIRST:
      pp_stage_first (pinpoint->stage);
      break;
    case PP_CONTROL_COMMAND_SET_BLANK:
      pp_stage_set_blank (pinpoint->stage, requested_state);
      pp_control_set_blank (pinpoint->control,
                            pp_stage_get_blank (pinpoint->stage));
      break;
    case PP_CONTROL_COMMAND_SET_FULLSCREEN:
      set_fullscreen (pinpoint, requested_state);
      break;
    case PP_CONTROL_COMMAND_SET_SPEAKER:
      set_speaker_visible (pinpoint, requested_state);
      break;
    case PP_CONTROL_COMMAND_SWAP_DISPLAYS:
      swap_displays (pinpoint);
      break;
    default:
      g_assert_not_reached ();
    }
}

static void
monitors_changed_cb (GListModel *model,
                     guint       position,
                     guint       removed,
                     guint       added,
                     gpointer    user_data)
{
  Pinpoint *pinpoint = user_data;
  guint count;
  PpDisplayCandidate *candidates;
  gconstpointer presenter;
  gconstpointer audience;

  (void) model;
  (void) position;
  (void) removed;
  (void) added;
  candidates = collect_monitor_candidates (pinpoint, &count);
  presenter = pinpoint->presenter_monitor;
  audience = pinpoint->audience_monitor;
  pp_display_selection_validate (candidates,
                                 count,
                                 &presenter,
                                 &audience);
  if (presenter == NULL)
    g_clear_object (&pinpoint->presenter_monitor);
  if (audience == NULL)
    g_clear_object (&pinpoint->audience_monitor);
  free_monitor_candidates (candidates, count);
  update_monitor_choices (pinpoint);
  if (pinpoint->fullscreen)
    set_fullscreen (pinpoint, TRUE);
}

static gboolean
key_pressed_cb (GtkEventControllerKey *controller,
                guint                  keyval,
                guint                  keycode,
                GdkModifierType        state,
                gpointer               user_data)
{
  Pinpoint *pinpoint = user_data;

  (void) controller;
  (void) keycode;
  (void) state;

  if (pp_control_handle_key (pinpoint->control,
                             keyval,
                             PP_CONTROL_KEY_AUDIENCE))
    return GDK_EVENT_STOP;

  switch (keyval)
    {
    case GDK_KEY_Escape:
    case GDK_KEY_q:
    case GDK_KEY_Q:
      g_application_quit (G_APPLICATION (pinpoint->application));
      return GDK_EVENT_STOP;

    case GDK_KEY_Return:
    case GDK_KEY_KP_Enter:
      run_current_command (pinpoint);
      return GDK_EVENT_STOP;

    case GDK_KEY_Tab:
      gtk_widget_set_visible (GTK_WIDGET (pinpoint->command_entry), TRUE);
      gtk_widget_set_can_target (GTK_WIDGET (pinpoint->command_entry), TRUE);
      gtk_widget_set_opacity (GTK_WIDGET (pinpoint->command_entry), 1.0);
      gtk_widget_grab_focus (GTK_WIDGET (pinpoint->command_entry));
      gtk_editable_select_region (GTK_EDITABLE (pinpoint->command_entry), 0, -1);
      return GDK_EVENT_STOP;

    default:
      return GDK_EVENT_PROPAGATE;
    }
}

static void
mouse_pressed_cb (GtkGestureClick *gesture,
                  int              n_press,
                  double           x,
                  double           y,
                  gpointer         user_data)
{
  Pinpoint *pinpoint = user_data;
  guint button = gtk_gesture_single_get_current_button (GTK_GESTURE_SINGLE (gesture));

  (void) n_press;
  (void) x;
  (void) y;

  if (button == GDK_BUTTON_PRIMARY)
    pp_control_activate (pinpoint->control, PP_CONTROL_ACTION_NEXT);
  else if (button == GDK_BUTTON_SECONDARY)
    pp_control_activate (pinpoint->control, PP_CONTROL_ACTION_PREVIOUS);
}

static gboolean
hide_cursor_cb (gpointer user_data)
{
  Pinpoint *pinpoint = user_data;

  pinpoint->hide_cursor_id = 0;
  gtk_widget_set_cursor_from_name (GTK_WIDGET (pinpoint->stage), "none");
  return G_SOURCE_REMOVE;
}

static void
motion_cb (GtkEventControllerMotion *controller,
           double                    x,
           double                    y,
           gpointer                  user_data)
{
  Pinpoint *pinpoint = user_data;

  (void) controller;
  (void) x;
  (void) y;

  if (pinpoint->hide_cursor_id != 0)
    g_source_remove (pinpoint->hide_cursor_id);
  gtk_widget_set_cursor_from_name (GTK_WIDGET (pinpoint->stage), "default");
  pinpoint->hide_cursor_id = g_timeout_add (500, hide_cursor_cb, pinpoint);
}

static void
motion_leave_cb (GtkEventControllerMotion *controller,
                 gpointer                  user_data)
{
  Pinpoint *pinpoint = user_data;

  (void) controller;
  if (pinpoint->hide_cursor_id != 0)
    {
      g_source_remove (pinpoint->hide_cursor_id);
      pinpoint->hide_cursor_id = 0;
    }
  gtk_widget_set_cursor_from_name (GTK_WIDGET (pinpoint->stage), "default");
}

static void
activate_cb (GtkApplication *application,
             gpointer        user_data)
{
  Pinpoint *pinpoint = user_data;
  GtkEventController *keys;
  GtkEventController *motion;
  GtkEventController *command_keys;
  GtkGesture *click;

  if (pinpoint->window != NULL)
    {
      gtk_window_present (pinpoint->window);
      return;
    }

  pinpoint->window = GTK_WINDOW (adw_application_window_new (application));
  g_signal_connect (pinpoint->window,
                    "close-request",
                    G_CALLBACK (main_window_close_request_cb),
                    pinpoint);
  gtk_window_set_default_size (pinpoint->window, 800, 600);
  pinpoint->stage = PP_STAGE (pp_stage_new ());
  pp_stage_set_camera_device (pinpoint->stage, pinpoint->camera_device);
  pinpoint->overlay = GTK_OVERLAY (gtk_overlay_new ());
  gtk_overlay_set_child (pinpoint->overlay, GTK_WIDGET (pinpoint->stage));

  pinpoint->command_entry = GTK_ENTRY (gtk_entry_new ());
  gtk_widget_set_halign (GTK_WIDGET (pinpoint->command_entry), GTK_ALIGN_FILL);
  gtk_widget_set_valign (GTK_WIDGET (pinpoint->command_entry), GTK_ALIGN_END);
  gtk_widget_set_margin_start (GTK_WIDGET (pinpoint->command_entry), 40);
  gtk_widget_set_margin_end (GTK_WIDGET (pinpoint->command_entry), 40);
  gtk_widget_set_margin_top (GTK_WIDGET (pinpoint->command_entry), 30);
  gtk_widget_set_margin_bottom (GTK_WIDGET (pinpoint->command_entry), 30);
  gtk_widget_set_opacity (GTK_WIDGET (pinpoint->command_entry), 0.33);
  gtk_widget_set_visible (GTK_WIDGET (pinpoint->command_entry), FALSE);
  gtk_widget_set_can_target (GTK_WIDGET (pinpoint->command_entry), FALSE);
  gtk_entry_set_has_frame (pinpoint->command_entry, TRUE);
  gtk_overlay_add_overlay (pinpoint->overlay, GTK_WIDGET (pinpoint->command_entry));
  g_signal_connect (pinpoint->command_entry,
                    "activate",
                    G_CALLBACK (command_activate_cb),
                    pinpoint);
  command_keys = gtk_event_controller_key_new ();
  g_signal_connect (command_keys,
                    "key-pressed",
                    G_CALLBACK (command_key_pressed_cb),
                    pinpoint);
  gtk_widget_add_controller (GTK_WIDGET (pinpoint->command_entry), command_keys);
  g_signal_connect (pinpoint->stage,
                    "slide-changed",
                    G_CALLBACK (slide_changed_cb),
                    pinpoint);

  pinpoint->control = pp_control_new (G_ACTION_MAP (application),
                                      G_ACTION_GROUP (application));
  g_signal_connect (pinpoint->control,
                    "command",
                    G_CALLBACK (control_command_cb),
                    pinpoint);

  keys = gtk_event_controller_key_new ();
  g_signal_connect (keys, "key-pressed", G_CALLBACK (key_pressed_cb), pinpoint);
  gtk_widget_add_controller (GTK_WIDGET (pinpoint->stage), keys);

  click = gtk_gesture_click_new ();
  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (click), 0);
  g_signal_connect (click, "pressed", G_CALLBACK (mouse_pressed_cb), pinpoint);
  gtk_widget_add_controller (GTK_WIDGET (pinpoint->stage), GTK_EVENT_CONTROLLER (click));

  motion = gtk_event_controller_motion_new ();
  g_signal_connect (motion, "motion", G_CALLBACK (motion_cb), pinpoint);
  g_signal_connect (motion, "leave", G_CALLBACK (motion_leave_cb), pinpoint);
  gtk_widget_add_controller (GTK_WIDGET (pinpoint->stage), motion);

  pinpoint->speaker = pp_speaker_new (application,
                                      pinpoint->stage,
                                      pinpoint->control);
  pinpoint->monitors = g_object_ref (
    gdk_display_get_monitors (gtk_widget_get_display (
      GTK_WIDGET (pinpoint->window))));
  pinpoint->monitors_changed_id =
    g_signal_connect (pinpoint->monitors,
                      "items-changed",
                      G_CALLBACK (monitors_changed_cb),
                      pinpoint);
  pinpoint->view_stack = GTK_STACK (gtk_stack_new ());
  gtk_stack_set_transition_type (pinpoint->view_stack,
                                 GTK_STACK_TRANSITION_TYPE_CROSSFADE);
  gtk_stack_add_named (pinpoint->view_stack,
                       create_setup_view (pinpoint),
                       "setup");
  gtk_stack_add_named (pinpoint->view_stack,
                       GTK_WIDGET (pinpoint->overlay),
                       "presentation");
  adw_application_window_set_content (ADW_APPLICATION_WINDOW (pinpoint->window),
                                      GTK_WIDGET (pinpoint->view_stack));
  update_monitor_choices (pinpoint);

  if (pinpoint->maximized)
    {
      gtk_window_set_decorated (pinpoint->window, FALSE);
      gtk_window_maximize (pinpoint->window);
    }

  gtk_window_present (pinpoint->window);
  if (pinpoint->file != NULL)
    {
      if (!start_loaded_presentation (pinpoint))
        {
          g_clear_object (&pinpoint->file);
          set_window_title (pinpoint);
          gtk_stack_set_visible_child_name (pinpoint->view_stack, "setup");
        }
    }
  else
    gtk_stack_set_visible_child_name (pinpoint->view_stack, "setup");
}

static void
pinpoint_clear (Pinpoint *pinpoint)
{
  set_presenting (pinpoint, FALSE);
  if (pinpoint->reload_id != 0)
    g_source_remove (pinpoint->reload_id);
  if (pinpoint->hide_cursor_id != 0)
    g_source_remove (pinpoint->hide_cursor_id);
  if (pinpoint->sigint_id != 0)
    {
      g_source_remove (pinpoint->sigint_id);
      pinpoint->sigint_id = 0;
    }
  if (pinpoint->sigterm_id != 0)
    {
      g_source_remove (pinpoint->sigterm_id);
      pinpoint->sigterm_id = 0;
    }
  g_clear_object (&pinpoint->monitor);
  g_clear_pointer (&pinpoint->asset_monitors, pp_asset_monitors_free);
  g_clear_object (&pinpoint->file);
  g_clear_object (&pinpoint->presentation_folder);
  if (pinpoint->monitors != NULL && pinpoint->monitors_changed_id != 0)
    g_signal_handler_disconnect (pinpoint->monitors,
                                 pinpoint->monitors_changed_id);
  g_clear_object (&pinpoint->presenter_monitor);
  g_clear_object (&pinpoint->audience_monitor);
  g_clear_object (&pinpoint->monitors);
  g_clear_pointer (&pinpoint->setup_monitor_choices, g_ptr_array_unref);
  g_clear_pointer (&pinpoint->speaker, pp_speaker_free);
  g_clear_object (&pinpoint->control);
  if (pinpoint->window != NULL)
    {
      gtk_window_destroy (pinpoint->window);
      pinpoint->window = NULL;
    }
  g_clear_object (&pinpoint->application);
  g_free (pinpoint->camera_device);
}

int
main (int   argc,
      char *argv[])
{
  Pinpoint pinpoint = { .pdf_options = PP_PDF_OPTIONS_DEFAULT };
  g_autoptr (GOptionContext) option_context = NULL;
  g_autoptr (GError) error = NULL;
  g_auto (GStrv) files = NULL;
  g_autofree char *output_filename = NULL;
  g_autofree char *pdf_page_size = NULL;
  g_autofree char *pdf_orientation = NULL;
  gboolean pdf_no_speaker_notes = FALSE;
  GOptionEntry options[] = {
    { "maximized", 'm', 0, G_OPTION_ARG_NONE, &pinpoint.maximized,
      "Maximize without window decoration", NULL },
    { "fullscreen", 'f', 0, G_OPTION_ARG_NONE, &pinpoint.fullscreen,
      "Start in fullscreen mode", NULL },
    { "speakermode", 's', 0, G_OPTION_ARG_NONE, &pinpoint.speaker_mode,
      "Show the speaker window", NULL },
    { "rehearse", 'r', 0, G_OPTION_ARG_NONE, &pinpoint.rehearse,
      "Rehearse timings", NULL },
    { "ignore-comments", 'i', 0, G_OPTION_ARG_NONE, &pinpoint.ignore_comments,
      "Do not show comments as speaker notes", NULL },
    { "output", 'o', 0, G_OPTION_ARG_FILENAME, &output_filename,
      "Output presentation to FILE (PDF)", "FILE" },
    { "pdf-page-size", 0, 0, G_OPTION_ARG_STRING, &pdf_page_size,
      "PDF paper size: a4 or letter", "SIZE" },
    { "pdf-orientation", 0, 0, G_OPTION_ARG_STRING, &pdf_orientation,
      "PDF orientation: landscape or portrait", "ORIENTATION" },
    { "pdf-no-speaker-notes", 0, 0, G_OPTION_ARG_NONE, &pdf_no_speaker_notes,
      "Do not add speaker-note pages to PDF output", NULL },
    { "camera", 'c', 0, G_OPTION_ARG_STRING, &pinpoint.camera_device,
      "Device to use for camera backgrounds", "DEVICE" },
    { G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_FILENAME_ARRAY, &files,
      NULL, "PRESENTATION" },
    { NULL }
  };
  int status;

  g_set_prgname ("pinpoint");
  g_set_application_name ("Pinpoint");
  gst_init (&argc, &argv);

  option_context = g_option_context_new ("- " PINPOINT_TAGLINE);
  g_option_context_add_main_entries (option_context, options, NULL);
  if (!g_option_context_parse (option_context, &argc, &argv, &error))
    {
      g_printerr ("pinpoint: %s\n", error->message);
      return EXIT_FAILURE;
    }

  if (pdf_page_size == NULL || g_ascii_strcasecmp (pdf_page_size, "a4") == 0)
    pinpoint.pdf_options.page_size = PP_PDF_PAGE_SIZE_A4;
  else if (g_ascii_strcasecmp (pdf_page_size, "letter") == 0)
    pinpoint.pdf_options.page_size = PP_PDF_PAGE_SIZE_LETTER;
  else
    {
      g_printerr ("pinpoint: unknown PDF paper size “%s”\n", pdf_page_size);
      pinpoint_clear (&pinpoint);
      return EXIT_FAILURE;
    }

  if (pdf_orientation == NULL ||
      g_ascii_strcasecmp (pdf_orientation, "landscape") == 0)
    pinpoint.pdf_options.orientation = PP_PDF_ORIENTATION_LANDSCAPE;
  else if (g_ascii_strcasecmp (pdf_orientation, "portrait") == 0)
    pinpoint.pdf_options.orientation = PP_PDF_ORIENTATION_PORTRAIT;
  else
    {
      g_printerr ("pinpoint: unknown PDF orientation “%s”\n", pdf_orientation);
      pinpoint_clear (&pinpoint);
      return EXIT_FAILURE;
    }
  pinpoint.pdf_options.include_speaker_notes = !pdf_no_speaker_notes;

  if (files != NULL && files[0] != NULL)
    pinpoint.file = g_file_new_for_commandline_arg (files[0]);

  if (output_filename != NULL)
    {
      g_autoptr (PpPresentation) presentation = NULL;
      g_autoptr (GFile) output = NULL;

      if (pinpoint.file == NULL)
        {
          g_printerr ("pinpoint: PDF export requires a presentation\n");
          pinpoint_clear (&pinpoint);
          return EXIT_FAILURE;
        }

      presentation = pp_presentation_load_for_pdf (pinpoint.file,
                                                   pinpoint.ignore_comments,
                                                   NULL,
                                                   &error);
      if (presentation == NULL)
        {
          g_printerr ("pinpoint: %s\n", error->message);
          pinpoint_clear (&pinpoint);
          return EXIT_FAILURE;
        }

      output = g_file_new_for_commandline_arg (output_filename);
      if (!pp_pdf_export_with_options (presentation,
                                       output,
                                       &pinpoint.pdf_options,
                                       &error))
        {
          g_printerr ("pinpoint: %s\n", error->message);
          pinpoint_clear (&pinpoint);
          return EXIT_FAILURE;
        }
      pinpoint_clear (&pinpoint);
      return EXIT_SUCCESS;
    }

  pinpoint.application = GTK_APPLICATION (
    adw_application_new (PINPOINT_APPLICATION_ID, G_APPLICATION_NON_UNIQUE));
  g_signal_connect (pinpoint.application,
                    "activate",
                    G_CALLBACK (activate_cb),
                    &pinpoint);
  g_signal_connect (pinpoint.application,
                    "shutdown",
                    G_CALLBACK (application_shutdown_cb),
                    &pinpoint);
  pinpoint.sigint_id = g_unix_signal_add (SIGINT,
                                          termination_signal_cb,
                                          &pinpoint);
  pinpoint.sigterm_id = g_unix_signal_add (SIGTERM,
                                           termination_signal_cb,
                                           &pinpoint);
  status = g_application_run (G_APPLICATION (pinpoint.application), argc, argv);
  pinpoint_clear (&pinpoint);

  return status;
}
