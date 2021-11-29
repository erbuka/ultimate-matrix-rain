#include "common.h"


#include <string>
#include <GLFW/glfw3.h>

#include "application.h"

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

}