#pragma once

#include <cstdlib>
#include <glad/glad.h>

namespace mr
{
  class blur_filter
  {
  private:
    GLuint m_prg_hblur = 0;
    GLuint m_prg_vblur = 0;
    GLuint m_quad_va = 0;
    GLuint m_quad_vb = 0;
    GLuint m_framebuffer = 0;
    GLuint m_ping_pong = 0;
    int32_t m_width = 0; 
    int32_t m_height = 0; 
  public:
    blur_filter();
    ~blur_filter();
    void apply(const GLuint target, const int32_t width, const int32_t height, const std::size_t iterations);
  };

}