#include "filter.h"

#include <algorithm>
#include <cassert>
#include <tuple>
#include <string_view>

#include "common.h"
#include "embed.h"

namespace mr
{

  blur_filter::blur_filter()
  {
    glGenFramebuffers(1, &m_framebuffer);
    glGenTextures(1, &m_ping_pong);
    glBindTexture(GL_TEXTURE_2D, m_ping_pong);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    m_prg_hblur = load_program(embed::s_vs_fullscreen, embed::s_fs_blur, {"HORIZONTAL"});
    m_prg_vblur = load_program(embed::s_vs_fullscreen, embed::s_fs_blur, {"VERTICAL"});

    std::tie(m_quad_va, m_quad_vb) = create_full_screen_quad();
  }

  blur_filter::~blur_filter()
  {
    glDeleteFramebuffers(1, &m_framebuffer);
    glDeleteTextures(1, &m_ping_pong);
    glDeleteVertexArrays(1, &m_quad_va);
    glDeleteBuffers(1, &m_quad_vb);
    glDeleteProgram(m_prg_hblur);
    glDeleteProgram(m_prg_vblur);
  }

  void blur_filter::resize(const std::int32_t width, const std::int32_t height)
  {
    glBindTexture(GL_TEXTURE_2D, m_ping_pong);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
  }

  void blur_filter::apply(const GLuint target, const float strength, const std::size_t iterations)
  {

    std::array passes = {
        std::make_tuple(m_ping_pong, target, m_prg_hblur),
        std::make_tuple(target, m_ping_pong, m_prg_vblur)};

    enable_scope scope{GL_BLEND};
    glDisable(GL_BLEND);

    for (std::size_t it = 0; it < iterations; ++it)
    {
      for (const auto &[dst, src, program] : passes)
      {
        glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, dst, 0);

        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(program);
        glUniform1f(glGetUniformLocation(program, "uStrength"), strength);
        glUniform1i(glGetUniformLocation(program, "uTexture"), 0);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, src);

        glBindVertexArray(m_quad_va);
        glDrawArrays(GL_TRIANGLES, 0, 6);
      }
    }
  }

  bloom::bloom()
  {
    glGenFramebuffers(1, &m_fb_render_target);
    m_prg_prefilter = load_program(embed::s_vs_fullscreen, embed::s_fs_bloom_prefilter);
    m_prg_downsample = load_program(embed::s_vs_fullscreen, embed::s_fs_bloom_downsample);
    m_prg_upsample = load_program(embed::s_vs_fullscreen, embed::s_fs_bloom_upsample);

    std::tie(m_quad_va, m_quad_vb) = create_full_screen_quad();
  }

  bloom::~bloom()
  {
    glDeleteProgram(m_prg_prefilter);
    glDeleteProgram(m_prg_downsample);
    glDeleteProgram(m_prg_upsample);

    glDeleteFramebuffers(1, &m_fb_render_target);

    glDeleteBuffers(1, &m_quad_vb);
    glDeleteVertexArrays(1, &m_quad_va);

    if (m_tx_downsample.size() > 0)
      glDeleteTextures(m_tx_downsample.size(), m_tx_downsample.data());

    if (m_tx_upsample.size() > 0)
      glDeleteTextures(m_tx_upsample.size(), m_tx_upsample.data());
  }

  void bloom::resize(int32_t width, int32_t height)
  {
    m_width = width;
    m_height = height;

    m_sizes.clear();

    while (width >= 1 && height >= 1)
    {
      m_sizes.push_back({width, height});
      width /= 2;
      height /= 2;
    }

    if (m_tx_downsample.size() > 0)
      glDeleteTextures(m_tx_downsample.size(), m_tx_downsample.data());

    if (m_tx_upsample.size() > 0)
      glDeleteTextures(m_tx_upsample.size(), m_tx_upsample.data());

    m_tx_downsample.resize(m_sizes.size());
    m_tx_upsample.resize(m_sizes.size());

    glGenTextures(m_sizes.size(), m_tx_downsample.data());
    glGenTextures(m_sizes.size(), m_tx_upsample.data());

    for (size_t i = 0; i < m_sizes.size(); ++i)
    {
      const auto [w, h] = m_sizes[i];
      glBindTexture(GL_TEXTURE_2D, m_tx_downsample[i]);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, w, h, 0, GL_RGB, GL_HALF_FLOAT, nullptr);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

      glBindTexture(GL_TEXTURE_2D, m_tx_upsample[i]);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, w, h, 0, GL_RGB, GL_HALF_FLOAT, nullptr);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    };
  }

  GLuint bloom::compute(const GLuint source, const float threshold, const float knee)
  {
    enable_scope scope({GL_BLEND});
    glDisable(GL_BLEND);

    assert(m_width > 0 && m_height > 0);

    // Prefilter
    glBindFramebuffer(GL_FRAMEBUFFER, m_fb_render_target);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_tx_downsample[0], 0);

    glViewport(0, 0, m_width, m_height);

    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(m_prg_prefilter);
    glUniform1f(glGetUniformLocation(m_prg_prefilter, "uThreshold"), threshold);
    glUniform1f(glGetUniformLocation(m_prg_prefilter, "uKnee"), knee);
    glUniform1i(glGetUniformLocation(m_prg_prefilter, "uSource"), 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, source);

    glBindVertexArray(m_quad_va);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    // Downsample
    {
      glUseProgram(m_prg_downsample);
      for (size_t i = 1; i < m_tx_downsample.size(); ++i)
      {
        const auto &[w, h] = m_sizes[i];

        glViewport(0, 0, w, h);

        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_tx_downsample[i], 0);
        glClear(GL_COLOR_BUFFER_BIT);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_tx_downsample[i - 1]);

        glUniform1i(glGetUniformLocation(m_prg_downsample, "uSource"), 0);

        glBindVertexArray(m_quad_va);
        glDrawArrays(GL_TRIANGLES, 0, 6);
      }
    }

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_tx_upsample.back(), 0);
    glClear(GL_COLOR_BUFFER_BIT);

    // Upsample
    {
      glUseProgram(m_prg_upsample);
      for (int32_t i = m_tx_upsample.size() - 2; i >= 0; --i)
      {
        const auto &[w, h] = m_sizes[i];

        glViewport(0, 0, w, h);

        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_tx_upsample[i], 0);
        glClear(GL_COLOR_BUFFER_BIT);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_tx_upsample[i + 1]);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_tx_downsample[i]);

        glUniform1i(glGetUniformLocation(m_prg_upsample, "uPrevious"), 0);
        glUniform1i(glGetUniformLocation(m_prg_upsample, "uDownsample"), 1);

        glBindVertexArray(m_quad_va);
        glDrawArrays(GL_TRIANGLES, 0, 6);
      }
    }
    return m_tx_upsample.front();
  }

}