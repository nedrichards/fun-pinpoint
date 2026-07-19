#include <gio/gio.h>
#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
  const char *label;
  const char *path;
  const char *container_caps;
  const char *video_caps;
  const char *audio_caps;
  const char *audio_profile;
  const char *profile;
  const char *chroma_format;
  const char *colorimetry;
  guint bit_depth;
} MediaExpectation;

static void
assert_runtime_factory (const char *name)
{
  g_autoptr (GstElementFactory) factory = gst_element_factory_find (name);
  g_autoptr (GstPlugin) plugin = NULL;
  const char *filename;

  g_assert_nonnull (factory);
  plugin = gst_plugin_feature_get_plugin (GST_PLUGIN_FEATURE (factory));
  g_assert_nonnull (plugin);
  filename = gst_plugin_get_filename (plugin);
  g_assert_nonnull (filename);
  g_assert_null (strstr (filename, "/extensions/"));
  g_assert_null (strstr (filename, "/codecs-extra/"));
}

static void
assert_caps_name (GstDiscovererStreamInfo *stream,
                  const char              *expected)
{
  g_autoptr (GstCaps) caps = gst_discoverer_stream_info_get_caps (stream);
  const GstStructure *structure;

  g_assert_nonnull (caps);
  g_assert_false (gst_caps_is_empty (caps));
  structure = gst_caps_get_structure (caps, 0);
  g_assert_cmpstr (gst_structure_get_name (structure), ==, expected);
}

static void
assert_video_details (GstDiscovererStreamInfo *stream,
                      const MediaExpectation *expected)
{
  g_autoptr (GstCaps) caps = gst_discoverer_stream_info_get_caps (stream);
  const GstStructure *structure;
  const char *actual;
  guint depth;

  g_assert_nonnull (caps);
  structure = gst_caps_get_structure (caps, 0);
  g_assert_cmpstr (gst_structure_get_name (structure), ==,
                   expected->video_caps);
  if (expected->profile != NULL)
    {
      actual = gst_structure_get_string (structure, "profile");
      g_assert_cmpstr (actual, ==, expected->profile);
    }
  if (expected->chroma_format != NULL)
    {
      actual = gst_structure_get_string (structure, "chroma-format");
      g_assert_cmpstr (actual, ==, expected->chroma_format);
    }
  if (expected->colorimetry != NULL)
    {
      actual = gst_structure_get_string (structure, "colorimetry");
      g_assert_cmpstr (actual, ==, expected->colorimetry);
    }
  if (expected->bit_depth > 0)
    {
      g_assert_true (gst_structure_get_uint (structure,
                                             "bit-depth-luma",
                                             &depth));
      g_assert_cmpuint (depth, ==, expected->bit_depth);
      g_assert_true (gst_structure_get_uint (structure,
                                             "bit-depth-chroma",
                                             &depth));
      g_assert_cmpuint (depth, ==, expected->bit_depth);
    }
}

static void
assert_decodes (const MediaExpectation *expected)
{
  g_autoptr (GError) error = NULL;
  g_autofree char *uri = gst_filename_to_uri (expected->path, &error);
  g_autoptr (GstElement) player = gst_element_factory_make ("playbin3", NULL);
  g_autoptr (GstElement) video_sink = gst_element_factory_make ("fakesink", NULL);
  g_autoptr (GstElement) audio_sink = gst_element_factory_make ("fakesink", NULL);
  g_autoptr (GstPad) pad = NULL;
  g_autoptr (GstCaps) caps = NULL;
  const GstStructure *structure;
  GstState state = GST_STATE_NULL;

  g_assert_no_error (error);
  g_assert_nonnull (uri);
  g_assert_nonnull (player);
  g_assert_nonnull (video_sink);
  g_assert_nonnull (audio_sink);
  gst_object_ref_sink (player);
  gst_object_ref_sink (video_sink);
  gst_object_ref_sink (audio_sink);
  g_object_set (video_sink, "sync", FALSE, NULL);
  g_object_set (audio_sink, "sync", FALSE, NULL);
  g_object_set (player,
                "uri", uri,
                "video-sink", video_sink,
                "audio-sink", audio_sink,
                NULL);

  g_assert_cmpint (gst_element_set_state (player, GST_STATE_PAUSED), !=,
                   GST_STATE_CHANGE_FAILURE);
  g_assert_cmpint (gst_element_get_state (player,
                                          &state,
                                          NULL,
                                          5 * GST_SECOND), ==,
                   GST_STATE_CHANGE_SUCCESS);
  g_assert_cmpint (state, ==, GST_STATE_PAUSED);

  pad = gst_element_get_static_pad (video_sink, "sink");
  g_assert_nonnull (pad);
  caps = gst_pad_get_current_caps (pad);
  g_assert_nonnull (caps);
  structure = gst_caps_get_structure (caps, 0);
  g_assert_cmpstr (gst_structure_get_name (structure), ==, "video/x-raw");

  if (expected->audio_caps != NULL)
    {
      g_clear_object (&pad);
      g_clear_pointer (&caps, gst_caps_unref);
      pad = gst_element_get_static_pad (audio_sink, "sink");
      g_assert_nonnull (pad);
      caps = gst_pad_get_current_caps (pad);
      g_assert_nonnull (caps);
      structure = gst_caps_get_structure (caps, 0);
      g_assert_cmpstr (gst_structure_get_name (structure), ==, "audio/x-raw");
    }

  gst_element_set_state (player, GST_STATE_NULL);
  gst_element_get_state (player, NULL, NULL, 2 * GST_SECOND);
}

static void
assert_supported_media (const MediaExpectation *expected)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (GstDiscoverer) discoverer = gst_discoverer_new (5 * GST_SECOND,
                                                             &error);
  g_autofree char *uri = gst_filename_to_uri (expected->path, &error);
  g_autoptr (GstDiscovererInfo) info = NULL;
  g_autoptr (GstDiscovererStreamInfo) root = NULL;
  GList *videos;
  GList *audios;

  g_assert_no_error (error);
  g_assert_nonnull (discoverer);
  g_assert_nonnull (uri);
  info = gst_discoverer_discover_uri (discoverer, uri, &error);
  g_assert_no_error (error);
  g_assert_nonnull (info);
  g_assert_cmpint (gst_discoverer_info_get_result (info), ==,
                   GST_DISCOVERER_OK);

  root = gst_discoverer_info_get_stream_info (info);
  g_assert_nonnull (root);
  if (expected->container_caps != NULL)
    {
      g_assert_true (GST_IS_DISCOVERER_CONTAINER_INFO (root));
      assert_caps_name (root, expected->container_caps);
    }

  videos = gst_discoverer_info_get_video_streams (info);
  g_assert_cmpuint (g_list_length (videos), ==, 1);
  assert_video_details (videos->data, expected);
  gst_discoverer_stream_info_list_free (videos);

  audios = gst_discoverer_info_get_audio_streams (info);
  if (expected->audio_caps != NULL)
    {
      g_assert_cmpuint (g_list_length (audios), ==, 1);
      assert_caps_name (audios->data, expected->audio_caps);
      if (expected->audio_profile != NULL)
        {
          g_autoptr (GstCaps) caps =
            gst_discoverer_stream_info_get_caps (audios->data);
          const GstStructure *structure = gst_caps_get_structure (caps, 0);

          g_assert_cmpstr (gst_structure_get_string (structure, "profile"),
                           ==,
                           expected->audio_profile);
        }
    }
  else
    g_assert_null (audios);
  gst_discoverer_stream_info_list_free (audios);

  g_test_message ("%s: supported runtime decode", expected->label);
  assert_decodes (expected);
}

static void
assert_rejected_media (const char *path)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (GstDiscoverer) discoverer = gst_discoverer_new (5 * GST_SECOND,
                                                             &error);
  g_autofree char *uri = gst_filename_to_uri (path, &error);
  g_autoptr (GstDiscovererInfo) info = NULL;

  g_assert_no_error (error);
  g_assert_nonnull (discoverer);
  g_assert_nonnull (uri);
  info = gst_discoverer_discover_uri (discoverer, uri, &error);
  g_assert_true (error != NULL ||
                 (info != NULL &&
                  gst_discoverer_info_get_result (info) != GST_DISCOVERER_OK));
}

static void
assert_av1_software_decodes (const char *path,
                             const char *demuxer)
{
  g_autofree char *escaped_path = g_strescape (path, NULL);
  g_autofree char *description = g_strdup_printf (
    "filesrc location=\"%s\" ! %s ! av1parse ! dav1ddec ! "
    "fakesink sync=false",
    escaped_path,
    demuxer);
  g_autoptr (GError) error = NULL;
  g_autoptr (GstElement) pipeline = gst_parse_launch (description, &error);
  GstState state = GST_STATE_NULL;

  g_assert_no_error (error);
  g_assert_nonnull (pipeline);
  gst_object_ref_sink (pipeline);
  g_assert_cmpint (gst_element_set_state (pipeline, GST_STATE_PAUSED), !=,
                   GST_STATE_CHANGE_FAILURE);
  g_assert_cmpint (gst_element_get_state (pipeline,
                                          &state,
                                          NULL,
                                          5 * GST_SECOND), ==,
                   GST_STATE_CHANGE_SUCCESS);
  g_assert_cmpint (state, ==, GST_STATE_PAUSED);
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_element_get_state (pipeline, NULL, NULL, 2 * GST_SECOND);
}

int
main (int   argc,
      char *argv[])
{
  static const char *const required_factories[] = {
    "playbin3", "gtk4paintablesink", "matroskademux", "vp8dec", "vp9dec",
    "av1parse", "dav1ddec", "opusdec", "vorbisdec", "qtdemux", "h264parse",
    "avdec_h264", "avdec_aac", "oggdemux", "theoradec", "gifdec",
    "videoconvertscale", NULL
  };
  MediaExpectation media[9];

  if (argc != 11)
    return EXIT_FAILURE;
  media[0] = (MediaExpectation) {
    "WebM VP9/Opus", argv[1], "video/webm", "video/x-vp9",
    "audio/x-opus", NULL, "0", "4:2:0", NULL, 8
  };
  media[1] = (MediaExpectation) {
    "WebM VP8/Vorbis", argv[2], "video/webm", "video/x-vp8",
    "audio/x-vorbis", NULL, NULL, NULL, "bt601", 0
  };
  media[2] = (MediaExpectation) {
    "MP4 H.264 Constrained Baseline/AAC-LC", argv[3],
    "video/quicktime", "video/x-h264", "audio/mpeg", "lc",
    "constrained-baseline", "4:2:0", "bt601", 8
  };
  media[3] = (MediaExpectation) {
    "QuickTime H.264 Main", argv[4], "video/quicktime", "video/x-h264",
    NULL, NULL, "main", "4:2:0", "bt601", 8
  };
  media[4] = (MediaExpectation) {
    "MP4 H.264 High", argv[5], "video/quicktime", "video/x-h264",
    NULL, NULL, "high", "4:2:0", "bt709", 8
  };
  media[5] = (MediaExpectation) {
    "Ogg Theora/Vorbis", argv[6], "video/ogg", "video/x-theora",
    "audio/x-vorbis", NULL, NULL, NULL, NULL, 0
  };
  media[6] = (MediaExpectation) {
    "Animated GIF", argv[7], NULL, "image/gif", NULL,
    NULL, NULL, NULL, NULL, 0
  };
  media[7] = (MediaExpectation) {
    "WebM AV1 Main/Opus", argv[8], "video/webm", "video/x-av1",
    "audio/x-opus", NULL, "main", "4:2:0", "bt601", 8
  };
  media[8] = (MediaExpectation) {
    "MP4 AV1 Main/AAC-LC", argv[9], "video/quicktime", "video/x-av1",
    "audio/mpeg", "lc", "main", "4:2:0", "bt601", 8
  };
  gst_init (NULL, NULL);
  g_test_init (&argc, &argv, NULL);

  for (guint i = 0; required_factories[i] != NULL; i++)
    assert_runtime_factory (required_factories[i]);
  for (guint i = 0; i < G_N_ELEMENTS (media); i++)
    assert_supported_media (&media[i]);
  assert_av1_software_decodes (argv[8], "matroskademux");
  assert_av1_software_decodes (argv[9], "qtdemux");
  assert_rejected_media (argv[10]);
  return EXIT_SUCCESS;
}
