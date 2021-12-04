#include "filter.h"

#include <algorithm>
#include <cassert>
#include <tuple>
#include <string_view>

#include "common.h"

namespace mr
{

  static constexpr std::string_view s_vs_fullscreen = R"(
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
    uniform float uStrength;

    smooth in vec2 fUv;

    out vec4 oColor;

    const float KERNEL[3] = float[] (
      0.25, 0.5, 0.25
    );

    void main() {
      vec2 step = 1.0 / vec2(textureSize(uTexture, 0));
      vec3 color = vec3(0.0);

      for(int i = 0; i < 3; ++i) {
        #if defined(HORIZONTAL)
          color += texture(uTexture, fUv + vec2(i - 1, 0.0) * step).rgb * KERNEL[i]; 
        #elif defined(VERTICAL)
          color += texture(uTexture, fUv + vec2(0.0, i - 1) * step).rgb * KERNEL[i]; 
        #else 
          #error "You bad person"
        #endif
      }

      vec3 weigthedColor = mix(texture(uTexture, fUv).rgb, color, uStrength);

      oColor = vec4(weigthedColor, 1.0);

    }  
  )";

  static constexpr std::string_view s_fs_pass_trough = R"(
    #version 330

    uniform sampler2D uTexture;

    out vec4 oColor;

    void main() {
      oColor = texture(uTexture, fUv);
    }  
  )";

  static constexpr std::string_view s_fs_bloom_prefilter = R"(
    #version 330 core

    uniform sampler2D uSource;
    uniform float uThreshold;
    uniform float uKnee;

    in vec2 fUv;

    out vec4 oColor;

    void main() {
        vec3 color = texture(uSource, fUv).rgb;
        float luma = dot(vec3(0.299, 0.587, 0.114), color);
        oColor = vec4(smoothstep(uThreshold - uKnee, uThreshold + uKnee, luma) * color,  1.0);
    }
  )";

  static constexpr std::string_view s_fs_bloom_downsample = R"(
    #version 330 core

    uniform sampler2D uSource;

    in vec2 fUv;

    out vec4 oColor;

    void main() {
        vec2 s = 1.0 / vec2(textureSize(uSource, 0));

        vec3 tl = texture(uSource, fUv + vec2(-s.x, +s.y)).rgb;
        vec3 tr = texture(uSource, fUv + vec2(+s.x, +s.y)).rgb;
        vec3 bl = texture(uSource, fUv + vec2(-s.x, -s.y)).rgb;
        vec3 br = texture(uSource, fUv + vec2(+s.x, -s.y)).rgb;

        oColor = vec4((tl + tr + bl + br) / 4.0,  1.0);
    }
  )";

  static constexpr std::string_view s_fs_bloom_upsample = R"(
    #version 330 core

    uniform sampler2D uPrevious;
    uniform sampler2D uDownsample;

    in vec2 fUv;

    out vec4 oColor;

    void main() {
        vec2 s = 1.0 / vec2(textureSize(uDownsample, 0));

        vec3 upsampleColor = vec3(0.0);

        upsampleColor += 1.0 * texture(uDownsample, fUv + vec2(-s.x, +s.y)).rgb;
        upsampleColor += 2.0 * texture(uDownsample, fUv + vec2(+0.0, +s.y)).rgb;
        upsampleColor += 1.0 * texture(uDownsample, fUv + vec2(+s.x, +s.y)).rgb;
        upsampleColor += 2.0 * texture(uDownsample, fUv + vec2(-s.x, +0.0)).rgb;
        upsampleColor += 4.0 * texture(uDownsample, fUv + vec2(+0.0, +0.0)).rgb;
        upsampleColor += 2.0 * texture(uDownsample, fUv + vec2(+s.x, +0.0)).rgb;
        upsampleColor += 1.0 * texture(uDownsample, fUv + vec2(-s.x, -s.y)).rgb;
        upsampleColor += 2.0 * texture(uDownsample, fUv + vec2(+0.0, -s.y)).rgb;
        upsampleColor += 1.0 * texture(uDownsample, fUv + vec2(+s.x, -s.y)).rgb;
        
        oColor = vec4(upsampleColor / 16.0 + texture(uPrevious, fUv).rgb, 1.0);

    }
  )";

  blur_filter::blur_filter()
  {
    glGenFramebuffers(1, &m_framebuffer);
    glGenTextures(1, &m_ping_pong);
    glBindTexture(GL_TEXTURE_2D, m_ping_pong);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    m_prg_hblur = load_program(s_vs_fullscreen, s_fs_blur, {"HORIZONTAL"});
    m_prg_vblur = load_program(s_vs_fullscreen, s_fs_blur, {"VERTICAL"});

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

  void blur_filter::apply(const GLuint target, const int32_t width, const int32_t height, const float strength, const std::size_t iterations)
  {

    std::array passes = {
        std::make_tuple(m_ping_pong, target, m_prg_hblur),
        std::make_tuple(target, m_ping_pong, m_prg_vblur)};

    enable_scope scope{GL_BLEND};
    glDisable(GL_BLEND);

    for (auto it = 0; it < iterations; ++it)
    {
      for (const auto [dst, src, program] : passes)
      {
        glBindTexture(GL_TEXTURE_2D, dst);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);

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
    m_prg_prefilter = load_program(s_vs_fullscreen, s_fs_bloom_prefilter);
    m_prg_downsample = load_program(s_vs_fullscreen, s_fs_bloom_downsample);
    m_prg_upsample = load_program(s_vs_fullscreen, s_fs_bloom_upsample);

    std::tie(m_quad_va, m_quad_vb) = create_full_screen_quad();
  }

  bloom::~bloom()
  {
    // TODO: cleanup textures and programs
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

    // TODO: check parameters (width, height)
    assert(m_width > 0 && m_height > 0);

    // Prefilter
    glBindFramebuffer(GL_FRAMEBUFFER, m_fb_render_target);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_tx_downsample[0], 0);

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

    // TODO: should copy last downsample to upsample ????

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