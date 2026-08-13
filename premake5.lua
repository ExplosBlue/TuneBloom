-- premake5.lua
workspace "TuneBloom"
    if _ACTION == "xcode4" then
        platforms {
            "GLFW_ARM64",
        }
    else
        platforms {
            "GLFW_x86",
            "GLFW_x86_64",
            "GLFW_ARM32",
            "GLFW_ARM64",
        }
    end

    configurations {
        "Debug",
        "Develop",
        "Release",
    }

    startproject "TuneBloom"

    toolset "clang"
    stl "libc++"

    filter "toolset:clang"
        -- TODO: Remove
        buildoptions {
        -- suppressed errors
            "-Wno-invalid-offsetof",
            "-Wno-undefined-var-template",
            "-Wno-missing-braces",
        -- keep, but as warnings
            "-Wno-error=switch",
            "-Wno-error=unused-private-field",
            "-Wno-error=unused-const-variable",
            "-Wno-error=logical-op-parentheses",
            "-Wno-error=bitwise-op-parentheses",
            "-Wno-error=delete-non-abstract-non-virtual-dtor",
        }

    filter "platforms:*_x86"
        architecture "x86"
        stl "gnu"

    filter "platforms:*_x86_64"
        architecture "x86_64"
        vectorextensions "AVX2"

    filter "platforms:*_ARM32"
        architecture "ARM"

    filter "platforms:*_ARM64"
        architecture "ARM64"

    filter "action:xcode4"
        xcodebuildsettings {
            ["ALWAYS_SEARCH_USER_PATHS"] = "YES",
            ["GCC_WARN_64_TO_32_BIT_CONVERSION"] = "NO",
        }

include "TuneBloom"
