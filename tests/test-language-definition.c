#include <string.h>

#include <gtksourceview/gtksource.h>

static GtkTextIter
iter_at_needle (GtkTextBuffer *buffer,
                const char    *text,
                const char    *needle)
{
  const char *position = strstr (text, needle);
  GtkTextIter iter;

  g_assert_nonnull (position);
  gtk_text_buffer_get_iter_at_offset (buffer,
                                      &iter,
                                      g_utf8_pointer_to_offset (text, position));
  return iter;
}

static void
assert_class_at (GtkSourceBuffer *buffer,
                 const char      *text,
                 const char      *needle,
                 const char      *context_class)
{
  GtkTextIter iter = iter_at_needle (GTK_TEXT_BUFFER (buffer), text, needle);
  g_auto (GStrv) classes = NULL;
  g_autofree char *class_list = NULL;

  if (gtk_source_buffer_iter_has_context_class (buffer, &iter, context_class))
    return;

  classes = gtk_source_buffer_get_context_classes_at_iter (buffer, &iter);
  class_list = classes != NULL && classes[0] != NULL
    ? g_strjoinv (", ", classes)
    : g_strdup ("none");
  g_error ("Expected context class '%s' at '%s'; found: %s",
           context_class,
           needle,
           class_list);
}

static void
assert_no_class_at (GtkSourceBuffer *buffer,
                    const char      *text,
                    const char      *needle,
                    const char      *context_class)
{
  GtkTextIter iter = iter_at_needle (GTK_TEXT_BUFFER (buffer), text, needle);

  g_assert_false (gtk_source_buffer_iter_has_context_class (buffer,
                                                            &iter,
                                                            context_class));
}

int
main (int   argc,
      char *argv[])
{
  g_autoptr (GtkSourceLanguageManager) manager = NULL;
  g_autoptr (GtkSourceBuffer) buffer = NULL;
  g_autofree char *language_directory = NULL;
  g_autofree char *text = NULL;
  g_autoptr (GError) error = NULL;
  const char *const *default_paths;
  g_auto (GStrv) search_paths = NULL;
  GtkSourceLanguage *language;
  GtkSourceLanguage *guessed;
  GtkTextIter start;
  GtkTextIter end;
  guint n_paths;

  g_assert_cmpint (argc, ==, 3);
  gtk_source_init ();

  manager = gtk_source_language_manager_new ();
  default_paths = gtk_source_language_manager_get_search_path (manager);
  n_paths = 0;
  while (default_paths[n_paths] != NULL)
    n_paths++;
  search_paths = g_new0 (char *, n_paths + 2);
  language_directory = g_path_get_dirname (argv[1]);
  search_paths[0] = g_strdup (language_directory);
  for (guint i = 0; i < n_paths; i++)
    search_paths[i + 1] = g_strdup (default_paths[i]);
  gtk_source_language_manager_set_search_path (manager,
                                                (const char *const *) search_paths);

  language = gtk_source_language_manager_get_language (manager, "pinpoint");
  g_assert_nonnull (language);
  g_assert_cmpstr (gtk_source_language_get_name (language), ==, "Pinpoint");

  guessed = gtk_source_language_manager_guess_language (manager,
                                                         "example.pin",
                                                         "application/x-pinpoint");
  g_assert_true (guessed == language);

  g_assert_true (g_file_get_contents (argv[2], &text, NULL, &error));
  g_assert_no_error (error);

  buffer = gtk_source_buffer_new (NULL);
  gtk_source_buffer_set_language (buffer, language);
  gtk_source_buffer_set_highlight_syntax (buffer, TRUE);
  gtk_text_buffer_set_text (GTK_TEXT_BUFFER (buffer), text, -1);
  gtk_text_buffer_get_bounds (GTK_TEXT_BUFFER (buffer), &start, &end);
  gtk_source_buffer_ensure_highlight (buffer, &start, &end);

  assert_class_at (buffer, text, "[stage-color", "pinpoint-defaults");
  assert_class_at (buffer, text, "Audience", "pinpoint-audience-text");
  assert_class_at (buffer, text, "<b>bold", "pinpoint-pango-markup");
  assert_class_at (buffer, text, "foreground=", "pinpoint-pango-attribute");
  assert_class_at (buffer, text, "\"yellow\"", "pinpoint-pango-value");
  assert_class_at (buffer, text, "&amp; clear", "pinpoint-pango-markup");
  assert_class_at (buffer, text, "#Speaker note", "pinpoint-speaker-note");
  assert_no_class_at (buffer,
                      text,
                      "<b>tags</b>",
                      "pinpoint-pango-markup");
  assert_class_at (buffer, text, "-- [photo.jpg]", "pinpoint-separator");
  assert_class_at (buffer, text, "[fill]", "pinpoint-setting");
  assert_class_at (buffer, text, "[command=printf 'next", "pinpoint-command");
  assert_class_at (buffer, text, "\\#This", "pinpoint-escaped-character");
  assert_class_at (buffer, text, "\\#This", "pinpoint-audience-text");

  g_clear_object (&buffer);
  g_clear_object (&manager);
  gtk_source_finalize ();
  return 0;
}
