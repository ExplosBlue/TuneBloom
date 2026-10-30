#include <bfsar/files/LooseWaveFile.h>

#include <core/TestHooks.h>

#include <bfsar/io/PackFile.h>
#include <bfsar/model/ArchiveFormat.h>
#include <core/ByteView.h>
#include <core/NwFile.h>

#include <messages/Messages.h>

#include <filedevice/seadPath.h>

#include <snd/snd_ElementType.h>

#include <algorithm>

static constexpr u32 cFwavInfoLoopEnd       = 0x0C;
static constexpr u32 cFwavInfoChannelTable  = 0x14;
static constexpr u32 cFwavChannelInfoSize   = 0x14;
static constexpr u32 cFwavChannelAdpcmRef   = 0x08;
static constexpr u32 cFwavAdpcmInfoSize     = 0x2C;
static constexpr u32 cAdpcmFrameBytes       = 8;
static constexpr u32 cAdpcmFrameSamples     = 14;

static bool FindNwBlock(const ByteView& view, u16 type, nwfile::Block* out)
{
    return nwfile::FindBlock(view, type, out) && out->offset + out->size <= view.readU32(nwfile::cHeaderFileSize);
}

static u64 GetFwavSampleBytes(u8 encoding, u32 frames)
{
    switch (static_cast<WaveFile::Encoding>(encoding))
    {
        case WaveFile::Encoding::Pcm8:
            return frames;

        case WaveFile::Encoding::Pcm16:
            return static_cast<u64>(frames) * sizeof(s16);

        default:
            break;
    }

    return (static_cast<u64>(frames) + cAdpcmFrameSamples - 1) / cAdpcmFrameSamples * cAdpcmFrameBytes;
}

static bool IsFwavChannelInBounds(const ByteView& view, u64 channelInfo, u64 infoEnd, u64 dataBody, u64 dataEnd, u8 encoding, u32 frames)
{
    if (channelInfo + cFwavChannelInfoSize > infoEnd)
        return false;

    const u64 samples = dataBody + view.readU32(channelInfo + nwfile::cReferenceOffset);

    if (samples + GetFwavSampleBytes(encoding, frames) > dataEnd)
        return false;

    if (static_cast<WaveFile::Encoding>(encoding) != WaveFile::Encoding::DspAdpcm)
        return true;

    return channelInfo + view.readU32(channelInfo + cFwavChannelAdpcmRef + nwfile::cReferenceOffset) + cFwavAdpcmInfoSize <= infoEnd;
}

static bool IsFwavInBounds(const u8* file, size_t size)
{
    if (size < nwfile::cHeaderSize || !MatchesInnerFileKind(file, InnerFileKind::Wave) || !ByteView::IsByteOrderMark(file, size, nwfile::cHeaderByteOrder))
        return false;

    const ByteView view(file, size, ByteView::IsBigEndianMark(file, size, nwfile::cHeaderByteOrder));

    nwfile::Block info;
    nwfile::Block data;

    if (view.readU32(nwfile::cHeaderFileSize) > size ||
        !FindNwBlock(view, nw::snd::internal::ElementType_WaveFile_InfoBlock, &info) ||
        !FindNwBlock(view, nw::snd::internal::ElementType_WaveFile_DataBlock, &data))
        return false;

    const u64 infoBody = info.offset + nwfile::cBlockHeaderSize;
    const u64 infoEnd = info.offset + info.size;
    const u64 table = infoBody + cFwavInfoChannelTable;

    if (table + sizeof(u32) > infoEnd)
        return false;

    const u8 encoding = view.readU8(infoBody);
    const u32 frames = view.readU32(infoBody + cFwavInfoLoopEnd);
    const u32 channelCount = view.readU32(table);

    if (encoding > static_cast<u8>(WaveFile::Encoding::DspAdpcm) || channelCount == 0 || table + sizeof(u32) + static_cast<u64>(channelCount) * nwfile::cReferenceSize > infoEnd)
        return false;

    const u64 dataBody = data.offset + nwfile::cBlockHeaderSize;
    const u64 dataEnd = data.offset + data.size;

    for (u32 i = 0; i < std::min<u32>(channelCount, snd::cWaveChannelMax); i++)
    {
        const u64 channelInfo = table + view.readU32(table + sizeof(u32) + i * nwfile::cReferenceSize + nwfile::cReferenceOffset);

        if (!IsFwavChannelInBounds(view, channelInfo, infoEnd, dataBody, dataEnd, encoding, frames))
            return false;
    }

    return true;
}

WaveFile* ReadFwavFile(const void* data, size_t size)
{
    const u8* file = static_cast<const u8*>(data);

    if (!IsFwavInBounds(file, size))
        return nullptr;

    WaveFile* wave = new WaveFile();

    if (!wave->read(file) || wave->getChannels().isEmpty())
    {
        delete wave;
        return nullptr;
    }

    wave->setStandaloneTarget({ FindInnerFileFormat(file, InnerFileKind::Wave)->format, wave->getVersion(), wave->getEndian() });

    return wave;
}

bool LooseWaveFile::IsWaveFile(const void* data, u32 size)
{
    return size >= sizeof(nw::ut::BinaryFileHeader) && FindInnerFileFormat(data, InnerFileKind::Wave) != nullptr;
}

bool LooseWaveFile::open(const void* data, u32 size, const sead::SafeString& path, sead::BufferedSafeString* outError)
{
    WaveFile* wave = IsWaveFile(data, size) ? ReadFwavFile(data, size) : nullptr;

    setPath(path);

    if (!wave)
    {
        outError->format(messages::wavefile::cFileUnreadableFormat, mFileName.cstr());
        return false;
    }

    adoptWave(wave);

    return true;
}

void LooseWaveFile::create(const InnerFileTarget& target, const sead::SafeString& fileName)
{
    WaveFile* wave = new WaveFile();
    wave->setStandaloneTarget(target);
    wave->setVersion(target.version);

    mFilePath.clear();
    mFileName = fileName;

    adoptWave(wave);
}

void LooseWaveFile::adoptWave(WaveFile* wave)
{
    mWave = wave;
    mWave->setEnableName(true);
    mWave->getName().copy(mFileName);
    mWave->setId(0);
    mWaveList.pushBack(mWave);

    mSavedSignature = mWave->computeStateSignature();
}

void LooseWaveFile::setPath(const sead::SafeString& path)
{
    mFilePath = path;
    sead::Path::getFileName(&mFileName, path);
}

bool LooseWaveFile::isModified() const
{
    return mWave->computeStateSignature() != mSavedSignature;
}

bool LooseWaveFile::writeTo(const sead::SafeString& path) const
{
    return mWave->writeFile(path);
}

SaveResult LooseWaveFile::save(sead::BufferedSafeString* outError)
{
    return saveAs(mFilePath, outError);
}

SaveResult LooseWaveFile::saveAs(const sead::SafeString& path, sead::BufferedSafeString* outError)
{
    const u64 signature = mWave->computeStateSignature();
    const bool isUnchanged = signature == mSavedSignature && !testhooks::IsStreamRewriteForced(path.cstr());
    const bool isCopied = isUnchanged && packfile::CopyFileOver(mFilePath.cstr(), path.cstr());

    if (!isCopied && !writeTo(path))
    {
        sead::FixedSafeString<256> fileName;
        sead::Path::getFileName(&fileName, path);

        outError->format(messages::wavefile::cWriteFailedFormat, fileName.cstr());
        return SaveResult::Failed;
    }

    setPath(path);
    mWave->getName().copy(mFileName);
    mSavedSignature = mWave->computeStateSignature();

    return SaveResult::Saved;
}
