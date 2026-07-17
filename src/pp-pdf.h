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
gboolean pp_pdf_export (const PpPresentation *presentation,
                        GFile                *output,
                        GError              **error);

G_END_DECLS
