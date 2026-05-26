project "TuneBloom"
    language "C++"
    cppdialect "C++20"

    multiprocessorcompile "on"
    staticruntime "on"
    exceptionhandling "off"
    rtti "off"
    -- fatalwarnings { "all" } -- TODO

    targetdir "bin/%{prj.name}-%{cfg.platform}-%{cfg.buildcfg}/out"
    objdir "bin/%{prj.name}-%{cfg.platform}-%{cfg.buildcfg}/int"
    debugdir "../workdir"

    includedirs {
        "include",
        "include/imgui",
        "include/snd-ply",

        "vendor/sead/include",
        "vendor/sead/libs/glad/include",
        "vendor/sead/libs/glfw/include",
    }

    files {
        "src/**.cpp"
    }

    links {
        "sead",
    }

    if os.getenv("COMMIT_SHA") then
        defines { "COMMIT_SHA=\"" .. string.sub(os.getenv("COMMIT_SHA"), 1, 7) .. "\"" }
    end

    filter "system:windows"
        systemversion "latest"

        defines { "SEAD_PLATFORM_WINDOWS" }

    filter "system:linux"
        systemversion "latest"

        defines { "SEAD_PLATFORM_POSIX", "SEAD_PLATFORM_LINUX" }

    filter "system:macosx"
        systemversion "14.0"

        defines { "SEAD_PLATFORM_POSIX", "SEAD_PLATFORM_MACOSX" }
        links {
            "Cocoa.framework",
            "IOKit.framework",
            "CoreVideo.framework",
            "OpenGL.framework",
            "QuartzCore.framework",
            "UniformTypeIdentifiers.framework"
        }

    filter "platforms:GLFW_*"
        defines {
            "SEAD_PLATFORM_GLFW",
            "SEAD_USE_GL",
        }

        includedirs {
            "libs/sead/libs/glad/include",
            "libs/sead/libs/glfw/include",
        }

        links {
            -- "glad",
            "glfw",
        }

    filter "configurations:Debug"
        kind "ConsoleApp"
        defines { "SEAD_TARGET_DEBUG" }
        runtime "debug"
        optimize "off"
        symbols "on"
        linktimeoptimization "off"

    filter "configurations:Develop"
        kind "ConsoleApp"
        defines { "SEAD_TARGET_DEBUG" } -- TODO: Use SEAD_TARGET_DEVELOP
        runtime "release"
        optimize "speed"
        symbols "on"
        linktimeoptimization "off"

    filter "configurations:Release"
        kind "WindowedApp"
        defines { "SEAD_TARGET_RELEASE", "NDEBUG" }
        runtime "release"
        optimize "speed"
        symbols "off"
        linktimeoptimization "on"

    filter { "system:windows", "configurations:Release" }
        entrypoint "mainCRTStartup"

group "Dependencies"
    include "vendor/sead"
group ""
