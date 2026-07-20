#pragma once

#include "pp-presentation.h"

G_BEGIN_DECLS

typedef enum
{
  PP_PDF_PAGE_SIZE_A4,
  PP_PDF_PAGE_SIZE_LETTER,
} PpPdfPageSize;

typedef enum
{
  PP_PDF_ORIENTATION_LANDSCAPE,
  PP_PDF_ORIENTATION_PORTRAIT,
} PpPdfOrientation;

typedef struct
{
  PpPdfPageSize page_size;
  PpPdfOrientation orientation;
  gboolean include_speaker_notes;
} PpPdfOptions;

typedef void (*PpPdfProgressCallback) (guint    completed_slides,
                                       guint    total_slides,
                                       gpointer user_data);

#define PP_PDF_OPTIONS_DEFAULT                    \
  ((PpPdfOptions) {                               \
    .page_size = PP_PDF_PAGE_SIZE_A4,             \
    .orientation = PP_PDF_ORIENTATION_LANDSCAPE,  \
    .include_speaker_notes = TRUE,                \
  })

void     pp_pdf_options_get_page_dimensions (const PpPdfOptions *options,
                                             double             *width,
                                             double             *height);
gboolean pp_pdf_export_with_options          (const PpPresentation *presentation,
                                              GFile                *output,
                                              const PpPdfOptions   *options,
                                              GError              **error);
gboolean pp_pdf_export_with_options_full     (const PpPresentation *presentation,
                                              GFile                *output,
                                              const PpPdfOptions   *options,
                                              GCancellable         *cancellable,
                                              PpPdfProgressCallback progress_callback,
                                              gpointer              progress_data,
                                              GError              **error);
gboolean pp_pdf_export (const PpPresentation *presentation,
                        GFile                *output,
                        GError              **error);
void     pp_pdf_export_file_async (GFile                *presentation_file,
                                   gboolean              ignore_comments,
                                   GFile                *output,
                                   const PpPdfOptions   *options,
                                   GCancellable         *cancellable,
                                   GAsyncReadyCallback   callback,
                                   gpointer              user_data);
void     pp_pdf_export_file_async_full (GFile                *presentation_file,
                                        gboolean              ignore_comments,
                                        GFile                *output,
                                        const PpPdfOptions   *options,
                                        GCancellable         *cancellable,
                                        PpPdfProgressCallback progress_callback,
                                        gpointer              progress_data,
                                        GAsyncReadyCallback   callback,
                                        gpointer              user_data);
gboolean pp_pdf_export_file_finish (GAsyncResult         *result,
                                    GError              **error);
GFile   *pp_pdf_export_file_get_output (GAsyncResult      *result);

G_END_DECLS
