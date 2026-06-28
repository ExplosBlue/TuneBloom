#include <snd/snd_WaveSoundFileReader.h>

#include <prim/seadMemUtil.h>

#include <ui/PopupMgr.h>
#include <ui/UI.h>

namespace nw { namespace snd { namespace internal {

WaveSoundFileReader::WaveSoundFileReader(const void* waveSoundFile)
    : mHeader(nullptr)
    , mInfoBlockBody(nullptr)
{
    if (!waveSoundFile)
    {
        return;
    }

    const char* wsdFmt;
    {
        const ut::BinaryFileHeader* header = reinterpret_cast<const ut::BinaryFileHeader*>(waveSoundFile);

        if (sead::MemUtil::compare(header->signature, "FWSD", 4) != 0 && sead::MemUtil::compare(header->signature, "CWSD", 4) != 0)
        {
            PopupMgr::instance()->pushCurrentItemError("File is not a valid wave sound file");
            return;
        }

        wsdFmt = sead::MemUtil::compare(header->signature, "CWSD", 4) == 0 ? "CWSD" : "FWSD";

        if (sead::MemUtil::compare(header->signature, "CWSD", 4) == 0)
        {
            u32 major = ((u32)header->version >> 24) & 0xFF;
            u32 minor = ((u32)header->version >> 16) & 0xFF;
            if (major < 1)
            {
                sead::FormatFixedSafeString<64> msg("CWSD version not supported (0x%08X)", (u32)header->version);
                return;
            }
        }
        else
        {
            if (!(0x00010000 <= (u32)header->version && (u32)header->version <= 0x00010100))
            {
                sead::FormatFixedSafeString<64> msg("FWSD version not supported (0x%08X)", (u32)header->version);
                return;
            }
        }
    }

    const WaveSoundFile::FileHeader* header = reinterpret_cast<const WaveSoundFile::FileHeader*>(waveSoundFile);

    const WaveSoundFile::InfoBlock* infoBlock = header->GetInfoBlock();
    if (!infoBlock)
    {
        PopupMgr::instance()->pushCurrentItemError(sead::FormatFixedSafeString<64>("%s: INFO block not found", wsdFmt).cstr());
        return;
    }

    if (!CheckBlockCorruptError(wsdFmt, "INFO", infoBlock))
    {
        return;
    }

    mHeader = header;
    mInfoBlockBody = &infoBlock->body;
}

u32 WaveSoundFileReader::GetWaveSoundCount() const
{
    SEAD_ASSERT(mInfoBlockBody);
    return mInfoBlockBody->GetWaveSoundCount();
}

u32 WaveSoundFileReader::GetNoteInfoCount(u32 index) const
{
    SEAD_ASSERT(mInfoBlockBody);
    SEAD_ASSERT(index < GetWaveSoundCount());

    const WaveSoundFile::WaveSoundData& wsdData = mInfoBlockBody->GetWaveSoundData(index);
    return wsdData.GetNoteCount();
}

u32 WaveSoundFileReader::GetTrackInfoCount(u32 index) const
{
    SEAD_ASSERT(mInfoBlockBody);
    SEAD_ASSERT(index < GetWaveSoundCount());

    const WaveSoundFile::WaveSoundData& wsdData = mInfoBlockBody->GetWaveSoundData(index);
    return wsdData.GetTrackCount();
}

bool WaveSoundFileReader::ReadWaveSoundInfo(WaveSoundInfo* dst, u32 index) const
{
    SEAD_ASSERT(mInfoBlockBody);
    SEAD_ASSERT(dst);

    const WaveSoundFile::WaveSoundInfo& src = mInfoBlockBody->GetWaveSoundData(index).GetWaveSoundInfo();

    dst->pitch = src.GetPitch();
    dst->pan = src.GetPan();
    dst->surroundPan = src.GetSurroundPan();
    src.GetSendValue(&dst->mainSend, dst->fxSend, AUX_BUS_NUM);
    dst->adshr = src.GetAdshrCurve();

    if (IsFilterSupportedVersion())
    {
        dst->lpfFreq = src.GetLpfFreq();
        dst->biquadType = src.GetBiquadType();
        dst->biquadValue = src.GetBiquadValue();
    }
    else
    {
        dst->lpfFreq = 64;
        dst->biquadType = 0;
        dst->biquadValue = 0;
    }

    return true;
}

bool WaveSoundFileReader::ReadNoteInfo(WaveSoundNoteInfo* dst, u32 index, u32 noteIndex) const
{
    SEAD_ASSERT(mInfoBlockBody);
    SEAD_ASSERT(dst);

    const WaveSoundFile::NoteInfo& src = mInfoBlockBody->GetWaveSoundData(index).GetNoteInfo(noteIndex);

    const Util::WaveId* pWaveId = mInfoBlockBody->GetWaveIdTable().GetWaveId(src.waveIdTableIndex);

    if (!pWaveId)
        return false;

    dst->waveArchiveId = pWaveId->waveArchiveId;
    dst->waveIndex = pWaveId->waveIndex;
    dst->pitch = src.GetPitch();
    dst->adshr = src.GetAdshrCurve();
    dst->rootKey = src.GetRootKey();
    dst->pan = src.GetPan();
    dst->surroundPan = src.GetSurroundPan();
    dst->volume = src.GetVolume();

    return true;
}

bool WaveSoundFileReader::IsFilterSupportedVersion() const
{
    const ut::BinaryFileHeader& header = *reinterpret_cast<const ut::BinaryFileHeader*>(mHeader);
    if (sead::MemUtil::compare(header.signature, "CWSD", 4) == 0)
    {
        u32 major = ((u32)header.version >> 24) & 0xFF;
        u32 minor = ((u32)header.version >> 16) & 0xFF;
        if (major > 1 || (major == 1 && minor >= 1))
            return true;
    }
    else
    {
        if ((u32)header.version >= 0x00010100)
            return true;
    }

    return false;
}

} } } // namespace nw::snd::internal
