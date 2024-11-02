#include <bfsar/BankFile.h>

#include <ui/UI.h>

#include <VectorSet.h>

#include <algorithm>
#include <functional>
#include <vector>

extern SequenceSoundPlayer sSequencePlayer;

enum ImGuiPianoKeyboardMsg
{
    NoteGetStatus,
    NoteOn,
    NoteOff,
};

using ImGuiPianoKeyboardProc = bool (*)(void* UserData, s32 Msg, s32 Key, f32 Vel);

struct ImGuiPianoStyles
{
    ImU32 Colors[5] {
        IM_COL32(255, 255, 255, 255), // light note
        IM_COL32(0, 0, 0, 255),       // dark note
        IM_COL32(255, 255, 0, 255),   // active light note
        IM_COL32(200, 200, 0, 255),   // active dark note
        IM_COL32(75, 75, 75, 255),    // background
    };

    f32 NoteDarkHeight = 2.0f / 3.0f; // dark note scale h
    f32 NoteDarkWidth  = 2.0f / 3.0f; // dark note scale w
};

void ImGui_PianoKeyboard(const char* IDName, ImVec2 Size, s32* PrevNoteActive, s32 BeginOctaveNote, s32 EndOctaveNote, ImGuiPianoKeyboardProc Callback, void* UserData, ImGuiPianoStyles* Style = nullptr);

static bool KeyPresed[128] = { false };

bool TestPianoBoardFunct(void* UserData, s32 Msg, s32 Key, f32 Vel)
{
    if (!sSelectedItem || sSelectedItem->getItemType() != Item::ItemType::BankFileInstrument)
    {
        return false;
    }

    BankFile::Instrument* instrument = static_cast<BankFile::Instrument*>(sSelectedItem);

    if (Key < 0 || Key >= 128)
    {
        return false; // midi max keys
    }

    if (Msg == NoteGetStatus)
    {
        return KeyPresed[Key];
    }
    else if (Msg == NoteOn)
    {
        KeyPresed[Key] = true;
        //Send_Midi_NoteOn(Key, Vel*127);

        u8 vel = Vel * 127;

        const BankFile::KeyRegion* keyRegion = instrument->getKeyRegion(Key);
        if (!keyRegion)
        {
            return false;
        }

        const BankFile::VelocityRegion* velocityRegion = keyRegion->getVelocityRegion(vel);
        SEAD_ASSERT(velocityRegion);

        PlayBankNote(Key, vel, *velocityRegion);
    }
    else if (Msg == NoteOff)
    {
        KeyPresed[Key] = false;
        //Send_Midi_NoteOff(Key, Vel*127);

        StopAllSoundPlayers();
    }

    return false;
}

void BankFile::VelocityRegion::read(const nw::snd::internal::BankFile::VelocityRegion* velocityRegionInfo, const nw::snd::internal::Util::WaveIdTable& waveIdTable)
{
    const nw::snd::internal::Util::WaveId* waveId = waveIdTable.GetWaveId(velocityRegionInfo->waveIdTableIndex);
    SEAD_ASSERT(waveId);

    //? waveId->waveIndex is patched with global wave index already
    SEAD_ASSERT(waveId->waveArchiveId == 0);
    WaveFile* waveFile = static_cast<WaveFile*>(sBfsar.getItem(waveId->waveIndex, sBfsar.getWaveFileList()));
    mWaveFileRef.attach(waveFile);

    mOriginalKey = velocityRegionInfo->GetOriginalKey();
    mVolume = velocityRegionInfo->GetVolume();
    mPan = velocityRegionInfo->GetPan();
    mPitch = velocityRegionInfo->GetPitch();
    mIsIgnoreNoteOff = velocityRegionInfo->IsIgnoreNoteOff();
    mKeyGroup = velocityRegionInfo->GetKeyGroup();
    mInterpolationType = velocityRegionInfo->GetInterpolationType();

    const nw::snd::AdshrCurve& adshrCurveInfo = velocityRegionInfo->GetAdshrCurve();
    mAdshrCurve.attack = adshrCurveInfo.attack;
    mAdshrCurve.decay = adshrCurveInfo.decay;
    mAdshrCurve.sustain = adshrCurveInfo.sustain;
    mAdshrCurve.hold = adshrCurveInfo.hold;
    mAdshrCurve.release = adshrCurveInfo.release;
}

void BankFile::KeyRegion::read(const nw::snd::internal::BankFile::KeyRegion* keyRegionInfo, const nw::snd::internal::Util::WaveIdTable& waveIdTable)
{
    SEAD_ASSERT(keyRegionInfo);

    switch (nw::snd::internal::GetRegionType(keyRegionInfo->toVelocityRegionChunk.typeId))
    {
        case nw::snd::internal::REGION_TYPE_DIRECT:
        {
            const nw::snd::internal::DirectChunk& directChunk = keyRegionInfo->GetDirectChunk();
            SEAD_ASSERT(directChunk.toRegion.typeId == nw::snd::internal::ElementType_BankFile_VelocityRegionInfo);
            SEAD_ASSERT(directChunk.toRegion.offset != nw::snd::internal::Util::Reference::INVALID_OFFSET);

            const nw::snd::internal::BankFile::VelocityRegion* velocityRegionInfo = static_cast<const nw::snd::internal::BankFile::VelocityRegion*>(directChunk.GetRegion());

            VelocityRegion* velocityRegion = new VelocityRegion(0, 127);
            velocityRegion->mId = 0;

            velocityRegion->mEnableName = true;
            velocityRegion->mName = "VelocityRegion";

            velocityRegion->read(velocityRegionInfo, waveIdTable);

            mVelocityRegionList.pushBack(velocityRegion);
            break;
        }

        case nw::snd::internal::REGION_TYPE_RANGE:
        {
            const nw::snd::internal::RangeChunk& rangeChunk = keyRegionInfo->GetRangeChunk();

            u32 velocityRegionCount = rangeChunk.borderTable.count;

            u8 velocityMin = 0;
            for (u32 i = 0; i < velocityRegionCount; i++)
            {
                const nw::snd::internal::Util::Reference* velocityRegionRef = static_cast<const nw::snd::internal::Util::Reference*>(rangeChunk.GetRegionRef(velocityMin));
                SEAD_ASSERT(velocityRegionRef);
                SEAD_ASSERT(velocityRegionRef->typeId == nw::snd::internal::ElementType_BankFile_VelocityRegionInfo);
                SEAD_ASSERT(velocityRegionRef->offset != nw::snd::internal::Util::Reference::INVALID_OFFSET);

                const nw::snd::internal::BankFile::VelocityRegion* velocityRegionInfo = static_cast<const nw::snd::internal::BankFile::VelocityRegion*>(rangeChunk.GetRegion(velocityMin));
                SEAD_ASSERT(velocityRegionInfo);

                u8 velocityMax = rangeChunk.borderTable.item[i];

                VelocityRegion* velocityRegion = new VelocityRegion(velocityMin, velocityMax);
                velocityRegion->mId = mVelocityRegionList.size();

                velocityRegion->mEnableName = true;
                velocityRegion->mName = "VelocityRegion";

                velocityRegion->read(velocityRegionInfo, waveIdTable);

                mVelocityRegionList.pushBack(velocityRegion);

                velocityMin = velocityMax + 1;
            }

            break;
        }

        case nw::snd::internal::REGION_TYPE_INDEX:
        {
            const nw::snd::internal::IndexChunk& indexChunk = keyRegionInfo->GetIndexChunk();

            std::vector<u8> borders;

            s32 prevOffset = nw::snd::internal::Util::Reference::INVALID_OFFSET;
            for (u32 i = indexChunk.min; i <= indexChunk.max; i++)
            {
                const nw::snd::internal::Util::Reference* velocityRegionRef = static_cast<const nw::snd::internal::Util::Reference*>(indexChunk.GetRegionRef(i));
                SEAD_ASSERT(velocityRegionRef);

                if (velocityRegionRef->offset != prevOffset)
                {
                    if (i != 0)
                    {
                        borders.push_back(i - 1);
                    }
                }

                prevOffset = velocityRegionRef->offset;
            }

            borders.push_back(indexChunk.max);

            u8 velocityMin = 0;
            for (u32 i = 0; i < borders.size(); i++)
            {
                const nw::snd::internal::Util::Reference* velocityRegionRef = static_cast<const nw::snd::internal::Util::Reference*>(indexChunk.GetRegionRef(velocityMin));
                SEAD_ASSERT(velocityRegionRef);
                SEAD_ASSERT(velocityRegionRef->typeId == nw::snd::internal::ElementType_BankFile_VelocityRegionInfo);
                SEAD_ASSERT(velocityRegionRef->offset != nw::snd::internal::Util::Reference::INVALID_OFFSET);

                const nw::snd::internal::BankFile::VelocityRegion* velocityRegionInfo = static_cast<const nw::snd::internal::BankFile::VelocityRegion*>(indexChunk.GetRegion(velocityMin));
                SEAD_ASSERT(velocityRegionInfo);

                u8 velocityMax = borders[i];

                VelocityRegion* velocityRegion = new VelocityRegion(velocityMin, velocityMax);
                velocityRegion->mId = mVelocityRegionList.size();

                velocityRegion->mEnableName = true;
                velocityRegion->mName = "VelocityRegion";

                velocityRegion->read(velocityRegionInfo, waveIdTable);

                mVelocityRegionList.pushBack(velocityRegion);

                velocityMin = velocityMax + 1;
            }
        }

        default:
            SEAD_ASSERT(false);
            break;
    }

    SEAD_ASSERT(mVelocityRegionList.size() > 0);
}

void BankFile::Instrument::read(const nw::snd::internal::BankFile::Instrument* instrumentInfo, const nw::snd::internal::Util::WaveIdTable& waveIdTable)
{
    SEAD_ASSERT(instrumentInfo);

    switch (nw::snd::internal::GetRegionType(instrumentInfo->toKeyRegionChunk.typeId))
    {
        case nw::snd::internal::REGION_TYPE_DIRECT:
        {
            const nw::snd::internal::DirectChunk& directChunk = instrumentInfo->GetDirectChunk();
            SEAD_ASSERT(directChunk.toRegion.typeId == nw::snd::internal::ElementType_BankFile_KeyRegionInfo);
            SEAD_ASSERT(directChunk.toRegion.offset != nw::snd::internal::Util::Reference::INVALID_OFFSET);

            const nw::snd::internal::BankFile::KeyRegion* keyRegionInfo = static_cast<const nw::snd::internal::BankFile::KeyRegion*>(directChunk.GetRegion());

            KeyRegion* keyRegion = new KeyRegion(0, 127);
            keyRegion->mId = 0;

            keyRegion->mEnableName = true;
            keyRegion->mName = "KeyRegion";

            keyRegion->read(keyRegionInfo, waveIdTable);

            mKeyRegionList.pushBack(keyRegion);
            break;
        }

        case nw::snd::internal::REGION_TYPE_RANGE:
        {
            const nw::snd::internal::RangeChunk& rangeChunk = instrumentInfo->GetRangeChunk();

            u32 keyRegionCount = rangeChunk.borderTable.count;

            u8 keyMin = 0;
            for (u32 i = 0; i < keyRegionCount; i++)
            {
                const nw::snd::internal::Util::Reference* keyRegionRef = static_cast<const nw::snd::internal::Util::Reference*>(rangeChunk.GetRegionRef(keyMin));
                SEAD_ASSERT(keyRegionRef);

                u8 keyMax = rangeChunk.borderTable.item[i];

                if (keyRegionRef->offset == nw::snd::internal::Util::Reference::INVALID_OFFSET)
                {
                    SEAD_ASSERT(keyRegionRef->typeId == nw::snd::internal::ElementType_BankFile_NullInfo);

                    keyMin = keyMax + 1;
                    continue;
                }
                else
                {
                    SEAD_ASSERT(keyRegionRef->typeId == nw::snd::internal::ElementType_BankFile_KeyRegionInfo);
                }

                const nw::snd::internal::BankFile::KeyRegion* keyRegionInfo = static_cast<const nw::snd::internal::BankFile::KeyRegion*>(rangeChunk.GetRegion(keyMin));
                SEAD_ASSERT(keyRegionInfo);

                KeyRegion* keyRegion = new KeyRegion(keyMin, keyMax);
                keyRegion->mId = mKeyRegionList.size();

                keyRegion->mEnableName = true;
                keyRegion->mName = "KeyRegion";

                keyRegion->read(keyRegionInfo, waveIdTable);

                mKeyRegionList.pushBack(keyRegion);

                keyMin = keyMax + 1;
            }

            break;
        }

        case nw::snd::internal::REGION_TYPE_INDEX:
        {
            const nw::snd::internal::IndexChunk& indexChunk = instrumentInfo->GetIndexChunk();

            std::vector<u8> borders;

            s32 prevOffset = nw::snd::internal::Util::Reference::INVALID_OFFSET;
            for (u32 i = indexChunk.min; i <= indexChunk.max; i++)
            {
                const nw::snd::internal::Util::Reference* keyRegionRef = static_cast<const nw::snd::internal::Util::Reference*>(indexChunk.GetRegionRef(i));
                SEAD_ASSERT(keyRegionRef);

                if (keyRegionRef->offset != prevOffset)
                {
                    if (i != 0)
                    {
                        borders.push_back(i - 1);
                    }
                }

                prevOffset = keyRegionRef->offset;
            }

            borders.push_back(indexChunk.max);

            u8 keyMin = 0;
            for (u32 i = 0; i < borders.size(); i++)
            {
                const nw::snd::internal::Util::Reference* keyRegionRef = static_cast<const nw::snd::internal::Util::Reference*>(indexChunk.GetRegionRef(keyMin));
                SEAD_ASSERT(keyRegionRef);

                u8 keyMax = borders[i];

                if (keyRegionRef->offset == nw::snd::internal::Util::Reference::INVALID_OFFSET)
                {
                    SEAD_ASSERT(keyRegionRef->typeId == nw::snd::internal::ElementType_BankFile_NullInfo);

                    keyMin = keyMax + 1;
                    continue;
                }
                else
                {
                    SEAD_ASSERT(keyRegionRef->typeId == nw::snd::internal::ElementType_BankFile_KeyRegionInfo);
                }

                const nw::snd::internal::BankFile::KeyRegion* keyRegionInfo = static_cast<const nw::snd::internal::BankFile::KeyRegion*>(indexChunk.GetRegion(keyMin));
                SEAD_ASSERT(keyRegionInfo);

                KeyRegion* keyRegion = new KeyRegion(keyMin, keyMax);
                keyRegion->mId = mKeyRegionList.size();

                keyRegion->mEnableName = true;
                keyRegion->mName = "KeyRegion";

                keyRegion->read(keyRegionInfo, waveIdTable);

                mKeyRegionList.pushBack(keyRegion);

                keyMin = keyMax + 1;
            }

            break;
        }

        default:
            SEAD_ASSERT(false);
            break;
    }

    SEAD_ASSERT(mKeyRegionList.size() > 0);
}

void BankFile::Instrument::drawUI()
{
    ImGui::Text("ProgramNo: %u", mProgramNo);

    for (Item* keyRegionItem : mKeyRegionList)
    {
        KeyRegion* keyRegion = static_cast<KeyRegion*>(keyRegionItem);

        if (ImGui::TreeNode(sead::FormatFixedSafeString<32>("KeyRegion (%u, %u)", keyRegion->getKeyMin(), keyRegion->getKeyMax()).cstr()))
        {
            for (Item* velocityRegionItem : keyRegion->getVelocityRegionList())
            {
                VelocityRegion* velocityRegion = static_cast<VelocityRegion*>(velocityRegionItem);

                if (ImGui::TreeNode(sead::FormatFixedSafeString<32>("VelocityRegion (%u, %u)", velocityRegion->getVelocityMin(), velocityRegion->getVelocityMax()).cstr()))
                {
                    {
                        Item* waveFile = velocityRegion->getWaveFileRef().getItem();
                        if (ItemSelector("Wave File", sBfsar.getWaveFileList(), &waveFile))
                        {
                            velocityRegion->getWaveFileRef().attach(waveFile);
                        }

                        ImGui::Text("Original Key: %u", velocityRegion->getOriginalKey());
                    }

                    ImGui::TreePop();
                }
            }

            ImGui::TreePop();
        }
    }
}

BankFile::~BankFile()
{
    if (sSequencePlayer.isActive())
    {
        snd::internal::driver::SoundThreadLock lock;
        sSequencePlayer.invalidateBankFile(*this);
    }
}

void BankFile::drawUI()
{
    InnerFile::drawUI();
}

void BankFile::drawFileUI()
{
    if (ImGui::BeginChild("Instruments", ImVec2(0.0f, ImGui::GetWindowHeight() / 2.0f), ImGuiChildFlags_Border | ImGuiChildFlags_ResizeY))
    {
        DrawAllItemsUI("Instrument", mInstrumentList);
    }
    ImGui::EndChild();

    if (ImGui::BeginChild("Keyboard", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border))
    {
        static s32 PrevNoteActive = -1;

        //ImVec2 size(1024, 70);
        ImVec2 size(ImGui::GetWindowContentRegionMax().x, 70);

        //ImDrawList* drawList = ImGui::GetWindowDrawList();

        //drawList->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(50.0f, 50.0f), ImColor(1.0f, 0.0f, 1.0f));

        ImGui_PianoKeyboard("PianoTest", size, &PrevNoteActive, 0, 127, TestPianoBoardFunct, sSelectedItem, nullptr);
    }
    ImGui::EndChild();
}

void BankFile::doRead(const void* fileAddr)
{
    nw::snd::internal::BankFileReader reader(fileAddr);
    SEAD_ASSERT(reader.IsInitialized());

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
}

u32 BankFile::doWrite(sead::FileHandle* handle, sead::WriteStream* stream, bool isLast) const
{
    SEAD_ASSERT(mBank);
    SEAD_ASSERT(mWaveArchive);

    struct WaveId
    {
        WaveId(u32 warcId_, u32 waveIdx_)
            : warcId(warcId_)
            , waveIdx(waveIdx_)
        {
        }

        u32 warcId;
        u32 waveIdx;
    };

    std::unordered_map<const WaveFile*, u32> waveIdIndexes;
    std::vector<WaveId> waveIds;

    for (const Item* instrumentItem : getInstrumentList())
    {
        SEAD_ASSERT(instrumentItem->getItemType() == Item::ItemType::BankFileInstrument);
        const BankFile::Instrument* instrument = static_cast<const BankFile::Instrument*>(instrumentItem);

        for (const Item* keyRegionItem : instrument->getKeyRegionList())
        {
            SEAD_ASSERT(keyRegionItem->getItemType() == Item::ItemType::BankFileKeyRegion);
            const BankFile::KeyRegion* keyRegion = static_cast<const BankFile::KeyRegion*>(keyRegionItem);

            for (const Item* velocityRegionItem : keyRegion->getVelocityRegionList())
            {
                SEAD_ASSERT(velocityRegionItem->getItemType() == Item::ItemType::BankFileVelocityRegion);
                const BankFile::VelocityRegion* velocityRegion = static_cast<const BankFile::VelocityRegion*>(velocityRegionItem);

                const Item* waveFileItem = velocityRegion->getWaveFileRef().getItem();
                SEAD_ASSERT(waveFileItem);
                SEAD_ASSERT(waveFileItem->getItemType() == Item::ItemType::WaveFile);

                const WaveFile* waveFile = static_cast<const WaveFile*>(waveFileItem);

                if (!waveIdIndexes.contains(waveFile))
                {
                    waveIdIndexes[waveFile] = waveIds.size();

                    SEAD_ASSERT(mWaveArchiveWaveFilesIndexes);

                    const auto& it = mWaveArchiveWaveFilesIndexes->find(waveFile);
                    SEAD_ASSERT(it != mWaveArchiveWaveFilesIndexes->end());

                    waveIds.push_back(
                        WaveId(
                            nw::snd::internal::Util::GetMaskedItemId(mWaveArchive->getId(), nw::snd::internal::ItemType_WaveArchive),
                            it->second
                        )
                    );
                }
            }
        }
    }

    struct KeyRegionInfo
    {
        KeyRegionInfo(u8 keyMin_, u8 keyMax_)
            : isNull(true)
            , keyMin(keyMin_)
            , keyMax(keyMax_)
        {
        }

        KeyRegionInfo(const KeyRegion* keyRegion)
            : isNull(false)
            , keyMin(keyRegion->getKeyMin())
            , keyMax(keyRegion->getKeyMax())
        {
            std::vector<const VelocityRegion*> velocityRegions;
            for (const Item* velocityRegionItem : keyRegion->getVelocityRegionList())
            {
                velocityRegions.push_back(static_cast<const VelocityRegion*>(velocityRegionItem));
            }

            std::sort(velocityRegions.begin(), velocityRegions.end(), [](const VelocityRegion* velocityRegionA, const VelocityRegion* velocityRegionB) -> bool
                {
                    return velocityRegionA->getVelocityMin() < velocityRegionB->getVelocityMin();
                }
            );

            u8 velocityMin = 0;
            for (const VelocityRegion* velocityRegion : velocityRegions)
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

            s32 num = (sortedVelocityRegions[0]->getVelocityMin() > 0) ? (sortedVelocityRegions.size() + 1) : sortedVelocityRegions.size();
            if (num <= 11)
            {
                SEAD_ASSERT(sortedVelocityRegions[0]->getVelocityMin() == 0);

                s32 rangeMax = sortedVelocityRegions[sortedVelocityRegions.size() - 1]->getVelocityMax();
                SEAD_ASSERT(rangeMax >= 127);

                tableType = nw::snd::internal::ElementType_BankFile_RangeReferenceTable;
                return;
            }

            const auto copy = sortedVelocityRegions;
            sortedVelocityRegions.clear();
            SEAD_ASSERT(copy.size() > 0);

            s32 rangeMin = copy[0]->getVelocityMin();
            s32 rangeMax = copy[copy.size() - 1]->getVelocityMax();
            SEAD_ASSERT_MSG(rangeMax - rangeMin + 1 >= 11, "IndexReferenceTable cannot apply.");

            num = 0;
            for (s32 i = rangeMin; i <= rangeMax; i++)
            {
                while (copy[num]->getVelocityMax() < i)
                {
                    num++;
                    SEAD_ASSERT_MSG(num < copy.size(), "failed to create index reference table.");
                }

                SEAD_ASSERT_MSG(i >= copy[num]->getVelocityMin(), "null object not found.");

                sortedVelocityRegions.emplace_back(copy[num]);
            }

            tableType = nw::snd::internal::ElementType_BankFile_IndexReferenceTable;
        }

        bool isNull;
        u8 keyMin;
        u8 keyMax;
        std::vector<const VelocityRegion*> sortedVelocityRegions;
        u16 tableType;
    };

    struct InstrumentInfo
    {
        InstrumentInfo(const Instrument* instrument)
            : programNo(-1)
            , isNull(instrument == nullptr)
            , tableType(0)
        {
            if (!instrument)
            {
                return;
            }

            programNo = instrument->getProgramNo();

            std::vector<const KeyRegion*> keyRegions;
            for (const Item* keyRegionItem : instrument->getKeyRegionList())
            {
                keyRegions.push_back(static_cast<const KeyRegion*>(keyRegionItem));
            }

            std::sort(keyRegions.begin(), keyRegions.end(), [](const KeyRegion* keyRegionA, const KeyRegion* keyRegionB) -> bool
                {
                    return keyRegionA->getKeyMin() < keyRegionB->getKeyMin();
                }
            );

            u8 keyMin = 0;
            for (const KeyRegion* keyRegion : keyRegions)
            {
                SEAD_ASSERT(keyMin <= keyRegion->getKeyMax());
                if (keyMin < keyRegion->getKeyMin())
                {
                    sortedKeyRegions.emplace_back(keyMin, keyRegion->getKeyMin() - 1);
                }

                sortedKeyRegions.emplace_back(keyRegion);
                keyMin = keyRegion->getKeyMax() + 1;
            }

            if (sortedKeyRegions.size() == 1 && sortedKeyRegions[0].keyMin == 0 && sortedKeyRegions[0].keyMax == 127)
            {
                tableType = nw::snd::internal::ElementType_BankFile_DirectReferenceTable;
                return;
            }

            s32 num = (sortedKeyRegions[0].keyMin > 0) ? (sortedKeyRegions.size() + 1) : sortedKeyRegions.size();
            if (num <= 11)
            {
                if (sortedKeyRegions[0].keyMin > 0)
                {
                    sortedKeyRegions.insert(sortedKeyRegions.begin(), KeyRegionInfo(0, sortedKeyRegions[0].keyMin - 1));
                }

                s32 rangeMax = sortedKeyRegions[sortedKeyRegions.size() - 1].keyMax;
                if (rangeMax < 127)
                {
                    sortedKeyRegions.emplace_back(rangeMax + 1, 127);
                }

                tableType = nw::snd::internal::ElementType_BankFile_RangeReferenceTable;
                return;
            }

            const auto copy = sortedKeyRegions;
            sortedKeyRegions.clear();
            SEAD_ASSERT(copy.size() > 0);

            s32 rangeMin = copy[0].keyMin;
            s32 rangeMax = copy[copy.size() - 1].keyMax;
            SEAD_ASSERT_MSG(rangeMax - rangeMin + 1 >= 11, "IndexReferenceTable cannot apply.");

            num = 0;
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

            tableType = nw::snd::internal::ElementType_BankFile_IndexReferenceTable;
        }

        s16 programNo;
        bool isNull;
        std::vector<KeyRegionInfo> sortedKeyRegions;
        u16 tableType;
    };

    std::vector<InstrumentInfo> instruments;

    {
        std::unordered_map<u32, const Instrument*> instrumentMap;

        for (const Item* instrumentItem : getInstrumentList())
        {
            SEAD_ASSERT(instrumentItem->getItemType() == Item::ItemType::BankFileInstrument);
            const BankFile::Instrument* instrument = static_cast<const BankFile::Instrument*>(instrumentItem);

            if (instrumentMap.contains(instrument->getProgramNo()))
            {
                SEAD_ASSERT_MSG(false, "'%s': program no %u already exists", getFormattedName().cstr(), instrument->getProgramNo());
                continue;
            }

            instrumentMap[instrument->getProgramNo()] = instrument;
        }

        u32 validInstrumentCount = 0;
        if (instrumentMap.size() > 0)
        {
            for (u32 i = 0; i < 32767; i++)
            {
                if (!instrumentMap.contains(i))
                {
                    instruments.push_back(InstrumentInfo(nullptr));
                    continue;
                }

                instruments.push_back(InstrumentInfo(instrumentMap[i]));
                validInstrumentCount++;

                if (validInstrumentCount >= instrumentMap.size())
                {
                    break;
                }
            }
        }
    }

    FileWriter writer(handle, stream);
    writer.openFile("FBNK", 1, mVersion);

    auto writeVelocityRegion = [&](const VelocityRegion& velocityRegion)
    {
        const Item* waveFileItem = velocityRegion.getWaveFileRef().getItem();
        SEAD_ASSERT(waveFileItem);
        SEAD_ASSERT(waveFileItem->getItemType() == Item::ItemType::WaveFile);

        const WaveFile* waveFile = static_cast<const WaveFile*>(waveFileItem);

        stream->writeU32(waveIdIndexes[waveFile]);

        stream->writeU32(nw::snd::internal::VelocityRegionBitFlag::VELOCITY_REGION_BASIC_PARAM_FLAG);
        stream->writeU32(velocityRegion.getOriginalKey());
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

        writer.align(0x4);
    };

    auto writeKeyRegion = [&](const KeyRegionInfo& keyRegion)
    {
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
                {
                    writer.openReference("VelocityRegion");

                    writer.closeReference("VelocityRegion", nw::snd::internal::ElementType_BankFile_VelocityRegionInfo);

                    const std::vector<const VelocityRegion*>& velocityRegions = keyRegion.sortedVelocityRegions;
                    SEAD_ASSERT(velocityRegions.size() == 1);
                    writer.pushOffsetBase();
                    {
                        writeVelocityRegion(*velocityRegions[0]);
                    }
                    writer.popOffsetBase();
                    break;
                }

                case nw::snd::internal::ElementType_BankFile_RangeReferenceTable:
                {
                    const std::vector<const VelocityRegion*>& velocityRegions = keyRegion.sortedVelocityRegions;
                    SEAD_ASSERT(velocityRegions.size() > 0);

                    stream->writeU32(velocityRegions.size()); // BorderTable size

                    for (const VelocityRegion* velocityRegion : velocityRegions)
                    {
                        stream->writeU8(velocityRegion->getVelocityMax());
                    }

                    writer.align(0x4);

                    for (u32 i = 0; i < velocityRegions.size(); i++)
                    {
                        writer.openReference(sead::FormatFixedSafeString<32>("VelocityRegion%u", i));
                    }

                    for (u32 i = 0; i < velocityRegions.size(); i++)
                    {
                        const VelocityRegion& velocityRegion = *velocityRegions[i];

                        writer.closeReference(sead::FormatFixedSafeString<32>("VelocityRegion%u", i), nw::snd::internal::ElementType_BankFile_VelocityRegionInfo);

                        writer.pushOffsetBase();
                        {
                            writeVelocityRegion(velocityRegion);
                        }
                        writer.popOffsetBase();
                    }
                    break;
                }

                case nw::snd::internal::ElementType_BankFile_IndexReferenceTable:
                    break;
            }
        }
        writer.popOffsetBase();
    };

    //? Info Block
    {
        writer.openBlock(nw::snd::internal::ElementType_BankFile_InfoBlock, "INFO");

        writer.openReference("WaveIdTable");
        writer.openReference("InstrumentTableRef");

        writer.closeReference("InstrumentTableRef", nw::snd::internal::ElementType_Table_ReferenceTable);
        writer.pushOffsetBase();
        {
            stream->writeU32(instruments.size());

            for (const InstrumentInfo& instrument : instruments)
            {
                sead::FormatFixedSafeString<32> refName("Instrument%i", instrument.programNo);
                writer.openReference(refName);

                if (instrument.isNull)
                {
                    writer.closeReference(refName, nw::snd::internal::ElementType_BankFile_NullInfo, -1);
                }
            }

            for (const Item* instrItem : getInstrumentList())
            {
                const Instrument* instr = static_cast<const Instrument*>(instrItem);
                const InstrumentInfo& instrument = instruments[instr->getProgramNo()];

                writer.closeReference(sead::FormatFixedSafeString<32>("Instrument%i", instr->getProgramNo()), nw::snd::internal::ElementType_BankFile_InstrumentInfo);

                writer.pushOffsetBase();
                {
                    writer.openReference("KeyRegionChunk");

                    if (instrument.tableType == 0)
                    {
                        writer.closeNullReference("KeyRegionChunk");
                    }
                    else
                    {
                        writer.closeReference("KeyRegionChunk", instrument.tableType);

                        writer.pushOffsetBase();
                        {
                            switch (instrument.tableType)
                            {
                                case nw::snd::internal::ElementType_BankFile_DirectReferenceTable:
                                {
                                    writer.openReference("KeyRegion");

                                    writer.closeReference("KeyRegion", nw::snd::internal::ElementType_BankFile_KeyRegionInfo);

                                    const std::vector<KeyRegionInfo>& keyRegions = instrument.sortedKeyRegions;
                                    SEAD_ASSERT(keyRegions.size() == 1);
                                    writer.pushOffsetBase();
                                    {
                                        writeKeyRegion(keyRegions[0]);
                                    }
                                    writer.popOffsetBase();
                                    break;
                                }

                                case nw::snd::internal::ElementType_BankFile_RangeReferenceTable:
                                {
                                    const std::vector<KeyRegionInfo>& keyRegions = instrument.sortedKeyRegions;
                                    SEAD_ASSERT(keyRegions.size() > 0);

                                    stream->writeU32(keyRegions.size()); // BorderTable size

                                    for (const KeyRegionInfo& keyRegion : keyRegions)
                                    {
                                        stream->writeU8(keyRegion.keyMax);
                                    }

                                    writer.align(0x4);

                                    for (u32 i = 0; i < keyRegions.size(); i++)
                                    {
                                        writer.openReference(sead::FormatFixedSafeString<32>("KeyRegion%u", i));
                                    }

                                    for (u32 i = 0; i < keyRegions.size(); i++)
                                    {
                                        const KeyRegionInfo& keyRegion = keyRegions[i];

                                        sead::FormatFixedSafeString<32> refName("KeyRegion%u", i);
                                        if (keyRegion.isNull)
                                        {
                                            writer.closeReference(refName, nw::snd::internal::ElementType_BankFile_NullInfo, nw::snd::internal::Util::Reference::INVALID_OFFSET);
                                            continue;
                                        }

                                        writer.closeReference(refName, nw::snd::internal::ElementType_BankFile_KeyRegionInfo);

                                        writer.pushOffsetBase();
                                        {
                                            writeKeyRegion(keyRegion);
                                        }
                                        writer.popOffsetBase();
                                    }
                                    break;
                                }

                                case nw::snd::internal::ElementType_BankFile_IndexReferenceTable:
                                {
                                    const std::vector<KeyRegionInfo>& keyRegions = instrument.sortedKeyRegions;
                                    SEAD_ASSERT(keyRegions.size() > 0);

                                    u32 startPos = writer.getPosition();

                                    stream->writeU8(keyRegions[0].keyMin);
                                    stream->writeU8(keyRegions[keyRegions.size() - 1].keyMax);
                                    stream->writeU16(0); // Padding

                                    for (u32 i = 0; i < keyRegions.size(); i++)
                                    {
                                        writer.openReference(sead::FormatFixedSafeString<32>("KeyRegion%u", i));
                                    }

                                    s32 prevKeyRegionPos = 0;
                                    for (u32 i = 0; i < keyRegions.size(); i++)
                                    {
                                        const KeyRegionInfo& keyRegion = keyRegions[i];

                                        sead::FormatFixedSafeString<32> refName("KeyRegion%u", i);
                                        if (keyRegion.isNull)
                                        {
                                            writer.closeReference(refName, nw::snd::internal::ElementType_BankFile_NullInfo, nw::snd::internal::Util::Reference::INVALID_OFFSET);
                                            continue;
                                        }

                                        if (i != 0)
                                        {
                                            if (keyRegion.keyMin == keyRegions[i - 1].keyMin)
                                            {
                                                writer.closeReference(refName, nw::snd::internal::ElementType_BankFile_KeyRegionInfo, prevKeyRegionPos - startPos);
                                                continue;
                                            }
                                        }

                                        writer.closeReference(refName, nw::snd::internal::ElementType_BankFile_KeyRegionInfo);

                                        prevKeyRegionPos = writer.getPosition();

                                        writer.pushOffsetBase();
                                        {
                                            writeKeyRegion(keyRegion);
                                        }
                                        writer.popOffsetBase();
                                    }

                                    break;
                                }
                            }
                        }
                        writer.popOffsetBase();
                    }
                }
                writer.popOffsetBase();
            }
        }
        writer.popOffsetBase();

        writer.closeReference("WaveIdTable", nw::snd::internal::ElementType_Table_EmbeddingTable);

        stream->writeU32(waveIds.size());

        for (const WaveId& waveId : waveIds)
        {
            stream->writeU32(waveId.warcId);
            stream->writeU32(waveId.waveIdx);
        }

        writer.closeBlock();
    }

    u32 fileSize = writer.getPosition();

    writer.closeFile();

    mBank = nullptr;
    mWaveArchive = nullptr;
    mWaveArchiveWaveFilesIndexes = nullptr;

    return fileSize;
}

//

void ImGui_PianoKeyboard(const char* IDName, ImVec2 Size, s32* PrevNoteActive, s32 BeginOctaveNote, s32 EndOctaveNote, ImGuiPianoKeyboardProc Callback, void* UserData, ImGuiPianoStyles* Style)
{
    // const
    static s32 NoteIsDark[12] = { 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0 };
    static s32 NoteLightNumber[12] = { 1, 1, 2, 2, 3, 4, 4, 5, 5, 6, 6, 7 };
    static f32 NoteDarkOffset[12] = { 0.0f,  -2.0f / 3.0f, 0.0f, -1.0f / 3.0f, 0.0f, 0.0f, -2.0f / 3.0f, 0.0f, -0.5f, 0.0f, -1.0f / 3.0f, 0.0f };

    // fix range dark keys
    if (NoteIsDark[BeginOctaveNote % 12] > 0) BeginOctaveNote++;
    if (NoteIsDark[EndOctaveNote % 12] > 0) EndOctaveNote--;

    // bad range
    if (!IDName || !Callback || BeginOctaveNote < 0 || EndOctaveNote < 0 || EndOctaveNote <= BeginOctaveNote) return;

    // style
    static ImGuiPianoStyles ColorsBase;
    if (!Style) Style = &ColorsBase;

    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return;
    
    const ImGuiID id = window->GetID(IDName);

    ImDrawList* draw_list = window->DrawList;

    ImVec2 Pos = window->DC.CursorPos;
    ImVec2 MousePos = ImGui::GetIO().MousePos;

    // sizes
    s32 CountNotesAllign7 = (EndOctaveNote / 12 - BeginOctaveNote / 12) * 7 + NoteLightNumber[EndOctaveNote % 12] - (NoteLightNumber[BeginOctaveNote % 12] - 1);

    f32 NoteHeight    = Size.y;
    f32 NoteWidth        = Size.x / (f32)CountNotesAllign7;

    f32 NoteHeight2    = NoteHeight * Style->NoteDarkHeight;
    f32 NoteWidth2    = NoteWidth * Style->NoteDarkWidth;
    
    // minimal size draw
    if (NoteHeight < 5.0 || NoteWidth < 3.0) return;

    // minimal size using mouse
    bool isMouseInput = (NoteHeight >= 10.0 && NoteWidth >= 5.0);

    // item
    const ImRect bb(Pos, ImVec2(Pos.x + Size.x, Pos.y + Size.y));
    ImGui::ItemSize(Size, 0);
    if (!ImGui::ItemAdd(bb, id)) return;

    // item input
    bool held = false;
    if (isMouseInput) {
        ImGui::ButtonBehavior(bb, id, nullptr, &held, 0);
    }

    s32        NoteMouseColision = -1;
    f32    NoteMouseVel = 0.0f;

    f32 OffsetX = bb.Min.x;
    f32 OffsetY = bb.Min.y;
    f32 OffsetY2 = OffsetY + NoteHeight;
    for (s32 RealNum = BeginOctaveNote; RealNum <= EndOctaveNote; RealNum++) {
        s32 Octave    = RealNum / 12;
        s32 i        = RealNum % 12;

        if (NoteIsDark[i] > 0) continue;
        
        ImRect NoteRect( 
            round(OffsetX), 
            OffsetY, 
            round(OffsetX + NoteWidth), 
            OffsetY2 
        );

        if (held && NoteRect.Contains(MousePos)) {
            NoteMouseColision    = RealNum;
            NoteMouseVel        = (MousePos.y - NoteRect.Min.y) / NoteHeight;
        }

        bool isActive = Callback(UserData, NoteGetStatus, RealNum, 0.0f);
        
        draw_list->AddRectFilled(    NoteRect.Min, NoteRect.Max, Style->Colors[isActive ? 2 : 0], 0.0f);

        draw_list->AddRect(            NoteRect.Min, NoteRect.Max, Style->Colors[4], 0.0f);

        OffsetX += NoteWidth;
    }

    // draw dark notes
    OffsetX = bb.Min.x;
    OffsetY = bb.Min.y;
    OffsetY2 = OffsetY + NoteHeight2;
    for (s32 RealNum = BeginOctaveNote; RealNum <= EndOctaveNote; RealNum++) {
        s32 Octave    = RealNum / 12;
        s32 i        = RealNum % 12;

        if (NoteIsDark[i] == 0)  {
            OffsetX += NoteWidth;
            continue;
        }
        
        f32 OffsetDark = NoteDarkOffset[i] * NoteWidth2;
        ImRect NoteRect(
            round(OffsetX + OffsetDark), 
            OffsetY, 
            round(OffsetX + NoteWidth2 + OffsetDark),
            OffsetY2
        );

        if (held && NoteRect.Contains(MousePos)) {
            NoteMouseColision    = RealNum;
            NoteMouseVel        = (MousePos.y - NoteRect.Min.y) / NoteHeight2;
        }

        bool isActive = Callback(UserData, NoteGetStatus, RealNum, 0.0f);

        draw_list->AddRectFilled(    NoteRect.Min, NoteRect.Max, Style->Colors[isActive ? 3 : 1], 0.0f);

        draw_list->AddRect(            NoteRect.Min, NoteRect.Max, Style->Colors[4], 0.0f);
    }

    // mouse note click
    if (*PrevNoteActive != NoteMouseColision) {
        Callback(UserData, NoteOff, *PrevNoteActive, 0.0f);
        *PrevNoteActive = -1;

        if (held && NoteMouseColision >= 0) {
            Callback(UserData, NoteOn, NoteMouseColision, NoteMouseVel);
            *PrevNoteActive = NoteMouseColision;
        }
    }
}
