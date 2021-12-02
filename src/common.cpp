#include "common.h"

#include <string>
#include <sstream>

#include <GLFW/glfw3.h>

#include "application.h"
#include "font.h"

namespace mr
{

  static std::string inject_defines_into_source(const std::string_view source, const std::initializer_list<std::string_view> &defines)
  {
    std::string src(source.data());

    if (defines.size() > 0)
    {
      auto pos = src.find("#version");
      pos = pos == std::string::npos ? 0 : src.find('\n', pos) + 1;

      // No std::format :(
      for(const auto d: defines) {
        std::stringstream ss;;
        ss << "#define " << d.data() << '\n';
        src.insert(pos, ss.str());
      }

    }

    return src;
  }

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

  GLuint load_program(const std::string_view vs_source, const std::string_view fs_source, const std::initializer_list<std::string_view> &defines)
  {
    const auto program = glCreateProgram();

    const auto vs = load_shader(inject_defines_into_source(vs_source, defines), GL_VERTEX_SHADER);
    const auto fs = load_shader(inject_defines_into_source(fs_source, defines), GL_FRAGMENT_SHADER);

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

  std::tuple<GLuint, GLuint> create_full_screen_quad()
  {
    static constexpr std::array<vec2f, 6> s_full_screen_quad = {
        vec2f{0.0f, 0.0f},
        vec2f{1.0f, 0.0f},
        vec2f{1.0f, 1.0f},
        vec2f{0.0f, 0.0f},
        vec2f{1.0f, 1.0f},
        vec2f{0.0f, 1.0f},
    };

    GLuint va, vb;

    glGenVertexArrays(1, &va);
    glGenBuffers(1, &vb);

    glBindVertexArray(va);
    glBindBuffer(GL_ARRAY_BUFFER, vb);

    glBufferData(GL_ARRAY_BUFFER, sizeof(vec2f) * s_full_screen_quad.size(), s_full_screen_quad.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(vec2f), 0);

    return std::tuple{va, vb};
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

  enable_scope::enable_scope(const std::initializer_list<GLenum> &bits)
  {
    for (const auto bit : bits)
      m_bits[bit] = glIsEnabled(bit) == GL_TRUE;
  }

  enable_scope::~enable_scope()
  {
    for (const auto [bit, enabled] : m_bits)
      enabled ? glEnable(bit) : glDisable(bit);
  }

}