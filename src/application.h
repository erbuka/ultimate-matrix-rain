#pragma once


#include <string_view>


namespace mr
{

  struct launch_config {
    bool full_screen = false;
    bool exit_on_input = false;
  };

  void run(const launch_config& config); 
  void terminate_with_error(const std::string_view descr);
}