#pragma once

#include <cstdlib>
#include <glad/glad.h>
#include <vector>
#include <tuple>

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


  class bloom 
  {
  private:
    GLuint m_fb_render_target = 0;
    GLuint m_quad_va = 0, m_quad_vb = 0;
    GLuint m_prg_prefilter = 0, m_prg_downsample = 0, m_prg_upsample = 0;
    int32_t m_width = 0, m_height = 0;
    std::vector<std::tuple<int32_t, int32_t>> m_sizes;
    std::vector<GLuint> m_tx_downsample;
    std::vector<GLuint> m_tx_upsample;
  public:
    bloom();
    ~bloom();
    void resize(int32_t width, int32_t height);
    GLuint compute(const GLuint source, const float threshold, const float knee);
  };


}