project "TuneBloom"
language "C++"
cppdialect "C++20"

staticruntime "on"
exceptionhandling "off"
rtti "off"
-- fatalwarnings { "all" } -- TODO

targetdir "bin/%{prj.name}-%{cfg.platform}-%{cfg.buildcfg}/out"
objdir "bin/%{prj.name}-%{cfg.platform}-%{cfg.buildcfg}/int"
debugdir "../workdir"

local thirdPartyLicense = path.getabsolute(path.join(_SCRIPT_DIR, "../THIRD_PARTY_LICENSES.txt"))
postbuildcommands {
    '{COPYFILE} "' .. thirdPartyLicense .. '" "%{cfg.targetdir}"',
}

includedirs {
    path.join(_SCRIPT_DIR, "include"),
    path.join(_SCRIPT_DIR, "include/imgui"),
    path.join(_SCRIPT_DIR, "include/snd-ply"),

    path.join(_SCRIPT_DIR, "vendor/sead/include"),
    path.join(_SCRIPT_DIR, "vendor/sead/libs/glad/include"),
    path.join(_SCRIPT_DIR, "vendor/sead/libs/glfw/include"),

    path.join(_SCRIPT_DIR, "vendor/lz4/lib"),
    path.join(_SCRIPT_DIR, "vendor/zstd/lib"),
    path.join(_SCRIPT_DIR, "vendor/opus/include"),
}

files {
    "src/**.cpp"
}

removefiles {
    "src/platform/macos/**"
}

links {
    "sead",
    "lz4",
    "zstd",
    "opus",
}

filter "action:gmake*"
pchheader "include/Precompiled.h"

filter { "action:gmake*", "files:**.mm" }
enablepch "Off"

filter {}

if os.getenv("COMMIT_SHA") then
    filter "files:src/app/Utilll.cpp"
    defines { "COMMIT_SHA=\"" .. string.sub(os.getenv("COMMIT_SHA"), 1, 7) .. "\"" }
    filter {}
end

filter "system:windows"
systemversion "latest"

defines { "SEAD_PLATFORM_WINDOWS", "_CRT_SECURE_NO_WARNINGS" }

links { "winmm" }

files { "src/platform/resource.rc" }

filter "system:linux"
systemversion "latest"

defines { "SEAD_PLATFORM_POSIX", "SEAD_PLATFORM_LINUX" }

links { "asound" }

filter "system:macosx"
systemversion "14.0"

defines { "SEAD_PLATFORM_POSIX", "SEAD_PLATFORM_MACOSX" }

files {
    "src/platform/macos/**.mm",
    "src/platform/macos/**.cpp",
}

links {
    "Cocoa.framework",
    "IOKit.framework",
    "CoreVideo.framework",
    "OpenGL.framework",
    "QuartzCore.framework",
    "UniformTypeIdentifiers.framework",
    "CoreMIDI.framework"
}

filter "platforms:GLFW_*"
defines {
    "SEAD_PLATFORM_GLFW",
    "SEAD_USE_GL",
}

includedirs {
    path.join(_SCRIPT_DIR, "libs/sead/libs/glad/include"),
    path.join(_SCRIPT_DIR, "libs/sead/libs/glfw/include"),
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

filter { "system:macosx", "configurations:Release" }
linktimeoptimization "off"
buildoptions { "-flto=thin" }
linkoptions {
    "-flto=thin",
    "-Wl,-cache_path_lto," .. path.join(_SCRIPT_DIR, "bin/lto-cache"),
    "-Wl,-export_dynamic",
}

local bundleScript = path.getabsolute(path.join(_SCRIPT_DIR, "src/platform/macos/make_bundle.sh"))
postbuildcommands {
    '"' .. bundleScript .. '" "%{cfg.targetdir}" "%{cfg.buildtarget.abspath}"',
}

group "Dependencies"
include "vendor/sead"
include "vendor/lz4-build"
include "vendor/zstd-build"
include "vendor/opus-build"
group ""
