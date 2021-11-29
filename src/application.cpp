#include "application.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <tuple>
#include <array>
#include <iostream>
#include <vector>
#include <concepts>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "common.h"
#include "font.h"

// TODO: hdr with additive coloring

namespace mr
{

  struct falling_string
  {
    std::int32_t x = 0.0f;
    float y = 0.0f;
    float speed = 0.0f;
    float depth = 1.0f;
    std::int32_t length = 0;
  };

  static constexpr std::int32_t s_col_count = 100;
  static constexpr float s_falling_strings_min_depth = 0.25f;
  static constexpr float s_falling_strings_max_depth = 1.0f;
  static constexpr std::int32_t s_falling_strings_count = 1000;
  static constexpr std::int32_t s_falling_string_min_length = 5;
  static constexpr std::int32_t s_falling_string_max_length = 50;
  static constexpr std::int32_t s_falling_string_min_speed = 10;
  static constexpr std::int32_t s_falling_string_max_speed = 30;

  static constexpr std::string_view s_vertex_source = R"(
    #version 330

    uniform float uScreenWidth;
    uniform float uScreenHeight;

    layout(location = 0) in vec2 aPosition;
    layout(location = 1) in vec2 aUv;
    layout(location = 2) in vec4 aColor;

    smooth out vec2 fUv;
    flat out vec4 fColor;

    void main() {
      vec2 ndcPos;
      ndcPos.x = (aPosition.x / uScreenWidth) * 2.0 - 1.0;
      ndcPos.y = (aPosition.y / uScreenHeight) * -2.0 + 1.0;

      gl_Position = vec4(ndcPos, 0.0, 1.0);
      fUv = aUv;
      fColor = aColor;
    }
  )";

  static constexpr std::string_view s_fragment_source = R"(
    #version 330

    uniform sampler2D uFont;

    smooth in vec2 fUv;
    flat in vec4 fColor;
    
    out vec4 oColor;

    void main() {
      float mask = texture(uFont, fUv).r;
      oColor = vec4(fColor.rgb, fColor.a * mask);
    }
  )";

  static constexpr color_palette<10> s_color_palette = {
      vec3f{0.0f, 1.0f, 0.0f},
      vec3f{0.0f, 1.0f, 0.0f},
      vec3f{0.0f, 1.0f, 0.0f},
      vec3f{0.0f, 1.0f, 0.0f},
      vec3f{0.0f, 1.0f, 0.0f},
      vec3f{0.0f, 1.0f, 0.0f},
      vec3f{0.0f, 1.0f, 0.0f},
      vec3f{0.0f, 1.0f, 0.0f},
      vec3f{1.0f, 1.0f, 1.0f},
      vec3f{1.0f, 1.0f, 1.0f},
  };

  static GLFWwindow *s_window = nullptr;
  static GLuint s_program = 0;
  static GLuint s_va = 0; // Vertex Array
  static GLuint s_vb = 0; // Vertex Buffer
  static std::vector<grid_cell> s_grid;
  static std::array<falling_string, s_falling_strings_count> s_falling_strings;
  static font s_font;

  // Randomly initialize a falling string. I just use the stantard rand(), it's enough for this project
  static void init_falling_string(falling_string &s)
  {
    s.depth = std::pow(s_falling_strings_min_depth + rand() / float(RAND_MAX) * (s_falling_strings_max_depth - s_falling_strings_min_depth), 3);
    s.speed = s_falling_string_min_speed + rand() % (s_falling_string_max_speed - s_falling_string_min_speed);
    s.length = s_falling_string_min_length + rand() % (s_falling_string_max_length - s_falling_string_min_length);
    s.x = (rand() % s_col_count) / s.depth;

    // The initial y position is actually randomized to be off screen. This gives more variance at the start of the program
    s.y = -(s.length + rand() % s_falling_string_max_length);
  }

  static auto get_window_size()
  {
    int32_t x, y;
    glfwGetWindowSize(s_window, &x, &y);
    return std::tuple{float(x), float(y)};
  }

  static const glyph &get_random_glyph(int32_t x, int32_t y, float depth)
  {
    const auto &glyphs = s_font.get_glyphs();
    const auto idx = static_cast<std::size_t>(float(x * s_col_count + y) * (1.0f + depth)) % glyphs.size();
    return glyphs[idx];
  }

  static void render(const float dt)
  {
    const auto [w, h] = get_window_size();

    constexpr float screen_width = s_col_count;
    const float screen_height = h / w * screen_width;

    // Clear our cells
    s_grid.clear();

    // Compute colors based on falling strings
    for (auto &s : s_falling_strings)
    {
      const float cell_size = screen_width / s_col_count * s.depth;
      const int32_t max_y = std::round(s.y);
      const int32_t min_y = max_y - s.length + 1;

      for (int32_t y = min_y; y <= max_y; ++y)
      {
        const float t = float(y - min_y) / (max_y - min_y);

        grid_cell cell;
        cell.set_color({ s_color_palette.get(t), t * s.depth});
        cell.set_glyph(get_random_glyph(s.x, y, s.depth));
        cell.set_position(vec2f{float(s.x), float(y)} * cell_size, cell_size);

        s_grid.push_back(cell);
      }

      // Move this string down
      s.y += dt * s.speed;

      // If the string is out of screen, reset it
      if (min_y * cell_size >= h)
        init_falling_string(s);
    }

    glViewport(0, 0, w, h);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(s_program);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s_font.get_texture());

    glUniform1f(glGetUniformLocation(s_program, "uScreenWidth"), screen_width);
    glUniform1f(glGetUniformLocation(s_program, "uScreenHeight"), screen_height);
    glUniform1i(glGetUniformLocation(s_program, "uFont"), 0);

    glBindVertexArray(s_va);
    glBindBuffer(GL_ARRAY_BUFFER, s_vb);
    glBufferData(GL_ARRAY_BUFFER, sizeof(grid_cell) * s_grid.size(), s_grid.data(), GL_DYNAMIC_DRAW);

    glDrawArrays(GL_TRIANGLES, 0, s_grid.size() * 6);
  }

  static void initialize()
  {
    // Load main program
    s_program = load_program(s_vertex_source, s_fragment_source);

    // Init vertex array
    glGenVertexArrays(1, &s_va);
    glGenBuffers(1, &s_vb);

    glBindVertexArray(s_va);
    glBindBuffer(GL_ARRAY_BUFFER, s_vb);

    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(vertex), (const void *)0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(vertex), (const void *)8);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(vertex), (const void *)16);

    // Init some OpenGL stuff
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Load font
    s_font.load("assets/font.ttf");

    // Initilize falling strings
    for (auto &s : s_falling_strings)
      init_falling_string(s);
  }

  static void on_window_resize(GLFWwindow *window, int32_t w, int32_t h)
  {
  }

  static void terminate(const int32_t exit_code)
  {
    glfwTerminate();
    exit(exit_code);
  }

  void terminate_with_error(std::string_view descr)
  {
    std::cerr << descr << '\n';
    terminate(-1);
  }

  void run()
  {

    using clock_t = std::chrono::high_resolution_clock;

    /* Initialize the library */
    if (!glfwInit())
      terminate_with_error("Could not initialize GLFW");

    /* Create a windowed mode window and its OpenGL context */
    s_window = glfwCreateWindow(1280, 768, "Hello World", NULL, NULL);
    if (!s_window)
      terminate_with_error("Could not create window");

    /* Make the window's context current */
    glfwMakeContextCurrent(s_window);
    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);

    glfwSetWindowSizeCallback(s_window, &on_window_resize);

    initialize();

    auto prev_time = clock_t::now();
    auto current_time = clock_t::now();

    /* Loop until the user closes the window */
    while (!glfwWindowShouldClose(s_window))
    {
      current_time = clock_t::now();
      std::chrono::duration<float> delta = current_time - prev_time;
      prev_time = current_time;

      /* Render here */
      render(delta.count());

      /* Swap front and back buffers */
      glfwSwapBuffers(s_window);

      /* Poll for and process events */
      glfwPollEvents();
    }

    terminate(0);
  }
}