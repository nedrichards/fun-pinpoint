#include <adwaita.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

GResource *pinpoint_get_resource (void);

static void
closed_cb (AdwDialog *dialog,
           guint     *closed_count)
{
  (void) dialog;
  (*closed_count)++;
}

static void
test_shortcuts_dialog_reopens (void)
{
  const char *resource_path =
    "/com/nedrichards/pinpoint/gtk/shortcuts-window.ui";
  g_autoptr (GBytes) contents = NULL;
  g_autoptr (GError) error = NULL;
  gsize length = 0;
  const char *xml;
  GtkWindow *parent = GTK_WINDOW (gtk_window_new ());

  contents = g_resource_lookup_data (pinpoint_get_resource (),
                                     resource_path,
                                     G_RESOURCE_LOOKUP_FLAGS_NONE,
                                     &error);
  g_assert_no_error (error);
  g_assert_nonnull (contents);
  xml = g_bytes_get_data (contents, &length);

  g_object_set (gtk_settings_get_default (),
                "gtk-enable-animations", FALSE,
                NULL);
  gtk_window_present (parent);
  while (g_main_context_iteration (NULL, FALSE));
  for (guint attempt = 0; attempt < 2; attempt++)
    {
      g_autoptr (GtkBuilder) builder = gtk_builder_new_from_string (xml,
                                                                    length);
      AdwDialog *dialog = ADW_DIALOG (
        gtk_builder_get_object (builder, "shortcuts_dialog"));
      guint closed_count = 0;

      g_assert_true (ADW_IS_SHORTCUTS_DIALOG (dialog));
      g_signal_connect (dialog, "closed", G_CALLBACK (closed_cb), &closed_count);
      adw_dialog_present (dialog, GTK_WIDGET (parent));
      g_assert_nonnull (gtk_widget_get_root (GTK_WIDGET (dialog)));
      for (guint spin = 0;
           !gtk_widget_get_mapped (GTK_WIDGET (dialog)) && spin < 1000;
           spin++)
        {
          while (g_main_context_iteration (NULL, FALSE));
          g_usleep (1000);
        }
      g_assert_true (gtk_widget_get_mapped (GTK_WIDGET (dialog)));

      g_assert_true (adw_dialog_close (dialog));
      for (guint spin = 0; closed_count == 0 && spin < 1000; spin++)
        {
          while (g_main_context_iteration (NULL, FALSE));
          g_usleep (1000);
        }
      g_assert_cmpuint (closed_count, ==, 1);
      g_assert_null (gtk_widget_get_root (GTK_WIDGET (dialog)));
    }

  gtk_window_destroy (parent);
}

int
main (int   argc,
      char *argv[])
{
  g_test_init (&argc, &argv, NULL);
  if (!gtk_init_check ())
    return 77;
  adw_init ();

  g_test_add_func ("/shortcuts/dialog-reopens", test_shortcuts_dialog_reopens);
  return g_test_run ();
}
