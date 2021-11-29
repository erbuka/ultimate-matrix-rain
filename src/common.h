#pragma once

#include <array>
#include <cstdlib>
#include <string_view>
#include <concepts>
#include <array>

#include <glad/glad.h>

namespace mr
{

  struct glyph;

  // Generic vector type, just testing some C++20 concepts
  template <std::size_t N, typename T>
  requires std::floating_point<T> || std::integral<T>
  struct vec2
  {
    std::array<T, N> components = {T(0)};

    constexpr vec2 operator+(const vec2 &v)
    {
      vec2 r;
      for (std::size_t i = 0; i < N; ++i)
        r.components[i] = components[i] + v.components[i];
      return r;
    }

    constexpr vec2 operator-(const vec2 &v)
    {
      vec2 r;
      for (std::size_t i = 0; i < N; ++i)
        r.components[i] = components[i] + v.components[i];
      return r;
    }

    constexpr auto operator[](size_t i) const { return components[i]; }

    template <typename S>
    requires std::is_convertible_v<S, T>
        vec2 operator*(const S s)
    {
      vec2 r;
      for (std::size_t i = 0; i < N; ++i)
        r.components[i] = components[i] * T(s);
      return r;
    }
  };


  using vec2f = vec2<2, float>;
  using vec3f = vec2<3, float>;
  using vec4f = vec2<4, float>;



  struct vertex
  {
    vec2f position;
    vec2f uv;
    vec3f color;
  };

  struct grid_cell
  {
    std::array<vertex, 6> vertices;
    vertex &operator[](const size_t i) { return vertices[i]; }
    const vertex &operator[](const size_t i) const { return vertices[i]; }

    auto begin() { return vertices.begin(); }
    auto end() { return vertices.end(); }

    void set_color(const vec3f& c);
    void set_glyph(const glyph& g);

  };

  GLuint load_program(const std::string_view vs_source, const std::string_view fs_source);

}