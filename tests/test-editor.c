#include "pp-editor-module.h"

#include <glib/gstdio.h>
#include <gmodule.h>
#include <gtksourceview/gtksource.h>
#include <string.h>

typedef struct
{
  guint close_count;
  gboolean close_quit;
  guint launch_count;
  char *launch_source;
  GFile *launch_file;
  guint launch_slide;
  gboolean launch_rehearsal;
} HostState;

static void
run_loop_for (guint milliseconds)
{
  gint64 deadline = g_get_monotonic_time () + (gint64) milliseconds * 1000;

  while (g_get_monotonic_time () < deadline)
    {
      while (g_main_context_iteration (NULL, FALSE));
      g_usleep (1000);
    }
}

static GtkWidget *
find_label (GtkWidget  *widget,
            const char *text)
{
  if (GTK_IS_LABEL (widget) &&
      g_strcmp0 (gtk_label_get_text (GTK_LABEL (widget)), text) == 0)
    return widget;

  for (GtkWidget *child = gtk_widget_get_first_child (widget);
       child != NULL;
       child = gtk_widget_get_next_sibling (child))
    {
      GtkWidget *label = find_label (child, text);

      if (label != NULL)
        return label;
    }
  return NULL;
}

static GtkWidget *
find_button (GtkWidget  *widget,
             const char *label)
{
  if (GTK_IS_BUTTON (widget) &&
      (g_strcmp0 (gtk_button_get_label (GTK_BUTTON (widget)), label) == 0 ||
       find_label (widget, label) != NULL))
    return widget;

  for (GtkWidget *child = gtk_widget_get_first_child (widget);
       child != NULL;
       child = gtk_widget_get_next_sibling (child))
    {
      GtkWidget *button = find_button (child, label);

      if (button != NULL)
        return button;
    }
  return NULL;
}

static GtkWidget *
wait_for_button (GtkWidget  *root,
                 const char *label)
{
  gint64 deadline = g_get_monotonic_time () + G_TIME_SPAN_SECOND * 2;
  GtkWidget *button;

  do
    {
      while (g_main_context_iteration (NULL, FALSE));
      button = find_button (root, label);
      if (button != NULL)
        return button;
      GListModel *toplevels = gtk_window_get_toplevels ();

      for (guint i = 0; i < g_list_model_get_n_items (toplevels); i++)
        {
          g_autoptr (GtkWindow) window = g_list_model_get_item (toplevels, i);

          if (GTK_WIDGET (window) == root)
            continue;
          button = find_button (GTK_WIDGET (window), label);
          if (button != NULL)
            return button;
        }
      g_usleep (1000);
    }
  while (g_get_monotonic_time () < deadline);
  return NULL;
}

static GtkWidget *
wait_for_dialog_button (const char *title,
                        const char *label)
{
  gint64 deadline = g_get_monotonic_time () + G_TIME_SPAN_SECOND * 2;

  do
    {
      GListModel *toplevels;

      while (g_main_context_iteration (NULL, FALSE));
      toplevels = gtk_window_get_toplevels ();
      for (guint i = 0; i < g_list_model_get_n_items (toplevels); i++)
        {
          g_autoptr (GtkWindow) window = g_list_model_get_item (toplevels, i);

          if (find_label (GTK_WIDGET (window), title) != NULL)
            return find_button (GTK_WIDGET (window), label);
        }
      g_usleep (1000);
    }
  while (g_get_monotonic_time () < deadline);
  return NULL;
}

static char *
get_buffer_text (GtkTextBuffer *buffer)
{
  GtkTextIter start;
  GtkTextIter end;

  gtk_text_buffer_get_bounds (buffer, &start, &end);
  return gtk_text_buffer_get_text (buffer, &start, &end, TRUE);
}

static gboolean
wait_for_buffer_text (GtkTextBuffer *buffer,
                      const char    *expected)
{
  gint64 deadline = g_get_monotonic_time () + G_TIME_SPAN_SECOND * 2;

  do
    {
      g_autofree char *text = NULL;

      while (g_main_context_iteration (NULL, FALSE));
      text = get_buffer_text (buffer);
      if (g_str_equal (text, expected))
        return TRUE;
      g_usleep (1000);
    }
  while (g_get_monotonic_time () < deadline);
  return FALSE;
}

static void
close_editor_cb (gboolean quit,
                 gpointer user_data)
{
  HostState *state = user_data;

  state->close_count++;
  state->close_quit = quit;
}

static void
launch_editor_cb (const char *source,
                  GFile      *file,
                  guint       initial_slide,
                  gboolean    rehearse,
                  gpointer    user_data)
{
  HostState *state = user_data;

  state->launch_count++;
  g_free (state->launch_source);
  state->launch_source = g_strdup (source);
  g_set_object (&state->launch_file, file);
  state->launch_slide = initial_slide;
  state->launch_rehearsal = rehearse;
}

static gboolean
wait_for_close_count (HostState *state,
                      guint      expected)
{
  gint64 deadline = g_get_monotonic_time () + G_TIME_SPAN_SECOND * 2;

  do
    {
      while (g_main_context_iteration (NULL, FALSE));
      if (state->close_count == expected)
        return TRUE;
      g_usleep (1000);
    }
  while (g_get_monotonic_time () < deadline);
  return FALSE;
}

static GtkSourceView *
find_source_view (GtkWidget *widget)
{
  if (GTK_SOURCE_IS_VIEW (widget))
    return GTK_SOURCE_VIEW (widget);

  for (GtkWidget *child = gtk_widget_get_first_child (widget);
       child != NULL;
       child = gtk_widget_get_next_sibling (child))
    {
      GtkSourceView *view = find_source_view (child);

      if (view != NULL)
        return view;
    }
  return NULL;
}

static GtkPopover *
find_popover (GtkWidget *widget)
{
  if (GTK_IS_POPOVER (widget))
    return GTK_POPOVER (widget);

  for (GtkWidget *child = gtk_widget_get_first_child (widget);
       child != NULL;
       child = gtk_widget_get_next_sibling (child))
    {
      GtkPopover *popover = find_popover (child);

      if (popover != NULL)
        return popover;
    }
  return NULL;
}

static GtkEventControllerKey *
find_key_controller (GtkWidget *widget)
{
  g_autoptr (GListModel) controllers = gtk_widget_observe_controllers (widget);

  for (guint i = 0; i < g_list_model_get_n_items (controllers); i++)
    {
      g_autoptr (GtkEventController) controller =
        g_list_model_get_item (controllers, i);

      if (GTK_IS_EVENT_CONTROLLER_KEY (controller))
        return GTK_EVENT_CONTROLLER_KEY (g_steal_pointer (&controller));
    }
  return NULL;
}

static gboolean
emit_key (GtkEventControllerKey *controller,
          guint                  keyval,
          GdkModifierType        state)
{
  gboolean handled = FALSE;

  g_signal_emit_by_name (controller,
                         "key-pressed",
                         keyval,
                         0,
                         state,
                         &handled);
  return handled;
}

static GtkListBox *
find_outline (GtkWidget *widget)
{
  if (GTK_IS_LIST_BOX (widget) &&
      gtk_widget_has_css_class (widget, "navigation-sidebar"))
    return GTK_LIST_BOX (widget);

  for (GtkWidget *child = gtk_widget_get_first_child (widget);
       child != NULL;
       child = gtk_widget_get_next_sibling (child))
    {
      GtkListBox *outline = find_outline (child);

      if (outline != NULL)
        return outline;
    }
  return NULL;
}

static void
assert_reparse_preserves_cursor (PpEditorModuleCreateFunc create_editor,
                                 const PpEditorHost       *host)
{
  GtkWindow *window = GTK_WINDOW (gtk_window_new ());
  GtkWidget *editor = create_editor (host, NULL);
  GtkSourceView *view;
  GtkListBox *outline;
  GtkScrolledWindow *outline_scroll;
  GtkTextBuffer *buffer;
  GtkTextIter insert;
  GtkTextIter match_start;
  GtkTextIter match_end;
  GtkListBoxRow *selected_row;
  GtkAdjustment *outline_adjustment;
  GtkAdjustment *source_adjustment;
  g_autoptr (GString) source = g_string_new (NULL);
  int expected_offset;

  g_assert_nonnull (editor);
  gtk_window_set_child (window, editor);
  gtk_window_present (window);
  run_loop_for (250);

  view = find_source_view (editor);
  g_assert_nonnull (view);
  outline = find_outline (editor);
  g_assert_nonnull (outline);
  outline_scroll = GTK_SCROLLED_WINDOW (gtk_widget_get_ancestor (
    GTK_WIDGET (outline), GTK_TYPE_SCROLLED_WINDOW));
  g_assert_nonnull (outline_scroll);
  buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (view));
  for (guint i = 0; i < 40; i++)
    g_string_append_printf (source,
                            "-- [white]\nSlide %u title%s\n"
                            "A second line makes the source view scroll.\n",
                            i + 1,
                            i == 39 ? " is excellent" : "");
  gtk_text_buffer_set_text (buffer, source->str, -1);
  gtk_text_buffer_get_start_iter (buffer, &insert);
  g_assert_true (gtk_text_iter_forward_search (&insert,
                                               "excellent",
                                               GTK_TEXT_SEARCH_TEXT_ONLY,
                                               &match_start,
                                               &match_end,
                                               NULL));
  insert = match_end;
  gtk_text_buffer_place_cursor (buffer, &insert);
  expected_offset = gtk_text_iter_get_offset (&insert) + 1;
  gtk_text_buffer_insert_at_cursor (buffer, "[", 1);

  run_loop_for (300);
  gtk_text_buffer_get_iter_at_mark (buffer,
                                    &insert,
                                    gtk_text_buffer_get_insert (buffer));
  g_assert_cmpint (gtk_text_iter_get_offset (&insert), ==, expected_offset);
  selected_row = gtk_list_box_get_selected_row (outline);
  g_assert_nonnull (selected_row);
  g_assert_cmpint (gtk_list_box_row_get_index (selected_row), ==, 39);
  g_assert_nonnull (gtk_list_box_get_row_at_index (outline, 0));
  g_assert_nonnull (gtk_list_box_get_row_at_index (outline, 39));
  g_assert_null (gtk_list_box_get_row_at_index (outline, 40));
  outline_adjustment = gtk_scrolled_window_get_vadjustment (outline_scroll);
  g_assert_cmpfloat (gtk_adjustment_get_value (outline_adjustment), >, 0.0);

  g_assert_true (gtk_widget_activate (GTK_WIDGET (
    gtk_list_box_get_row_at_index (outline, 0))));
  run_loop_for (100);
  g_assert_cmpint (gtk_list_box_row_get_index (
                     gtk_list_box_get_selected_row (outline)),
                   ==,
                   0);
  gtk_text_buffer_get_iter_at_mark (buffer,
                                    &insert,
                                    gtk_text_buffer_get_insert (buffer));
  g_assert_cmpint (gtk_text_iter_get_line (&insert), ==, 1);
  g_assert_cmpint (gtk_text_iter_get_line_offset (&insert), ==, 0);
  source_adjustment = gtk_scrollable_get_vadjustment (GTK_SCROLLABLE (view));
  g_assert_cmpfloat (gtk_adjustment_get_value (source_adjustment), ==, 0.0);
  g_assert_true (gtk_root_get_focus (GTK_ROOT (window)) == GTK_WIDGET (view));

  gtk_window_destroy (window);
  run_loop_for (50);
}

static void
assert_file_lifecycle (PpEditorModuleCreateFunc create_editor,
                       const PpEditorHost       *host)
{
  static const char initial[] = "--\nOriginal\n";
  static const char edited[] = "--\nEdited\n";
  static const char external[] = "--\nExternal\n";
  static const char local[] = "--\nLocal\n";
  static const char conflict[] = "--\nConflict\n";
  g_autoptr (GError) error = NULL;
  g_autofree char *directory = g_dir_make_tmp ("pinpoint-editor-XXXXXX", &error);
  g_autofree char *path = NULL;
  g_autofree char *saved = NULL;
  g_autoptr (GFile) file = NULL;
  GtkWindow *window = GTK_WINDOW (gtk_window_new ());
  GtkWidget *editor;
  GtkSourceView *view;
  GtkTextBuffer *buffer;
  GtkWidget *button;

  g_assert_no_error (error);
  g_assert_nonnull (directory);
  path = g_build_filename (directory, "lifecycle.pin", NULL);
  g_assert_true (g_file_set_contents (path, initial, -1, &error));
  g_assert_no_error (error);
  file = g_file_new_for_path (path);
  editor = create_editor (host, file);
  g_assert_nonnull (editor);
  gtk_window_set_child (window, editor);
  gtk_window_present (window);
  run_loop_for (100);
  view = find_source_view (editor);
  g_assert_nonnull (view);
  buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (view));

  gtk_text_buffer_set_text (buffer, edited, -1);
  button = wait_for_button (GTK_WIDGET (window), "Save");
  g_assert_nonnull (button);
  g_assert_true (gtk_widget_activate (button));
  run_loop_for (300);
  g_assert_true (g_file_get_contents (path, &saved, NULL, &error));
  g_assert_no_error (error);
  g_assert_cmpstr (saved, ==, edited);
  g_clear_pointer (&saved, g_free);

  g_assert_true (g_file_set_contents (path, external, -1, &error));
  g_assert_no_error (error);
  g_assert_true (wait_for_buffer_text (buffer, external));
  g_assert_false (gtk_text_buffer_get_modified (buffer));
  run_loop_for (300);

  gtk_text_buffer_set_text (buffer, local, -1);
  g_assert_true (g_file_set_contents (path, conflict, -1, &error));
  g_assert_no_error (error);
  button = wait_for_dialog_button ("Presentation Changed on Disk", "Reload");
  g_assert_nonnull (button);
  g_assert_true (gtk_widget_activate (button));
  g_assert_true (wait_for_buffer_text (buffer, conflict));
  g_assert_false (gtk_text_buffer_get_modified (buffer));

  gtk_window_destroy (window);
  run_loop_for (50);
  g_assert_cmpint (g_remove (path), ==, 0);
  g_assert_cmpint (g_rmdir (directory), ==, 0);
}

static void
assert_close_and_durations (PpEditorModuleCreateFunc         create_editor,
                            PpEditorModuleApplyDurationsFunc apply_durations,
                            PpEditorModuleRequestCloseFunc   request_close,
                            PpEditorHost                    *host,
                            HostState                       *state)
{
  static const char source[] = "-- [duration=1]\nOne\n--\nTwo\n";
  static const char expected[] =
    "-- [duration=5]\nOne\n-- [duration=8.375]\nTwo\n";
  const double durations[] = { 5.0, 8.375 };
  GtkWindow *window = GTK_WINDOW (gtk_window_new ());
  GtkWidget *editor = create_editor (host, NULL);
  GtkSourceView *view;
  GtkTextBuffer *buffer;
  GtkWidget *button;
  g_autofree char *updated = NULL;

  g_assert_nonnull (editor);
  gtk_window_set_child (window, editor);
  gtk_window_present (window);
  run_loop_for (100);
  view = find_source_view (editor);
  g_assert_nonnull (view);
  buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (view));
  gtk_text_buffer_set_text (buffer, source, -1);
  run_loop_for (250);

  apply_durations (editor, durations, G_N_ELEMENTS (durations));
  button = wait_for_dialog_button ("Apply Rehearsal Timings?", "Apply Timings");
  g_assert_nonnull (button);
  g_assert_true (gtk_widget_activate (button));
  g_assert_true (wait_for_buffer_text (buffer, expected));
  updated = get_buffer_text (buffer);
  g_assert_cmpstr (updated, ==, expected);
  g_assert_true (gtk_text_buffer_get_modified (buffer));

  request_close (editor, FALSE);
  button = wait_for_dialog_button ("Save Changes?", "Discard");
  g_assert_nonnull (button);
  g_assert_true (gtk_widget_activate (button));
  g_assert_true (wait_for_close_count (state, 1));
  g_assert_false (state->close_quit);

  gtk_text_buffer_set_modified (buffer, FALSE);
  request_close (editor, TRUE);
  g_assert_cmpuint (state->close_count, ==, 2);
  g_assert_true (state->close_quit);

  gtk_window_destroy (window);
  run_loop_for (50);
}

static void
assert_completion_and_shortcuts (PpEditorModuleCreateFunc create_editor,
                                 PpEditorHost             *host,
                                 HostState                *state)
{
  static const char initial[] = "--\nSlide\n";
  static const char two_slides[] = "--\nFirst\n--\nSecond\n";
  g_autoptr (GError) error = NULL;
  g_autofree char *directory = g_dir_make_tmp ("pinpoint-editor-XXXXXX", &error);
  g_autofree char *path = NULL;
  g_autofree char *asset_path = NULL;
  g_autofree char *other_pin_path = NULL;
  g_autoptr (GFile) file = NULL;
  GtkWindow *window = GTK_WINDOW (gtk_window_new ());
  GtkWidget *editor;
  GtkSourceView *view;
  GtkTextBuffer *buffer;
  GtkTextIter iter;
  GtkPopover *popover;
  GtkEventControllerKey *keys = NULL;
  GtkWidget *button;
  GtkWidget *focus;
  GtkListBox *outline;
  g_autofree char *updated = NULL;

  g_assert_no_error (error);
  path = g_build_filename (directory, "completion.pin", NULL);
  asset_path = g_build_filename (directory, "visual.png", NULL);
  other_pin_path = g_build_filename (directory, "other.pin", NULL);
  g_assert_true (g_file_set_contents (path, initial, -1, &error));
  g_assert_no_error (error);
  g_assert_true (g_file_set_contents (asset_path, "asset", -1, &error));
  g_assert_no_error (error);
  g_assert_true (g_file_set_contents (other_pin_path, initial, -1, &error));
  g_assert_no_error (error);
  file = g_file_new_for_path (path);
  editor = create_editor (host, file);
  g_assert_nonnull (editor);
  gtk_window_set_child (window, editor);
  gtk_window_present (window);
  run_loop_for (250);

  view = find_source_view (editor);
  g_assert_nonnull (view);
  buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (view));
  popover = find_popover (editor);
  g_assert_nonnull (popover);
  keys = find_key_controller (GTK_WIDGET (view));
  g_assert_nonnull (keys);
  outline = find_outline (editor);
  g_assert_nonnull (outline);

  gtk_text_buffer_get_end_iter (buffer, &iter);
  gtk_text_buffer_place_cursor (buffer, &iter);
  g_assert_true (emit_key (keys, GDK_KEY_space, GDK_CONTROL_MASK));
  run_loop_for (50);
  g_assert_true (gtk_widget_get_visible (GTK_WIDGET (popover)));
  g_assert_nonnull (find_button (GTK_WIDGET (popover), "text-align=center"));
  g_assert_nonnull (find_button (GTK_WIDGET (popover), "Asset: visual.png"));
  g_assert_null (find_button (GTK_WIDGET (popover), "Asset: other.pin"));
  button = find_button (GTK_WIDGET (popover), "duration=");
  g_assert_nonnull (button);
  g_signal_emit_by_name (button, "clicked");
  updated = get_buffer_text (buffer);
  g_assert_cmpstr (updated, ==, "--\nSlide\n[duration=]");
  gtk_text_buffer_get_iter_at_mark (buffer,
                                    &iter,
                                    gtk_text_buffer_get_insert (buffer));
  g_assert_cmpint (gtk_text_iter_get_offset (&iter), ==,
                   (int) strlen (updated) - 1);

  gtk_text_buffer_set_text (buffer, two_slides, -1);
  gtk_text_buffer_get_end_iter (buffer, &iter);
  gtk_text_buffer_place_cursor (buffer, &iter);
  run_loop_for (300);
  g_assert_true (emit_key (keys, GDK_KEY_Return, GDK_CONTROL_MASK));
  g_assert_cmpuint (state->launch_count, ==, 1);
  g_assert_cmpstr (state->launch_source, ==, two_slides);
  g_assert_true (g_file_equal (state->launch_file, file));
  g_assert_cmpuint (state->launch_slide, ==, 1);
  g_assert_false (state->launch_rehearsal);
  g_assert_true (emit_key (keys,
                           GDK_KEY_R,
                           GDK_CONTROL_MASK | GDK_SHIFT_MASK));
  g_assert_cmpuint (state->launch_count, ==, 2);
  g_assert_cmpuint (state->launch_slide, ==, 0);
  g_assert_true (state->launch_rehearsal);

  g_assert_true (gtk_widget_grab_focus (GTK_WIDGET (view)));
  g_assert_true (emit_key (keys, GDK_KEY_F6, 0));
  focus = gtk_root_get_focus (GTK_ROOT (window));
  g_assert_true (focus != GTK_WIDGET (view));
  g_assert_true (emit_key (keys, GDK_KEY_F6, 0));
  focus = gtk_root_get_focus (GTK_ROOT (window));
  g_assert_true (focus == GTK_WIDGET (outline) ||
                 gtk_widget_is_ancestor (focus, GTK_WIDGET (outline)));
  g_assert_true (emit_key (keys, GDK_KEY_F6, 0));
  g_assert_true (gtk_root_get_focus (GTK_ROOT (window)) == GTK_WIDGET (view));
  g_assert_false (emit_key (keys, GDK_KEY_x, 0));

  gtk_window_destroy (window);
  run_loop_for (50);
  g_clear_object (&keys);
  g_clear_pointer (&state->launch_source, g_free);
  g_clear_object (&state->launch_file);
  g_assert_cmpint (g_remove (path), ==, 0);
  g_assert_cmpint (g_remove (asset_path), ==, 0);
  g_assert_cmpint (g_remove (other_pin_path), ==, 0);
  g_assert_cmpint (g_rmdir (directory), ==, 0);
}

int
main (int   argc,
      char *argv[])
{
  GModule *module;
  PpEditorModuleGetAbiFunc get_abi = NULL;
  PpEditorModuleCreateFunc create_editor = NULL;
  PpEditorModuleApplyDurationsFunc apply_durations = NULL;
  PpEditorModuleRequestCloseFunc request_close = NULL;
  g_autoptr (GtkApplication) application = NULL;
  PpEditorHost host;
  HostState state = { 0 };

  g_test_init (&argc, &argv, NULL);
  g_assert_cmpint (argc, ==, 2);
  if (!gtk_init_check ())
    return 77;

  module = g_module_open (argv[1], G_MODULE_BIND_LAZY | G_MODULE_BIND_LOCAL);
  g_assert_nonnull (module);
  g_module_make_resident (module);
  g_assert_true (g_module_symbol (module,
                                  "pp_editor_module_get_abi",
                                  (gpointer *) &get_abi));
  g_assert_true (g_module_symbol (module,
                                  "pp_editor_module_create",
                                  (gpointer *) &create_editor));
  g_assert_true (g_module_symbol (module,
                                  "pp_editor_module_apply_durations",
                                  (gpointer *) &apply_durations));
  g_assert_true (g_module_symbol (module,
                                  "pp_editor_module_request_close",
                                  (gpointer *) &request_close));
  g_assert_cmpuint (get_abi (), ==, PP_EDITOR_MODULE_ABI);

  application = gtk_application_new ("com.nedrichards.pinpoint.EditorTest",
                                      G_APPLICATION_NON_UNIQUE);
  host = (PpEditorHost) {
    .abi_version = PP_EDITOR_MODULE_ABI,
    .application = application,
    .launch = launch_editor_cb,
    .close = close_editor_cb,
    .user_data = &state,
  };

  assert_reparse_preserves_cursor (create_editor, &host);
  assert_reparse_preserves_cursor (create_editor, &host);
  assert_file_lifecycle (create_editor, &host);
  assert_close_and_durations (create_editor,
                              apply_durations,
                              request_close,
                              &host,
                              &state);
  assert_completion_and_shortcuts (create_editor, &host, &state);

  return 0;
}
