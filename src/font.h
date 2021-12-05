#pragma once

#include <vector>
#include <string_view>

#include <glad/glad.h>

#include "common.h"

namespace mr
{

  struct glyph
  {
    vec2f uv0, uv1;
    // The glyph offset relative to the origin, normalized by font size
    vec2f norm_offset;
    // The glyph size normalized with the font size
    vec2f norm_size; 
  };

  struct font
  {
  private:
    GLuint m_texture = 0;
    std::vector<glyph> m_glyphs;

  public:
    font() = default;
    ~font();

    // Some characters change from time to time in the original matrix rain, so
    // this is an helper function that swaps randomly the given amount of glyphs
    void swap_glyphs(const std::size_t count);
    void load(const unsigned char* data, const size_t length);
    void load(const std::string_view file_name);
    GLuint get_texture() const { return m_texture; }
    const std::vector<glyph> &get_glyphs() const { return m_glyphs; }

  };

}