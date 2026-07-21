-- Build wrapper for the lz4 submodule (vendor/lz4)
project "lz4"
language "C"
kind "StaticLib"

staticruntime "on"
warnings "off"

targetdir "%{wks.location}/bin/%{prj.name}-%{cfg.platform}-%{cfg.buildcfg}/out"
objdir "%{wks.location}/bin/%{prj.name}-%{cfg.platform}-%{cfg.buildcfg}/int"

includedirs {
    "../lz4/lib",
}

files {
    "../lz4/lib/*.c",
    "../lz4/lib/*.h",
}

removefiles {
    "../lz4/lib/lz4file.c", --unused
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
