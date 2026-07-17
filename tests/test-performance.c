#include "pp-page-curl.h"

#include <glib.h>
#include <math.h>

#define CURVED_FRAME_COUNT 600
#define CURVED_FRAME_BUDGET_US 900000

static void
test_page_curl_mesh (void)
{
  PpPageCurlMeshVertex vertices[PP_PAGE_CURL_VERTEX_COUNT];
  guint indices[PP_PAGE_CURL_INDEX_COUNT];
  gboolean found_shading = FALSE;

  pp_page_curl_build_mesh (1280.0f,
                           720.0f,
                           0.55,
                           25.0,
                           vertices,
                           indices);
  for (guint i = 0; i < PP_PAGE_CURL_VERTEX_COUNT; i++)
    {
      g_assert_true (isfinite (vertices[i].x));
      g_assert_true (isfinite (vertices[i].y));
      g_assert_cmpfloat (vertices[i].shade, >, 0.0f);
      g_assert_cmpfloat (vertices[i].shade, <=, 1.0f);
      if (vertices[i].shade < 0.99f)
        found_shading = TRUE;
    }
  for (guint i = 0; i < PP_PAGE_CURL_INDEX_COUNT; i++)
    g_assert_cmpuint (indices[i], <, PP_PAGE_CURL_VERTEX_COUNT);
  g_assert_true (found_shading);

  pp_page_curl_build_mesh (800.0f,
                           600.0f,
                           0.0,
                           0.0,
                           vertices,
                           indices);
  g_assert_cmpfloat_with_epsilon (vertices[0].x, -1.0f, 0.0001f);
  g_assert_cmpfloat_with_epsilon (vertices[0].y, 1.0f, 0.0001f);
  g_assert_cmpfloat_with_epsilon (
    vertices[PP_PAGE_CURL_VERTEX_COUNT - 1].x,
    1.0f,
    0.0001f);
  g_assert_cmpfloat_with_epsilon (
    vertices[PP_PAGE_CURL_VERTEX_COUNT - 1].y,
    -1.0f,
    0.0001f);
  g_assert_cmpuint (indices[0], ==, 0);
  g_assert_cmpuint (indices[1], ==, 1);
  g_assert_cmpuint (indices[2], ==, PP_PAGE_CURL_TILES + 2);
}

static void
test_page_curl_cpu_budget (void)
{
  PpPageCurlMeshVertex vertices[PP_PAGE_CURL_VERTEX_COUNT];
  guint indices[PP_PAGE_CURL_INDEX_COUNT];
  gint64 started;
  gint64 elapsed;
  volatile guint checksum = 0;

  for (guint i = 0; i < 10; i++)
    pp_page_curl_build_mesh (1920.0f,
                             1080.0f,
                             (i + 1.0) / 11.0,
                             25.0,
                             vertices,
                             indices);

  started = g_get_monotonic_time ();
  for (guint frame = 0; frame < CURVED_FRAME_COUNT; frame++)
    {
      double period = ((frame % 59) + 1.0) / 60.0;

      pp_page_curl_build_mesh (1920.0f,
                               1080.0f,
                               period,
                               25.0,
                               vertices,
                               indices);
      checksum += indices[frame % PP_PAGE_CURL_INDEX_COUNT];
    }
  elapsed = g_get_monotonic_time () - started;
  g_test_message ("%u curved meshes: %.2f ms (%.3f ms/frame), checksum %u",
                  CURVED_FRAME_COUNT,
                  elapsed / 1000.0,
                  elapsed / 1000.0 / CURVED_FRAME_COUNT,
                  checksum);
  g_assert_cmpint (elapsed, <, CURVED_FRAME_BUDGET_US);
}

int
main (int   argc,
      char *argv[])
{
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/performance/page-curl-mesh", test_page_curl_mesh);
  g_test_add_func ("/performance/page-curl-cpu-budget",
                   test_page_curl_cpu_budget);
  return g_test_run ();
}
