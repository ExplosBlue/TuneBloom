-- Build wrapper for the opus submodule (vendor/opus)
project "opus"
language "C"
kind "StaticLib"

staticruntime "on"
warnings "off"

targetdir "%{wks.location}/bin/%{prj.name}-%{cfg.platform}-%{cfg.buildcfg}/out"
objdir "%{wks.location}/bin/%{prj.name}-%{cfg.platform}-%{cfg.buildcfg}/int"

includedirs {
    "../opus/include",
    "../opus/celt",
    "../opus/silk",
    "../opus/silk/float",
    "../opus/src",
}

defines {
    "OPUS_BUILD",
    "USE_ALLOCA",
    "HAVE_LRINTF",
    "HAVE_LRINT",
}

files {
    "../opus/src/*.c",
    "../opus/src/*.h",
    "../opus/celt/*.c",
    "../opus/celt/*.h",
    "../opus/silk/*.c",
    "../opus/silk/*.h",
    "../opus/silk/float/*.c",
    "../opus/silk/float/*.h",
    "../opus/include/*.h",
}

removefiles {
    "../opus/src/opus_demo.c",
    "../opus/src/repacketizer_demo.c",
    "../opus/src/opus_compare.c",
    "../opus/celt/opus_custom_demo.c",
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
