#include "application.h"

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

// TODO: hdr with additive coloring

namespace mr
{

  struct falling_string
  {
    std::int32_t x = 0.0f;
    float y = 0.0f;
    float speed = 0.0f;
    size_t layer_index = 0;
    std::int32_t length = 0;
  };

  static constexpr std::int32_t s_blur_scale = 2;

  static constexpr std::int32_t s_col_count = 100;
  static constexpr std::int32_t s_falling_strings_count = 1500;
  static constexpr std::int32_t s_falling_string_min_length = 5;
  static constexpr std::int32_t s_falling_string_max_length = 50;
  static constexpr std::int32_t s_falling_string_min_speed = 10;
  static constexpr std::int32_t s_falling_string_max_speed = 30;

  static constexpr std::array<float, 4> s_depth_layers = {
      0.15,
      0.30,
      0.50,
      1.00,
  };

  // TODO: revert to constexpr after
  static color_palette<1> s_color_palette = {
      vec3f{0.094f, 1.0f, 0.153f},
  };

  static constexpr std::string_view s_vs_full_screen = R"(
    #version 330

    layout(location = 0) in vec2 aUv;

    smooth out vec2 fUv;

    void main() {
      gl_Position = vec4(aUv * 2.0 - 1.0, 0.0, 1.0);
      fUv = aUv;
    }
  )";

  static constexpr std::string_view s_fs_pass_trough = R"(
    #version 330

    uniform sampler2D uTexture;

    smooth in vec2 fUv;

    out vec4 oColor;

    void main() {
      oColor = texture(uTexture, fUv);
    }  
  )";

  static constexpr std::string_view s_fs_hdr = R"(
    #version 330

    uniform sampler2D uTexture;
    uniform float uExposure;

    smooth in vec2 fUv;

    out vec4 oColor;

    void main() {
      vec3 hdrColor = texture(uTexture, fUv).rgb; 
      vec3 color = vec3(1.0) - exp(-hdrColor * uExposure);
      oColor = vec4(color, 1.0);
    }  
  )";

  static constexpr std::string_view s_vs_strings = R"(
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

  static constexpr std::string_view s_fs_strings = R"(
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

  static GLFWwindow *s_window = nullptr;

  static GLuint s_prg_hdr = 0;
  static GLuint s_prg_strings = 0;
  static GLuint s_prg_pass_trough = 0;

  static GLuint s_va = 0; // Vertex Array
  static GLuint s_vb = 0; // Vertex Buffer

  static GLuint s_fb_render_target = 0;

  // Final render texture
  static GLuint s_tx_final_render = 0;

  // Blur textures
  static GLuint s_tx_blur0 = 0;
  static GLuint s_tx_blur1 = 0;

  static GLuint s_va_quad = 0;
  static GLuint s_vb_quad = 0;

  // Shader params
  static float s_exposure = 1.0f;
  static float s_blur_color_scale = 0.95f;

  // Data
  static std::array<std::vector<grid_cell>, s_depth_layers.size()> s_grids;
  static std::array<falling_string, s_falling_strings_count> s_falling_strings;
  static std::unique_ptr<font> s_font;
  static std::unique_ptr<blur_filter> s_blur_filter;

  // Randomly initialize a falling string. I just use the stantard rand(), it's enough for this project
  static void init_falling_string(falling_string &s)
  {
    static constexpr auto get_random_layer = +[]() -> std::size_t
    {
      float t = rand() / float(RAND_MAX);
      return static_cast<std::size_t>(t * t * t * s_depth_layers.size());
    };

    s.layer_index = get_random_layer();
    s.speed = s_falling_string_min_speed + rand() % (s_falling_string_max_speed - s_falling_string_min_speed);
    s.length = s_falling_string_min_length + rand() % (s_falling_string_max_length - s_falling_string_min_length);
    s.x = (rand() % s_col_count) / s_depth_layers[s.layer_index];

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
      cell.set_color({s_color_palette.get(t), t * depth});
      cell.set_glyph(get_random_glyph(s.x, y, depth));
      cell.set_position(vec2f{float(s.x), float(y)} * cell_size, cell_size);

      s_grids[s.layer_index].push_back(cell);
    }

    // Move this string down
    s.y += dt * s.speed;

    // If the string is out of screen, reset it
    if (min_y * cell_size >= view_height)
      init_falling_string(s);
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

      s_blur_filter->apply(tx_dst, w / s_blur_scale, h / s_blur_scale, 1, s_blur_color_scale);

      std::swap(tx_dst, tx_src);
    }

    // Render to final buffer
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

    // Draw to screen
    {
      enable_scope scope({GL_BLEND, GL_FRAMEBUFFER_SRGB});
      glDisable(GL_BLEND);
      glEnable(GL_FRAMEBUFFER_SRGB);

      glBindFramebuffer(GL_FRAMEBUFFER, 0);

      glViewport(0, 0, w, h);

      glClearColor(0, 0, 0, 1);
      glClear(GL_COLOR_BUFFER_BIT);

      glUseProgram(s_prg_hdr);
      glUniform1i(glGetUniformLocation(s_prg_hdr, "uTexture"), 0);
      glUniform1f(glGetUniformLocation(s_prg_hdr, "uExposure"), s_exposure);

      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, s_tx_final_render);

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
    ImGui::DragFloat("Blur Color Scale", &s_blur_color_scale, 0.01f, 0.1f, 1.0f);

    for(std::size_t i = 0; i < s_color_palette.colors.size(); ++i)
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

    glBindTexture(GL_TEXTURE_2D, s_tx_final_render);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    // TODO: replace this shit with framebuffer + clear
    std::vector<std::uint8_t> data(w * h * 4);
    std::ranges::fill(data, 0);

    for (const auto tx : {s_tx_blur0, s_tx_blur1})
    {
      glBindTexture(GL_TEXTURE_2D, tx);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w / s_blur_scale, h / s_blur_scale, 0, GL_RGBA, GL_UNSIGNED_BYTE, data.data());
    }
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
    s_prg_strings = load_program(s_vs_strings, s_fs_strings);
    s_prg_pass_trough = load_program(s_vs_full_screen, s_fs_pass_trough);
    s_prg_hdr = load_program(s_vs_full_screen, s_fs_hdr);

    // Load font
    s_font = std::make_unique<font>();
    s_font->load("assets/font.ttf");

    // Blur filter
    s_blur_filter = std::make_unique<blur_filter>();

    // Full screen quad
    std::tie(s_va_quad, s_vb_quad) = create_full_screen_quad();

    // Render target
    glGenFramebuffers(1, &s_fb_render_target);

    // Textures
    glGenTextures(1, &s_tx_blur0);
    glBindTexture(GL_TEXTURE_2D, s_tx_blur0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glGenTextures(1, &s_tx_blur1);
    glBindTexture(GL_TEXTURE_2D, s_tx_blur1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glGenTextures(1, &s_tx_final_render);
    glBindTexture(GL_TEXTURE_2D, s_tx_final_render);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // Initilize falling strings
    for (auto &s : s_falling_strings)
      init_falling_string(s);

    // Initialize and resize stuff
    resize();

#ifdef DEBUG
    // Init Debug GUI
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui_ImplGlfw_InitForOpenGL(s_window, true);
    ImGui_ImplOpenGL3_Init("#version 330");
#endif

  }

  static void on_window_resize(GLFWwindow *window, int32_t w, int32_t h) { resize(); }

  static void terminate(const int32_t exit_code)
  {
    s_font = nullptr;
    s_blur_filter = nullptr;

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
    s_window = glfwCreateWindow(1280, 768, "Matrix Rain", NULL, NULL);
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