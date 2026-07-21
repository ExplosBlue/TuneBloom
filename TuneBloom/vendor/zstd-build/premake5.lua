-- Build wrapper for zstd submodule (vendor/zstd)
project "zstd"
language "C"
kind "StaticLib"

staticruntime "on"
warnings "off"

targetdir "%{wks.location}/bin/%{prj.name}-%{cfg.platform}-%{cfg.buildcfg}/out"
objdir "%{wks.location}/bin/%{prj.name}-%{cfg.platform}-%{cfg.buildcfg}/int"

includedirs {
    "../zstd/lib",
    "../zstd/lib/common",
}

defines {
    "ZSTD_DISABLE_ASM",
}

files {
    "../zstd/lib/zstd.h",
    "../zstd/lib/zstd_errors.h",
    "../zstd/lib/common/*.c",
    "../zstd/lib/common/*.h",
    "../zstd/lib/compress/*.c",
    "../zstd/lib/compress/*.h",
    "../zstd/lib/decompress/*.c",
    "../zstd/lib/decompress/*.h",
}

filter "configurations:Debug"
runtime "debug"
optimize "off"
symbols "on"

filter "configurations:Develop"
runtime "release"
optimize "speed"

filter "configurations:Release"
runtime "release"
optimize "speed"
