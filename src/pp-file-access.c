#include "config.h"

#include "pp-file-access.h"

static gint
compare_files (gconstpointer a,
               gconstpointer b)
{
  GFile *file_a = *(GFile *const *) a;
  GFile *file_b = *(GFile *const *) b;
  g_autofree char *name_a = g_file_get_basename (file_a);
  g_autofree char *name_b = g_file_get_basename (file_b);

  return g_utf8_collate (name_a, name_b);
}

GPtrArray *
pp_file_access_find_presentations (GFile         *folder,
                                   GCancellable  *cancellable,
                                   GError       **error)
{
  g_autoptr (GFileEnumerator) enumerator = NULL;
  GPtrArray *files;

  g_return_val_if_fail (G_IS_FILE (folder), NULL);

  enumerator = g_file_enumerate_children (
    folder,
    G_FILE_ATTRIBUTE_STANDARD_NAME "," G_FILE_ATTRIBUTE_STANDARD_TYPE,
    G_FILE_QUERY_INFO_NONE,
    cancellable,
    error);
  if (enumerator == NULL)
    return NULL;

  files = g_ptr_array_new_with_free_func (g_object_unref);
  while (TRUE)
    {
      g_autoptr (GFileInfo) info = NULL;
      const char *name;

      info = g_file_enumerator_next_file (enumerator, cancellable, error);
      if (info == NULL)
        {
          if (error != NULL && *error != NULL)
            {
              g_ptr_array_unref (files);
              return NULL;
            }
          break;
        }

      name = g_file_info_get_name (info);
      if (name != NULL &&
          g_file_info_get_file_type (info) == G_FILE_TYPE_REGULAR &&
          g_str_has_suffix (name, ".pin"))
        g_ptr_array_add (files, g_file_get_child (folder, name));
    }

  g_ptr_array_sort (files, compare_files);
  return files;
}
