#include <bfsar/files/BankFile.h>

#include <core/Debug.h>

#include <bfsar/io/FileWriter.h>
#include <bfsar/io/WaveIdTableWriter.h>

#include <ui/Messages.h>
#include <ui/app/PopupMgr.h>
#include <ui/app/UI.h>

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <vector>

static constexpr u8 cInterpolationTypeMax = 2;
static constexpr u8 cRegionValueMax = 127;
static constexpr u32 cRegionCountMax = 128;
static constexpr s32 cInstrumentCountMax = 32767;

static void PushInstrumentError(const char* format, u32 instrumentId)
{
    sead::FormatFixedSafeString<256> msg(format, instrumentId);
    PopupMgr::instance()->pushCurrentItemError(msg);
}

static std::vector<u8> CollectIndexChunkBorders(const nw::snd::internal::IndexChunk& indexChunk, const char* errorFormat, u32 instrumentId)
{
    std::vector<u8> borders;

    s32 prevOffset = nw::snd::internal::Util::Reference::INVALID_OFFSET;
    for (u32 i = indexChunk.min; i <= indexChunk.max; i++)
    {
        const nw::snd::internal::Util::Reference* regionRef = indexChunk.GetRegionRef(i);
        if (!regionRef)
        {
            PushInstrumentError(errorFormat, instrumentId);
            continue;
        }

        if (regionRef->offset != prevOffset && i != indexChunk.min)
            borders.push_back(i - 1);

        prevOffset = regionRef->offset;
    }

    borders.push_back(indexChunk.max);

    return borders;
}

static bool IsIndexChunkRangeValid(const nw::snd::internal::IndexChunk& indexChunk)
{
    return indexChunk.min <= indexChunk.max && indexChunk.max <= cRegionValueMax;
}

template <typename Chunk>
static const nw::snd::internal::BankFile::VelocityRegion* FindVelocityRegionInfo(const Chunk& chunk, u8 velocity, const char* errorFormat, u32 instrumentId)
{
    const nw::snd::internal::Util::Reference* velocityRegionRef = chunk.GetRegionRef(velocity);
    if (!velocityRegionRef ||
        velocityRegionRef->typeId != nw::snd::internal::ElementType_BankFile_VelocityRegionInfo ||
        velocityRegionRef->offset == nw::snd::internal::Util::Reference::INVALID_OFFSET)
    {
        PushInstrumentError(errorFormat, instrumentId);
        return nullptr;
    }

    const nw::snd::internal::BankFile::VelocityRegion* velocityRegionInfo = static_cast<const nw::snd::internal::BankFile::VelocityRegion*>(chunk.GetRegion(velocity));
    if (!velocityRegionInfo)
        PushInstrumentError(errorFormat, instrumentId);

    return velocityRegionInfo;
}

template <typename Chunk>
static const nw::snd::internal::BankFile::KeyRegion* FindKeyRegionInfo(const Chunk& chunk, u8 key, const char* errorFormat, u32 instrumentId)
{
    const nw::snd::internal::Util::Reference* keyRegionRef = chunk.GetRegionRef(key);
    if (!keyRegionRef)
    {
        PushInstrumentError(errorFormat, instrumentId);
        return nullptr;
    }

    if (keyRegionRef->offset == nw::snd::internal::Util::Reference::INVALID_OFFSET)
    {
        if (keyRegionRef->typeId != 0 && keyRegionRef->typeId != nw::snd::internal::ElementType_BankFile_NullInfo)
            PushInstrumentError(errorFormat, instrumentId);

        return nullptr;
    }

    if (keyRegionRef->typeId != nw::snd::internal::ElementType_BankFile_KeyRegionInfo)
    {
        PushInstrumentError(errorFormat, instrumentId);
        return nullptr;
    }

    const nw::snd::internal::BankFile::KeyRegion* keyRegionInfo = static_cast<const nw::snd::internal::BankFile::KeyRegion*>(chunk.GetRegion(key));
    if (!keyRegionInfo)
        PushInstrumentError(errorFormat, instrumentId);

    return keyRegionInfo;
}

void BankFile::VelocityRegion::read(const nw::snd::internal::BankFile::VelocityRegion* velocityRegionInfo, const nw::snd::internal::Util::WaveIdTable& waveIdTable, u32 instrumentId)
{
    LOG_FUNC();
    LOG_U32("instrumentId", instrumentId);
    LOG_U32("waveIdTableIndex", velocityRegionInfo->waveIdTableIndex);
    LOG_U32("waveIdTableCount", waveIdTable.GetCount());

    const nw::snd::internal::Util::WaveId* waveId = waveIdTable.GetWaveId(velocityRegionInfo->waveIdTableIndex);
    if (waveId)
        LOG("waveId=%p waveArchiveId=%u waveIndex=%u\n", waveId, u32(waveId->waveArchiveId), u32(waveId->waveIndex));

    if (waveId && waveId->waveArchiveId == 0)
    {
        WaveFile* waveFile = static_cast<WaveFile*>(sBfsar.getItem(waveId->waveIndex, sBfsar.getWaveFileList()));

        mWaveFileRef.attach(waveFile);
        if (!mWaveFileRef.isAttached())
            PushInstrumentError(messages::bank::cInstrumentWaveFileLoadFailedFormat, instrumentId);
    }
    else if (!waveId)
    {
        PushInstrumentError(messages::bank::cInstrumentWaveIdNullFormat, instrumentId);
    }
    else
    {
        PushInstrumentError(messages::bank::cInstrumentWaveLinkFailedFormat, instrumentId);
    }

    setRootKey(velocityRegionInfo->GetRootKey());
    mVolume = velocityRegionInfo->GetVolume();
    setPan(velocityRegionInfo->GetPan());
    mPitch = velocityRegionInfo->GetPitch();
    mIsIgnoreNoteOff = velocityRegionInfo->IsIgnoreNoteOff();
    setKeyGroup(velocityRegionInfo->GetKeyGroup());
    mInterpolationType = sead::MathCalcCommon<u8>::clampMax(velocityRegionInfo->GetInterpolationType(), cInterpolationTypeMax);

    const nw::snd::AdshrCurve& adshrCurveInfo = velocityRegionInfo->GetAdshrCurve();
    snd::AdshrCurve adshrCurve;
    adshrCurve.attack = adshrCurveInfo.attack;
    adshrCurve.decay = adshrCurveInfo.decay;
    adshrCurve.sustain = adshrCurveInfo.sustain;
    adshrCurve.hold = adshrCurveInfo.hold;
    adshrCurve.release = adshrCurveInfo.release;
    setAdshrCurve(adshrCurve);
}

void BankFile::KeyRegion::addVelocityRegion_(u8 velocityMin, u8 velocityMax, const nw::snd::internal::BankFile::VelocityRegion* velocityRegionInfo, const nw::snd::internal::Util::WaveIdTable& waveIdTable, u32 instrumentId)
{
    VelocityRegion* velocityRegion = new VelocityRegion(velocityMin, velocityMax);
    velocityRegion->mId = mVelocityRegionList.size();

    velocityRegion->mEnableName = true;
    velocityRegion->mName = "VelocityRegion";

    velocityRegion->read(velocityRegionInfo, waveIdTable, instrumentId);

    mVelocityRegionList.pushBack(velocityRegion);
}

template <typename Chunk>
void BankFile::KeyRegion::readVelocityRegions_(const Chunk& chunk, u8 velocityMin, const u8* borders, u32 borderCount, const char* errorFormat, const nw::snd::internal::Util::WaveIdTable& waveIdTable, u32 instrumentId)
{
    if (borderCount > cRegionCountMax)
    {
        PushInstrumentError(errorFormat, instrumentId);
        return;
    }

    for (u32 i = 0; i < borderCount; i++)
    {
        u8 velocityMax = borders[i];
        if (velocityMin > cRegionValueMax || velocityMax > cRegionValueMax || velocityMax < velocityMin)
        {
            PushInstrumentError(errorFormat, instrumentId);
            return;
        }

        const nw::snd::internal::BankFile::VelocityRegion* velocityRegionInfo = FindVelocityRegionInfo(chunk, velocityMin, errorFormat, instrumentId);
        if (velocityRegionInfo)
            addVelocityRegion_(velocityMin, velocityMax, velocityRegionInfo, waveIdTable, instrumentId);

        velocityMin = velocityMax + 1;
    }
}

void BankFile::KeyRegion::read(const nw::snd::internal::BankFile::KeyRegion* keyRegionInfo, const nw::snd::internal::Util::WaveIdTable& waveIdTable, u32 instrumentId)
{
    LOG_FUNC();
    LOG_U32("instrumentId", instrumentId);
    SEAD_ASSERT(keyRegionInfo);

    switch (nw::snd::internal::GetRegionType(keyRegionInfo->toVelocityRegionChunk.typeId))
    {
        case nw::snd::internal::REGION_TYPE_DIRECT:
        {
            const nw::snd::internal::DirectChunk& directChunk = keyRegionInfo->GetDirectChunk();
            if (directChunk.toRegion.typeId != nw::snd::internal::ElementType_BankFile_VelocityRegionInfo ||
                directChunk.toRegion.offset == nw::snd::internal::Util::Reference::INVALID_OFFSET)
            {
                PushInstrumentError(messages::bank::cVelocityRegionInvalidFormat, instrumentId);
                break;
            }

            addVelocityRegion_(0, cRegionValueMax, static_cast<const nw::snd::internal::BankFile::VelocityRegion*>(directChunk.GetRegion()), waveIdTable, instrumentId);
            break;
        }

        case nw::snd::internal::REGION_TYPE_RANGE:
        {
            const nw::snd::internal::RangeChunk& rangeChunk = keyRegionInfo->GetRangeChunk();

            readVelocityRegions_(rangeChunk, 0, rangeChunk.borderTable.item, rangeChunk.borderTable.count, messages::bank::cVelocityRegionInvalidFormat, waveIdTable, instrumentId);
            break;
        }

        case nw::snd::internal::REGION_TYPE_INDEX:
        {
            const nw::snd::internal::IndexChunk& indexChunk = keyRegionInfo->GetIndexChunk();
            if (!IsIndexChunkRangeValid(indexChunk))
            {
                PushInstrumentError(messages::bank::cVelocityRegionInvalidFormat, instrumentId);
                break;
            }

            const std::vector<u8> borders = CollectIndexChunkBorders(indexChunk, messages::bank::cVelocityRegionInvalidFormat, instrumentId);

            readVelocityRegions_(indexChunk, indexChunk.min, borders.data(), static_cast<u32>(borders.size()), messages::bank::cVelocityRegionInvalidFormat, waveIdTable, instrumentId);
            break;
        }

        default:
            PushInstrumentError(messages::bank::cInvalidKeyRegionTypeFormat, instrumentId);
            break;
    }

    if (mVelocityRegionList.isEmpty())
        PushInstrumentError(messages::bank::cVelocityRegionEmptyFormat, instrumentId);
}

void BankFile::Instrument::addKeyRegion_(u8 keyMin, u8 keyMax, const nw::snd::internal::BankFile::KeyRegion* keyRegionInfo, const nw::snd::internal::Util::WaveIdTable& waveIdTable)
{
    KeyRegion* keyRegion = new KeyRegion(keyMin, keyMax);
    keyRegion->mId = mKeyRegionList.size();

    keyRegion->mEnableName = true;
    keyRegion->mName = "KeyRegion";

    keyRegion->read(keyRegionInfo, waveIdTable, mId);

    mKeyRegionList.pushBack(keyRegion);
}

template <typename Chunk>
void BankFile::Instrument::readKeyRegions_(const Chunk& chunk, u8 keyMin, const u8* borders, u32 borderCount, const char* errorFormat, const nw::snd::internal::Util::WaveIdTable& waveIdTable)
{
    if (borderCount > cRegionCountMax)
    {
        PushInstrumentError(errorFormat, mId);
        return;
    }

    for (u32 i = 0; i < borderCount; i++)
    {
        u8 keyMax = borders[i];
        if (keyMin > cRegionValueMax || keyMax > cRegionValueMax || keyMax < keyMin)
        {
            PushInstrumentError(errorFormat, mId);
            return;
        }

        const nw::snd::internal::BankFile::KeyRegion* keyRegionInfo = FindKeyRegionInfo(chunk, keyMin, errorFormat, mId);
        if (keyRegionInfo)
            addKeyRegion_(keyMin, keyMax, keyRegionInfo, waveIdTable);

        keyMin = keyMax + 1;
    }
}

void BankFile::Instrument::read(const nw::snd::internal::BankFile::Instrument* instrumentInfo, const nw::snd::internal::Util::WaveIdTable& waveIdTable)
{
    LOG_FUNC();
    SEAD_ASSERT(instrumentInfo);

    switch (nw::snd::internal::GetRegionType(instrumentInfo->toKeyRegionChunk.typeId))
    {
        case nw::snd::internal::REGION_TYPE_DIRECT:
        {
            const nw::snd::internal::DirectChunk& directChunk = instrumentInfo->GetDirectChunk();
            if (directChunk.toRegion.typeId != nw::snd::internal::ElementType_BankFile_KeyRegionInfo ||
                directChunk.toRegion.offset == nw::snd::internal::Util::Reference::INVALID_OFFSET)
            {
                PushInstrumentError(messages::bank::cKeyRegionInvalidFormat, mId);
                break;
            }

            addKeyRegion_(0, cRegionValueMax, static_cast<const nw::snd::internal::BankFile::KeyRegion*>(directChunk.GetRegion()), waveIdTable);
            break;
        }

        case nw::snd::internal::REGION_TYPE_RANGE:
        {
            const nw::snd::internal::RangeChunk& rangeChunk = instrumentInfo->GetRangeChunk();

            readKeyRegions_(rangeChunk, 0, rangeChunk.borderTable.item, rangeChunk.borderTable.count, messages::bank::cKeyRegionInvalidFormat, waveIdTable);
            break;
        }

        case nw::snd::internal::REGION_TYPE_INDEX:
        {
            const nw::snd::internal::IndexChunk& indexChunk = instrumentInfo->GetIndexChunk();
            if (!IsIndexChunkRangeValid(indexChunk))
            {
                PushInstrumentError(messages::bank::cKeyRegionInvalidFormat, mId);
                break;
            }

            const std::vector<u8> borders = CollectIndexChunkBorders(indexChunk, messages::bank::cKeyRegionInvalidFormat, mId);

            readKeyRegions_(indexChunk, indexChunk.min, borders.data(), static_cast<u32>(borders.size()), messages::bank::cKeyRegionInvalidFormat, waveIdTable);
            break;
        }

        default:
            PushInstrumentError(messages::bank::cInvalidInstrumentTypeFormat, mId);
            break;
    }

    if (mKeyRegionList.isEmpty())
        PushInstrumentError(messages::bank::cKeyRegionEmptyFormat, mId);
}

sead::FixedSafeString<256> BankFile::Instrument::getFormattedName() const
{
    sead::FixedSafeString<256> name;

    if (isNameValid())
        name.appendWithFormat("[%d] %s", mProgramNo, mName.cstr());
    else
        name.appendWithFormat(messages::bank::cInstrumentNameFormat, mProgramNo);

    return name;
}

BankFile::VelocityRegion::~VelocityRegion()
{
    sSoundPlayer.forgetBankRegion(*this);
}

BankFile::~BankFile()
{
    if (sSoundPlayer.isCurrentPlayerSequence() && sSoundPlayer.isActive())
    {
        sSoundPlayer.invalidateBankFile(*this);
    }
}

const Item* BankFile::validate(sead::BufferedSafeString& error) const
{
    std::unordered_set<s16> programNos;

    u32 i = 0;
    for (const Item* instrumentItem : getInstrumentList())
    {
        SEAD_ASSERT(instrumentItem->getItemType() == Item::ItemType::BankFileInstrument);
        const BankFile::Instrument* instrument = static_cast<const BankFile::Instrument*>(instrumentItem);

        if (instrument->getProgramNo() < Instrument::cProgramNoMin || instrument->getProgramNo() > Instrument::cProgramNoMax)
        {
            error.format(messages::bank::cInvalidProgramNumberFormat, i);
            return instrument;
        }

        if (programNos.count(instrument->getProgramNo()) > 0)
        {
            error.format(messages::bank::cProgramNumberExistsFormat, i, instrument->getProgramNo());
            return instrument;
        }

        programNos.insert(instrument->getProgramNo());

        if (instrument->getKeyRegionList().isEmpty())
        {
            error.format(messages::bank::cInstrumentNoKeyRegionFormat, i);
            return instrument;
        }

        for (const Item* keyRegionItem : instrument->getKeyRegionList())
        {
            SEAD_ASSERT(keyRegionItem->getItemType() == Item::ItemType::BankFileKeyRegion);
            const BankFile::KeyRegion* keyRegion = static_cast<const BankFile::KeyRegion*>(keyRegionItem);

            if (keyRegion->getVelocityRegionList().isEmpty())
            {
                error.format(messages::bank::cKeyRegionNoVelocityRegionFormat, i);
                return instrument;
            }

            for (const Item* velocityRegionItem : keyRegion->getVelocityRegionList())
            {
                SEAD_ASSERT(velocityRegionItem->getItemType() == Item::ItemType::BankFileVelocityRegion);
                const BankFile::VelocityRegion* velocityRegion = static_cast<const BankFile::VelocityRegion*>(velocityRegionItem);

                if (!velocityRegion->getWaveFileRef().isAttached())
                {
                    error.format(messages::bank::cVelocityRegionNoWaveFileFormat, i);
                    return instrument;
                }
            }
        }

        i++;
    }

    return nullptr;
}

void CopyVelocityRegion(BankFile::VelocityRegion* dst, const BankFile::VelocityRegion* src)
{
    dst->getWaveFileRef().attach(const_cast<Item*>(src->getWaveFileRef().getItem()));
    dst->setRootKey(src->getRootKey());
    dst->setVolume(src->getVolume());
    dst->setPan(src->getPan());
    dst->setPitch(src->getPitch());
    dst->setIsIgnoreNoteOff(src->getIsIgnoreNoteOff());
    dst->setKeyGroup(src->getKeyGroup());
    dst->setInterpolationType(src->getInterpolationType());
    dst->setAdshrCurve(src->getAdshrCurve());
}

s16 FindNextAvailableProgramNo(const Item::List &instrumentList, const BankFile::Instrument *skipInstrument)
{
    std::unordered_set<s16> used;
    for (const Item *it : instrumentList)
    {
        if (it == skipInstrument)
            continue;

        used.insert(static_cast<const BankFile::Instrument *>(it)->getProgramNo());
    }

    s16 prog = 0;

    while (used.count(prog))
        prog++;

    return prog;
}

struct SharedProgramNoCache
{
    const sead::TList<Item*>* instrumentList;
    std::vector<s16> sharedProgramNos;
};

static std::vector<SharedProgramNoCache> sSharedProgramNoCaches;
static s32 sSharedProgramNoCacheFrame = -1;

static std::vector<s16> CollectSharedProgramNos(const sead::TList<Item*>& instrumentList)
{
    std::vector<s16> programNos;
    programNos.reserve(instrumentList.size());

    for (const Item* item : instrumentList)
        programNos.push_back(static_cast<const BankFile::Instrument*>(item)->getProgramNo());

    std::sort(programNos.begin(), programNos.end());

    std::vector<s16> sharedProgramNos;

    for (size_t i = 1; i < programNos.size(); i++)
    {
        if (programNos[i] == programNos[i - 1] && (sharedProgramNos.empty() || sharedProgramNos.back() != programNos[i]))
            sharedProgramNos.push_back(programNos[i]);
    }

    return sharedProgramNos;
}

static const std::vector<s16>& FindSharedProgramNos(const sead::TList<Item*>& instrumentList)
{
    const s32 frame = ImGui::GetFrameCount();

    if (sSharedProgramNoCacheFrame != frame)
    {
        sSharedProgramNoCaches.clear();
        sSharedProgramNoCacheFrame = frame;
    }

    for (const SharedProgramNoCache& cache : sSharedProgramNoCaches)
    {
        if (cache.instrumentList == &instrumentList)
            return cache.sharedProgramNos;
    }

    sSharedProgramNoCaches.push_back({ &instrumentList, CollectSharedProgramNos(instrumentList) });

    return sSharedProgramNoCaches.back().sharedProgramNos;
}

bool IsProgramNoShared(const BankFile::Instrument* instrument)
{
    const sead::TList<Item*>* instrumentList = instrument->list();

    if (!instrumentList)
        return false;

    const std::vector<s16>& sharedProgramNos = FindSharedProgramNos(*instrumentList);

    return std::binary_search(sharedProgramNos.begin(), sharedProgramNos.end(), instrument->getProgramNo());
}

bool BankFile::doRead(const void* fileAddr)
{
    LOG_FUNC();
    nw::snd::internal::BankFileReader reader(fileAddr);
    if (!reader.IsInitialized())
    {
        LOG("BankFile::doRead - reader not initialized\n");
        return false;
    }

    LOG_S32("GetInstrumentCount", reader.GetInstrumentCount());

    if (reader.GetInstrumentCount() < 0 || reader.GetInstrumentCount() > cInstrumentCountMax)
    {
        sead::FormatFixedSafeString<256> msg(messages::bank::cInstrumentCountInvalidFormat, reader.GetInstrumentCount());
        PopupMgr::instance()->pushCurrentItemError(msg);
        return false;
    }

    using InstrumentItemPair = std::pair<s16, const nw::snd::internal::BankFile::Instrument*>;
    std::vector<InstrumentItemPair> instruments;

    for (s32 programNo = 0; programNo < reader.GetInstrumentCount(); programNo++)
    {
        const nw::snd::internal::BankFile::Instrument* instrumentInfo = reader.GetInstrument(programNo);
        if (!instrumentInfo)
            continue;

        instruments.emplace_back(s16(programNo), instrumentInfo);
    }

    std::sort(instruments.begin(), instruments.end(), [](const InstrumentItemPair& a, const InstrumentItemPair& b) -> bool
        {
            return a.second < b.second;
        }
    );

    LOG_SIZE("instruments", instruments.size());

    for (const InstrumentItemPair& pair : instruments)
    {
        const s16& programNo = pair.first;
        const nw::snd::internal::BankFile::Instrument* const& instrumentInfo = pair.second;

        Instrument* instrument = new Instrument();
        instrument->mId = mInstrumentList.size();

        instrument->mEnableName = true;
        instrument->mName = "Instrument";

        instrument->mProgramNo = programNo;
        instrument->read(instrumentInfo, *reader.GetWaveIdTable());

        mInstrumentList.pushBack(instrument);
    }

    LOG_SIZE("mInstrumentList", mInstrumentList.size());
    return true;
}

namespace
{

struct KeyRegionWriteInfo
{
    KeyRegionWriteInfo(u8 keyMin_, u8 keyMax_)
        : isNull(true)
        , keyMin(keyMin_)
        , keyMax(keyMax_)
    {
    }

    KeyRegionWriteInfo(const BankFile::KeyRegion* keyRegion)
        : isNull(false)
        , keyMin(keyRegion->getKeyMin())
        , keyMax(keyRegion->getKeyMax())
    {
        std::vector<const BankFile::VelocityRegion*> velocityRegions;
        for (const Item* velocityRegionItem : keyRegion->getVelocityRegionList())
        {
            velocityRegions.push_back(static_cast<const BankFile::VelocityRegion*>(velocityRegionItem));
        }

        std::sort(velocityRegions.begin(), velocityRegions.end(), [](const BankFile::VelocityRegion* velocityRegionA, const BankFile::VelocityRegion* velocityRegionB) -> bool
            {
                return velocityRegionA->getVelocityMin() < velocityRegionB->getVelocityMin();
            }
        );

        u8 velocityMin = 0;
        for (const BankFile::VelocityRegion* velocityRegion : velocityRegions)
        {
            SEAD_ASSERT(velocityRegion->getVelocityMin() <= velocityMin && velocityMin <= velocityRegion->getVelocityMax());

            sortedVelocityRegions.emplace_back(velocityRegion);
            velocityMin = velocityRegion->getVelocityMax() + 1;
        }

        if (sortedVelocityRegions.size() == 1 && sortedVelocityRegions[0]->getVelocityMin() == 0 && sortedVelocityRegions[0]->getVelocityMax() == 127)
        {
            tableType = nw::snd::internal::ElementType_BankFile_DirectReferenceTable;
            return;
        }

        SEAD_ASSERT(sortedVelocityRegions[0]->getVelocityMin() == 0);

        s32 rangeMax = sortedVelocityRegions[sortedVelocityRegions.size() - 1]->getVelocityMax();
        SEAD_ASSERT(rangeMax >= 127);

        tableType = nw::snd::internal::ElementType_BankFile_RangeReferenceTable;
    }

    bool isNull;
    u8 keyMin;
    u8 keyMax;
    std::vector<const BankFile::VelocityRegion*> sortedVelocityRegions;
    u16 tableType;
};

struct InstrumentWriteInfo
{
    InstrumentWriteInfo(const BankFile::Instrument* instrument)
        : programNo(-1)
        , isNull(instrument == nullptr)
        , tableType(0)
    {
        if (!instrument)
            return;

        programNo = instrument->getProgramNo();

        collectKeyRegions_(instrument);

        if (sortedKeyRegions.size() == 1 && sortedKeyRegions[0].keyMin == 0 && sortedKeyRegions[0].keyMax == 127)
        {
            tableType = nw::snd::internal::ElementType_BankFile_DirectReferenceTable;
            return;
        }

        s32 num = (sortedKeyRegions[0].keyMin > 0) ? (sortedKeyRegions.size() + 1) : sortedKeyRegions.size();
        if (num <= 11)
        {
            fillRangeTable_();
            tableType = nw::snd::internal::ElementType_BankFile_RangeReferenceTable;
            return;
        }

        fillIndexTable_();
        tableType = nw::snd::internal::ElementType_BankFile_IndexReferenceTable;
    }

    s16 programNo;
    bool isNull;
    std::vector<KeyRegionWriteInfo> sortedKeyRegions;
    u16 tableType;

private:
    void collectKeyRegions_(const BankFile::Instrument* instrument)
    {
        std::vector<const BankFile::KeyRegion*> keyRegions;
        for (const Item* keyRegionItem : instrument->getKeyRegionList())
        {
            keyRegions.push_back(static_cast<const BankFile::KeyRegion*>(keyRegionItem));
        }

        std::sort(keyRegions.begin(), keyRegions.end(), [](const BankFile::KeyRegion* keyRegionA, const BankFile::KeyRegion* keyRegionB) -> bool
            {
                return keyRegionA->getKeyMin() < keyRegionB->getKeyMin();
            }
        );

        u8 keyMin = 0;
        for (const BankFile::KeyRegion* keyRegion : keyRegions)
        {
            SEAD_ASSERT(keyMin <= keyRegion->getKeyMax());
            if (keyMin < keyRegion->getKeyMin())
            {
                sortedKeyRegions.emplace_back(keyMin, keyRegion->getKeyMin() - 1);
            }

            sortedKeyRegions.emplace_back(keyRegion);
            keyMin = keyRegion->getKeyMax() + 1;
        }
    }

    void fillRangeTable_()
    {
        if (sortedKeyRegions[0].keyMin > 0)
        {
            sortedKeyRegions.insert(sortedKeyRegions.begin(), KeyRegionWriteInfo(0, sortedKeyRegions[0].keyMin - 1));
        }

        s32 rangeMax = sortedKeyRegions[sortedKeyRegions.size() - 1].keyMax;
        if (rangeMax < 127)
        {
            sortedKeyRegions.emplace_back(rangeMax + 1, 127);
        }
    }

    void fillIndexTable_()
    {
        const std::vector<KeyRegionWriteInfo> copy = sortedKeyRegions;
        sortedKeyRegions.clear();
        SEAD_ASSERT(copy.size() > 0);

        s32 rangeMin = copy[0].keyMin;
        s32 rangeMax = copy[copy.size() - 1].keyMax;
        SEAD_ASSERT_MSG(rangeMax - rangeMin + 1 >= 11, "IndexReferenceTable cannot apply.");

        s32 num = 0;
        for (s32 i = rangeMin; i <= rangeMax; i++)
        {
            while (copy[num].keyMax < i)
            {
                num++;
                SEAD_ASSERT_MSG(num < copy.size(), "failed to create index reference table.");
            }

            SEAD_ASSERT_MSG(i >= copy[num].keyMin, "null object not found.");

            sortedKeyRegions.emplace_back(copy[num]);
        }
    }
};

struct BankWriteContext
{
    BankWriteContext(sead::FileHandle* handle, sead::WriteStream* stream_, u32 maskedWaveArchiveId, const std::unordered_map<const WaveFile*, u32>* waveArchiveIndexes)
        : writer(handle, stream_)
        , stream(stream_)
        , waveIds(maskedWaveArchiveId, waveArchiveIndexes)
    {
    }

    FileWriter writer;
    sead::WriteStream* stream;
    WaveIdTableWriter waveIds;
    std::vector<InstrumentWriteInfo> instruments;
};

}

static const WaveFile* GetRegionWaveFile(const BankFile::VelocityRegion& velocityRegion)
{
    const Item* waveFileItem = velocityRegion.getWaveFileRef().getItem();
    SEAD_ASSERT(waveFileItem);
    SEAD_ASSERT(waveFileItem->getItemType() == Item::ItemType::WaveFile);

    return static_cast<const WaveFile*>(waveFileItem);
}

static void CollectWaveIds(BankWriteContext& context, const Item::List& instrumentList)
{
    for (const Item* instrumentItem : instrumentList)
    {
        SEAD_ASSERT(instrumentItem->getItemType() == Item::ItemType::BankFileInstrument);
        const BankFile::Instrument* instrument = static_cast<const BankFile::Instrument*>(instrumentItem);

        ForEachVelocityRegion(*instrument, [&](const BankFile::KeyRegion&, const BankFile::VelocityRegion& velocityRegion)
        {
            context.waveIds.add(GetRegionWaveFile(velocityRegion));
        });
    }
}

static void BuildInstrumentTable(BankWriteContext& context, const BankFile& bankFile)
{
    std::unordered_map<u32, const BankFile::Instrument*> instrumentMap;

    for (const Item* instrumentItem : bankFile.getInstrumentList())
    {
        SEAD_ASSERT(instrumentItem->getItemType() == Item::ItemType::BankFileInstrument);
        const BankFile::Instrument* instrument = static_cast<const BankFile::Instrument*>(instrumentItem);

        if (instrumentMap.contains(instrument->getProgramNo()))
        {
            SEAD_ASSERT_MSG(false, "'%s': program no %u already exists", bankFile.getFormattedName().cstr(), instrument->getProgramNo());
            continue;
        }

        instrumentMap[instrument->getProgramNo()] = instrument;
    }

    if (instrumentMap.empty())
        return;

    u32 validInstrumentCount = 0;
    for (u32 i = 0; i < 32767; i++)
    {
        if (!instrumentMap.contains(i))
        {
            context.instruments.push_back(InstrumentWriteInfo(nullptr));
            continue;
        }

        context.instruments.push_back(InstrumentWriteInfo(instrumentMap[i]));
        validInstrumentCount++;

        if (validInstrumentCount >= instrumentMap.size())
            break;
    }
}

static void WriteVelocityRegion(BankWriteContext& context, const BankFile::VelocityRegion& velocityRegion)
{
    sead::WriteStream* stream = context.stream;

    stream->writeU32(context.waveIds.getTableIndex(GetRegionWaveFile(velocityRegion)));

    stream->writeU32(nw::snd::internal::VelocityRegionBitFlag::VELOCITY_REGION_BASIC_PARAM_FLAG);
    stream->writeU32(velocityRegion.getRootKey());
    stream->writeU32(velocityRegion.getVolume());
    stream->writeU32(velocityRegion.getPan());
    stream->writeF32(velocityRegion.getPitch());
    stream->writeU32(velocityRegion.getIsIgnoreNoteOff() | (velocityRegion.getKeyGroup() << 8) | (velocityRegion.getInterpolationType() << 16));
    stream->writeU32(0x20); // Envelope

    stream->writeU32(0x0); // AdshrCurveRef type
    stream->writeU32(0x8); // AdshrCurveRef offset

    const snd::AdshrCurve& adshrCurve = velocityRegion.getAdshrCurve();
    stream->writeU8(adshrCurve.attack);
    stream->writeU8(adshrCurve.decay);
    stream->writeU8(adshrCurve.sustain);
    stream->writeU8(adshrCurve.hold);
    stream->writeU8(adshrCurve.release);

    context.writer.align(0x4);
}

static void WriteVelocityRegionDirectTable(BankWriteContext& context, const std::vector<const BankFile::VelocityRegion*>& velocityRegions)
{
    FileWriter& writer = context.writer;

    writer.openReference("VelocityRegion");

    writer.closeReference("VelocityRegion", nw::snd::internal::ElementType_BankFile_VelocityRegionInfo);

    SEAD_ASSERT(velocityRegions.size() == 1);
    writer.pushOffsetBase();
    {
        WriteVelocityRegion(context, *velocityRegions[0]);
    }
    writer.popOffsetBase();
}

static void WriteVelocityRegionRangeTable(BankWriteContext& context, const std::vector<const BankFile::VelocityRegion*>& velocityRegions)
{
    FileWriter& writer = context.writer;

    SEAD_ASSERT(velocityRegions.size() > 0);

    context.stream->writeU32(velocityRegions.size()); // BorderTable size

    for (const BankFile::VelocityRegion* velocityRegion : velocityRegions)
    {
        context.stream->writeU8(velocityRegion->getVelocityMax());
    }

    writer.align(0x4);

    for (u32 i = 0; i < velocityRegions.size(); i++)
    {
        writer.openReference(sead::FormatFixedSafeString<32>("VelocityRegion%u", i));
    }

    for (u32 i = 0; i < velocityRegions.size(); i++)
    {
        writer.closeReference(sead::FormatFixedSafeString<32>("VelocityRegion%u", i), nw::snd::internal::ElementType_BankFile_VelocityRegionInfo);

        writer.pushOffsetBase();
        {
            WriteVelocityRegion(context, *velocityRegions[i]);
        }
        writer.popOffsetBase();
    }
}

static void WriteKeyRegion(BankWriteContext& context, const KeyRegionWriteInfo& keyRegion)
{
    FileWriter& writer = context.writer;

    writer.openReference("VelocityRegionChunk");

    if (keyRegion.tableType == 0)
    {
        writer.closeNullReference("VelocityRegionChunk");
        return;
    }

    writer.closeReference("VelocityRegionChunk", keyRegion.tableType);

    writer.pushOffsetBase();
    {
        switch (keyRegion.tableType)
        {
            case nw::snd::internal::ElementType_BankFile_DirectReferenceTable:
                WriteVelocityRegionDirectTable(context, keyRegion.sortedVelocityRegions);
                break;

            case nw::snd::internal::ElementType_BankFile_RangeReferenceTable:
                WriteVelocityRegionRangeTable(context, keyRegion.sortedVelocityRegions);
                break;

            case nw::snd::internal::ElementType_BankFile_IndexReferenceTable:
                break;
        }
    }
    writer.popOffsetBase();
}

static void WriteKeyRegionDirectTable(BankWriteContext& context, const std::vector<KeyRegionWriteInfo>& keyRegions)
{
    FileWriter& writer = context.writer;

    writer.openReference("KeyRegion");

    writer.closeReference("KeyRegion", nw::snd::internal::ElementType_BankFile_KeyRegionInfo);

    SEAD_ASSERT(keyRegions.size() == 1);
    writer.pushOffsetBase();
    {
        WriteKeyRegion(context, keyRegions[0]);
    }
    writer.popOffsetBase();
}

static void WriteKeyRegionRangeTable(BankWriteContext& context, const std::vector<KeyRegionWriteInfo>& keyRegions)
{
    FileWriter& writer = context.writer;

    SEAD_ASSERT(keyRegions.size() > 0);

    context.stream->writeU32(keyRegions.size()); // BorderTable size

    for (const KeyRegionWriteInfo& keyRegion : keyRegions)
    {
        context.stream->writeU8(keyRegion.keyMax);
    }

    writer.align(0x4);

    for (u32 i = 0; i < keyRegions.size(); i++)
    {
        writer.openReference(sead::FormatFixedSafeString<32>("KeyRegion%u", i));
    }

    for (u32 i = 0; i < keyRegions.size(); i++)
    {
        const KeyRegionWriteInfo& keyRegion = keyRegions[i];

        sead::FormatFixedSafeString<32> refName("KeyRegion%u", i);
        if (keyRegion.isNull)
        {
            writer.closeReference(refName, nw::snd::internal::ElementType_BankFile_NullInfo, nw::snd::internal::Util::Reference::INVALID_OFFSET);
            continue;
        }

        writer.closeReference(refName, nw::snd::internal::ElementType_BankFile_KeyRegionInfo);

        writer.pushOffsetBase();
        {
            WriteKeyRegion(context, keyRegion);
        }
        writer.popOffsetBase();
    }
}

static void WriteKeyRegionIndexTable(BankWriteContext& context, const std::vector<KeyRegionWriteInfo>& keyRegions)
{
    FileWriter& writer = context.writer;

    SEAD_ASSERT(keyRegions.size() > 0);

    u32 startPos = writer.getPosition();

    context.stream->writeU8(keyRegions[0].keyMin);
    context.stream->writeU8(keyRegions[keyRegions.size() - 1].keyMax);
    context.stream->writeU16(0); // Padding

    for (u32 i = 0; i < keyRegions.size(); i++)
    {
        writer.openReference(sead::FormatFixedSafeString<32>("KeyRegion%u", i));
    }

    s32 prevKeyRegionPos = 0;
    for (u32 i = 0; i < keyRegions.size(); i++)
    {
        const KeyRegionWriteInfo& keyRegion = keyRegions[i];

        sead::FormatFixedSafeString<32> refName("KeyRegion%u", i);
        if (keyRegion.isNull)
        {
            writer.closeReference(refName, nw::snd::internal::ElementType_BankFile_NullInfo, nw::snd::internal::Util::Reference::INVALID_OFFSET);
            continue;
        }

        if (i != 0 && keyRegion.keyMin == keyRegions[i - 1].keyMin)
        {
            writer.closeReference(refName, nw::snd::internal::ElementType_BankFile_KeyRegionInfo, prevKeyRegionPos - startPos);
            continue;
        }

        writer.closeReference(refName, nw::snd::internal::ElementType_BankFile_KeyRegionInfo);

        prevKeyRegionPos = writer.getPosition();

        writer.pushOffsetBase();
        {
            WriteKeyRegion(context, keyRegion);
        }
        writer.popOffsetBase();
    }
}

static void WriteInstrument(BankWriteContext& context, const InstrumentWriteInfo& instrument)
{
    FileWriter& writer = context.writer;

    writer.openReference("KeyRegionChunk");

    if (instrument.tableType == 0)
    {
        writer.closeNullReference("KeyRegionChunk");
        return;
    }

    writer.closeReference("KeyRegionChunk", instrument.tableType);

    writer.pushOffsetBase();
    {
        switch (instrument.tableType)
        {
            case nw::snd::internal::ElementType_BankFile_DirectReferenceTable:
                WriteKeyRegionDirectTable(context, instrument.sortedKeyRegions);
                break;

            case nw::snd::internal::ElementType_BankFile_RangeReferenceTable:
                WriteKeyRegionRangeTable(context, instrument.sortedKeyRegions);
                break;

            case nw::snd::internal::ElementType_BankFile_IndexReferenceTable:
                WriteKeyRegionIndexTable(context, instrument.sortedKeyRegions);
                break;
        }
    }
    writer.popOffsetBase();
}

static void WriteInstrumentTable(BankWriteContext& context, const Item::List& instrumentList)
{
    FileWriter& writer = context.writer;

    context.stream->writeU32(context.instruments.size());

    for (const InstrumentWriteInfo& instrument : context.instruments)
    {
        sead::FormatFixedSafeString<32> refName("Instrument%i", instrument.programNo);
        writer.openReference(refName);

        if (instrument.isNull)
        {
            writer.closeReference(refName, nw::snd::internal::ElementType_BankFile_NullInfo, -1);
        }
    }

    for (const Item* instrumentItem : instrumentList)
    {
        const BankFile::Instrument* instrument = static_cast<const BankFile::Instrument*>(instrumentItem);

        writer.closeReference(sead::FormatFixedSafeString<32>("Instrument%i", instrument->getProgramNo()), nw::snd::internal::ElementType_BankFile_InstrumentInfo);

        writer.pushOffsetBase();
        {
            WriteInstrument(context, context.instruments[instrument->getProgramNo()]);
        }
        writer.popOffsetBase();
    }
}

static void WriteInfoBlock(BankWriteContext& context, const Item::List& instrumentList)
{
    FileWriter& writer = context.writer;

    writer.openBlock(nw::snd::internal::ElementType_BankFile_InfoBlock, "INFO");

    writer.openReference("WaveIdTable");
    writer.openReference("InstrumentTableRef");

    writer.closeReference("InstrumentTableRef", nw::snd::internal::ElementType_Table_ReferenceTable);
    writer.pushOffsetBase();
    {
        WriteInstrumentTable(context, instrumentList);
    }
    writer.popOffsetBase();

    writer.closeReference("WaveIdTable", nw::snd::internal::ElementType_Table_EmbeddingTable);

    context.waveIds.write(context.stream);

    writer.closeBlock();
}

u32 BankFile::doWrite(sead::FileHandle* handle, sead::WriteStream* stream, bool isLast) const
{
    LOG_FUNC();
    LOG_SIZE("getInstrumentList()", getInstrumentList().size());
    SEAD_ASSERT(mBank);
    SEAD_ASSERT(mWaveArchive);

    u32 maskedWaveArchiveId = nw::snd::internal::Util::GetMaskedItemId(mWaveArchive->getId(), nw::snd::internal::ItemType_WaveArchive);

    BankWriteContext context(handle, stream, maskedWaveArchiveId, mWaveArchiveWaveFilesIndexes);
    CollectWaveIds(context, getInstrumentList());
    BuildInstrumentTable(context, *this);

    LOG_SIZE("waveIds", context.waveIds.getCount());
    LOG_SIZE("instruments (pre-write)", context.instruments.size());

    context.writer.openFile(GetInnerFileMagic(mFormat, InnerFileKind::Bank), 1, mVersion);

    WriteInfoBlock(context, getInstrumentList());

    u32 fileSize = context.writer.getPosition();
    LOG_U32("fileSize", fileSize);

    context.writer.closeFile();

    mBank = nullptr;
    mWaveArchive = nullptr;
    mWaveArchiveWaveFilesIndexes = nullptr;

    return fileSize;
}
