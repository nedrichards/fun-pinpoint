#include "pp-editor-module.h"

#include <gmodule.h>
#include <gtksourceview/gtksource.h>

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

  gtk_list_box_select_row (outline,
                           gtk_list_box_get_row_at_index (outline, 0));
  run_loop_for (100);
  gtk_text_buffer_get_iter_at_mark (buffer,
                                    &insert,
                                    gtk_text_buffer_get_insert (buffer));
  g_assert_cmpint (gtk_text_iter_get_line (&insert), ==, 1);
  g_assert_cmpint (gtk_text_iter_get_line_offset (&insert), ==, 0);
  source_adjustment = gtk_scrollable_get_vadjustment (GTK_SCROLLABLE (view));
  g_assert_cmpfloat (gtk_adjustment_get_value (source_adjustment), ==, 0.0);

  gtk_window_destroy (window);
  run_loop_for (50);
}

int
main (int   argc,
      char *argv[])
{
  GModule *module;
  PpEditorModuleGetAbiFunc get_abi = NULL;
  PpEditorModuleCreateFunc create_editor = NULL;
  g_autoptr (GtkApplication) application = NULL;
  PpEditorHost host;

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
  g_assert_cmpuint (get_abi (), ==, PP_EDITOR_MODULE_ABI);

  application = gtk_application_new ("com.nedrichards.pinpoint.EditorTest",
                                      G_APPLICATION_NON_UNIQUE);
  host = (PpEditorHost) {
    .abi_version = PP_EDITOR_MODULE_ABI,
    .application = application,
  };

  assert_reparse_preserves_cursor (create_editor, &host);
  assert_reparse_preserves_cursor (create_editor, &host);

  return 0;
}
