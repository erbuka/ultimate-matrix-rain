#include "application.h"

#include <thread>
#include <memory>
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

#ifdef DEBUG
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#endif

#include "filter.h"
#include "common.h"
#include "font.h"
#include "embed.h"

namespace mr
{

  struct falling_string
  {
    std::int32_t x = 0;
    float y = 0.0f;
    float speed = 0.0f;
    size_t layer_index = 0;
    std::int32_t length = 0;
  };

  using clock_t = std::chrono::high_resolution_clock;

  // Fixed animation settings
  static constexpr std::int32_t s_blur_scale = 1;
  static constexpr std::int32_t s_col_count = 100;
  static constexpr std::int32_t s_falling_strings_count = 1500;
  static constexpr std::int32_t s_falling_string_min_length = 15;
  static constexpr std::int32_t s_falling_string_max_length = 40;
  static constexpr std::int32_t s_falling_string_min_speed = 10;
  static constexpr std::int32_t s_falling_string_max_speed = 30;

  static constexpr std::array<float, 4> s_depth_layers = {
      0.15,
      0.30,
      0.50,
      1.00,
  };

  static constexpr std::array<float, s_depth_layers.size()> s_depth_layers_fade = ([]
                                                                                   {
                                                                                     std::array<float, s_depth_layers.size()> result;
                                                                                     for (size_t i = 0; i < result.size(); ++i)
                                                                                       result[i] = s_depth_layers[i] * s_depth_layers[i];
                                                                                     return result;
                                                                                   })();

  static constexpr auto s_exit_on_input_delay = std::chrono::milliseconds(1500);

  static GLFWwindow *s_window = nullptr;

  // Programs
  static GLuint s_prg_hdr = 0;
  static GLuint s_prg_strings = 0;
  static GLuint s_prg_pass_trough = 0;

  static GLuint s_va = 0; // Vertex Array
  static GLuint s_vb = 0; // Vertex Buffer

  // Framebuffer
  static GLuint s_fb_render_target = 0;

  // Final render texture
  static GLuint s_tx_final_render = 0;

  // Blur textures
  static GLuint s_tx_blur0 = 0;
  static GLuint s_tx_blur1 = 0;

  // Full screen quad vertex array & vertex buffer
  static GLuint s_va_quad = 0;
  static GLuint s_vb_quad = 0;

  // Shader params (no constexpr because I need to tune them with ImGui)
  static float s_exposure = 1.0f;
  static float s_bloom_threshold = 1.0f;
  static float s_bloom_knee = 0.5f;
  static float s_blur_str_multiplier = 0.5f;

  // Falling strings color palette
  static color_palette<2> s_color_palette = {
      vec3f{0.0f, 1.0f, 0.0f},
      vec3f{0.1f, 2.0f, 0.3f},
  };

  // Config
  static launch_config s_config;
  static clock_t::time_point s_start_time;

  // Data
  static std::array<std::vector<grid_cell>, s_depth_layers.size()> s_grids;
  static std::array<falling_string, s_falling_strings_count> s_falling_strings;
  static std::unique_ptr<font> s_font;
  static std::unique_ptr<blur_filter> s_blur_filter;
  static std::unique_ptr<bloom> s_fx_bloom;

  // Randomly initialize a falling string. I just use the stantard rand(), it's enough for this project
  static void init_falling_string(falling_string &s, const float view_height)
  {

    static constexpr auto get_random_layer = +[]() -> std::size_t
    {
      // cubic on t so that front layers are less likely to come out
      float t = rand() / float(RAND_MAX);
      return static_cast<std::size_t>(t * t * s_depth_layers.size());
    };

    s.layer_index = get_random_layer();
    s.speed = s_falling_string_min_speed + rand() % (s_falling_string_max_speed - s_falling_string_min_speed);
    s.length = s_falling_string_min_length + rand() % (s_falling_string_max_length - s_falling_string_min_length);

    // Number of columns depends on the depth
    const int32_t col_count = s_col_count / s_depth_layers[s.layer_index];
    s.x = rand() % col_count;

    // TODO: try to still improve this
    // The initial y position is actually randomized to be off screen.
    s.y = -(s.length + rand() % static_cast<int32_t>(view_height / s_depth_layers[s.layer_index]));
  }

  static auto get_window_size()
  {
    int32_t x, y;
    glfwGetWindowSize(s_window, &x, &y);
    return std::tuple{float(x), float(y)};
  }

  static auto get_view_size()
  {
    const auto [w, h] = get_window_size();
    return std::tuple{static_cast<int32_t>(s_col_count), static_cast<int32_t>(h / w * s_col_count)};
  }

  static auto get_mouse_pos()
  {
    double x, y;
    glfwGetCursorPos(s_window, &x, &y);
    return std::tuple{int32_t(x), int32_t(y)};
  }

  static const glyph &get_random_glyph(int32_t x, int32_t y)
  {
    constexpr size_t s0 = 2836, s1 = 23873;
    const auto &glyphs = s_font->get_glyphs();
    const auto idx = static_cast<std::size_t>(x * s0 + y * s1) % glyphs.size();
    return glyphs[idx];
  }

  static void update_falling_string(falling_string &s, const float dt, const float view_width, const float view_height)
  {
    const float depth = s_depth_layers[s.layer_index];
    const float cell_size = view_width / s_col_count * depth;
    const int32_t max_y = std::round(s.y);
    const int32_t min_y = max_y - s.length + 1;

    for (int32_t y = min_y; y <= max_y; ++y)
    {
      const float t = float(y - min_y) / (max_y - min_y);

      grid_cell cell;

      cell.set_color({s_color_palette.get(t), t * s_depth_layers_fade[s.layer_index]});

      //cell.set_color({s_color_palette.get(t), t });
      cell.set_glyph(get_random_glyph(s.x, y));
      cell.set_position(vec2f{float(s.x), float(y)} * cell_size, cell_size);

      s_grids[s.layer_index].push_back(cell);
    }

    // Move this string down
    s.y += dt * s.speed;

    // If the string is out of screen, reset it
    if (min_y * cell_size >= view_height)
      init_falling_string(s, view_height);
  }

  static void render_layer(const std::vector<grid_cell> &cells, const float view_width, const float view_height)
  {
    // Rendering falling strings
    glUseProgram(s_prg_strings);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s_font->get_texture());

    glUniform1f(glGetUniformLocation(s_prg_strings, "uScreenWidth"), view_width);
    glUniform1f(glGetUniformLocation(s_prg_strings, "uScreenHeight"), view_height);
    glUniform1i(glGetUniformLocation(s_prg_strings, "uFont"), 0);

    glBindVertexArray(s_va);
    glBindBuffer(GL_ARRAY_BUFFER, s_vb);
    glBufferData(GL_ARRAY_BUFFER, sizeof(grid_cell) * cells.size(), cells.data(), GL_DYNAMIC_DRAW);

    glDrawArrays(GL_TRIANGLES, 0, cells.size() * 6);
  }

  static void render(const float dt)
  {
    const auto [w, h] = get_window_size();

    constexpr float view_width = s_col_count;
    const float view_height = h / w * view_width;

    // Clear grids
    for (auto &g : s_grids)
      g.clear();

    // Update all the falling strings
    for (auto &s : s_falling_strings)
      update_falling_string(s, dt, view_width, view_height);

    auto tx_src = s_tx_blur0;
    auto tx_dst = s_tx_blur1;

    // Clear source from previous frame
    glBindFramebuffer(GL_FRAMEBUFFER, s_fb_render_target);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tx_src, 0);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // Blur all the back layers
    for (size_t i = 0; i < s_depth_layers.size() - 1; ++i)
    {
      const auto &current_grid = s_grids[i];

      // Render to render target
      glBindFramebuffer(GL_FRAMEBUFFER, s_fb_render_target);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tx_dst, 0);

      glViewport(0, 0, w / s_blur_scale, h / s_blur_scale);

      glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
      glClear(GL_COLOR_BUFFER_BIT);

      // Render previous pass (bg)
      glUseProgram(s_prg_pass_trough);
      glUniform1i(glGetUniformLocation(s_prg_pass_trough, "uTexture"), 0);

      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, tx_src);

      glBindVertexArray(s_va_quad);
      glDrawArrays(GL_TRIANGLES, 0, 6);

      // Rendering falling strings
      render_layer(current_grid, view_width, view_height);

      s_blur_filter->apply(tx_dst, w / s_blur_scale, h / s_blur_scale, (1.0f - s_depth_layers[i]) * s_blur_str_multiplier, 1);

      std::swap(tx_dst, tx_src);
    }

    // Render background + top layer
    {

      glBindFramebuffer(GL_FRAMEBUFFER, s_tx_final_render);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_tx_final_render, 0);

      glViewport(0, 0, w, h);

      glClearColor(0, 0, 0, 1);
      glClear(GL_COLOR_BUFFER_BIT);

      glUseProgram(s_prg_pass_trough);
      glUniform1i(glGetUniformLocation(s_prg_pass_trough, "uTexture"), 0);

      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, tx_src);

      glBindVertexArray(s_va_quad);
      glDrawArrays(GL_TRIANGLES, 0, 6);

      render_layer(s_grids.back(), view_width, view_height);
    }

    const auto tx_bloom = s_fx_bloom->compute(s_tx_final_render, s_bloom_threshold, s_bloom_knee);

    // Draw to screen
    {
      enable_scope scope({GL_BLEND, GL_FRAMEBUFFER_SRGB});

      glBindFramebuffer(GL_FRAMEBUFFER, 0);

      glDisable(GL_BLEND);
      glEnable(GL_FRAMEBUFFER_SRGB);

      glViewport(0, 0, w, h);

      glClearColor(0, 0, 0, 1);
      glClear(GL_COLOR_BUFFER_BIT);

      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, s_tx_final_render);

      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, tx_bloom);

      glUseProgram(s_prg_hdr);
      glUniform1i(glGetUniformLocation(s_prg_hdr, "uTexture"), 0);
      glUniform1i(glGetUniformLocation(s_prg_hdr, "uBloom"), 1);
      glUniform1f(glGetUniformLocation(s_prg_hdr, "uExposure"), s_exposure);

      glBindVertexArray(s_va_quad);
      glDrawArrays(GL_TRIANGLES, 0, 6);
    }

#ifdef DEBUG
    // Debug GUI
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGui::Begin("Debug");
    ImGui::DragFloat("Exposure", &s_exposure, 0.01f, 0.1f, 10.0f);
    ImGui::DragFloat("Bloom Threshold", &s_bloom_threshold, 0.01f, 0.1f, 5.0f);
    ImGui::DragFloat("Bloom Knee", &s_bloom_knee, 0.0f, 0.0f, 0.5f);
    ImGui::DragFloat("Blur Str Multiplier", &s_blur_str_multiplier, 0.0f, 0.0f, 1.0f);

    for (std::size_t i = 0; i < s_color_palette.colors.size(); ++i)
    {
      char buffer[20];
      sprintf(buffer, "Palette #%ld", i);
      ImGui::ColorEdit3(buffer, s_color_palette.colors[i].components.data(), ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
    }

    ImGui::End();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

#endif
  }

  static void resize()
  {

    const auto [w, h] = get_window_size();
    const auto [vw, vh] = get_view_size();

    // Initilize falling strings
    for (auto &s : s_falling_strings)
      init_falling_string(s, vh);

    glBindTexture(GL_TEXTURE_2D, s_tx_final_render);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);

    for (const auto tx : {s_tx_blur0, s_tx_blur1})
    {
      glBindTexture(GL_TEXTURE_2D, tx);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w / s_blur_scale, h / s_blur_scale, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);

      glBindFramebuffer(GL_FRAMEBUFFER, s_fb_render_target);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tx, 0);

      glClearColor(0, 0, 0, 1);
      glClear(GL_COLOR_BUFFER_BIT);
    }

    s_fx_bloom->resize(w, h);
  }

  static void initialize()
  {

    // Init some OpenGL stuff
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

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

    // Load programs
    s_prg_strings = load_program(embed::s_vs_strings, embed::s_fs_strings);
    s_prg_pass_trough = load_program(embed::s_vs_fullscreen, embed::s_fs_pass_trough);
    s_prg_hdr = load_program(embed::s_vs_fullscreen, embed::s_fs_hdr);

    // Load font
    s_font = std::make_unique<font>();
    s_font->load(embed::s_font.data(), embed::s_font.size());

    // Blur filter
    s_blur_filter = std::make_unique<blur_filter>();

    // Bloom
    s_fx_bloom = std::make_unique<bloom>();

    // Full screen quad
    std::tie(s_va_quad, s_vb_quad) = create_full_screen_quad();

    // Render target
    glGenFramebuffers(1, &s_fb_render_target);

    // Textures
    glGenTextures(1, &s_tx_blur0);
    glBindTexture(GL_TEXTURE_2D, s_tx_blur0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenTextures(1, &s_tx_blur1);
    glBindTexture(GL_TEXTURE_2D, s_tx_blur1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenTextures(1, &s_tx_final_render);
    glBindTexture(GL_TEXTURE_2D, s_tx_final_render);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Initialize and resize stuff
    resize();

#ifdef DEBUG
    // Init Debug GUI
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    (void)io;
    ImGui_ImplGlfw_InitForOpenGL(s_window, true);
    ImGui_ImplOpenGL3_Init("#version 330");
#endif
  }

  static void terminate(const int32_t exit_code)
  {
    // Force destructors before glfwTerminate (otherwise it causes segmentaion fault)
    s_font = nullptr;
    s_blur_filter = nullptr;
    s_fx_bloom = nullptr;

    // Destroy OpenGL context and all the resources associated with it
    glfwTerminate();
    exit(exit_code);
  }

  static void on_window_resize(GLFWwindow *window, int32_t w, int32_t h) { resize(); }

  static void on_key(GLFWwindow *, std::int32_t, std::int32_t, std::int32_t, std::int32_t)
  {
    if (s_config.exit_on_input && clock_t::now() - s_start_time > s_exit_on_input_delay)
      terminate(0);
  }

  static void on_cursor_pos(GLFWwindow *, double, double)
  {
    if (s_config.exit_on_input && clock_t::now() - s_start_time > s_exit_on_input_delay)
      terminate(0);
  }

  void terminate_with_error(const std::string_view descr)
  {
    std::cerr << descr << '\n';
    terminate(-1);
  }

  void run(const launch_config &config)
  {

    s_config = config;

    /* Initialize the library */
    if (!glfwInit())
      terminate_with_error("Could not initialize GLFW");

    if (s_config.full_screen)
    {
      const auto videoMode = glfwGetVideoMode(glfwGetPrimaryMonitor());
      s_window = glfwCreateWindow(videoMode->width, videoMode->height, "Ultimate Matrix Rain", glfwGetPrimaryMonitor(), NULL);
      // TODO: this doens't seem to be working
      glfwSetInputMode(s_window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
    }
    else
    {
      s_window = glfwCreateWindow(1280, 768, "Ultimate Matrix Rain", NULL, NULL);
    }

    if (!s_window)
      terminate_with_error("Could not create window");

    /* Make the window's context current */
    glfwMakeContextCurrent(s_window);
    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);

    glfwSetWindowSizeCallback(s_window, &on_window_resize);
    glfwSetCursorPosCallback(s_window, &on_cursor_pos);
    glfwSetKeyCallback(s_window, &on_key);

    initialize();

    s_start_time = clock_t::now();
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