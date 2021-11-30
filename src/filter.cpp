#include "filter.h"

#include <tuple>
#include <string_view>

#include "common.h"

namespace mr
{

  static constexpr std::string_view s_vs_blur = R"(
    #version 330

    layout(location = 0) in vec2 aUv;

    smooth out vec2 fUv;

    void main() {
      gl_Position = vec4(aUv * 2.0 - 1.0, 0.0, 1.0);
      fUv = aUv;
    }
  )";

  static constexpr std::string_view s_fs_blur = R"(
    #version 330

    uniform sampler2D uTexture;

    smooth in vec2 fUv;

    out vec4 oColor;

    const float KERNEL[9] = float[] (
      0.0625,0.125,0.0625,
      0.125,0.25,0.125,
      0.0625,0.125,0.0625
    );

    void main() {
      vec2 step = 1.0 / vec2(textureSize(uTexture, 0));
      vec4 color = vec4(0.0);

      for(int i = 0; i < 3; ++i) {
        for(int j = 0; j < 3; ++j) {
          color += texture(uTexture, fUv + vec2(i - 1, j - 1) * step) * KERNEL[i * 3 + j]; 
        }
      } 

      oColor = color;

    }  
  )";

  blur_filter::blur_filter()
  {
    glGenFramebuffers(1, &m_framebuffer);
    glGenTextures(1, &m_ping_pong);
    glBindTexture(GL_TEXTURE_2D, m_ping_pong);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    m_phblur = load_program(s_vs_blur, s_fs_blur);
    m_pvblur = load_program(s_vs_blur, s_fs_blur);

    std::tie(m_quad_va, m_quad_vb) = create_full_screen_quad();
  }

  blur_filter::~blur_filter()
  {
    glDeleteFramebuffers(1, &m_framebuffer);
    glDeleteTextures(1, &m_ping_pong);
    glDeleteVertexArrays(1, &m_quad_va);
    glDeleteBuffers(1, &m_quad_vb);
    glDeleteProgram(m_phblur);
    glDeleteProgram(m_pvblur);
  }

  void blur_filter::apply(const GLuint target, const int32_t width, const int32_t height)
  {

    std::array passes = {
        std::make_tuple(m_ping_pong, target, m_phblur),
        std::make_tuple(target, m_ping_pong, m_pvblur)};

    enable_scope scope{GL_BLEND};
    glDisable(GL_BLEND);

    for (const auto [dst, src, program] : passes)
    {
      glBindTexture(GL_TEXTURE_2D, dst);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

      glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, dst, 0);

      glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
      glClear(GL_COLOR_BUFFER_BIT);

      glUseProgram(program);
      glUniform1i(glGetUniformLocation(program, "uTexture"), 0);

      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, src);

      glBindVertexArray(m_quad_va);
      glDrawArrays(GL_TRIANGLES, 0, 6);
    }
  }

}