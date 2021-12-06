#include "application.h"

#include <thread>
#include <memory>
#include <numeric>
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

  using namespace std::literals::string_view_literals;

  // Types
  struct falling_string
  {
    std::int32_t x = 0;
    float y = 0.0f;
    float speed = 0.0f;
    size_t layer_index = 0;
    std::int32_t length = 0;
  };

  struct terminal_state
  {
    std::size_t cur_line = 0;
    std::size_t cur_char = 0;
    float timer = 0.5f;
    bool done = false; // TODO: use global state
  };

  using clock_t = std::chrono::high_resolution_clock;

  // Function declarations
  static auto get_window_size();
  static auto get_view_size();
  static auto get_mouse_pos();

  static const glyph &get_random_glyph(const std::int32_t x, const std::int32_t y);
  static void init_falling_string(falling_string &s, const float view_height);
  static void update_falling_string(falling_string &s, const float dt, const float view_width, const float view_height);

  static void render_debug_gui();
  static void render_characters(const std::vector<character_cell> &cells, const font& font, const float view_width, const float view_height);
  static void render_terminal(const float dt);
  static void render_code(const float dt);
  static void render_hdr_to_screen(const GLuint tx_base, const GLuint tx_bloom);

  static void resize();
  static void initialize();
  static void terminate(const int32_t exit_code);
  static void on_window_resize(GLFWwindow *, std::int32_t, std::int32_t);
  static void on_key(GLFWwindow *, std::int32_t, std::int32_t, std::int32_t, std::int32_t);

  // Fixed animation settings
  static constexpr float s_glyph_swaps_per_second = 10.0f; // Number of glyphs swapped per second (roughly)
  static constexpr std::int32_t s_blur_scale = 1;
  static constexpr std::int32_t s_col_count = 80;
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

  static constexpr std::array s_terminal_lines = {
      "Wake up Neo"sv,
      "The Matrix has you"sv,
      "Follow the white rabbit"sv};

  static constexpr auto s_exit_on_input_delay = std::chrono::milliseconds(1500);

  static GLFWwindow *s_window = nullptr;

  // Programs
  static GLuint s_prg_hdr = 0;
  static GLuint s_prg_strings = 0;
  static GLuint s_prg_pass_trough = 0;

  static GLuint s_va = 0; // Vertex Array
  static GLuint s_vb = 0; // Vertex Buffer

  // Generic framebuffer to render to a texture
  static GLuint s_fb_render_target = 0;

  // Final render texture
  static GLuint s_tx_final_render = 0;

  // Blur textures
  static GLuint s_tx_blur0 = 0;
  static GLuint s_tx_blur1 = 0;

  // Full screen quad vertex array and vertex buffer
  static GLuint s_va_quad = 0;
  static GLuint s_vb_quad = 0;

  // Shader params (no constexpr because I need to tune them with ImGui)
  static float s_exposure = 1.0f;
  static float s_bloom_threshold = 0.7f;
  static float s_bloom_knee = 0.5f;
  static float s_blur_str_multiplier = 0.5f;

  // Colors
  static vec3f s_string_color = {0.1f, 1.5f, 0.2f};
  static vec3f s_string_head_color = {0.7f, 1.0f, 0.7f};

  // Config
  static launch_config s_config;
  static clock_t::time_point s_start_time;

  // Data
  static terminal_state s_terminal_state;
  static std::vector<character_cell> s_terminal_cells;
  static std::array<std::vector<character_cell>, s_depth_layers.size()> s_grids;
  static std::array<falling_string, s_falling_strings_count> s_falling_strings;
  static std::unique_ptr<font> s_font, s_terminal_font;
  static std::unique_ptr<blur_filter> s_blur_filter;
  static std::unique_ptr<bloom> s_fx_bloom;

  static void init_falling_string(falling_string &s, const float view_height)
  {
    const float t = rng::next();

    s.layer_index = static_cast<std::size_t>(t * t * s_depth_layers.size());
    s.speed = rng::next(s_falling_string_min_speed, s_falling_string_max_speed);
    s.length = rng::next(s_falling_string_min_length, s_falling_string_max_length);

    // Number of columns depends on the depth
    const int32_t col_count = s_col_count / s_depth_layers[s.layer_index];
    s.x = rng::next(0, col_count);

    // The initial y position is actually randomized to be off screen.
    s.y = -(s.length + rng::next(0, static_cast<std::int32_t>(view_height / s_depth_layers[s.layer_index])));
  }

  static auto get_window_size()
  {
    std::int32_t x, y;
    glfwGetWindowSize(s_window, &x, &y);
    return std::tuple{float(x), float(y)};
  }

  static auto get_view_size()
  {
    const auto [w, h] = get_window_size();
    return std::tuple{static_cast<std::int32_t>(s_col_count), static_cast<std::int32_t>(h / w * s_col_count)};
  }

  [[maybe_unused]] static auto get_mouse_pos()
  {
    double x, y;
    glfwGetCursorPos(s_window, &x, &y);
    return std::tuple{std::int32_t(x), std::int32_t(y)};
  }

  static const glyph &get_random_glyph(const std::int32_t x, const std::int32_t y)
  {
    // This numbers are completely made up. Worst hash function ever
    constexpr std::size_t s0 = 2836, s1 = 23873;
    const auto &glyphs = s_font->get_glyphs();
    const auto idx = static_cast<std::size_t>(x * s0 + y * s1) % glyphs.size();
    return glyphs[idx];
  }

  static void update_falling_string(falling_string &s, const float dt, const float view_width, const float view_height)
  {
    const float depth = s_depth_layers[s.layer_index];
    const float cell_size = view_width / s_col_count * depth;
    const std::int32_t max_y = std::round(s.y);
    const std::int32_t min_y = max_y - s.length + 1;

    // Update all the characters besides the head (y < max_y)
    for (std::int32_t y = min_y; y < max_y; ++y)
    {
      const float t = float(y - min_y) / (max_y - min_y);

      character_cell cell;
      cell.set(get_random_glyph(s.x, y),
               {s_string_color * t, t * s_depth_layers_fade[s.layer_index]},
               vec2f{float(s.x), float(y)} * cell_size,
               cell_size);

      s_grids[s.layer_index].push_back(cell);
    }

    // Set the head color (y == max_y)
    // For the head I use alpha = 1.0f for every layer
    character_cell head;
    head.set(get_random_glyph(s.x, max_y),
             {s_string_head_color, 1.0f},
             vec2f{float(s.x), float(max_y)} * cell_size,
             cell_size);

    s_grids[s.layer_index].push_back(head);

    // Move this string down
    s.y += dt * s.speed;

    // If the string is out of screen, reset it
    if (min_y * cell_size >= view_height)
      init_falling_string(s, view_height);
  }

  static void render_debug_gui()
  {
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
    ImGui::ColorEdit3("String Color", s_string_color.components.data(), ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
    ImGui::ColorEdit3("String Head Color", s_string_head_color.components.data(), ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);

    ImGui::End();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

#endif
  }

  static void render_hdr_to_screen(const GLuint tx_base, const GLuint tx_bloom)
  {
    auto [w, h] = get_window_size();
    // Draw to screen
    enable_scope scope({GL_BLEND, GL_FRAMEBUFFER_SRGB});

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glDisable(GL_BLEND);
    glEnable(GL_FRAMEBUFFER_SRGB);

    glViewport(0, 0, w, h);

    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tx_base);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, tx_bloom);

    glUseProgram(s_prg_hdr);
    glUniform1i(glGetUniformLocation(s_prg_hdr, "uTexture"), 0);
    glUniform1i(glGetUniformLocation(s_prg_hdr, "uBloom"), 1);
    glUniform1f(glGetUniformLocation(s_prg_hdr, "uExposure"), s_exposure);

    glBindVertexArray(s_va_quad);
    glDrawArrays(GL_TRIANGLES, 0, 6);
  }

  static void render_terminal(const float dt)
  {

    static constexpr float s_font_size = 1.0f;

    auto [w, h] = get_window_size();
    auto [vw, vh] = get_view_size();

    auto &state = s_terminal_state;

    // Update timer and state
    state.timer = std::max(0.0f, state.timer - dt);

    // Fill the string
    s_terminal_cells.clear();

    const std::string_view current_line{s_terminal_lines[state.cur_line].data(), state.cur_char + 1};
    float str_width = 0.0f;
    for (const auto ch : current_line)
      str_width += s_terminal_font->find_glyph(ch).norm_advance * s_font_size;

    vec2f pos = vec2f{vw - str_width, vh - s_font_size} / 2.0f;

    for (auto ch : std::string_view(s_terminal_lines[state.cur_line].data(), state.cur_char + 1))
    {
      const auto &g = s_terminal_font->find_glyph(ch);
      character_cell cell;
      cell.set(g, {s_string_color, 1.0f}, pos, s_font_size);
      s_terminal_cells.push_back(std::move(cell));
      pos[0] += g.norm_advance * s_font_size;
    }

    // Render

    glBindFramebuffer(GL_FRAMEBUFFER, s_fb_render_target);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_tx_final_render, 0);

    glViewport(0, 0, w, h);

    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);

    render_characters(s_terminal_cells, *(s_terminal_font.get()), vw, vh);

    auto tx_bloom = s_fx_bloom->compute(s_tx_final_render, s_bloom_threshold, s_bloom_knee);

    // Draw to screen
    render_hdr_to_screen(s_tx_final_render, tx_bloom);

    if (state.timer == 0.0f)
    {

      // Check done
      if (state.cur_line == s_terminal_lines.size() - 1 && state.cur_char == s_terminal_lines[state.cur_line].size() - 1)
      {
        state.done = true;
        return;
      }

      state.cur_char++;
      if (state.cur_char == s_terminal_lines[state.cur_line].size())
      {
        state.cur_char = 0;
        state.cur_line++;
        state.timer = 0.1f;
      }
      else if (state.cur_char == s_terminal_lines[state.cur_line].size() - 1)
      {
        state.timer = 1.5f;
      }
      else
      {
        state.timer = 0.1f;
      }
    }
  }

  static void render_characters(const std::vector<character_cell> &cells, const font& font, const float view_width, const float view_height)
  {
    // Rendering falling strings
    glUseProgram(s_prg_strings);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, font.get_texture());

    glUniform1f(glGetUniformLocation(s_prg_strings, "uScreenWidth"), view_width);
    glUniform1f(glGetUniformLocation(s_prg_strings, "uScreenHeight"), view_height);
    glUniform1i(glGetUniformLocation(s_prg_strings, "uFont"), 0);

    glBindVertexArray(s_va);
    glBindBuffer(GL_ARRAY_BUFFER, s_vb);
    glBufferData(GL_ARRAY_BUFFER, sizeof(character_cell) * cells.size(), cells.data(), GL_DYNAMIC_DRAW);

    glDrawArrays(GL_TRIANGLES, 0, cells.size() * 6);
  }

  // TODO: implement scanlines (like old terminal)?
  static void render_code(const float dt)
  {
    const auto [w, h] = get_window_size();

    constexpr float view_width = s_col_count;
    const float view_height = h / w * view_width;

    // Clear grids
    for (auto &g : s_grids)
      g.clear();

    // Swap some glyphs
    // TODO: It's not 100% correct but it's ok
    if (rng::next() < s_glyph_swaps_per_second * dt)
      s_font->swap_glyphs(1);

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

    // Render and blur background layers (size - 1) to a texture

    for (size_t i = 0; i < s_depth_layers.size() - 1; ++i)
    {
      const auto &current_grid = s_grids[i];

      glBindFramebuffer(GL_FRAMEBUFFER, s_fb_render_target);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tx_dst, 0);

      glViewport(0, 0, w / s_blur_scale, h / s_blur_scale);

      glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
      glClear(GL_COLOR_BUFFER_BIT);

      // Render previous pass as background
      glUseProgram(s_prg_pass_trough);
      glUniform1i(glGetUniformLocation(s_prg_pass_trough, "uTexture"), 0);

      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, tx_src);

      glBindVertexArray(s_va_quad);
      glDrawArrays(GL_TRIANGLES, 0, 6);

      // Render the current layer
      render_characters(current_grid, *(s_font.get()), view_width, view_height);

      // Blur the current texture
      s_blur_filter->apply(tx_dst, (1.0f - s_depth_layers[i]) * s_blur_str_multiplier, 1);

      std::swap(tx_dst, tx_src);
    }

    // Render background + top layer
    {

      glBindFramebuffer(GL_FRAMEBUFFER, s_fb_render_target);
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

      render_characters(s_grids.back(), *(s_font.get()), view_width, view_height);
    }

    const auto tx_bloom = s_fx_bloom->compute(s_tx_final_render, s_bloom_threshold, s_bloom_knee);

    render_hdr_to_screen(s_tx_final_render, tx_bloom);
  }

  static void resize()
  {

    const auto [w, h] = get_window_size();
    const auto [vw, vh] = get_view_size();

    // (Re)Initilize falling strings
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
    s_blur_filter->resize(w / s_blur_scale, h / s_blur_scale);
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

    // TODO: this is interesting. So basically, on integrated cards, if I don't preallocate this buffer,
    // memory is going to grow over 1 GB in the first 20 seconds of the program, and then decreses slowly to 100mb. Probably if the buffer
    // is not big enough, it keeps reallocating it and then the memory is freed after a few seconds.
    // Anyway, this should be fixed by initially allocating a buffer that is big enough to contain all the geometry.
    // So here it is. I'm overshooting a bit, but I'm sure that this is enough.
    constexpr std::size_t prealloc_size = 6 * sizeof(vertex) * s_falling_strings_count * (s_falling_string_max_length + 1);
    glBufferData(GL_ARRAY_BUFFER, prealloc_size, nullptr, GL_DYNAMIC_DRAW);

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

    // Load fonts
    s_font = std::make_unique<font>();
    s_font->load(embed::s_font.data(), embed::s_font.size());

    s_terminal_font = std::make_unique<font>();
    s_terminal_font->load(embed::s_terminal_font.data(), embed::s_terminal_font.size());

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
    // Force destructors before glfwTerminate (otherwise they cause segmentation fault)
    s_terminal_font = nullptr;
    s_font = nullptr;
    s_blur_filter = nullptr;
    s_fx_bloom = nullptr;

    // Destroy window, OpenGL context and all the resources associated with it
    glfwTerminate();
    exit(exit_code);
  }

  static void on_window_resize(GLFWwindow *, std::int32_t, std::int32_t) { resize(); }

  static void on_key(GLFWwindow *, std::int32_t, std::int32_t, std::int32_t, std::int32_t)
  {
    if (s_config.exit_on_input && clock_t::now() - s_start_time > s_exit_on_input_delay)
      terminate(0);
  }

  static void on_cursor_pos(GLFWwindow *, double, double)
  {
    // This event is fired immeditately even if the mouse doesn't move, so wait a little bit before actually considering it
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
      s_window = glfwCreateWindow(videoMode->width, videoMode->height, "Ultimate Matrix Rain", glfwGetPrimaryMonitor(), nullptr);
      glfwSetInputMode(s_window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN); // TODO: this doesn't seem to be working
    }
    else
    {
      s_window = glfwCreateWindow(1280, 768, "Ultimate Matrix Rain", nullptr, nullptr);
    }

    if (!s_window)
      terminate_with_error("Could not create window");

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
      // render(delta.count());
      if (s_terminal_state.done)
        render_code(delta.count());
      else
        render_terminal(delta.count());

      render_debug_gui();

      /* Swap front and back buffers */
      glfwSwapBuffers(s_window);

      /* Poll for and process events */
      glfwPollEvents();
    }

    terminate(0);
  }
}