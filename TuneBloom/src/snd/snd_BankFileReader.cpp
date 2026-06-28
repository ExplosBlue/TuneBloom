#include <snd/snd_BankFileReader.h>

#include <basis/seadWarning.h>
#include <prim/seadMemUtil.h>

#include <ui/PopupMgr.h>
#include <ui/UI.h>

namespace nw { namespace snd { namespace internal {

BankFileReader::BankFileReader()
    : mHeader(nullptr)
    , mInfoBlockBody(nullptr)
    , mIsInitialized(false)
{
}

BankFileReader::BankFileReader(const void* bankFile)
    : mHeader(nullptr)
    , mInfoBlockBody(nullptr)
    , mIsInitialized(false)
{
    Initialize(bankFile);
}

void BankFileReader::Initialize(const void* bankFile)
{
    if (!bankFile)
        return;

    const char* bankFmt;
    {
        const ut::BinaryFileHeader* header = reinterpret_cast<const ut::BinaryFileHeader*>(bankFile);

        if (sead::MemUtil::compare(header->signature, "FBNK", 4) != 0 && sead::MemUtil::compare(header->signature, "CBNK", 4) != 0)
        {
            PopupMgr::instance()->pushCurrentItemError("File is not a valid bank file");
            return;
        }

        bankFmt = sead::MemUtil::compare(header->signature, "CBNK", 4) == 0 ? "CBNK" : "FBNK";

        if (sead::MemUtil::compare(header->signature, "CBNK", 4) == 0)
        {
            u32 major = ((u32)header->version >> 24) & 0xFF;
            if (major < 1)
            {
                sead::FormatFixedSafeString<64> msg("CBNK version not supported (0x%08X)", (u32)header->version);
                PopupMgr::instance()->pushCurrentItemError(msg);
                return;
            }
        }
        else
        {
            if ((u32)header->version != 0x00010000)
            {
                sead::FormatFixedSafeString<64> msg("FBNK version not supported (0x%08X)", (u32)header->version);
                PopupMgr::instance()->pushCurrentItemError(msg);
                return;
            }
        }
    }

    mHeader = reinterpret_cast<const BankFile::FileHeader*>(bankFile);

    const BankFile::InfoBlock* infoBlock = mHeader->GetInfoBlock();
    if (!infoBlock)
    {
        PopupMgr::instance()->pushCurrentItemError(sead::FormatFixedSafeString<64>("%s: INFO block not found", bankFmt).cstr());
        return;
    }

    if (!CheckBlockCorruptError(bankFmt, "INFO", infoBlock))
    {
        return;
    }

    mInfoBlockBody = &infoBlock->body;

    mIsInitialized = true;
}

void BankFileReader::Finalize()
{
    if (mIsInitialized)
    {
        mHeader = nullptr;
        mInfoBlockBody = nullptr;
        mIsInitialized = false;
    }
}

bool BankFileReader::ReadVelocityRegionInfo(VelocityRegionInfo* info, s32 programNo, s32 key, s32 velocity) const
{
    SEAD_ASSERT(info);

    if (!mIsInitialized)
        return false;

    if (programNo < 0 || programNo >= mInfoBlockBody->GetInstrumentCount())
        return false;

    const BankFile::Instrument* instrument = mInfoBlockBody->GetInstrument(programNo);
    if (!instrument)
        return false;

    const BankFile::KeyRegion* keyRegion = instrument->GetKeyRegion(key);
    if (!keyRegion)
        return false;

    const BankFile::VelocityRegion* velocityRegion = keyRegion->GetVelocityRegion(velocity);
    if (!velocityRegion)
        return false;

    SEAD_ASSERT(velocityRegion->waveIdTableIndex < mInfoBlockBody->GetWaveIdCount());
    const Util::WaveId* pWaveId = mInfoBlockBody->GetWaveId(velocityRegion->waveIdTableIndex);

    if (!pWaveId)
        return false;

    if (pWaveId->waveIndex == 0xFFFFFFFF)
    {
        SEAD_WARNING("This region [programNo(%d) key(%d) velocity(%d)] is not assigned wave file.", programNo, key, velocity);
        return false;
    }

    info->waveArchiveId = pWaveId->waveArchiveId;
    info->waveIndex = pWaveId->waveIndex;

    const BankFile::RegionParameter* regionParameter = velocityRegion->GetRegionParameter();
    if (!regionParameter)
    {
        info->rootKey       = velocityRegion->GetRootKey();
        info->volume            = velocityRegion->GetVolume();
        info->pan               = velocityRegion->GetPan();
        info->pitch             = velocityRegion->GetPitch();
        info->isIgnoreNoteOff   = velocityRegion->IsIgnoreNoteOff();
        info->keyGroup          = velocityRegion->GetKeyGroup();
        info->interpolationType = velocityRegion->GetInterpolationType();
        info->adshrCurve        = velocityRegion->GetAdshrCurve();
    }
    else
    {
        info->rootKey       = regionParameter->rootKey;
        info->volume            = regionParameter->volume;
        info->pan               = regionParameter->pan;
        info->pitch             = regionParameter->pitch;
        info->isIgnoreNoteOff   = regionParameter->isIgnoreNoteOff;
        info->keyGroup          = regionParameter->keyGroup;
        info->interpolationType = regionParameter->interpolationType;
        info->adshrCurve        = regionParameter->adshrCurve;
    }

    return true;
}

const Util::WaveIdTable* BankFileReader::GetWaveIdTable() const
{
    if (!mIsInitialized)
        return nullptr;

    return &mInfoBlockBody->GetWaveIdTable();
}

} } } // namespace nw::snd::internal
