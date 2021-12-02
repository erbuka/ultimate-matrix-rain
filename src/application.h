#pragma once


#include <string_view>


namespace mr
{
  void run(); 
  void terminate_with_error(const std::string_view descr);
}