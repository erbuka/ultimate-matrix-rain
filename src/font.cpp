#include "font.h"

#include <algorithm>
#include <fstream>
#include <filesystem> //TODO remove this
#include <iostream>   //TODO remove this

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

#include "application.h"

namespace mr
{

  static constexpr std::int32_t s_bitmap_width = 1024;
  static constexpr std::int32_t s_bitmap_height = 1024;

  void font::load(const std::string_view file_name)
  {
    std::ifstream is;

    is.open(file_name.data(), std::ios_base::binary);

    if (!is.is_open())
    {
      terminate_with_error("Could not open font file");
    }

    std::vector<unsigned char> font_data;
    std::vector<unsigned char> pixels;
    pixels.resize(s_bitmap_width * s_bitmap_height);

    std::copy(
        std::istreambuf_iterator<char>(is),
        std::istreambuf_iterator<char>(),
        std::back_inserter(font_data));

    is.close();

    stbtt_pack_context pack_context;

    stbtt_PackBegin(&pack_context, pixels.data(), s_bitmap_width, s_bitmap_height,
                    0, 2, nullptr);

    std::vector<stbtt_pack_range> pack_ranges;
    pack_ranges.push_back({.font_size = 32.0f,
                           .first_unicode_codepoint_in_range = 'a',
                           .array_of_unicode_codepoints = nullptr,
                           .num_chars = 'z' - 'a',
                           .chardata_for_range = new stbtt_packedchar['z' - 'a']});

    stbtt_PackFontRanges(&pack_context, font_data.data(), 0, pack_ranges.data(), pack_ranges.size());

    stbtt_PackEnd(&pack_context);

    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, s_bitmap_width, s_bitmap_height, 0, GL_RED, GL_UNSIGNED_BYTE, pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    for (const auto &range : pack_ranges)
    {
      for (std::int32_t i = 0; i < range.num_chars; ++i)
      {
        const auto &ginfo = range.chardata_for_range[i];

        m_glyphs.push_back({
            .uv0 = {
                float(ginfo.x0) / s_bitmap_width,
                float(ginfo.y1) / s_bitmap_height,
            },
            .uv1 = {
                float(ginfo.x1) / s_bitmap_width,
                float(ginfo.y0) / s_bitmap_height,
            },
        });
      }

      // Delet font ranges
      delete[] range.chardata_for_range;

    }

  }

  font::~font()
  {
    //TODO: fixme
    /*
    if (m_texture)
      glDeleteTextures(1, &m_texture);
    */
  }

}