#include "application.h"

#ifdef WIN64_SCREEN_SAVER 
#include <Windows.h>
int main(int argc, char** argv)
{

  enum class actions { launch, options, preview };

  actions action = actions::options;

  ShowWindow(GetConsoleWindow(), SW_HIDE);

  for (std::int32_t i = 1; i < argc; ++i)
  {
    const std::string_view arg{ argv[i] };
    if (arg == "/s")
    {
      action = actions::launch;
    }
    else if (arg == "/p")
    {
      action = actions::preview;
    }
    else if (arg == "/c")
    {
      action = actions::options;
    }
  }

  switch (action)
  {
  case actions::preview:
  case actions::options:
    return 0; // No preview or options
  case actions::launch:
    mr::run({
      .full_screen = true,
      .exit_on_input = true
      });
    return 0;
  }

  return -1;
}
#else
int main([[maybe_unused]] int argc, [[maybe_unused]] char** argv)
{
  mr::run({});
  return 0;
}
#endif


