#include <glib.h>
#include <glib/gstdio.h>

#define EXECUTABLE_SIZE_BUDGET (3 * 1024 * 1024)
#define BUNDLED_VIDEO_SIZE_BUDGET (1400 * 1024)

static const char *executable_path;
static const char *video_path;

static void
test_size_budgets (void)
{
  GStatBuf executable_stat;
  GStatBuf video_stat;

  g_assert_cmpint (g_stat (executable_path, &executable_stat), ==, 0);
  g_assert_cmpint (g_stat (video_path, &video_stat), ==, 0);
  g_test_message ("SDK executable: %.2f MiB; bundled video: %.2f MiB",
                  executable_stat.st_size / 1024.0 / 1024.0,
                  video_stat.st_size / 1024.0 / 1024.0);
  g_assert_cmpint (executable_stat.st_size, <=, EXECUTABLE_SIZE_BUDGET);
  g_assert_cmpint (video_stat.st_size, <=, BUNDLED_VIDEO_SIZE_BUDGET);
}

int
main (int   argc,
      char *argv[])
{
  g_test_init (&argc, &argv, NULL);
  g_assert_cmpint (argc, ==, 3);
  executable_path = argv[1];
  video_path = argv[2];
  g_test_add_func ("/size/budgets", test_size_budgets);
  return g_test_run ();
}
