#include "config.h"

#include "pp-page-curl-view.h"

#include "pp-page-curl.h"
#include "pp-render.h"

#include <epoxy/gl.h>
#include <math.h>

typedef PpPageCurlMeshVertex CurlGlVertex;

struct _PpPageCurlView
{
  GtkGLArea parent_instance;

  GdkTexture *slides[2];
  GdkTexture *uploaded_slides[2];
  GLuint slide_textures[2];
  GLuint program;
  GLuint vertex_array;
  GLuint vertex_buffer;
  GLuint index_buffer;
  GLuint flat_vertex_array;
  GLuint flat_vertex_buffer;
  GLuint flat_index_buffer;
  GLint texture_location;
  gboolean gl_ready;

  double periods[2];
  double angles[2];
  gboolean backwards;
};

G_DEFINE_FINAL_TYPE (PpPageCurlView, pp_page_curl_view, GTK_TYPE_GL_AREA)

static GLuint
compile_shader (GLenum      type,
                const char *source)
{
  GLuint shader = glCreateShader (type);
  GLint status;

  glShaderSource (shader, 1, &source, NULL);
  glCompileShader (shader);
  glGetShaderiv (shader, GL_COMPILE_STATUS, &status);
  if (status == GL_FALSE)
    {
      char log[1024];

      glGetShaderInfoLog (shader, sizeof log, NULL, log);
      g_warning ("Unable to compile page-curl shader: %s", log);
      glDeleteShader (shader);
      return 0;
    }
  return shader;
}

static gboolean
create_program (PpPageCurlView *self)
{
  static const char vertex_source[] =
    "#version 330 core\n"
    "layout(location = 0) in vec2 position;\n"
    "layout(location = 1) in vec2 texture_coordinate;\n"
    "layout(location = 2) in float lighting;\n"
    "out vec2 texture_position;\n"
    "out float shade;\n"
    "void main() {\n"
    "  gl_Position = vec4(position, 0.0, 1.0);\n"
    "  texture_position = texture_coordinate;\n"
    "  shade = lighting;\n"
    "}\n";
  static const char fragment_source[] =
    "#version 330 core\n"
    "uniform sampler2D page;\n"
    "in vec2 texture_position;\n"
    "in float shade;\n"
    "out vec4 color;\n"
    "void main() {\n"
    "  color = texture(page, texture_position) * vec4(shade, shade, shade, 1.0);\n"
    "}\n";
  GLuint vertex_shader = compile_shader (GL_VERTEX_SHADER, vertex_source);
  GLuint fragment_shader;
  GLint status;

  if (vertex_shader == 0)
    return FALSE;
  fragment_shader = compile_shader (GL_FRAGMENT_SHADER, fragment_source);
  if (fragment_shader == 0)
    {
      glDeleteShader (vertex_shader);
      return FALSE;
    }

  self->program = glCreateProgram ();
  glAttachShader (self->program, vertex_shader);
  glAttachShader (self->program, fragment_shader);
  glLinkProgram (self->program);
  glDeleteShader (vertex_shader);
  glDeleteShader (fragment_shader);
  glGetProgramiv (self->program, GL_LINK_STATUS, &status);
  if (status == GL_FALSE)
    {
      char log[1024];

      glGetProgramInfoLog (self->program, sizeof log, NULL, log);
      g_warning ("Unable to link page-curl shader: %s", log);
      glDeleteProgram (self->program);
      self->program = 0;
      return FALSE;
    }

  self->texture_location = glGetUniformLocation (self->program, "page");
  return TRUE;
}

static void
configure_mesh (GLuint                       vertex_array,
                GLuint                       vertex_buffer,
                GLuint                       index_buffer,
                const PpPageCurlMeshVertex *vertices,
                const guint                 *indices,
                GLenum                       usage)
{
  glBindVertexArray (vertex_array);
  glBindBuffer (GL_ARRAY_BUFFER, vertex_buffer);
  glBufferData (GL_ARRAY_BUFFER,
                sizeof (PpPageCurlMeshVertex) * PP_PAGE_CURL_VERTEX_COUNT,
                vertices,
                usage);
  glVertexAttribPointer (0,
                         2,
                         GL_FLOAT,
                         GL_FALSE,
                         sizeof (PpPageCurlMeshVertex),
                         (void *) G_STRUCT_OFFSET (PpPageCurlMeshVertex, x));
  glVertexAttribPointer (1,
                         2,
                         GL_FLOAT,
                         GL_FALSE,
                         sizeof (PpPageCurlMeshVertex),
                         (void *) G_STRUCT_OFFSET (PpPageCurlMeshVertex, u));
  glVertexAttribPointer (2,
                         1,
                         GL_FLOAT,
                         GL_FALSE,
                         sizeof (PpPageCurlMeshVertex),
                         (void *) G_STRUCT_OFFSET (PpPageCurlMeshVertex, shade));
  glEnableVertexAttribArray (0);
  glEnableVertexAttribArray (1);
  glEnableVertexAttribArray (2);
  glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, index_buffer);
  glBufferData (GL_ELEMENT_ARRAY_BUFFER,
                sizeof (guint) * PP_PAGE_CURL_INDEX_COUNT,
                indices,
                usage);
}

static gboolean
ensure_gl (PpPageCurlView *self)
{
  PpPageCurlMeshVertex flat_vertices[PP_PAGE_CURL_VERTEX_COUNT];
  guint flat_indices[PP_PAGE_CURL_INDEX_COUNT];

  if (self->gl_ready)
    return TRUE;
  if (!create_program (self))
    return FALSE;

  glGenVertexArrays (1, &self->vertex_array);
  glGenBuffers (1, &self->vertex_buffer);
  glGenBuffers (1, &self->index_buffer);
  glGenVertexArrays (1, &self->flat_vertex_array);
  glGenBuffers (1, &self->flat_vertex_buffer);
  glGenBuffers (1, &self->flat_index_buffer);
  glGenTextures (G_N_ELEMENTS (self->slide_textures), self->slide_textures);

  configure_mesh (self->vertex_array,
                  self->vertex_buffer,
                  self->index_buffer,
                  NULL,
                  NULL,
                  GL_STREAM_DRAW);
  pp_page_curl_build_mesh (1.0f,
                           1.0f,
                           0.0,
                           0.0,
                           flat_vertices,
                           flat_indices);
  configure_mesh (self->flat_vertex_array,
                  self->flat_vertex_buffer,
                  self->flat_index_buffer,
                  flat_vertices,
                  flat_indices,
                  GL_STATIC_DRAW);
  glBindVertexArray (0);
  self->gl_ready = TRUE;
  return TRUE;
}

static gboolean
upload_texture (PpPageCurlView *self,
                guint           index)
{
  GdkTextureDownloader *downloader;
  g_autoptr (GBytes) bytes = NULL;
  const guint8 *pixels;
  gsize stride;
  gsize byte_length;
  int width;
  int height;

  if (self->slides[index] == self->uploaded_slides[index])
    return TRUE;

  width = gdk_texture_get_width (self->slides[index]);
  height = gdk_texture_get_height (self->slides[index]);
  downloader = gdk_texture_downloader_new (self->slides[index]);
  gdk_texture_downloader_set_format (downloader,
                                     GDK_MEMORY_R8G8B8A8_PREMULTIPLIED);
  bytes = gdk_texture_downloader_download_bytes (downloader, &stride);
  gdk_texture_downloader_free (downloader);
  pixels = g_bytes_get_data (bytes, &byte_length);
  if (stride > G_MAXINT || stride % 4 != 0 ||
      !pp_render_validate_pixel_layout (width,
                                        height,
                                        (int) stride,
                                        4,
                                        byte_length,
                                        NULL,
                                        NULL,
                                        NULL))
    {
      g_warning ("Unable to upload a page-curl texture with an invalid pixel layout");
      return FALSE;
    }

  glBindTexture (GL_TEXTURE_2D, self->slide_textures[index]);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
  glPixelStorei (GL_UNPACK_ROW_LENGTH, (GLint) (stride / 4));
  glTexImage2D (GL_TEXTURE_2D,
                0,
                GL_RGBA8,
                width,
                height,
                0,
                GL_RGBA,
                GL_UNSIGNED_BYTE,
                pixels);
  glPixelStorei (GL_UNPACK_ROW_LENGTH, 0);
  g_set_object (&self->uploaded_slides[index], self->slides[index]);
  return TRUE;
}

static void
draw_page (PpPageCurlView *self,
           guint           slide,
           double          period,
           double          angle,
           float           width,
           float           height)
{
  if (period == 0.0)
    {
      glBindVertexArray (self->flat_vertex_array);
    }
  else
    {
      CurlGlVertex vertices[PP_PAGE_CURL_VERTEX_COUNT];
      guint indices[PP_PAGE_CURL_INDEX_COUNT];

      pp_page_curl_build_mesh (width,
                               height,
                               period,
                               angle,
                               vertices,
                               indices);
      glBindVertexArray (self->vertex_array);
      glBindBuffer (GL_ARRAY_BUFFER, self->vertex_buffer);
      glBufferSubData (GL_ARRAY_BUFFER, 0, sizeof vertices, vertices);
      glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, self->index_buffer);
      glBufferSubData (GL_ELEMENT_ARRAY_BUFFER, 0, sizeof indices, indices);
    }
  glActiveTexture (GL_TEXTURE0);
  glBindTexture (GL_TEXTURE_2D, self->slide_textures[slide]);
  glUniform1i (self->texture_location, 0);
  glDrawElements (GL_TRIANGLES,
                  PP_PAGE_CURL_INDEX_COUNT,
                  GL_UNSIGNED_INT,
                  NULL);
}

static gboolean
render_cb (GtkGLArea    *area,
           GdkGLContext *context,
           gpointer      user_data)
{
  PpPageCurlView *self = PP_PAGE_CURL_VIEW (area);
  int width = gtk_widget_get_width (GTK_WIDGET (area));
  int height = gtk_widget_get_height (GTK_WIDGET (area));
  int scale = gtk_widget_get_scale_factor (GTK_WIDGET (area));
  gint64 viewport_width;
  gint64 viewport_height;

  (void) context;
  (void) user_data;
  viewport_width = (gint64) width * scale;
  viewport_height = (gint64) height * scale;
  if (width <= 0 || height <= 0 || scale <= 0 ||
      viewport_width > G_MAXINT || viewport_height > G_MAXINT ||
      self->slides[0] == NULL || self->slides[1] == NULL)
    return TRUE;

  if (!ensure_gl (self))
    g_error ("Unable to initialise the required page-curl OpenGL renderer");

  if (!upload_texture (self, 0) || !upload_texture (self, 1))
    g_error ("Unable to upload textures to the required page-curl OpenGL renderer");
  glViewport (0, 0, (GLsizei) viewport_width, (GLsizei) viewport_height);
  glDisable (GL_DEPTH_TEST);
  glDisable (GL_CULL_FACE);
  glDisable (GL_BLEND);
  glClearColor (0.0f, 0.0f, 0.0f, 1.0f);
  glClear (GL_COLOR_BUFFER_BIT);
  glUseProgram (self->program);

  if (self->backwards)
    {
      draw_page (self, 0, self->periods[0], self->angles[0], width, height);
      draw_page (self, 1, self->periods[1], self->angles[1], width, height);
    }
  else
    {
      draw_page (self, 1, self->periods[1], self->angles[1], width, height);
      draw_page (self, 0, self->periods[0], self->angles[0], width, height);
    }

  glBindVertexArray (0);
  glUseProgram (0);
  return TRUE;
}

static void
unrealize_cb (GtkWidget *widget,
              gpointer   user_data)
{
  PpPageCurlView *self = PP_PAGE_CURL_VIEW (widget);

  (void) user_data;
  gtk_gl_area_make_current (GTK_GL_AREA (self));
  if (gtk_gl_area_get_error (GTK_GL_AREA (self)) == NULL && self->gl_ready)
    {
      glDeleteTextures (G_N_ELEMENTS (self->slide_textures), self->slide_textures);
      glDeleteBuffers (1, &self->flat_index_buffer);
      glDeleteBuffers (1, &self->flat_vertex_buffer);
      glDeleteVertexArrays (1, &self->flat_vertex_array);
      glDeleteBuffers (1, &self->index_buffer);
      glDeleteBuffers (1, &self->vertex_buffer);
      glDeleteVertexArrays (1, &self->vertex_array);
      glDeleteProgram (self->program);
    }
  self->gl_ready = FALSE;
  self->program = 0;
  g_clear_object (&self->uploaded_slides[0]);
  g_clear_object (&self->uploaded_slides[1]);
}

static void
realize_cb (GtkWidget *widget,
            gpointer   user_data)
{
  GtkGLArea *area = GTK_GL_AREA (widget);
  const GError *error;

  (void) user_data;
  gtk_gl_area_make_current (area);
  error = gtk_gl_area_get_error (area);
  if (error != NULL)
    g_error ("OpenGL 3.3 is required for page-curl rendering: %s",
             error->message);
}

static void
pp_page_curl_view_dispose (GObject *object)
{
  PpPageCurlView *self = PP_PAGE_CURL_VIEW (object);

  g_clear_object (&self->slides[0]);
  g_clear_object (&self->slides[1]);
  g_clear_object (&self->uploaded_slides[0]);
  g_clear_object (&self->uploaded_slides[1]);
  G_OBJECT_CLASS (pp_page_curl_view_parent_class)->dispose (object);
}

static void
pp_page_curl_view_class_init (PpPageCurlViewClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = pp_page_curl_view_dispose;
}

static void
pp_page_curl_view_init (PpPageCurlView *self)
{
  gtk_gl_area_set_allowed_apis (GTK_GL_AREA (self), GDK_GL_API_GL);
  gtk_gl_area_set_required_version (GTK_GL_AREA (self), 3, 3);
  gtk_gl_area_set_auto_render (GTK_GL_AREA (self), FALSE);
  g_signal_connect_after (self, "realize", G_CALLBACK (realize_cb), NULL);
  g_signal_connect (self, "render", G_CALLBACK (render_cb), NULL);
  g_signal_connect (self, "unrealize", G_CALLBACK (unrealize_cb), NULL);
}

GtkWidget *
pp_page_curl_view_new (void)
{
  return g_object_new (PP_TYPE_PAGE_CURL_VIEW, NULL);
}

void
pp_page_curl_view_set_transition (PpPageCurlView *self,
                                  GdkTexture     *previous,
                                  GdkTexture     *current,
                                  double          previous_period,
                                  double          previous_angle,
                                  double          current_period,
                                  double          current_angle,
                                  gboolean        backwards)
{
  g_return_if_fail (PP_IS_PAGE_CURL_VIEW (self));
  g_return_if_fail (GDK_IS_TEXTURE (previous));
  g_return_if_fail (GDK_IS_TEXTURE (current));

  g_set_object (&self->slides[0], previous);
  g_set_object (&self->slides[1], current);
  self->periods[0] = previous_period;
  self->periods[1] = current_period;
  self->angles[0] = previous_angle;
  self->angles[1] = current_angle;
  self->backwards = backwards;
  gtk_gl_area_queue_render (GTK_GL_AREA (self));
}

void
pp_page_curl_view_clear (PpPageCurlView *self)
{
  g_return_if_fail (PP_IS_PAGE_CURL_VIEW (self));
  g_clear_object (&self->slides[0]);
  g_clear_object (&self->slides[1]);
  g_clear_object (&self->uploaded_slides[0]);
  g_clear_object (&self->uploaded_slides[1]);
}
