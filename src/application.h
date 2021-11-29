#pragma once


#include <string_view>


namespace mr
{
  void run(); 
  void terminate_with_error(std::string_view descr);
}