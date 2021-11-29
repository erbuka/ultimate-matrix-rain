workspace "MatrixRain"
    architecture "x86_64"
    configurations { "Debug", "Release" }
    startproject "MatrixRain"
    location(_ACTION)

    filter "configurations:Debug"
        symbols "On"
        optimize "Off"

    filter "configurations:Release"
        symbols "Off"
        optimize "On"


    filter "system:windows"
        systemversion "latest"

project "Glad"
    location(_ACTION)
    kind "StaticLib"
    language "C"

    objdir "bin-int/%{cfg.buildcfg}/%{prj.name}"
    targetdir "bin/%{cfg.buildcfg}/%{prj.name}"
    debugdir "bin/%{cfg.buildcfg}/%{prj.name}"

    includedirs { "vendor/glad/include" }
    files { "vendor/glad/src/**.c" }

project "GLFW"
    location(_ACTION)
    kind "StaticLib"
    language "C"

    objdir "bin-int/%{cfg.buildcfg}/%{prj.name}"
    targetdir "bin/%{cfg.buildcfg}/%{prj.name}"
    debugdir "bin/%{cfg.buildcfg}/%{prj.name}"

    defines { "GLFW_STATIC", "_CRT_SECURE_NO_WARNINGS" }

    includedirs {
        "vendor/glfw/src"
    }
    files {
        "vendor/glfw/src/egl_context.c",
        "vendor/glfw/src/platform.c",
        "vendor/glfw/src/context.c",
        "vendor/glfw/src/init.c",
        "vendor/glfw/src/input.c",
        "vendor/glfw/src/monitor.c",
        "vendor/glfw/src/osmesa_context.c",
        "vendor/glfw/src/vulkan.c",
        "vendor/glfw/src/window.c",
        "vendor/glfw/src/xkb_unicode.c",
        "vendor/glfw/src/null_init.c",
        "vendor/glfw/src/null_monitor.c", 
        "vendor/glfw/src/null_window.c", 
        "vendor/glfw/src/null_joystick.c"
    }

    filter "system:windows"
        defines { "_GLFW_WIN32" }
        files {
            "vendor/glfw/src/wgl_context.c",
            "vendor/glfw/src/win32_module.c",
            "vendor/glfw/src/win32_init.c",
            "vendor/glfw/src/win32_joystick.c",
            "vendor/glfw/src/win32_monitor.c",
            "vendor/glfw/src/win32_thread.c",
            "vendor/glfw/src/win32_time.c",
            "vendor/glfw/src/win32_window.c",
        }

    filter "system:linux"
        defines { "_GLFW_X11" }
        files {
            "vendor/glfw/src/glx_context.c",
            "vendor/glfw/src/x11_init.c",
            "vendor/glfw/src/x11_monitor.c",
            "vendor/glfw/src/x11_window.c",
            "vendor/glfw/src/posix_module.c",
            "vendor/glfw/src/posix_thread.c",
            "vendor/glfw/src/posix_time.c",
            "vendor/glfw/src/linux_joystick.c",
        }

project "MatrixRain"
    location(_ACTION)
    language "C++"
    cppdialect "C++20"
    kind "ConsoleApp"

    objdir "bin-int/%{cfg.buildcfg}/%{prj.name}"
    targetdir "bin/%{cfg.buildcfg}/%{prj.name}"
    debugdir "bin/%{cfg.buildcfg}/%{prj.name}"

    includedirs {
        "src",
        "vendor/glad/include",
        "vendor/glfw/include"
    }

    files { "src/**.cpp" }

    links { "Glad", "GLFW" }

    postbuildcommands {
        "{COPY} ../src/assets ../bin/%{cfg.buildcfg}/%{prj.name}/assets"
    }

    filter "system:windows"
        links { "opengl32" }

    filter "system:linux"
        links { "GL", "X11", "pthread", "dl" }