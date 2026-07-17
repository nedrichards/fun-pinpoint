#include "config.h"

#include "pp-introduction.h"

#define INTRODUCTION_RESOURCE_URI \
  "resource:///com/nedrichards/pinpoint/introduction/"

GResource *pinpoint_get_resource (void);

static const char *const introduction_files[] = {
  "introduction.pin",
  "bg.jpg",
  "bowls.jpg",
  "linus.jpg",
  "bunny.webm",
  "ORIGIN.md",
  NULL,
};

static void
ensure_introduction_resource (void)
{
  /* Referencing the exported symbol also retains the generated constructor
   * when this module is linked from the static core library. */
  (void) pinpoint_get_resource ();
}

GFile *
pp_introduction_get_presentation (void)
{
  ensure_introduction_resource ();
  return g_file_new_for_uri (INTRODUCTION_RESOURCE_URI "introduction.pin");
}

gboolean
pp_introduction_copy_to_folder (GFile         *folder,
                                GFile        **presentation_out,
                                GCancellable  *cancellable,
                                GError       **error)
{
  g_autoptr (GPtrArray) copied = NULL;

  g_return_val_if_fail (G_IS_FILE (folder), FALSE);
  g_return_val_if_fail (presentation_out == NULL || *presentation_out == NULL,
                        FALSE);

  ensure_introduction_resource ();

  for (guint i = 0; introduction_files[i] != NULL; i++)
    {
      g_autoptr (GFile) destination = g_file_get_child (
        folder, introduction_files[i]);

      if (g_file_query_exists (destination, cancellable))
        {
          g_set_error (error,
                       G_IO_ERROR,
                       G_IO_ERROR_EXISTS,
                       "The folder already contains “%s”",
                       introduction_files[i]);
          return FALSE;
        }
    }

  copied = g_ptr_array_new_with_free_func (g_object_unref);
  for (guint i = 0; introduction_files[i] != NULL; i++)
    {
      g_autofree char *source_uri = g_strconcat (
        INTRODUCTION_RESOURCE_URI, introduction_files[i], NULL);
      g_autoptr (GFile) source = g_file_new_for_uri (source_uri);
      g_autoptr (GFile) destination = g_file_get_child (
        folder, introduction_files[i]);

      if (!g_file_copy (source,
                        destination,
                        G_FILE_COPY_NONE,
                        cancellable,
                        NULL,
                        NULL,
                        error))
        {
          for (guint j = 0; j < copied->len; j++)
            g_file_delete (g_ptr_array_index (copied, j), NULL, NULL);
          return FALSE;
        }
      g_ptr_array_add (copied, g_object_ref (destination));
    }

  if (presentation_out != NULL)
    *presentation_out = g_file_get_child (folder, "introduction.pin");
  return TRUE;
}
