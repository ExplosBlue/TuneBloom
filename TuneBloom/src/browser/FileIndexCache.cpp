#include <browser/FileIndexCache.h>

#include <core/ByteBuilder.h>
#include <core/ByteView.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

static constexpr u32 cCacheMagic         = 0x49464254;
static constexpr u32 cCacheVersion       = 4;
static constexpr u32 cCacheStringMax     = 4096;
static constexpr const char* cTempSuffix = ".tmp";

class CacheReader
{
public:
    explicit CacheReader(const std::vector<u8>& bytes)
        : mView(bytes.data(), bytes.size(), false)
    {
    }

    bool isValid() const
    {
        return mIsValid;
    }

    bool isAtEnd() const
    {
        return mOffset == mView.size();
    }

    u8 readU8()
    {
        return has(sizeof(u8)) ? mView.readU8(advance(sizeof(u8))) : 0;
    }

    u32 readU32()
    {
        return has(sizeof(u32)) ? mView.readU32(advance(sizeof(u32))) : 0;
    }

    u64 readU64()
    {
        const u64 low = readU32();
        const u64 high = readU32();

        return high << 32 | low;
    }

    std::string readString()
    {
        const u32 length = readU32();

        if (length > cCacheStringMax || !has(length))
        {
            mIsValid = false;
            return {};
        }

        const u64 offset = advance(length);

        return std::string(reinterpret_cast<const char*>(mView.at(offset)), length);
    }

    void readChars(char* out, u32 size)
    {
        const std::string text = readString();

        std::snprintf(out, size, "%s", text.c_str());
    }

private:
    bool has(u64 size)
    {
        mIsValid = mIsValid && mView.has(mOffset, size);

        return mIsValid;
    }

    u64 advance(u64 size)
    {
        const u64 offset = mOffset;
        mOffset += size;

        return offset;
    }

    ByteView mView;
    u64 mOffset = 0;
    bool mIsValid = true;
};

static void AppendU64(ByteBuilder& out, u64 value)
{
    out.appendU32(static_cast<u32>(value));
    out.appendU32(static_cast<u32>(value >> 32));
}

static void AppendString(ByteBuilder& out, const char* text)
{
    const u32 length = static_cast<u32>(std::strlen(text));

    out.appendU32(length);
    out.appendBytes(text, length);
}

static void WriteContents(ByteBuilder& out, const ArchiveContents& contents)
{
    out.appendU32(contents.soundCount);

    for (u32 count : contents.soundTypeCounts)
        out.appendU32(count);

    out.appendU32(contents.soundSetCount);
    out.appendU32(contents.bankCount);
    out.appendU32(contents.waveArchiveCount);
    out.appendU32(contents.groupCount);
    out.appendU32(contents.playerCount);
    out.appendU32(static_cast<u32>(contents.sounds.size()));

    for (const ArchiveSound& sound : contents.sounds)
    {
        AppendString(out, sound.name.c_str());
        out.appendU8(static_cast<u8>(sound.type));
    }
}

static void ReadContents(CacheReader& in, ArchiveContents* out)
{
    out->soundCount = in.readU32();

    for (u32& count : out->soundTypeCounts)
        count = in.readU32();

    out->soundSetCount = in.readU32();
    out->bankCount = in.readU32();
    out->waveArchiveCount = in.readU32();
    out->groupCount = in.readU32();
    out->playerCount = in.readU32();

    const u32 soundCount = in.readU32();

    for (u32 i = 0; i < soundCount && in.isValid(); i++)
    {
        ArchiveSound& sound = out->sounds.emplace_back();
        sound.name = in.readString();
        sound.type = static_cast<ArchiveSoundType>(std::min<u8>(in.readU8(), static_cast<u8>(ArchiveSoundType::Unknown)));
    }
}

static void WriteProbe(ByteBuilder& out, const FileProbe& probe)
{
    out.appendU8(static_cast<u8>(probe.kind));
    AppendString(out, probe.formatName);
    AppendString(out, probe.versionText);
    out.appendU8(probe.isCompressed);
    out.appendU8(probe.hasItemCount);
    out.appendU32(probe.itemCount);
    out.appendU32(probe.channelCount);
    out.appendU32(probe.sampleRate);
    out.appendU32(probe.sampleCount);
    out.appendU8(probe.isLoop);
    out.appendU8(probe.isPrefetch);
    out.appendU8(probe.isPack);
    WriteContents(out, probe.contents);
}

static void ReadProbe(CacheReader& in, FileProbe* out)
{
    out->kind = static_cast<BrowserFileKind>(std::min<u8>(in.readU8(), static_cast<u8>(BrowserFileKind::Count) - 1));
    in.readChars(out->formatName, sizeof(out->formatName));
    in.readChars(out->versionText, sizeof(out->versionText));
    out->isCompressed = in.readU8() != 0;
    out->hasItemCount = in.readU8() != 0;
    out->itemCount = in.readU32();
    out->channelCount = in.readU32();
    out->sampleRate = in.readU32();
    out->sampleCount = in.readU32();
    out->isLoop = in.readU8() != 0;
    out->isPrefetch = in.readU8() != 0;
    out->isPack = in.readU8() != 0;
    ReadContents(in, &out->contents);
}

bool LoadFileIndexCache(const std::filesystem::path& path, FileIndexCache* out)
{
    out->clear();

    std::ifstream stream(path, std::ios::binary);

    if (!stream)
        return false;

    const std::vector<u8> bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    CacheReader in(bytes);

    if (in.readU32() != cCacheMagic || in.readU32() != cCacheVersion)
        return false;

    const u32 count = in.readU32();

    for (u32 i = 0; i < count && in.isValid(); i++)
    {
        std::string key = in.readString();

        CachedFile file;
        file.size = in.readU64();
        file.modified = static_cast<s64>(in.readU64());
        file.isAudio = in.readU8() != 0;

        if (file.isAudio)
            ReadProbe(in, &file.probe);

        out->emplace(std::move(key), std::move(file));
    }

    if (in.isValid() && in.isAtEnd())
        return true;

    out->clear();

    return false;
}

bool SaveFileIndexCache(const std::filesystem::path& path, const FileIndexCache& cache)
{
    ByteBuilder out(false);
    out.appendU32(cCacheMagic);
    out.appendU32(cCacheVersion);
    out.appendU32(static_cast<u32>(cache.size()));

    for (const auto& [key, file] : cache)
    {
        AppendString(out, key.c_str());
        AppendU64(out, file.size);
        AppendU64(out, static_cast<u64>(file.modified));
        out.appendU8(file.isAudio);

        if (file.isAudio)
            WriteProbe(out, file.probe);
    }

    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);

    std::filesystem::path temp = path;
    temp += cTempSuffix;

    {
        std::ofstream stream(temp, std::ios::binary | std::ios::trunc);

        if (!stream)
            return false;

        stream.write(reinterpret_cast<const char*>(out.bytes().data()), static_cast<std::streamsize>(out.size()));

        if (!stream)
            return false;
    }

    std::filesystem::rename(temp, path, error);

    return !error;
}
