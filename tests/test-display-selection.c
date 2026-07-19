#include "pp-display-selection.h"

static void
test_two_display_defaults_and_swap (void)
{
  static const char internal;
  static const char external;
  const PpDisplayCandidate candidates[] = {
    { &internal, TRUE },
    { &external, FALSE },
  };
  gconstpointer presenter;
  gconstpointer audience;

  presenter = pp_display_selection_presenter (candidates,
                                               G_N_ELEMENTS (candidates),
                                               NULL,
                                               NULL,
                                               &external);
  audience = pp_display_selection_audience (candidates,
                                             G_N_ELEMENTS (candidates),
                                             presenter,
                                             NULL);
  g_assert_true (presenter == &internal);
  g_assert_true (audience == &external);

  presenter = audience;
  audience = &internal;
  g_assert_true (presenter == &external);
  g_assert_true (audience == &internal);
}

static void
test_three_displays_preserve_explicit_pair (void)
{
  static const char internal;
  static const char projector;
  static const char confidence;
  const PpDisplayCandidate candidates[] = {
    { &internal, TRUE },
    { &projector, FALSE },
    { &confidence, FALSE },
  };

  g_assert_true (
    pp_display_selection_presenter (candidates,
                                    G_N_ELEMENTS (candidates),
                                    &projector,
                                    &confidence,
                                    &internal) == &confidence);
  g_assert_true (
    pp_display_selection_audience (candidates,
                                   G_N_ELEMENTS (candidates),
                                   &confidence,
                                   &projector) == &projector);
}

static void
test_unplug_and_replug_recalculates_pair (void)
{
  static const char internal;
  static const char external;
  const PpDisplayCandidate internal_only[] = {
    { &internal, TRUE },
  };
  const PpDisplayCandidate replugged[] = {
    { &internal, TRUE },
    { &external, FALSE },
  };
  gconstpointer presenter = &internal;
  gconstpointer audience = &external;

  pp_display_selection_validate (internal_only,
                                 G_N_ELEMENTS (internal_only),
                                 &presenter,
                                 &audience);
  g_assert_null (presenter);
  g_assert_null (audience);

  presenter = pp_display_selection_presenter (replugged,
                                               G_N_ELEMENTS (replugged),
                                               audience,
                                               presenter,
                                               &internal);
  audience = pp_display_selection_audience (replugged,
                                             G_N_ELEMENTS (replugged),
                                             presenter,
                                             audience);
  g_assert_true (presenter == &internal);
  g_assert_true (audience == &external);
}

static void
test_removed_member_does_not_poison_remaining_pair (void)
{
  static const char internal;
  static const char projector;
  static const char confidence;
  const PpDisplayCandidate remaining[] = {
    { &internal, TRUE },
    { &confidence, FALSE },
  };
  gconstpointer presenter = &internal;
  gconstpointer audience = &projector;

  pp_display_selection_validate (remaining,
                                 G_N_ELEMENTS (remaining),
                                 &presenter,
                                 &audience);
  g_assert_true (presenter == &internal);
  g_assert_null (audience);
  audience = pp_display_selection_audience (remaining,
                                             G_N_ELEMENTS (remaining),
                                             presenter,
                                             audience);
  g_assert_true (audience == &confidence);
}

static void
test_fallback_and_exhausted_paths (void)
{
  static const char first;
  static const char second;
  static const char missing;
  const PpDisplayCandidate external[] = {
    { &first, FALSE },
    { &second, FALSE },
  };
  const PpDisplayCandidate builtin[] = {
    { &first, TRUE },
    { &second, TRUE },
  };
  gconstpointer presenter = &missing;
  gconstpointer audience = &first;

  g_assert_true (
    pp_display_selection_presenter (external,
                                    G_N_ELEMENTS (external),
                                    &first,
                                    NULL,
                                    &second) == &second);
  g_assert_true (
    pp_display_selection_presenter (external,
                                    G_N_ELEMENTS (external),
                                    &first,
                                    NULL,
                                    NULL) == &second);
  g_assert_null (pp_display_selection_presenter (external,
                                                 1,
                                                 &first,
                                                 NULL,
                                                 &first));
  g_assert_true (
    pp_display_selection_audience (builtin,
                                   G_N_ELEMENTS (builtin),
                                   &first,
                                   NULL) == &second);
  g_assert_null (pp_display_selection_audience (builtin, 1, &first, NULL));

  pp_display_selection_validate (external,
                                 G_N_ELEMENTS (external),
                                 &presenter,
                                 &audience);
  g_assert_null (presenter);
  g_assert_true (audience == &first);
  presenter = &first;
  audience = &first;
  pp_display_selection_validate (external,
                                 G_N_ELEMENTS (external),
                                 &presenter,
                                 &audience);
  g_assert_true (presenter == &first);
  g_assert_null (audience);
}

int
main (int   argc,
      char *argv[])
{
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/display-selection/two-defaults-and-swap",
                   test_two_display_defaults_and_swap);
  g_test_add_func ("/display-selection/three-preserve-explicit-pair",
                   test_three_displays_preserve_explicit_pair);
  g_test_add_func ("/display-selection/unplug-replug",
                   test_unplug_and_replug_recalculates_pair);
  g_test_add_func ("/display-selection/removed-member",
                   test_removed_member_does_not_poison_remaining_pair);
  g_test_add_func ("/display-selection/fallback-and-exhausted",
                   test_fallback_and_exhausted_paths);
  return g_test_run ();
}
