#pragma once

#include <algorithm>
#include <cmath>
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
  struct vec
  {
    std::array<T, N> components = {T(0)};

    vec() = default;

    constexpr vec(const std::initializer_list<T> &l)
    {
      std::ranges::copy(l, components.begin());
    }

    constexpr vec(const vec<N - 1, T> &v, T x)
    {
      std::ranges::copy(v.components, components.begin());
      components.back() = x;
    }

    constexpr vec operator+(const vec &v) const
    {
      vec r;
      for (std::size_t i = 0; i < N; ++i)
        r.components[i] = components[i] + v.components[i];

      return r;
    }

    constexpr vec operator-(const vec &v) const
    {
      vec r;
      for (std::size_t i = 0; i < N; ++i)
        r.components[i] = components[i] + v.components[i];
      return r;
    }

    template <typename S>
    requires std::is_convertible_v<S, T>
        vec operator*(const S s) const
    {
      vec r;
      for (std::size_t i = 0; i < N; ++i)
        r.components[i] = components[i] * T(s);
      return r;
    }

    constexpr auto operator[](size_t i) const { return components[i]; }
  };

  using vec2f = vec<2, float>;
  using vec3f = vec<3, float>;
  using vec4f = vec<4, float>;

  struct vertex
  {
    vec2f position;
    vec2f uv;
    vec4f color;
  };

  struct grid_cell
  {
    std::array<vertex, 6> vertices;
    vertex &operator[](const size_t i) { return vertices[i]; }
    const vertex &operator[](const size_t i) const { return vertices[i]; }

    auto begin() { return vertices.begin(); }
    auto end() { return vertices.end(); }

    void set_color(const vec4f &c);
    void set_glyph(const glyph &g);
    void set_position(const vec2f pixel_pos, const float pixel_size);
  };

  template <std::size_t N>
  struct color_palette
  {
    const std::array<vec3f, N> colors;

    const vec3f get(const float t) const
    {
      const int32_t idx0 = t * (N - 1);
      if (idx0 >= N - 1)
        return colors[N - 1];
      else
      {
        const float t0 = t * (N - 1) - idx0;
        return colors[idx0] * (1.0f - t0) + colors[idx0 + 1] * t0;
      }
    }
  };

  GLuint load_program(const std::string_view vs_source, const std::string_view fs_source);

}