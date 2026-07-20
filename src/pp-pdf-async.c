#include "config.h"

#include "pp-pdf.h"

typedef struct
{
  GFile *presentation_file;
  GFile *output;
  gboolean ignore_comments;
  PpPdfOptions options;
  PpPdfProgressCallback progress_callback;
  gpointer progress_data;
} PdfExportJob;

static void
pdf_export_job_free (PdfExportJob *job)
{
  g_clear_object (&job->presentation_file);
  g_clear_object (&job->output);
  g_free (job);
}

static void
pdf_export_thread (GTask        *task,
                   gpointer      source_object,
                   gpointer      task_data,
                   GCancellable *cancellable)
{
  PdfExportJob *job = task_data;
  g_autoptr (PpPresentation) presentation = NULL;
  g_autoptr (GError) error = NULL;

  (void) source_object;
  presentation = pp_presentation_load_for_pdf (job->presentation_file,
                                                job->ignore_comments,
                                                cancellable,
                                                &error);
  if (presentation == NULL ||
      !pp_pdf_export_with_options_full (presentation,
                                        job->output,
                                        &job->options,
                                        cancellable,
                                        job->progress_callback,
                                        job->progress_data,
                                        &error))
    g_task_return_error (task, g_steal_pointer (&error));
  else
    g_task_return_boolean (task, TRUE);
}

void
pp_pdf_export_file_async (GFile               *presentation_file,
                          gboolean             ignore_comments,
                          GFile               *output,
                          const PpPdfOptions  *options,
                          GCancellable        *cancellable,
                          GAsyncReadyCallback  callback,
                          gpointer             user_data)
{
  pp_pdf_export_file_async_full (presentation_file,
                                 ignore_comments,
                                 output,
                                 options,
                                 cancellable,
                                 NULL,
                                 NULL,
                                 callback,
                                 user_data);
}

void
pp_pdf_export_file_async_full (GFile                *presentation_file,
                               gboolean              ignore_comments,
                               GFile                *output,
                               const PpPdfOptions   *options,
                               GCancellable         *cancellable,
                               PpPdfProgressCallback progress_callback,
                               gpointer              progress_data,
                               GAsyncReadyCallback   callback,
                               gpointer              user_data)
{
  g_autoptr (GTask) task = NULL;
  PdfExportJob *job;

  g_return_if_fail (G_IS_FILE (presentation_file));
  g_return_if_fail (G_IS_FILE (output));
  g_return_if_fail (options != NULL);
  g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

  job = g_new0 (PdfExportJob, 1);
  job->presentation_file = g_object_ref (presentation_file);
  job->output = g_object_ref (output);
  job->ignore_comments = ignore_comments;
  job->options = *options;
  job->progress_callback = progress_callback;
  job->progress_data = progress_data;
  task = g_task_new (presentation_file, cancellable, callback, user_data);
  /* The worker owns cancellation through the final atomic replace. Avoid a
   * late GTask cancellation overriding a successfully committed export. */
  g_task_set_check_cancellable (task, FALSE);
  g_task_set_task_data (task,
                        job,
                        (GDestroyNotify) pdf_export_job_free);
  g_task_run_in_thread (task, pdf_export_thread);
}

gboolean
pp_pdf_export_file_finish (GAsyncResult *result,
                           GError      **error)
{
  g_return_val_if_fail (G_IS_TASK (result), FALSE);
  return g_task_propagate_boolean (G_TASK (result), error);
}

GFile *
pp_pdf_export_file_get_output (GAsyncResult *result)
{
  PdfExportJob *job;

  g_return_val_if_fail (G_IS_TASK (result), NULL);
  job = g_task_get_task_data (G_TASK (result));
  return job->output;
}
