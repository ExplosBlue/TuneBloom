#pragma once

#include <snd/snd_SoundArchive.h>

#include <string>

class InnerFile;

struct File
{
    File()
        : id(nw::snd::SoundArchive::INVALID_ID)
        , innerFile(nullptr)
        , includeInBfsar(false)
        , external(false)
        , externalPath()
    {
    }

    File(u32 id_, const InnerFile* innerFile_, bool includeInBfsar_)
        : id(id_)
        , innerFile(innerFile_)
        , includeInBfsar(includeInBfsar_)
        , external(false)
        , externalPath()
    {
    }

    File(u32 id_, const std::string& externalPath_)
        : id(id_)
        , innerFile(nullptr)
        , includeInBfsar(false)
        , external(true)
        , externalPath(externalPath_)
    {
    }

    u32 id;
    const InnerFile* innerFile;
    bool includeInBfsar;
    bool external;
    std::string externalPath;
};
