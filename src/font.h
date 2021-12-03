#pragma once

#include <vector>
#include <string_view>

#include <glad/glad.h>

#include "common.h"

// TODO: embed font in source code, get rid of assets

namespace mr
{

  struct glyph
  {
    vec2f uv0, uv1;
  };

  struct font
  {
  private:
    GLuint m_texture = 0;
    std::vector<glyph> m_glyphs;

  public:
    font() = default;
    ~font();

    void load(const std::string_view file_name);
    const GLuint get_texture() const { return m_texture; }
    const std::vector<glyph> &get_glyphs() const { return m_glyphs; }

  };

}