#pragma once


#include <glad/glad.h>

namespace mr
{
  class blur_filter
  {
  private:
    GLuint m_framebuffer;
  public:
    blur_filter();
    ~blur_filter();
    void apply(GLuint src, GLuint dst);
  };

}