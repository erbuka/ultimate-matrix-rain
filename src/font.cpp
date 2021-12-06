#include "font.h"

#include <algorithm>
#include <fstream>
#include <iostream>

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

#include "application.h"

namespace mr
{

  static constexpr float s_font_size = 64.0f;
  static constexpr std::int32_t s_bitmap_width = 1024;
  static constexpr std::int32_t s_bitmap_height = 1024;

  static constexpr std::string_view s_characters =
      "abcdefghijklmnopqrstuvwxyz"
      "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
      "0123456789"
      ". ";

  // I can make this function consteval in gcc, while VS is complaining that 
  // a "call to immediate function is not a constant expression".
  // From what I understand consteval just means that this is an immediate function
  // and I should be able to call it even from a non-constexpr context
  static /* consteval */ auto get_code_points()
  {
    std::array<std::int32_t, s_characters.size()> ret = {0};
    std::ranges::transform(s_characters, ret.begin(), [](const char c)
                           { return static_cast<std::int32_t>(c); });
    return ret;
  };

  const glyph& font::find_glyph(const int32_t code_point)
  {
    const auto it = std::ranges::find_if(m_glyphs, [code_point](const glyph& g) { return g.code_point == code_point; });
    assert(it != m_glyphs.end());
    return *it;
  }


  void font::load(const unsigned char *font_data, [[maybe_unused]] const size_t length)
  {

    std::vector<unsigned char> pixels;
    pixels.resize(s_bitmap_width * s_bitmap_height);

    stbtt_pack_context pack_context;

    stbtt_PackBegin(&pack_context, pixels.data(), s_bitmap_width, s_bitmap_height,
                    0, 2, nullptr);

    auto code_points = get_code_points();

    std::vector<stbtt_pack_range> pack_ranges;
    pack_ranges.push_back({.font_size = s_font_size,
                           .first_unicode_codepoint_in_range = 0,
                           .array_of_unicode_codepoints = code_points.data(),
                           .num_chars = code_points.size(),
                           .chardata_for_range = new stbtt_packedchar[code_points.size()]});

    stbtt_PackFontRanges(&pack_context, font_data, 0, pack_ranges.data(), pack_ranges.size());

    stbtt_PackEnd(&pack_context);

    // The font is packed into a 8-bit bitmap (basically grayscale). I'll store it as RED 8
    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, s_bitmap_width, s_bitmap_height, 0, GL_RED, GL_UNSIGNED_BYTE, pixels.data());

    // For this program, linear filtering works much better than mipmaps
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    for (const auto &range : pack_ranges)
    {
      for (std::int32_t i = 0; i < range.num_chars; ++i)
      {
        const auto &ginfo = range.chardata_for_range[i];

        // It is a classic bitmap, so the origin (0, 0) is a the top left corner, and each glyph coordinates are given
        // in pixel space. So I need to invert the y-axis to get OpenGL uv coordinates
        m_glyphs.push_back({
          .code_point = range.array_of_unicode_codepoints[i],
          .uv0 = {
            float(ginfo.x0) / s_bitmap_width,
            float(ginfo.y1) / s_bitmap_height,
          },
          .uv1 = {
            float(ginfo.x1) / s_bitmap_width,
            float(ginfo.y0) / s_bitmap_height,
          },
          .norm_advance = ginfo.xadvance / s_font_size,
          .norm_offset = {
            float(ginfo.xoff) / s_font_size,
            float(ginfo.yoff) / s_font_size
          },
          .norm_size = {
            float(ginfo.x1 - ginfo.x0) / s_font_size,
            float(ginfo.y1 - ginfo.y0) / s_font_size
          }
        });
      }

      // Delete font ranges
      delete[] range.chardata_for_range;
    }

  }

  void font::load(const std::string_view file_name)
  {
    std::ifstream is;

    is.open(file_name.data(), std::ios_base::binary);

    if (!is.is_open())
    {
      terminate_with_error("Could not open font file");
    }

    std::vector<unsigned char> font_data;

    std::copy(
        std::istreambuf_iterator<char>(is),
        std::istreambuf_iterator<char>(),
        std::back_inserter(font_data));

    is.close();

    load(font_data.data(), font_data.size());
  }

  void font::swap_glyphs(const std::size_t count)
  {
    for(std::size_t i = 0; i < count; ++i)
    {
      const std::size_t idx0 = rng::next(std::size_t{0}, m_glyphs.size());
      const std::size_t idx1 = rng::next(std::size_t{0}, m_glyphs.size());
      std::swap(m_glyphs[idx0], m_glyphs[idx1]);
    }
  }
  

  font::~font()
  {
    if (m_texture)
      glDeleteTextures(1, &m_texture);
  }

}