#include <snd/snd_WaveFileReader.h>

#include <prim/seadMemUtil.h>

#include <ui/PopupMgr.h>
#include <ui/UI.h>

namespace nw { namespace snd { namespace internal {

SampleFormat WaveFileReader::GetSampleFormat(u8 format)
{
    switch (format)
    {
        case WaveFile::PCM8:        return SAMPLE_FORMAT_PCM_S8;
        case WaveFile::PCM16:       return SAMPLE_FORMAT_PCM_S16;
        case WaveFile::DSP_ADPCM:   return SAMPLE_FORMAT_DSP_ADPCM;

        default:
            SEAD_ASSERT_MSG(false, "Unknown wave data format(%d)", format);
            return SAMPLE_FORMAT_DSP_ADPCM;
    }
}

WaveFileReader::WaveFileReader(const void* waveFile, s8 waveType)
    : mHeader(nullptr)
    , mInfoBlockBody(nullptr)
    , mDataBlockBody(nullptr)
    , mWaveType(waveType)
{
    if (!waveFile)
    {
        return;
    }

    switch (mWaveType)
    {
        case WAVE_TYPE_NWWAV:
        {
            {
                const ut::BinaryFileHeader* header = reinterpret_cast<const ut::BinaryFileHeader*>(waveFile);

                if (sead::MemUtil::compare(header->signature, "FWAV", 4) != 0 && sead::MemUtil::compare(header->signature, "CWAV", 4) != 0)
                {
                    PopupMgr::instance()->pushCurrentItemError("File is not a valid BFWAV");
                    return;
                }

                if (sead::MemUtil::compare(header->signature, "CWAV", 4) == 0)
                {
                    u32 major = ((u32)header->version >> 24) & 0xFF;
                    if (major < 1)
                    {
                        sead::FormatFixedSafeString<64> msg("CWAV version not supported (0x%08X)", (u32)header->version);
                        PopupMgr::instance()->pushCurrentItemError(msg);
                        return;
                    }
                }
                else
                {
                    if (!(0x00010000 <= (u32)header->version && (u32)header->version <= 0x00010200))
                    {
                        sead::FormatFixedSafeString<64> msg("BFWAV version not supported (0x%08X)", (u32)header->version);
                        PopupMgr::instance()->pushCurrentItemError(msg);
                        return;
                    }
                }
            }

            const WaveFile::FileHeader* header = reinterpret_cast<const WaveFile::FileHeader*>(waveFile);
            const WaveFile::InfoBlock* infoBlock = header->GetInfoBlock();
            const WaveFile::DataBlock* dataBlock = header->GetDataBlock();

            if (!infoBlock)
            {
                PopupMgr::instance()->pushCurrentItemError("BFWAV: INFO block not found");
                return;
            }

            if (!dataBlock)
            {
                PopupMgr::instance()->pushCurrentItemError("BFWAV: DATA block not found");
                return;
            }

            if (!CheckBlockCorruptError("BFWAV", "INFO", infoBlock))
            {
                return;
            }

            if (!CheckBlockCorruptError("BFWAV", "DATA", dataBlock))
            {
                return;
            }

            mHeader = header;
            mInfoBlockBody = &infoBlock->body;
            mDataBlockBody = &dataBlock->byte;

            break;
        }

        case WAVE_TYPE_DSPADPCM:
            SEAD_ASSERT_MSG(false, "not implemented");
            mDspadpcmReader.Initialize(waveFile);
            break;
    }
}

bool WaveFileReader::IsOriginalLoopAvailable() const
{
    const ut::BinaryFileHeader& header = *reinterpret_cast<const ut::BinaryFileHeader*>(mHeader);
    if (sead::MemUtil::compare(&header.signature, "CSTM", 4) == 0)
        return false;
    if (header.version >= 0x00010200)
        return true;

    return false;
}

bool WaveFileReader::ReadWaveInfo(WaveInfo* info, const void* waveDataOffsetOrigin) const
{
    switch (mWaveType)
    {
        case WAVE_TYPE_NWWAV:
        {
            SEAD_ASSERT(mInfoBlockBody);

            info->endian = ut::GetFileEndian(mHeader->header);

            const SampleFormat format = GetSampleFormat(mInfoBlockBody->encoding);
            info->sampleFormat   = format;
            info->channelCount   = mInfoBlockBody->GetChannelCount();
            info->sampleRate     = mInfoBlockBody->sampleRate;
            info->loopFlag       = mInfoBlockBody->isLoop == 1;
            info->loopStartFrame = mInfoBlockBody->loopStartFrame;
            info->loopEndFrame   = mInfoBlockBody->loopEndFrame;

            if (IsOriginalLoopAvailable())
                info->originalLoopStartFrame = mInfoBlockBody->originalLoopStartFrame;
            else
                info->originalLoopStartFrame = mInfoBlockBody->loopStartFrame;

            for (s32 i = 0; i < mInfoBlockBody->GetChannelCount(); i++)
            {
                if (i >= WAVE_CHANNEL_MAX)
                    continue;

                WaveInfo::ChannelParam& channelParam = info->channelParam[i];

                const WaveFile::ChannelInfo& channelInfo = mInfoBlockBody->GetChannelInfo(i);

                // if (channelInfo.offsetToAdpcmInfo != 0)
                if (channelInfo.referToAdpcmInfo.offset != 0)
                {
                    const WaveFile::DspAdpcmInfo& adpcmInfo = channelInfo.GetDspAdpcmInfo();
                    channelParam.adpcmParam = adpcmInfo.adpcmParam;
                    channelParam.adpcmLoopParam = adpcmInfo.adpcmLoopParam;
                }

                channelParam.dataAddress = GetWaveDataAddress(&channelInfo, waveDataOffsetOrigin);
            }

            break;
        }

        case WAVE_TYPE_DSPADPCM:
        {
            mDspadpcmReader.ReadWaveInfo(info);
            break;
        }
    }

    return true;
}

const void* WaveFileReader::GetWaveDataAddress(const WaveFile::ChannelInfo* info, const void* waveDataOffsetOrigin) const
{
    SEAD_ASSERT(mInfoBlockBody);
    SEAD_ASSERT(info);

    SEAD_UNUSED(waveDataOffsetOrigin);

    return info->GetSamplesAddress(mDataBlockBody);
}

} } } // namespace nw::snd::internal
