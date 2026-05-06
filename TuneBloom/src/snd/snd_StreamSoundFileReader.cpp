#include <snd/snd_StreamSoundFileReader.h>

#include <math/seadMathCalcCommon.h>

#include <ui/PopupMgr.h>
#include <ui/UI.h>

namespace nw { namespace snd { namespace internal {

StreamSoundFileReader::StreamSoundFileReader()
    : mHeader(nullptr)
    , mInfoBlockBody(nullptr)
{
}

void StreamSoundFileReader::Initialize(const void* streamSoundFile)
{
    if (!streamSoundFile)
    {
        return;
    }

    if (!IsValidFileHeader(streamSoundFile))
        return;

    const StreamSoundFile::FileHeader* header = reinterpret_cast<const StreamSoundFile::FileHeader*>(streamSoundFile);

    const StreamSoundFile::InfoBlock* infoBlock = header->GetInfoBlock();
    if (!infoBlock)
    {
        PopupMgr::instance()->pushCurrentItemError("BFSTM: INFO block not found");
        return;
    }

    if (!CheckBlockCorruptError("BFSTM", "INFO", infoBlock))
    {
        return;
    }

    mInfoBlockBody = &infoBlock->body;
    if (mInfoBlockBody->GetStreamSoundInfo()->oneBlockBytes % 32 != 0)
    {
        PopupMgr::instance()->pushCurrentItemError("BFSTM: Block bytes not aligned");
        return;
    }

    if (mInfoBlockBody->GetStreamSoundInfo()->lastBlockPaddedBytes % 32 != 0)
    {
        PopupMgr::instance()->pushCurrentItemError("BFSTM: Block bytes not aligned");
        return;
    }

    mHeader = header;
}

void StreamSoundFileReader::Finalize()
{
    mHeader = nullptr;
    mInfoBlockBody = nullptr;
}

bool StreamSoundFileReader::IsTrackInfoAvailable() const
{
    const ut::BinaryFileHeader& header = *reinterpret_cast<const ut::BinaryFileHeader*>(mHeader);
    if (header.version <= 0x00020000)
        return true;

    return false;
}

bool StreamSoundFileReader::IsOriginalLoopAvailable() const
{
    const ut::BinaryFileHeader& header = *reinterpret_cast<const ut::BinaryFileHeader*>(mHeader);
    if (header.version >= 0x00040000)
        return true;

    return false;
}

bool StreamSoundFileReader::IsValidFileHeader(const void* streamSoundFile) const
{
    SEAD_ASSERT(streamSoundFile);

    const ut::BinaryFileHeader* header = reinterpret_cast<const ut::BinaryFileHeader*>(streamSoundFile);

    // if (sead::MemUtil::compare(header->signature, "CSTM", 4) != 0)
    if (sead::MemUtil::compare(header->signature, "FSTM", 4) != 0)
    {
        PopupMgr::instance()->pushCurrentItemError("File is not a valid BFSTM");
        return false;
    }

    // if (false)
    if (!(0x00010000 <= header->version && header->version <= 0x00040000))
    {
        sead::FormatFixedSafeString<64> msg("BFSTM version not supported (0x%08X)", (u32)header->version);
        PopupMgr::instance()->pushCurrentItemError(msg);
        return false;
    }

    return true;
}

bool StreamSoundFileReader::ReadStreamSoundInfo(StreamSoundFile::StreamSoundInfo* strmInfo) const
{
    SEAD_ASSERT(mInfoBlockBody);
    const StreamSoundFile::StreamSoundInfo* info = mInfoBlockBody->GetStreamSoundInfo();

    SEAD_ASSERT(info->oneBlockBytes % 32 == 0);
    SEAD_ASSERT(info->lastBlockPaddedBytes % 32 == 0);

    strmInfo->encodeMethod = info->encodeMethod;
    strmInfo->isLoop = info->isLoop;
    strmInfo->channelCount = info->channelCount;
    strmInfo->regionCount = info->regionCount;
    strmInfo->sampleRate = info->sampleRate;
    strmInfo->loopStart = info->loopStart;
    strmInfo->frameCount = info->frameCount;
    strmInfo->blockCount = info->blockCount;

    strmInfo->oneBlockBytes = info->oneBlockBytes;
    strmInfo->oneBlockSamples = info->oneBlockSamples;

    strmInfo->lastBlockBytes = info->lastBlockBytes;
    strmInfo->lastBlockSamples = info->lastBlockSamples;
    strmInfo->lastBlockPaddedBytes = info->lastBlockPaddedBytes;

    strmInfo->sizeofSeekInfoAtom = info->sizeofSeekInfoAtom;
    strmInfo->seekInfoIntervalSamples = info->seekInfoIntervalSamples;

    strmInfo->sampleDataOffset = info->sampleDataOffset;

    strmInfo->regionInfoBytes = info->regionInfoBytes;
    strmInfo->regionDataOffset = info->regionDataOffset;

    if (IsOriginalLoopAvailable())
    {
        strmInfo->originalLoopStart = info->originalLoopStart;
        strmInfo->originalLoopEnd = info->originalLoopEnd;
    }
    else
    {
        strmInfo->originalLoopStart = info->loopStart;
        strmInfo->originalLoopEnd = info->frameCount;
    }

    return true;
}

bool StreamSoundFileReader::ReadStreamTrackInfo(TrackInfo* pTrackInfo, s32 trackIndex) const
{
    SEAD_ASSERT(mInfoBlockBody);
    SEAD_ASSERT(pTrackInfo);

    const StreamSoundFile::TrackInfoTable* table = mInfoBlockBody->GetTrackInfoTable();
    if (!table)
        return false;

    if (trackIndex >= static_cast<s32>(table->GetTrackCount()))
        return false;

    const StreamSoundFile::TrackInfo* src = table->GetTrackInfo(trackIndex);
    pTrackInfo->volume = src->volume;
    pTrackInfo->pan = src->pan;
    pTrackInfo->span = src->span;
    pTrackInfo->flags = src->flags;
    pTrackInfo->channelCount = static_cast<u8>(src->GetTrackChannelCount());

    u32 count = sead::Mathu::min(static_cast<u32>(pTrackInfo->channelCount), WAVE_CHANNEL_MAX);
    for (u32 i = 0; i < count; i++)
    {
        pTrackInfo->globalChannelIndex[i] = src->GetGlobalChannelIndex(i);
    }

    return true;
}

bool StreamSoundFileReader::ReadDspAdpcmChannelInfo(DspAdpcmParam* pParam, DspAdpcmLoopParam* pLoopParam, s32 channelIndex) const
{
    SEAD_ASSERT(mInfoBlockBody);
    SEAD_ASSERT(pParam);
    SEAD_ASSERT(pLoopParam);

    const StreamSoundFile::ChannelInfoTable* channelTable = mInfoBlockBody->GetChannelInfoTable();
    if (!channelTable)
        return false;

    if (channelIndex >= channelTable->GetChannelCount())
        return false;

    const StreamSoundFile::DspAdpcmChannelInfo* src = channelTable->GetChannelInfo(channelIndex)->GetDspAdpcmChannelInfo();
    if (!src)
        return false;

    *pParam = src->param;
    *pLoopParam = src->loopParam;

    return true;
}

} } } // namespace nw::snd::internal
