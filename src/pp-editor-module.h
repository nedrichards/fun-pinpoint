#pragma once

#include <gio/gio.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define PP_EDITOR_MODULE_ABI 1

typedef void (*PpEditorLaunchFunc) (const char *source,
                                    GFile      *file,
                                    guint       initial_slide,
                                    gboolean    rehearse,
                                    gpointer    user_data);
typedef void (*PpEditorCloseFunc)  (gboolean quit,
                                    gpointer user_data);

typedef struct
{
  guint abi_version;
  GtkApplication *application;
  PpEditorLaunchFunc launch;
  PpEditorCloseFunc close;
  gpointer user_data;
} PpEditorHost;

typedef guint      (*PpEditorModuleGetAbiFunc) (void);
typedef GtkWidget *(*PpEditorModuleCreateFunc) (const PpEditorHost *host,
                                                GFile              *file);
typedef void       (*PpEditorModuleApplyDurationsFunc) (GtkWidget    *editor,
                                                       const double *durations,
                                                       guint         n_durations);
typedef void       (*PpEditorModuleRequestCloseFunc) (GtkWidget *editor,
                                                     gboolean   quit);

G_END_DECLS
