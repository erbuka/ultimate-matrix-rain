#include "common.h"

#include <string>

#include <GLFW/glfw3.h>

#include "application.h"
#include "font.h"

namespace mr
{

  static auto load_shader(const std::string_view source, const GLenum type)
  {
    const auto shader = glCreateShader(type);

    const GLchar *src = source.data();
    const GLint len = source.size();

    glShaderSource(shader, 1, &src, &len);

    glCompileShader(shader);

    GLint is_compiled = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &is_compiled);
    if (is_compiled == GL_FALSE)
    {
      GLint max_length = 0;
      glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &max_length);

      std::string error_log;
      error_log.resize(max_length);
      glGetShaderInfoLog(shader, max_length, &max_length, error_log.data());

      terminate_with_error(error_log);
    }

    return shader;
  }

  GLuint load_program(const std::string_view vs_source, const std::string_view fs_source)
  {
    const auto program = glCreateProgram();

    const auto vs = load_shader(vs_source, GL_VERTEX_SHADER);
    const auto fs = load_shader(fs_source, GL_FRAGMENT_SHADER);

    glAttachShader(program, vs);
    glAttachShader(program, fs);

    glLinkProgram(program);

    GLint is_linked;
    glGetProgramiv(program, GL_LINK_STATUS, &is_linked);

    if (is_linked == GL_FALSE)
    {
      GLint max_length = 0;
      glGetProgramiv(program, GL_INFO_LOG_LENGTH, &max_length);

      std::string error_log;
      error_log.resize(max_length);
      glGetProgramInfoLog(program, max_length, &max_length, error_log.data());

      terminate_with_error(error_log);
    }

    glDetachShader(program, vs);
    glDetachShader(program, fs);

    return program;
  }

  void grid_cell::set_color(const vec4f &c)
  {
    for (auto &v : vertices)
      v.color = c;
  }

  void grid_cell::set_glyph(const glyph &g)
  {
    vertices[0].uv = {g.uv0[0], g.uv1[1]};
    vertices[1].uv = {g.uv0[0], g.uv0[1]};
    vertices[2].uv = {g.uv1[0], g.uv1[1]};
    vertices[3].uv = {g.uv0[0], g.uv0[1]};
    vertices[4].uv = {g.uv1[0], g.uv0[1]};
    vertices[5].uv = {g.uv1[0], g.uv1[1]};
  }

  void grid_cell::set_position(const vec2f pixel_pos, const float pixel_size)
  {

    const float fx = pixel_pos[0];
    const float fy = pixel_pos[1];

    vertices[0].position = {fx, fy};
    vertices[1].position = {fx, fy + pixel_size};
    vertices[2].position = {fx + pixel_size, fy};
    vertices[3].position = {fx, fy + pixel_size};
    vertices[4].position = {fx + pixel_size, fy + pixel_size};
    vertices[5].position = {fx + pixel_size, fy};
  }

}