#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

typedef enum
{
  PP_SOURCE_DIAGNOSTIC_WARNING,
  PP_SOURCE_DIAGNOSTIC_ERROR,
} PpSourceDiagnosticSeverity;

typedef struct
{
  gsize start;
  gsize end;
  PpSourceDiagnosticSeverity severity;
  char *message;
} PpSourceDiagnostic;

typedef struct
{
  gsize start;
  gsize separator_end;
  gsize end;
  char *title;
} PpSourceSlide;

typedef struct _PpSourceAnalysis PpSourceAnalysis;

PpSourceAnalysis *pp_source_analyze (const char *source,
                                     GFile      *file);
void              pp_source_analysis_free (PpSourceAnalysis *self);
guint             pp_source_analysis_get_n_slides (const PpSourceAnalysis *self);
const PpSourceSlide *pp_source_analysis_get_slide (const PpSourceAnalysis *self,
                                                    guint                   index);
guint             pp_source_analysis_find_slide (const PpSourceAnalysis *self,
                                                  gsize                   offset);
guint             pp_source_analysis_get_n_diagnostics (const PpSourceAnalysis *self);
const PpSourceDiagnostic *pp_source_analysis_get_diagnostic (const PpSourceAnalysis *self,
                                                              guint                   index);

const char *const *pp_source_setting_names (void);
const char *const *pp_source_setting_values (const char *name);

char *pp_source_apply_durations (const char   *source,
                                 const double *durations,
                                 guint         n_durations,
                                 GError      **error);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (PpSourceAnalysis, pp_source_analysis_free)

G_END_DECLS
