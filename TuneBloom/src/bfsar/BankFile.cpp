#include <bfsar/BankFile.h>

#include <bfsar/SeqCommand.h>

#include <Debug.h>

#include <ui/PopupMgr.h>
#include <ui/UI.h>

#include <midi/MidiInput.h>

#include <VectorSet.h>

#include <algorithm>
#include <functional>
#include <vector>
#include <cstdio>

static sead::FixedSafeString<24> FormatKeyName(s32 key)
{
    if (key < 0 || key >= static_cast<s32>(MmlCommandNote::sKeysNum))
        return sead::FixedSafeString<24>("-");

    const char* s = MmlCommandNote::sKeys[key];
    char letter = s[0];
    
    if (letter >= 'a' && letter <= 'z')
        letter = static_cast<char>(letter - 'a' + 'A');
    
    const char* accidental = (s[1] == 's') ? "#" : "";

    char octave[8];
    if (s[2] == 'm')
        snprintf(octave, sizeof(octave), "-%s", s + 3);
    else
        snprintf(octave, sizeof(octave), "%s", s + 2);

    return sead::FormatFixedSafeString<24>("%c%s%s (%d)", letter, accidental, octave, key);
}

static bool KeyPresed[128] = { false };

static bool KeyboardFunc(void* UserData, s32 Msg, s32 Key, f32 Vel)
{
    if (Key < 0 || Key >= 128)
    {
        return false; // Midi max keys
    }

    Item* item = static_cast<Item*>(UserData);
    if (!item || item->getItemType() != Item::ItemType::BankFileInstrument)
    {
        return false;
    }

    BankFile::Instrument* instrument = static_cast<BankFile::Instrument*>(item);

    switch (Msg)
    {
        case NoteGetStatus:
        {
            return KeyPresed[Key];
        }

        case NoteOn:
        {
            KeyPresed[Key] = true;

            const BankFile::KeyRegion* keyRegion = instrument->getKeyRegion(Key);
            if (!keyRegion)
            {
                return false;
            }

            u8 vel = static_cast<u8>(Vel * 127.0f);

            const BankFile::VelocityRegion* velocityRegion = keyRegion->getVelocityRegion(vel);
            if (!velocityRegion)
            {
                PopupMgr::instance()->addPopup({ sead::FormatFixedSafeString<32>("Invalid VelocityRegion(%d)", vel) });
            }

            sSoundPlayer.playBankNote(static_cast<u8>(Key), vel, *velocityRegion);

            break;
        }

        case NoteOff:
        {
            KeyPresed[Key] = false;

            sSoundPlayer.stopAllPlayers(false);

            break;
        }
    }

    return false;
}

static MidiInput sMidiInput;

static void MidiInputCallback(void* userData, s32 msg, s32 key, f32 vel)
{
    if (key < 0 || key >= 128)
        return;

    Item* selected = sSelectedItem;
    if (selected && selected->getItemType() == Item::ItemType::BankFileInstrument)
    {
        if (msg == 1)
            KeyboardFunc(selected, NoteOn, key, vel);
        else if (msg == 2)
            KeyboardFunc(selected, NoteOff, key, 0.0f);
    }
    else
    {
        if (msg == 2)
            KeyPresed[key] = false;
    }
}

void PollMidiInput()
{
    sMidiInput.poll();
}

void BankFile::VelocityRegion::read(const nw::snd::internal::BankFile::VelocityRegion* velocityRegionInfo, const nw::snd::internal::Util::WaveIdTable& waveIdTable, u32 instrumentId)
{
    LOG_FUNC();
    LOG_U32("instrumentId", instrumentId);
    LOG_U32("waveIdTableIndex", velocityRegionInfo->waveIdTableIndex);
    LOG_U32("waveIdTableCount", waveIdTable.GetCount());
    //? waveId->waveIndex is patched with global wave index already
    const nw::snd::internal::Util::WaveId* waveId = waveIdTable.GetWaveId(velocityRegionInfo->waveIdTableIndex);
    if (waveId)
    {
        LOG("waveId=%p waveArchiveId=%u waveIndex=%u\n", waveId, u32(waveId->waveArchiveId), u32(waveId->waveIndex));
    }
    if (waveId && waveId->waveArchiveId == 0)
    {
        WaveFile* waveFile = static_cast<WaveFile*>(sBfsar.getItem(waveId->waveIndex, sBfsar.getWaveFileList()));

        mWaveFileRef.attach(waveFile);
        if (!mWaveFileRef.isAttached())
        {
            sead::FormatFixedSafeString<256> msg("Instrument %u: Couldn't load the Wave File referenced", instrumentId);
            PopupMgr::instance()->pushCurrentItemError(msg);
        }
    }
    else if (!waveId)
    {
        sead::FormatFixedSafeString<256> msg("Instrument %u: Internal error (waveId is null - table count %u, index %u)", instrumentId, u32(waveIdTable.GetCount()), u32(velocityRegionInfo->waveIdTableIndex));
        PopupMgr::instance()->pushCurrentItemError(msg);
    }
    else
    {
        sead::FormatFixedSafeString<256> msg("Instrument %u: Internal error (failed BFBNK patch - waveArchiveId=%u)", instrumentId, u32(waveId->waveArchiveId));
        PopupMgr::instance()->pushCurrentItemError(msg);
    }

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

void BankFile::VelocityRegion::drawUI()
{
    static const ImU8 cStepU8 = 1;

    if (sSelectedItem && sSelectedItem->getItemType() == Item::ItemType::BankFileInstrument)
    {
        Instrument* instr = static_cast<Instrument*>(sSelectedItem);
        instr->drawUI();

        ImGui::SeparatorText("");
    }

    {
        Item* waveFile = getWaveFileRef().getItem();
        if (ItemSelector("Wave File", sBfsar.getWaveFileList(), &waveFile))
        {
            getWaveFileRef().attach(waveFile);
            SetUnsavedChanges(true);
        }

        if (!waveFile)
        {
            ImGui::BeginDisabled();
        }

        ImGui::SameLine();
        if (ImGui::Button(ICON_LC_EXTERNAL_LINK "###GoWave"))
        {
            SelectItem(waveFile);
        }

        if (!waveFile)
        {
            ImGui::EndDisabled();
        }
    }

    {
        u8 originalKey = getOriginalKey();
        if (ImGui::InputScalar("Root Key", ImGuiDataType_U8, &originalKey, &cStepU8))
        {
            setOriginalKey(originalKey);
            SetUnsavedChanges(true);
        }
        
        ImGui::SameLine();
        ImGui::TextDisabled("%s", FormatKeyName(originalKey).cstr());
    }

    {
        u8 volume = getVolume();
        if (ImGui::InputScalar(sead::FormatFixedSafeString<32>("Volume (%.3f)###vol", static_cast<f32>(volume) / 127.0f).cstr(), ImGuiDataType_U8, &volume, &cStepU8))
        {
            setVolume(volume);
            SetUnsavedChanges(true);
        }
    }

    {
        u8 pan = getPan();
        if (ImGui::InputScalar(sead::FormatFixedSafeString<32>("Pan (%.3f)###pan", (static_cast<f32>(pan) / 64.0f) - 1.0f).cstr(), ImGuiDataType_U8, &pan, &cStepU8))
        {
            setPan(pan);
            SetUnsavedChanges(true);
        }
    }

    {
        f32 pitch = getPitch();
        if (ImGui::SliderFloat("Pitch", &pitch, 0.0f, 8.0f))
        {
            setPitch(pitch);
            SetUnsavedChanges(true);
        }
    }

    {
        bool ignoreNoteOff = getIsIgnoreNoteOff();
        if (ImGui::Checkbox("Ignore Note Off (Percussion Mode)", &ignoreNoteOff))
        {
            setIsIgnoreNoteOff(ignoreNoteOff);
            SetUnsavedChanges(true);
        }

        ImGui::SameLine();
        HelpMarker("You can middle click the stop button on the Player window to kill all Voices if you need to");
    }

    {
        u8 keyGroup = getKeyGroup();
        if (ImGui::InputScalar("Key Group", ImGuiDataType_U8, &keyGroup, &cStepU8))
        {
            setKeyGroup(keyGroup);
            SetUnsavedChanges(true);
        }
    }

    {
        static const char* sInterpolationTypes[] = { 
          //"Polyphase (4-point) interpolation",
            "Polyphase (4-point)",
          //"Linear interpolation",
            "Linear",
        };

        u32 interpolationType = getInterpolationType();
        if (ImGui::Combo("Interpolation Type", (s32*)&interpolationType, sInterpolationTypes, IM_ARRAYSIZE(sInterpolationTypes)))
        {
            setInterpolationType(interpolationType);
            SetUnsavedChanges(true);
        }
    }

    {
        snd::AdshrCurve adshrCurve = getAdshrCurve();

        bool edited = false;
        if (ImGui::InputScalar("Attack", ImGuiDataType_U8, &adshrCurve.attack, &cStepU8))
        {
            edited = true;
        }

        if (ImGui::InputScalar("Decay", ImGuiDataType_U8, &adshrCurve.decay, &cStepU8))
        {
            edited = true;
        }

        if (ImGui::InputScalar("Sustain", ImGuiDataType_U8, &adshrCurve.sustain, &cStepU8))
        {
            edited = true;
        }

        if (ImGui::InputScalar("Hold", ImGuiDataType_U8, &adshrCurve.hold, &cStepU8))
        {
            edited = true;
        }

        if (ImGui::InputScalar("Release", ImGuiDataType_U8, &adshrCurve.release, &cStepU8))
        {
            edited = true;
        }

        if (edited)
        {
            setAdshrCurve(adshrCurve);
            SetUnsavedChanges(true);
        }
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
                sead::FormatFixedSafeString<256> msg("Instrument %u: VelocityRegion error (Direct)", instrumentId);
                PopupMgr::instance()->pushCurrentItemError(msg);
                break;
            }

            const nw::snd::internal::BankFile::VelocityRegion* velocityRegionInfo = static_cast<const nw::snd::internal::BankFile::VelocityRegion*>(directChunk.GetRegion());

            VelocityRegion* velocityRegion = new VelocityRegion(0, 127);
            velocityRegion->mId = 0;

            velocityRegion->mEnableName = true;
            velocityRegion->mName = "VelocityRegion";

            velocityRegion->read(velocityRegionInfo, waveIdTable, instrumentId);

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
                u8 velocityMax = rangeChunk.borderTable.item[i];

                const nw::snd::internal::Util::Reference* velocityRegionRef = static_cast<const nw::snd::internal::Util::Reference*>(rangeChunk.GetRegionRef(velocityMin));
                if (!velocityRegionRef ||
                    velocityRegionRef->typeId != nw::snd::internal::ElementType_BankFile_VelocityRegionInfo ||
                    velocityRegionRef->offset == nw::snd::internal::Util::Reference::INVALID_OFFSET)
                {
                    sead::FormatFixedSafeString<256> msg("Instrument %u: VelocityRegion error (Range)", instrumentId);
                    PopupMgr::instance()->pushCurrentItemError(msg);
                    velocityMin = velocityMax + 1;
                    continue;
                }

                const nw::snd::internal::BankFile::VelocityRegion* velocityRegionInfo = static_cast<const nw::snd::internal::BankFile::VelocityRegion*>(rangeChunk.GetRegion(velocityMin));
                if (!velocityRegionInfo)
                {
                    sead::FormatFixedSafeString<256> msg("Instrument %u: VelocityRegion error (Range)", instrumentId);
                    PopupMgr::instance()->pushCurrentItemError(msg);
                    velocityMin = velocityMax + 1;
                    continue;
                }

                VelocityRegion* velocityRegion = new VelocityRegion(velocityMin, velocityMax);
                velocityRegion->mId = mVelocityRegionList.size();

                velocityRegion->mEnableName = true;
                velocityRegion->mName = "VelocityRegion";

                velocityRegion->read(velocityRegionInfo, waveIdTable, instrumentId);

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
                if (!velocityRegionRef)
                {
                    sead::FormatFixedSafeString<256> msg("Instrument %u: VelocityRegion error (Index)", instrumentId);
                    PopupMgr::instance()->pushCurrentItemError(msg);
                    continue;
                }

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
                u8 velocityMax = borders[i];

                const nw::snd::internal::Util::Reference* velocityRegionRef = static_cast<const nw::snd::internal::Util::Reference*>(indexChunk.GetRegionRef(velocityMin));
                if (!velocityRegionRef ||
                    velocityRegionRef->typeId != nw::snd::internal::ElementType_BankFile_VelocityRegionInfo ||
                    velocityRegionRef->offset == nw::snd::internal::Util::Reference::INVALID_OFFSET)
                {
                    sead::FormatFixedSafeString<256> msg("Instrument %u: VelocityRegion error (Index)", instrumentId);
                    PopupMgr::instance()->pushCurrentItemError(msg);
                    velocityMin = velocityMax + 1;
                    continue;
                }

                const nw::snd::internal::BankFile::VelocityRegion* velocityRegionInfo = static_cast<const nw::snd::internal::BankFile::VelocityRegion*>(indexChunk.GetRegion(velocityMin));
                if (!velocityRegionInfo)
                {
                    sead::FormatFixedSafeString<256> msg("Instrument %u: VelocityRegion error (Index)", instrumentId);
                    PopupMgr::instance()->pushCurrentItemError(msg);
                    velocityMin = velocityMax + 1;
                    continue;
                }

                VelocityRegion* velocityRegion = new VelocityRegion(velocityMin, velocityMax);
                velocityRegion->mId = mVelocityRegionList.size();

                velocityRegion->mEnableName = true;
                velocityRegion->mName = "VelocityRegion";

                velocityRegion->read(velocityRegionInfo, waveIdTable, instrumentId);

                mVelocityRegionList.pushBack(velocityRegion);

                velocityMin = velocityMax + 1;
            }
        }

        default:
            sead::FormatFixedSafeString<256> msg("Instrument %u: Invalid KeyRegion type", instrumentId);
            PopupMgr::instance()->pushCurrentItemError(msg);
            break;
    }

    if (mVelocityRegionList.isEmpty())
    {
        sead::FormatFixedSafeString<256> msg("Instrument %u: VelocityRegion empty", instrumentId);
        PopupMgr::instance()->pushCurrentItemError(msg);
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
                sead::FormatFixedSafeString<256> msg("Instrument %u: KeyRegion error (Direct)", mId);
                PopupMgr::instance()->pushCurrentItemError(msg);
                break;
            }

            const nw::snd::internal::BankFile::KeyRegion* keyRegionInfo = static_cast<const nw::snd::internal::BankFile::KeyRegion*>(directChunk.GetRegion());

            KeyRegion* keyRegion = new KeyRegion(0, 127);
            keyRegion->mId = 0;

            keyRegion->mEnableName = true;
            keyRegion->mName = "KeyRegion";

            keyRegion->read(keyRegionInfo, waveIdTable, mId);

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
                u8 keyMax = rangeChunk.borderTable.item[i];

                const nw::snd::internal::Util::Reference* keyRegionRef = static_cast<const nw::snd::internal::Util::Reference*>(rangeChunk.GetRegionRef(keyMin));
                if (!keyRegionRef)
                {
                    sead::FormatFixedSafeString<256> msg("Instrument %u: KeyRegion error (Range)", mId);
                    PopupMgr::instance()->pushCurrentItemError(msg);
                    keyMin = keyMax + 1;
                    continue;
                }

                if (keyRegionRef->offset == nw::snd::internal::Util::Reference::INVALID_OFFSET)
                {
                    if (keyRegionRef->typeId != 0 && keyRegionRef->typeId != nw::snd::internal::ElementType_BankFile_NullInfo)
                    {
                        sead::FormatFixedSafeString<256> msg("Instrument %u: KeyRegion error (Range)", mId);
                        PopupMgr::instance()->pushCurrentItemError(msg);
                    }

                    keyMin = keyMax + 1;
                    continue;
                }
                else
                {
                    if (keyRegionRef->typeId != nw::snd::internal::ElementType_BankFile_KeyRegionInfo)
                    {
                        sead::FormatFixedSafeString<256> msg("Instrument %u: KeyRegion error (Range)", mId);
                        PopupMgr::instance()->pushCurrentItemError(msg);
                        keyMin = keyMax + 1;
                        continue;
                    }
                }

                const nw::snd::internal::BankFile::KeyRegion* keyRegionInfo = static_cast<const nw::snd::internal::BankFile::KeyRegion*>(rangeChunk.GetRegion(keyMin));
                if (!keyRegionInfo)
                {
                    sead::FormatFixedSafeString<256> msg("Instrument %u: KeyRegion error (Range)", mId);
                    PopupMgr::instance()->pushCurrentItemError(msg);
                    keyMin = keyMax + 1;
                    continue;
                }

                KeyRegion* keyRegion = new KeyRegion(keyMin, keyMax);
                keyRegion->mId = mKeyRegionList.size();

                keyRegion->mEnableName = true;
                keyRegion->mName = "KeyRegion";

                keyRegion->read(keyRegionInfo, waveIdTable, mId);

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
                if (!keyRegionRef)
                {
                    sead::FormatFixedSafeString<256> msg("Instrument %u: KeyRegion error (Index)", mId);
                    PopupMgr::instance()->pushCurrentItemError(msg);
                    continue;
                }

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
                u8 keyMax = borders[i];

                const nw::snd::internal::Util::Reference* keyRegionRef = static_cast<const nw::snd::internal::Util::Reference*>(indexChunk.GetRegionRef(keyMin));
                if (!keyRegionRef)
                {
                    sead::FormatFixedSafeString<256> msg("Instrument %u: KeyRegion error (Index)", mId);
                    PopupMgr::instance()->pushCurrentItemError(msg);
                    keyMin = keyMax + 1;
                    continue;
                }

                if (keyRegionRef->offset == nw::snd::internal::Util::Reference::INVALID_OFFSET)
                {
                    if (keyRegionRef->typeId != 0 && keyRegionRef->typeId != nw::snd::internal::ElementType_BankFile_NullInfo)
                    {
                        sead::FormatFixedSafeString<256> msg("Instrument %u: KeyRegion error (Index)", mId);
                        PopupMgr::instance()->pushCurrentItemError(msg);
                    }

                    keyMin = keyMax + 1;
                    continue;
                }
                else
                {
                    if (keyRegionRef->typeId != nw::snd::internal::ElementType_BankFile_KeyRegionInfo)
                    {
                        sead::FormatFixedSafeString<256> msg("Instrument %u: KeyRegion error (Index)", mId);
                        PopupMgr::instance()->pushCurrentItemError(msg);
                        keyMin = keyMax + 1;
                        continue;
                    }
                }

                const nw::snd::internal::BankFile::KeyRegion* keyRegionInfo = static_cast<const nw::snd::internal::BankFile::KeyRegion*>(indexChunk.GetRegion(keyMin));
                if (!keyRegionInfo)
                {
                    sead::FormatFixedSafeString<256> msg("Instrument %u: KeyRegion error (Index)", mId);
                    PopupMgr::instance()->pushCurrentItemError(msg);
                    keyMin = keyMax + 1;
                    continue;
                }

                KeyRegion* keyRegion = new KeyRegion(keyMin, keyMax);
                keyRegion->mId = mKeyRegionList.size();

                keyRegion->mEnableName = true;
                keyRegion->mName = "KeyRegion";

                keyRegion->read(keyRegionInfo, waveIdTable, mId);

                mKeyRegionList.pushBack(keyRegion);

                keyMin = keyMax + 1;
            }

            break;
        }

        default:
            sead::FormatFixedSafeString<256> msg("Instrument %u: Invalid Instrument type", mId);
            PopupMgr::instance()->pushCurrentItemError(msg);
            break;
    }

    if (mKeyRegionList.isEmpty())
    {
        sead::FormatFixedSafeString<256> msg("Instrument %u: KeyRegion empty", mId);
        PopupMgr::instance()->pushCurrentItemError(msg);
    }
}

void BankFile::Instrument::drawUI()
{
    static ImS16 cStepS16 = 1;
    {
        s16 program = getProgramNo();
        if (ImGui::InputScalar("Program Number", ImGuiDataType_S16, &program, &cStepS16))
        {
            setProgramNo(program);
            SetUnsavedChanges(true);
        }
    }
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

        if (instrument->getProgramNo() < 0 || instrument->getProgramNo() > 32766)
        {
            error.format("Instrument %u has an invalid program number. corrent range is [0, 32766]", i);
            return instrument;
        }

        if (programNos.count(instrument->getProgramNo()) > 0)
        {
            error.format("Instrument %u: Program number '%d' already exists", i, instrument->getProgramNo());
            return instrument;
        }

        programNos.insert(instrument->getProgramNo());

        for (const Item* keyRegionItem : instrument->getKeyRegionList())
        {
            SEAD_ASSERT(keyRegionItem->getItemType() == Item::ItemType::BankFileKeyRegion);
            const BankFile::KeyRegion* keyRegion = static_cast<const BankFile::KeyRegion*>(keyRegionItem);

            if (keyRegion->getVelocityRegionList().isEmpty())
            {
                error.format("Instrument %u: Key Region has no Velocity Region", i);
                return instrument;
            }

            for (const Item* velocityRegionItem : keyRegion->getVelocityRegionList())
            {
                SEAD_ASSERT(velocityRegionItem->getItemType() == Item::ItemType::BankFileVelocityRegion);
                const BankFile::VelocityRegion* velocityRegion = static_cast<const BankFile::VelocityRegion*>(velocityRegionItem);

                if (!velocityRegion->getWaveFileRef().isAttached())
                {
                    error.format("Instrument %u: Velocity Region has no Wave File attached", i);
                    return instrument;
                }
            }
        }

        i++;
    }

    return nullptr;
}

void BankFile::drawUI()
{
    mVersion = sBfsar.getVersionForBfbnk();
    mEndian = sBfsar.getEndian();
    mFormat = sBfsar.getFormat();

    HelpMarker("Those are derived from the BFSAR");

    ImGui::BeginDisabled();
    InnerFile::drawUI();
    ImGui::EndDisabled();
}

static BankFile::KeyRegion* sContextKeyRegion = nullptr;

void SelectVelocity(BankFile::KeyRegion* keyRegion, BankFile::VelocityRegion* velocityRegion)
{
    sSubSelectedItem = velocityRegion;
    sSelectedItemIsSubWindow = true;
    sContextKeyRegion = keyRegion;

    FocusPropertiesWindow(); // TODO: Why is this broken ugh
}

void DeselectVelocity()
{
    sSubSelectedItem = nullptr;
    sSelectedItemIsSubWindow = false;
    sContextKeyRegion = nullptr;
}

void DeleteVeloctity(BankFile::KeyRegion* keyRegion, BankFile::VelocityRegion* velocityRegion)
{
    DeselectVelocity();

    snd::internal::driver::SoundThreadLock lock;

    if (keyRegion->getVelocityRegionList().size() == 1) //? Deleted last VelocityRegion, this KeyRegion can die now
    {
        delete keyRegion;
        SetUnsavedChanges(true);
    }
    else
    {
        //? VelocityRegions can't contain gaps, so handle that

        if (velocityRegion == keyRegion->getVelocityRegionList().front()) //? VelocityRegion is the bottom one, extend the one above to cover it
        {
            velocityRegion->getNext(*keyRegion)->setVelocityMin(0, *keyRegion);
            SetUnsavedChanges(true);
        }
        else if (velocityRegion == keyRegion->getVelocityRegionList().back()) //? VelocityRegion is the top one, extend the one below to cover it
        {
            velocityRegion->getPrev(*keyRegion)->setVelocityMax(127, *keyRegion);
            SetUnsavedChanges(true);
        }
        else //? VelocityRegion is sandwiched, extend the one below to cover it
        {
            velocityRegion->getPrev(*keyRegion)->setVelocityMax(velocityRegion->getVelocityMax(), *keyRegion);
            SetUnsavedChanges(true);
        }

        delete velocityRegion;
        SetUnsavedChanges(true);
    }
}

void VelocityContextMenu(BankFile::Instrument* instrument, BankFile::KeyRegion* keyRegion, BankFile::VelocityRegion* velocityRegion)
{
    auto copyVel = [](BankFile::VelocityRegion* dst, BankFile::VelocityRegion* src)
    {
        dst->getWaveFileRef().attach(src->getWaveFileRef().getItem());
        dst->setOriginalKey(src->getOriginalKey());
        dst->setVolume(src->getVolume());
        dst->setPan(src->getPan());
        dst->setPitch(src->getPitch());
        dst->setIsIgnoreNoteOff(src->getIsIgnoreNoteOff());
        dst->setKeyGroup(src->getKeyGroup());
        dst->setInterpolationType(src->getInterpolationType());
        dst->setAdshrCurve(src->getAdshrCurve());
    };

    if (ImGui::BeginPopup("VelocityRegionContextMenu"))
    {
        bool disable = false;
        if (keyRegion->getVelocityRegionList().size() > 1)
        {
            disable = true;
            ImGui::BeginDisabled();
        }

        if (ImGui::MenuItem("Split Key Region") && keyRegion->getKeyNum() > 1)
        {
            snd::internal::driver::SoundThreadLock lock;

            u8 newKeyMax = keyRegion->getKeyMax();
            u8 newKeyMin = (keyRegion->getKeyMax() + keyRegion->getKeyMin()) / 2 + 1;

            keyRegion->setKeyMax(newKeyMin - 1, *instrument);
            SetUnsavedChanges(true);

            BankFile::KeyRegion* newKeyRegion = new BankFile::KeyRegion(newKeyMin, newKeyMax);
            newKeyRegion->setId(0);
            newKeyRegion->setEnableName(true);
            newKeyRegion->getName() = "KeyRegion";

            keyRegion->insertBack(newKeyRegion);

            sBfsar.updateList(instrument->getKeyRegionList());
            SetUnsavedChanges(true);

            BankFile::VelocityRegion* newVelRegion = new BankFile::VelocityRegion(0, 127);
            newVelRegion->setId(0);
            newVelRegion->setEnableName(true);
            newVelRegion->getName() = "VelocityRegion";
            copyVel(newVelRegion, velocityRegion);

            newKeyRegion->getVelocityRegionList().pushBack(newVelRegion);
        }

        if (disable)
        {
            ImGui::EndDisabled();
        }

        if (ImGui::MenuItem("Split Velocity Region") && velocityRegion->getVelocityNum() > 1)
        {
            snd::internal::driver::SoundThreadLock lock;

            u8 newVelMax = velocityRegion->getVelocityMax();
            u8 newVelMin = (velocityRegion->getVelocityMax() + velocityRegion->getVelocityMin()) / 2 + 1;

            velocityRegion->setVelocityMax(newVelMin - 1, *keyRegion);
            SetUnsavedChanges(true);

            BankFile::VelocityRegion* newVelRegion = new BankFile::VelocityRegion(newVelMin, newVelMax);
            newVelRegion->setId(0);
            newVelRegion->setEnableName(true);
            newVelRegion->getName() = "VelocityRegion";
            copyVel(newVelRegion, velocityRegion);

            velocityRegion->insertBack(newVelRegion);

            sBfsar.updateList(keyRegion->getVelocityRegionList());
            SetUnsavedChanges(true);
        }

        if (ImGui::MenuItem("Delete"))
        {
            DeleteVeloctity(keyRegion, velocityRegion);
            SetUnsavedChanges(true);
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

// YEAHH i clanked it so what

enum class DragMode
{
    None,
    ResizeL,
    ResizeR,
    ResizeVTop
};

struct DragState
{
    DragMode mode = DragMode::None;
    BankFile::KeyRegion* region = nullptr;
    BankFile::KeyRegion* prev = nullptr;
    BankFile::KeyRegion* next = nullptr;

    BankFile::VelocityRegion* velRegion = nullptr;
    BankFile::VelocityRegion* vNext = nullptr;

    bool onLeftEdge = false;
    bool onRightEdge = false;
    bool onTopEdge = false;
    ImVec2 r0;
    ImVec2 r1;

    ImVec2 v0;
    ImVec2 v1;

    ImVec2 initialCanvasPos;
};

static DragState sDrag;

static const s32 NoteIsDark[12] = { 0,1,0,1,0,0,1,0,1,0,1,0 };
static const s32 NoteLightNumber[12] = { 1,1,2,2,3,4,4,5,5,6,6,7 };
static const f32 NoteDarkOffset[12] = {
    0.0f, -2.0f/3.0f, 0.0f, -1.0f/3.0f, 0.0f, 0.0f,
    -2.0f/3.0f, 0.0f, -0.5f, 0.0f, -1.0f/3.0f, 0.0f
};

#ifndef IM_ROUND
#define IM_ROUND(x) ((f32)(s32)((x) + 0.5f))
#endif

void DrawKeyboardWithRegions(
    f32 width,
    f32 keyboardHeight,
    f32 regionHeight,
    s32 beginNote,
    s32 endNote,
    BankFile::Instrument* instrument
)
{
    ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    ImVec2 visibleCanvasPos = canvasPos;
    ImVec2 canvasSize(width, regionHeight + keyboardHeight);

    ImDrawList* draw = ImGui::GetWindowDrawList();

    if (sDrag.mode != DragMode::None)
    {
        canvasPos = sDrag.initialCanvasPos;
    }

    const s32 fixedBegin = beginNote;
    const s32 fixedEnd = endNote;
    const s32 numKeys = std::max(1, fixedEnd - fixedBegin + 1);

    static f32 sZoom = 1.0f;
    static f32 sZoomTarget = 1.0f;
    static f32 sScrollX = 0.0f;
    static f32 sAnchorNorm = 0.0f;
    static f32 sAnchorScreenX = 0.0f;
    static bool sMiddlePan = false;

    auto requestZoom = [&](f32 target, f32 screenX)
    {
        const f32 curZW = std::max(1.0f, sZoom * width);
        const f32 sx = sead::MathCalcCommon<f32>::clamp2(0.0f, screenX, width);
        sAnchorNorm = sead::MathCalcCommon<f32>::clamp2(0.0f, (sScrollX + sx) / curZW, 1.0f);
        sAnchorScreenX = sx;
        sZoomTarget = sead::MathCalcCommon<f32>::clamp2(1.0f, target, 8.0f);
    };

    {
        ImGuiIO& io = ImGui::GetIO();

        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
            sMiddlePan = true;
        
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle))
            sMiddlePan = false;
        if (sMiddlePan)
        {
            sScrollX -= io.MouseDelta.x;
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        }

        if (ImGui::IsWindowHovered())
        {
            if (io.MouseWheel != 0.0f && !io.KeyShift)
                requestZoom(sZoomTarget * (1.0f + io.MouseWheel * 0.20f), io.MousePos.x - visibleCanvasPos.x);

            if (io.KeyShift && io.MouseWheel != 0.0f)
                sScrollX += io.MouseWheel * 30.0f;

            if (io.MouseWheelH != 0.0f)
                sScrollX -= io.MouseWheelH * 30.0f;

            if (io.KeyCtrl)
            {
                if (ImGui::IsKeyPressed(ImGuiKey_Equal)) requestZoom(sZoomTarget * 1.25f, width * 0.5f);
                if (ImGui::IsKeyPressed(ImGuiKey_Minus)) requestZoom(sZoomTarget / 1.25f, width * 0.5f);
            }
        }

        if (fabsf(sZoomTarget - sZoom) > 0.0005f)
        {
            const f32 dt = io.DeltaTime > 0.0f ? io.DeltaTime : (1.0f / 60.0f);
            const f32 a = 1.0f - expf(-dt * 40.0f);
            sZoom += (sZoomTarget - sZoom) * a;
            if (fabsf(sZoomTarget - sZoom) < 0.0015f) sZoom = sZoomTarget;
            const f32 zw = std::max(1.0f, sZoom * width);
            sScrollX = sAnchorNorm * zw - sAnchorScreenX;
        }
        sZoom = sead::MathCalcCommon<f32>::clamp2(1.0f, sZoom, 8.0f);
    }

    f32 zoomedWidth = std::max(1.0f, sZoom * width);
    f32 maxScroll = std::max(0.0f, zoomedWidth - width);
    sScrollX = sead::MathCalcCommon<f32>::clamp2(0.0f, sScrollX, maxScroll);

    canvasPos.x -= sScrollX;
    canvasSize.x = zoomedWidth;

    f32 noteWidth = zoomedWidth / (f32)numKeys;

    auto GetKeyRect = [&](s32 key, f32& outMin, f32& outMax)
    {
        outMin = IM_ROUND((key - fixedBegin) * noteWidth);
        outMax = IM_ROUND((key - fixedBegin + 1) * noteWidth);
    };

    auto XToKey = [&](f32 x) -> s32
    {
        f32 local = x - canvasPos.x;
        s32 k = fixedBegin + static_cast<s32>(std::floor(local / noteWidth));
        return sead::MathCalcCommon<s32>::clamp2(fixedBegin, k, fixedEnd);
    };

    ImVec2 mouse = ImGui::GetIO().MousePos;
    bool mouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    bool mouseClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);

    draw->PushClipRect(visibleCanvasPos, ImVec2(visibleCanvasPos.x + width, visibleCanvasPos.y + regionHeight + keyboardHeight), true);

    // BG
    draw->AddRectFilled(
        canvasPos,
        ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
        IM_COL32(20, 20, 30, 255)
    );

    if (instrument)
    {
        f32 edgeSize = 6.0f;
        static f32 edgeOffset = 0.0f;

        ImDrawListSplitter splitter;
        splitter.Split(draw, 2);

        enum class AddMode
        {
            None,
            Front,
            Back,
            After
        };

        AddMode addMode = AddMode::None;
        BankFile::KeyRegion* addNode = nullptr;
        ImU32 emptyAreaColor = IM_COL32(210, 100, 100, 150);

        if (instrument->getKeyRegionList().size() == 0)
        {
            ImVec2 n0(canvasPos.x, canvasPos.y);
            ImVec2 n1(canvasPos.x + canvasSize.x, canvasPos.y + regionHeight);

            draw->AddRect(n0, n1, emptyAreaColor);
            ImGui::ItemAdd(ImRect(n0, n1), ImGui::GetID(instrument));
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                addMode = AddMode::Front;
                addNode = nullptr;
            }
            else if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                DeselectVelocity();
            }
            else if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            {
                ImGui::SetTooltip("Double click to add");
            }
        }

        bool drawGrabBar = sDrag.mode != DragMode::None;
        for (Item* keyRegionItem : instrument->getKeyRegionList())
        {
            splitter.SetCurrentChannel(draw, 0);

            BankFile::KeyRegion* keyRegion = static_cast<BankFile::KeyRegion*>(keyRegionItem);

            BankFile::KeyRegion* prev = keyRegion->getPrevNeighbor(*instrument);
            BankFile::KeyRegion* next = keyRegion->getNextNeighbor(*instrument);

            f32 x0, ignored_max;
            GetKeyRect(keyRegion->getKeyMin(), x0, ignored_max);

            f32 ignored_min, x1;
            if (keyRegion->getKeyMax() == 127)
            {
                x1 = canvasSize.x; //? Special case for the last key to fill to the end
            }
            else
            {
                GetKeyRect(keyRegion->getKeyMax() + 1, x1, ignored_min);
            }

            ImVec2 r0(canvasPos.x + x0, canvasPos.y);
            ImVec2 r1(canvasPos.x + x1, canvasPos.y + regionHeight);

            if (keyRegion == instrument->getKeyRegionList().front())
            {
                if (keyRegion->getKeyMin() != 0)
                {
                    ImVec2 n0(canvasPos.x, canvasPos.y);
                    ImVec2 n1(canvasPos.x + x0, canvasPos.y + regionHeight);

                    draw->AddRect(n0, n1, emptyAreaColor);
                    ImGui::ItemAdd(ImRect(n0, n1), ImGui::GetID(keyRegion));
                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    {
                        addMode = AddMode::Front;
                        addNode = keyRegion;
                    }
                    else if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                    {
                        DeselectVelocity();
                    }
                    else if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                    {
                        ImGui::SetTooltip("Double click to add");
                    }
                }
            }

            if (keyRegion == instrument->getKeyRegionList().back())
            {
                if (keyRegion->getKeyMax() != 127)
                {
                    ImVec2 n0(canvasPos.x + x1, canvasPos.y);
                    ImVec2 n1(canvasPos.x + canvasSize.x, canvasPos.y + regionHeight);

                    draw->AddRect(n0, n1, emptyAreaColor);
                    ImGui::ItemAdd(ImRect(n0, n1), ImGui::GetID(keyRegion));
                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    {
                        addMode = AddMode::Back;
                        addNode = keyRegion;
                    }
                    else if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                    {
                        DeselectVelocity();
                    }
                    else if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                    {
                        ImGui::SetTooltip("Double click to add");
                    }
                }
            }

            {
                BankFile::KeyRegion* next = keyRegion->getNext(*instrument);
                if (next && keyRegion->getKeyMax() + 1 != next->getKeyMin())
                {
                    f32 next_x0;
                    GetKeyRect(next->getKeyMin(), next_x0, ignored_max);

                    ImVec2 n0(canvasPos.x + x1, canvasPos.y);
                    ImVec2 n1(canvasPos.x + next_x0, canvasPos.y + regionHeight);

                    draw->AddRect(n0, n1, emptyAreaColor);
                    ImGui::ItemAdd(ImRect(n0, n1), ImGui::GetID(keyRegion));
                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    {
                        addMode = AddMode::After;
                        addNode = keyRegion;
                    }
                    else if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                    {
                        DeselectVelocity();
                    }
                    else if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                    {
                        ImGui::SetTooltip("Double click to add");
                    }
                }
            }

            bool hoveredRegionY = mouse.y >= r0.y && mouse.y <= r1.y;

            bool onLeftEdge  = hoveredRegionY && (mouse.x <= r0.x + edgeSize) && (mouse.x >= r0.x - edgeSize);
            bool onRightEdge = hoveredRegionY && (mouse.x >= r1.x - edgeSize) && (mouse.x <= r1.x + edgeSize);

            if (keyRegion == sDrag.region)
            {
                sDrag.r0 = r0;
                sDrag.r1 = r1;
            }

            for (Item* velRegionItem : keyRegion->getVelocityRegionList())
            {
                BankFile::VelocityRegion* velRegion = static_cast<BankFile::VelocityRegion*>(velRegionItem);

                s32 velMin = velRegion->getVelocityMin();
                s32 velMax = velRegion->getVelocityMax();

                //? Map velocity (0–127) to Y
                f32 y0 = regionHeight * (1.0f - (velMax + 1) / 128.0f);
                f32 y1 = regionHeight * (1.0f - velMin / 128.0f);

                ImVec2 p0 = ImVec2(canvasPos.x + x0, canvasPos.y + y0);
                ImVec2 p1 = ImVec2(canvasPos.x + x1, canvasPos.y + y1);

                ImGui::ItemAdd(ImRect(p0, p1), ImGui::GetID(velRegion));

                if (ImGui::IsItemHovered())
                {
                    drawGrabBar = true;
                }

                if (velRegion == sDrag.velRegion)
                {
                    sDrag.v0 = p0;
                    sDrag.v1 = p1;
                }

                bool hoveredRegionX = mouse.x >= p0.x && mouse.x <= p1.x && hoveredRegionY;
                bool onTopEdge = hoveredRegionX && (mouse.y <= p0.y + edgeSize) && (mouse.y >= p0.y - edgeSize);
                bool onBottomEdge = hoveredRegionX && (mouse.y >= p1.y - edgeSize) && (mouse.y <= p1.y + edgeSize);

                if (drawGrabBar)
                {
                    splitter.SetCurrentChannel(draw, 1);
                    auto drawGrabTop = [&draw](ImVec2 p0, ImVec2 p1)
                    {
                        draw->AddLine(
                            ImVec2(p0.x, p0.y),
                            ImVec2(p1.x, p0.y),
                            IM_COL32(255, 255, 0, 255),
                            2.0f
                        );
                        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
                    };

                    if (sDrag.mode == DragMode::None)
                    {
                        if (!onLeftEdge && !onRightEdge)
                        {
                            if (onTopEdge && velMax < 127 && velRegion->getNext(*keyRegion))
                            {
                                drawGrabTop(p0, p1);
                            }
                            else if (onBottomEdge && velMin > 0 && velRegion->getPrev(*keyRegion))
                            {
                                draw->AddLine(
                                    ImVec2(p0.x, p1.y),
                                    ImVec2(p1.x, p1.y),
                                    IM_COL32(255, 255, 0, 255),
                                    2.0f
                                );
                                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
                            }
                        }
                    }
                    else if (sDrag.mode == DragMode::ResizeVTop && sDrag.velRegion == velRegion)
                    {
                        drawGrabTop(sDrag.v0, sDrag.v1);
                    }
                    splitter.SetCurrentChannel(draw, 0);
                }

                const bool selected = (sSubSelectedItem == velRegion);
                ImU32 color = selected ? ImGui::GetColorU32(ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive)) : ImGui::GetColorU32(ImGui::GetStyleColorVec4(ImGuiCol_Button));

                if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
                {
                    SelectVelocity(keyRegion, velRegion);

                    if (sDrag.mode == DragMode::None)
                    {
                        if (onLeftEdge)
                        {
                            sDrag.mode = DragMode::ResizeL;
                            sDrag.onLeftEdge = true;
                            edgeOffset = r0.x - mouse.x;
                        }
                        else if (onRightEdge)
                        {
                            sDrag.mode = DragMode::ResizeR;
                            sDrag.onRightEdge = true;
                            edgeOffset = r1.x - mouse.x;
                        }
                        else if (onTopEdge && velMax < 127 && velRegion->getNext(*keyRegion))
                        {
                            sDrag.mode = DragMode::ResizeVTop;
                            sDrag.onTopEdge = true;
                            edgeOffset = p0.y - mouse.y;
                            sDrag.velRegion = velRegion;
                            sDrag.vNext = velRegion->getNext(*keyRegion);
                        }
                        else if (onBottomEdge && velMin > 0 && velRegion->getPrev(*keyRegion))
                        {
                            sDrag.mode = DragMode::ResizeVTop;
                            sDrag.onTopEdge = true;
                            edgeOffset = p1.y - mouse.y;
                            sDrag.velRegion = velRegion->getPrev(*keyRegion);
                            sDrag.vNext = velRegion;
                        }

                        sDrag.region = keyRegion;
                        sDrag.prev = prev;
                        sDrag.next = next;
                        sDrag.initialCanvasPos = canvasPos;
                        sDrag.initialCanvasPos.x += sScrollX;
                    }
                }
                else if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                {
                    ImGui::OpenPopup("VelocityRegionContextMenu");
                    SelectVelocity(keyRegion, velRegion);
                }

                draw->AddRectFilled(p0, p1, color);
                draw->AddRect(p0, p1, selected ? IM_COL32(255, 226, 120, 255) : IM_COL32(0, 0, 0, 160), 0.0f, 0, selected ? 2.0f : 1.0f);

                const char* name = "(null)";
                if (velRegion->getWaveFileRef().isAttached())
                {
                    name = velRegion->getWaveFileRef().getItem()->getName().cstr();
                }

                draw->PushClipRect(p0, p1, true);
                draw->AddText(
                    nullptr,
                    0,
                    ImVec2(p0.x + 3, p0.y + 2),
                    IM_COL32(255, 255, 255, 255),
                    name,
                    nullptr,
                    p1.x - p0.x - 6
                );
                draw->PopClipRect();
            }

            splitter.SetCurrentChannel(draw, 1);

            if (drawGrabBar)
            {
                auto drawGrabLeft = [&draw](ImVec2 r0, ImVec2 r1)
                {
                    draw->AddLine(
                        ImVec2(r0.x, r0.y),
                        ImVec2(r0.x, r1.y),
                        IM_COL32(255, 255, 0, 255),
                        2.0f
                    );

                    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                };

                auto drawGrabRight = [&draw](ImVec2 r0, ImVec2 r1)
                {
                    draw->AddLine(
                        ImVec2(r1.x, r0.y),
                        ImVec2(r1.x, r1.y),
                        IM_COL32(255, 255, 0, 255),
                        2.0f
                    );

                    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                };

                if (sDrag.mode == DragMode::None)
                {
                    if (onLeftEdge)
                    {
                        drawGrabLeft(r0, r1);
                    }
                    else if (onRightEdge)
                    {
                        drawGrabRight(r0, r1);
                    }
                }
                else if (sDrag.mode == DragMode::ResizeL || sDrag.mode == DragMode::ResizeR)
                {
                    if (sDrag.onLeftEdge)
                    {
                        drawGrabLeft(sDrag.r0, sDrag.r1);
                    }
                    else if (sDrag.onRightEdge)
                    {
                        drawGrabRight(sDrag.r0, sDrag.r1);
                    }
                }
            }
        }

        splitter.Merge(draw);

        for (s32 gk = fixedBegin; gk <= fixedEnd; gk++)
        {
            if (gk % 12 != 0)
                continue;
            
            f32 gMin, gMax;
            GetKeyRect(gk, gMin, gMax);
            const f32 lx = canvasPos.x + gMin;
            draw->AddLine(ImVec2(lx, canvasPos.y), ImVec2(lx, canvasPos.y + regionHeight), IM_COL32(255, 255, 255, 40));
            draw->AddText(ImVec2(lx + 3.0f, canvasPos.y + regionHeight - ImGui::GetFontSize() - 2.0f),IM_COL32(225, 230, 240, 170), FormatKeyName(gk).cstr());
        }

        if (addMode != AddMode::None)
        {
            snd::internal::driver::SoundThreadLock lock;

            BankFile::KeyRegion* newRegion = nullptr;
            if (addMode == AddMode::Front)
            {
                if (addNode)
                {
                    newRegion = new BankFile::KeyRegion(0, addNode->getKeyMin() - 1);
                    addNode->insertFront(newRegion);
                    SetUnsavedChanges(true);
                }
                else
                {
                    newRegion = new BankFile::KeyRegion(0, 127);
                    instrument->getKeyRegionList().pushBack(newRegion);
                    SetUnsavedChanges(true);
                }
            }
            else if (addMode == AddMode::Back)
            {
                SEAD_ASSERT(addNode);
                newRegion = new BankFile::KeyRegion(addNode->getKeyMax() + 1, 127);
                addNode->insertBack(newRegion);
                SetUnsavedChanges(true);
            }
            else if (addMode == AddMode::After)
            {
                SEAD_ASSERT(addNode);
                BankFile::KeyRegion* next = addNode->getNext(*instrument);
                u8 newMin = addNode->getKeyMax() + 1;
                u8 newMax = next ? next->getKeyMin() - 1 : 127;

                newRegion = new BankFile::KeyRegion(newMin, newMax);
                addNode->insertBack(newRegion);
                SetUnsavedChanges(true);
            }

            newRegion->setId(0);
            newRegion->setEnableName(true);
            newRegion->getName() = "KeyRegion";

            sBfsar.updateList(instrument->getKeyRegionList());
            SetUnsavedChanges(true);

            BankFile::VelocityRegion* newVelRegion = new BankFile::VelocityRegion(0, 127);
            newVelRegion->setId(0);
            newVelRegion->setEnableName(true);
            newVelRegion->getName() = "VelocityRegion";

            newRegion->getVelocityRegionList().pushBack(newVelRegion);
            SetUnsavedChanges(true);

            SelectVelocity(newRegion, newVelRegion);
        }

        if (sDrag.mode == DragMode::None && sSubSelectedItem && sSubSelectedItem->getItemType() == Item::ItemType::BankFileVelocityRegion && ImGui::IsWindowFocused())
        {
            BankFile::VelocityRegion* velRegion = static_cast<BankFile::VelocityRegion*>(sSubSelectedItem);
            BankFile::KeyRegion* keyRegion = sContextKeyRegion;
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
            {
                BankFile::VelocityRegion* prev = velRegion->getPrev(*keyRegion);
                if (prev)
                {
                    SelectVelocity(keyRegion, prev);
                }
            }

            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
            {
                BankFile::VelocityRegion* next = velRegion->getNext(*keyRegion);
                if (next)
                {
                    SelectVelocity(keyRegion, next);
                }
            }
        }

        static s32 mouseKey = 0;
        if (sDrag.mode != DragMode::None && mouseDown && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
        {
            if (sDrag.mode == DragMode::ResizeVTop)
            {
                f32 y = mouse.y + edgeOffset - canvasPos.y;
                s32 mouseVel = 127 - (s32)IM_ROUND((y / regionHeight) * 128.0f);

                BankFile::VelocityRegion* velRegion = sDrag.velRegion;
                BankFile::VelocityRegion* vNext = sDrag.vNext;

                if (mouseVel < velRegion->getVelocityMin())
                {
                    mouseVel = velRegion->getVelocityMin();
                }

                if (mouseVel > vNext->getVelocityMax() - 1)
                {
                    mouseVel = vNext->getVelocityMax() - 1;
                }

                snd::internal::driver::SoundThreadLock lock;
                
                velRegion->setVelocityMax(mouseVel, *sDrag.region);
                SetUnsavedChanges(true);
                vNext->setVelocityMin(mouseVel + 1, *sDrag.region);
                SetUnsavedChanges(true);
            }
            else
            {
                mouseKey = XToKey(mouse.x + edgeOffset + (sDrag.mode == DragMode::ResizeL ? 1.0f : -1.0f));
                // draw->AddLine(
                //     ImVec2(mouse.x + edgeOffset, canvasPos.y),
                //     ImVec2(mouse.x + edgeOffset, canvasPos.y + regionHeight),
                //     IM_COL32(255, 0, 0, 255),
                //     1.0f
                // );

                BankFile::KeyRegion* region = sDrag.region;

                if (sDrag.mode == DragMode::ResizeL)
                {
                    if (mouseKey > region->getKeyMax())
                    {
                        mouseKey = region->getKeyMax();
                    }

                    if (region->getPrev(*instrument))
                    {
                        BankFile::KeyRegion* prev = region->getPrev(*instrument);
                        if (mouseKey < prev->getKeyMin() + 1)
                        {
                            mouseKey = prev->getKeyMin() + 1;
                        }
                    }

                    if (sDrag.prev == nullptr && region->getPrev(*instrument))
                    {
                        mouseKey = std::clamp(mouseKey, region->getPrev(*instrument)->getKeyMax() + 1, (s32)region->getKeyMax());
                    }
                }
                else if (sDrag.mode == DragMode::ResizeR)
                {
                    if (mouseKey < region->getKeyMin())
                    {
                        mouseKey = region->getKeyMin();
                    }

                    if (region->getNext(*instrument))
                    {
                        BankFile::KeyRegion* next = region->getNext(*instrument);
                        if (mouseKey > next->getKeyMax() - 1)
                        {
                            mouseKey = next->getKeyMax() - 1;
                        }
                    }

                    if (sDrag.next == nullptr && region->getNext(*instrument))
                    {
                        mouseKey = std::clamp(mouseKey, (s32)region->getKeyMin(), region->getNext(*instrument)->getKeyMin() - 1);
                    }
                }

                if (sDrag.mode == DragMode::ResizeL)
                {
                    snd::internal::driver::SoundThreadLock lock;

                    s32 newMin = mouseKey;

                    if (sDrag.prev)
                    {
                        sDrag.prev->setKeyMax(newMin - 1, *instrument);
                        SetUnsavedChanges(true);
                    }

                    region->setKeyMin(newMin, *instrument);
                    SetUnsavedChanges(true);

                    if (sDrag.prev)
                    {
                        sDrag.prev->setKeyMax(region->getKeyMin() - 1, *instrument);
                        SetUnsavedChanges(true);
                    }
                }
                else if (sDrag.mode == DragMode::ResizeR)
                {
                    snd::internal::driver::SoundThreadLock lock;

                    s32 newMax = mouseKey;

                    if (sDrag.next)
                    {
                        sDrag.next->setKeyMin(newMax + 1, *instrument);
                        SetUnsavedChanges(true);
                    }

                    region->setKeyMax(newMax, *instrument);
                    SetUnsavedChanges(true);

                    if (sDrag.next)
                    {
                        sDrag.next->setKeyMin(region->getKeyMax() + 1, *instrument);
                        SetUnsavedChanges(true);
                    }
                }
            }

            // f32 xEdge, dummy;
            // if (sDrag.mode == DragMode::ResizeL)
            //     GetKeyRect(mouseKey, xEdge, dummy);
            // else
            //     GetKeyRect(mouseKey + 1, xEdge, dummy);

            // if (sDrag.mode != DragMode::ResizeL && mouseKey >= 127)
            // {
            //     xEdge = canvasSize.x; //? Special case for the last key to fill to the end
            // }
            
            // f32 lx = canvasPos.x + xEdge;
            // draw->AddLine(ImVec2(lx, canvasPos.y), ImVec2(lx, canvasPos.y + regionHeight), IM_COL32(255, 255, 0, 255), 2.0f);
            // ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        }

        if (sDrag.mode != DragMode::None && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            sDrag = {};
        }

        if (ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_Delete) && sSubSelectedItem && sSubSelectedItem->getItemType() == Item::ItemType::BankFileVelocityRegion)
        {
            BankFile::VelocityRegion* velRegion = static_cast<BankFile::VelocityRegion*>(sSubSelectedItem);
            BankFile::KeyRegion* keyRegion = sContextKeyRegion;
            DeleteVeloctity(keyRegion, velRegion);
        }

        if (ImGui::IsPopupOpen("VelocityRegionContextMenu") && sSubSelectedItem && sSubSelectedItem->getItemType() == Item::ItemType::BankFileVelocityRegion)
        {
            BankFile::VelocityRegion* velRegion = static_cast<BankFile::VelocityRegion*>(sSubSelectedItem);
            BankFile::KeyRegion* keyRegion = sContextKeyRegion;
            VelocityContextMenu(instrument, keyRegion, velRegion);
        }
    }
    else
    {
        const char* text = "Select an Instrument";
        ImVec2 ts = ImGui::CalcTextSize(text);

        f32 x = canvasPos.x + canvasSize.x / 2.0f - ts.x / 2.0f;
        f32 y = canvasPos.y + regionHeight / 2.0f - ts.y / 2.0f;

        draw->AddText(ImVec2(x, y), IM_COL32_WHITE, text);
    }

    ImGui::SetCursorScreenPos(ImVec2(canvasPos.x, canvasPos.y + regionHeight));

    static s32 sPrevNote = -1;

    s32 originalKey = -1;
    if (sSubSelectedItem && sSubSelectedItem->getItemType() == Item::ItemType::BankFileVelocityRegion)
    {
        BankFile::VelocityRegion* velRegion = static_cast<BankFile::VelocityRegion*>(sSubSelectedItem);
        originalKey = velRegion->getOriginalKey();
    }

    ImGui_PianoKeyboard(
        "Keyboard",
        ImVec2(zoomedWidth, keyboardHeight),
        &sPrevNote,
        beginNote,
        endNote,
        &KeyboardFunc,
        instrument,
        nullptr,
        originalKey
    );

    draw->PopClipRect();

    {
        ImVec2 afterKB = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(ImVec2(visibleCanvasPos.x, afterKB.y));
    }

    {
        ImGuiIO& io = ImGui::GetIO();
        ImDrawList* d2 = ImGui::GetWindowDrawList();

        const f32 trackH = 14.0f;
        ImVec2 trackOrigin = ImGui::GetCursorScreenPos();
        d2->AddRectFilled(trackOrigin, ImVec2(trackOrigin.x + width, trackOrigin.y + trackH),IM_COL32(0x2a, 0x33, 0x40, 255));

        const f32 visibleFrac = (zoomedWidth > 0.0f) ? (width / zoomedWidth) : 1.0f;
        const f32 thumbW = std::max(16.0f, std::min(width, visibleFrac * width));
        const f32 travel = std::max(0.0f, width - thumbW);
        const f32 thumbLeft = (maxScroll > 0.0f) ? (sScrollX / maxScroll) * travel : 0.0f;
        const bool fitsAll = zoomedWidth <= width + 0.5f;
        d2->AddRectFilled(ImVec2(trackOrigin.x + thumbLeft, trackOrigin.y + 1.0f), ImVec2(trackOrigin.x + thumbLeft + thumbW, trackOrigin.y + trackH - 1.0f), fitsAll ? IM_COL32(0x3d, 0x5a, 0x73, 160) : IM_COL32(0x7f, 0xd1, 0xff, 255), 3.0f);

        ImGui::InvisibleButton("###PianoScroll", ImVec2(width, trackH));
        if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (ImGui::IsItemActive() && travel > 0.0f)
        {
            const f32 mx = io.MousePos.x - trackOrigin.x - thumbW * 0.5f;
            const f32 t = sead::MathCalcCommon<f32>::clamp2(0.0f, mx, travel) / travel;
            sScrollX = t * maxScroll;
        }

        if (ImGui::SmallButton("-###PianoZoomOut")) requestZoom(sZoomTarget / 1.25f, width * 0.5f);
        ImGui::SameLine();
        ImGui::Text("zoom %.0f%%", sZoom * 100.0f);
        ImGui::SameLine();
        if (ImGui::SmallButton("+###PianoZoomIn")) requestZoom(sZoomTarget * 1.25f, width * 0.5f);
        ImGui::SameLine();
        if (ImGui::Button("Fit###PianoZoomFit")) requestZoom(1.0f, width * 0.5f);
        ImGui::SameLine();
        ImGui::TextDisabled("(scroll: zoom, Shift+scroll / middle-drag: pan)");
    }

    s32 key[2] = { -1, -1 };
    s32 vel[2] = { -1, -1 };
    if (sSubSelectedItem && sSubSelectedItem->getItemType() == Item::ItemType::BankFileVelocityRegion)
    {
        BankFile::VelocityRegion* velRegion = static_cast<BankFile::VelocityRegion*>(sSubSelectedItem);
        BankFile::KeyRegion* keyRegion = sContextKeyRegion;
        key[0] = keyRegion->getKeyMin();
        key[1] = keyRegion->getKeyMax();
        vel[0] = velRegion->getVelocityMin();
        vel[1] = velRegion->getVelocityMax();
    }

    sead::FixedSafeString<24> keyMinStr = FormatKeyName(key[0]);
    sead::FixedSafeString<24> keyMaxStr = FormatKeyName(key[1]);

    ImGui::Text("Key Range   ");
    ImGui::SameLine();

    ImGui::BeginDisabled();
    ImGui::SetNextItemWidth(98.0f);
    ImGui::InputText("###KeyMin", keyMinStr.getBuffer(), keyMinStr.getBufferSize());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(98.0f);
    ImGui::InputText("###KeyMax", keyMaxStr.getBuffer(), keyMaxStr.getBufferSize());
    ImGui::EndDisabled();

    ImGui::Text("Vel Range   ");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200.0f);

    ImGui::BeginDisabled();
    ImGui::InputInt2("###Velocity", vel);
    ImGui::EndDisabled();

    ImGui::Text("Root Key    ");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200.0f);

    sead::FixedSafeString<24> formattedRoot = FormatKeyName(originalKey);

    ImGui::BeginDisabled();
    ImGui::InputText("###Orig", formattedRoot.getBuffer(), formattedRoot.getBufferSize());
    ImGui::EndDisabled();
}

InstanciateItemCallback CreateInstrumentFunc(bool clear)
{
    auto doCreate = []() -> Item*
    {
        BankFile::Instrument* instr = new BankFile::Instrument();
        instr->setEnableName(true);
        instr->getName() = "Instrument";

        BankFile::KeyRegion* keyRegion = new BankFile::KeyRegion(0, 127);
        keyRegion->setId(0);
        keyRegion->setEnableName(true);
        keyRegion->getName() = "KeyRegion";

        instr->getKeyRegionList().pushBack(keyRegion);

        BankFile::VelocityRegion* velRegion = new BankFile::VelocityRegion(0, 127);
        velRegion->setId(0);
        velRegion->setEnableName(true);
        velRegion->getName() = "VelocityRegion";

        keyRegion->getVelocityRegionList().pushBack(velRegion);

        return instr;
    };

    return doCreate;
}

void BankFile::drawFileUI()
{
    static f32 sKeyboardHeight = 200.0f; // initial guess
    f32 totalHeight = ImGui::GetContentRegionAvail().y;

    f32 topHeight = totalHeight - sKeyboardHeight;
    if (topHeight < 0.0f)
    {
        topHeight = 0.0f;
    }

    if (ImGui::BeginChild("Instruments", ImVec2(0.0f, topHeight), ImGuiChildFlags_Border))
    {
        DrawAllItemsUI("Instrument", mInstrumentList, &CreateInstrumentFunc, nullptr, nullptr, nullptr, true);
    }
    ImGui::EndChild();

    f32 startY = ImGui::GetCursorScreenPos().y;
    if (ImGui::BeginChild("Keyboard", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border, ImGuiWindowFlags_NoScrollWithMouse))
    {
        {
            static s32 sMidiDeviceIndex = 0;
            u32 devCount = MidiInput::getDeviceCount();

            bool connected = sMidiInput.isRunning();
            f32 comboW = ImGui::CalcTextSize("MMMMMMMMMMMMMMM").x + ImGui::GetStyle().FramePadding.x * 4;
            f32 btnW = ImGui::CalcTextSize("Disconnect MIDI").x + ImGui::GetStyle().FramePadding.x * 2;
            f32 helpW = ImGui::CalcTextSize("?").x + ImGui::GetStyle().FramePadding.x * 2;
            f32 spacing = ImGui::GetStyle().ItemSpacing.x;

            float rightX = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;

            if (connected) ImGui::BeginDisabled();
            ImGui::SetCursorPosX(rightX - comboW - btnW - helpW - spacing * 2);
            if (devCount > 0)
            {
                const char* preview = MidiInput::getDeviceName(static_cast<u32>(sMidiDeviceIndex));
                if (!preview || !*preview) preview = "No device";
                ImGui::SetNextItemWidth(comboW);
                if (ImGui::BeginCombo("##midiDev", preview))
                {
                    for (u32 i = 0; i < devCount; i++)
                    {
                        bool isSelected = (static_cast<u32>(sMidiDeviceIndex) == i);
                        if (ImGui::Selectable(MidiInput::getDeviceName(i), isSelected))
                            sMidiDeviceIndex = static_cast<s32>(i);
                        if (isSelected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
            }
            else
            {
                ImGui::SetNextItemWidth(comboW);
                ImGui::TextUnformatted("No MIDI devices");
            }
            if (connected) ImGui::EndDisabled();

            ImGui::SameLine(rightX - btnW - helpW - spacing, 0);
            const char* label = connected ? "Disconnect MIDI" : "Connect MIDI";
            if (ImGui::SmallButton(label))
            {
                if (connected)
                    sMidiInput.stop();
                else
                {
                    sMidiInput.start(&MidiInputCallback, nullptr, static_cast<u32>(sMidiDeviceIndex));
                    if (!sMidiInput.isRunning())
                        ImGui::OpenPopup("MIDI Unsupported");
                }
            }
            if (ImGui::BeginPopup("MIDI Unsupported"))
            {
                ImGui::TextUnformatted("Could not connect to the selected MIDI input device.\nMake sure it is connected and working.");
                ImGui::EndPopup();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("?"))
                ImGui::OpenPopup("Keyboard Help");
            if (ImGui::BeginPopup("Keyboard Help"))
            {
                ImGui::TextUnformatted("PC Keyboard Mapping");
                ImGui::Separator();
                ImGui::TextUnformatted("Lower octave (Z-M):");
                ImGui::TextUnformatted("  White: Z  X  C  V  B  N  M");
                ImGui::TextUnformatted("  Black: S     D     G  H  J");
                ImGui::TextUnformatted("Upper octave (Q-U):");
                ImGui::TextUnformatted("  White: Q  W  E  R  T  Y  U");
                ImGui::TextUnformatted("  Black: 2  3     5  6  7");
                ImGui::Separator();
                ImGui::TextUnformatted("Controls:");
                ImGui::TextUnformatted("  Left/Right arrows: shift octave");
                ImGui::TextUnformatted("  Backspace: reset to C3");
                ImGui::Separator();
                ImGui::TextUnformatted("Zoom/Scroll:");
                ImGui::TextUnformatted("  Ctrl+Scroll or Ctrl++/-: zoom in/out");
                ImGui::TextUnformatted("  Shift+Scroll or horizontal scroll: pan");
                ImGui::EndPopup();
            }
        }

        f32 width = ImGui::GetContentRegionAvail().x;

        DrawKeyboardWithRegions(
            width,
            70.0f, // keyboard height
            128.0f,  // region height
            0,
            127,
            (sSelectedItem && sSelectedItem->getItemType() == Item::ItemType::BankFileInstrument)
                ? static_cast<Instrument*>(sSelectedItem)
                : nullptr
        );

        f32 endY = ImGui::GetCursorScreenPos().y;
        sKeyboardHeight = endY - startY + ImGui::GetStyle().WindowPadding.y;
    }
    ImGui::EndChild();
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

u32 BankFile::doWrite(sead::FileHandle* handle, sead::WriteStream* stream, bool isLast) const
{
    LOG_FUNC();
    LOG_SIZE("getInstrumentList()", getInstrumentList().size());
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

    LOG_SIZE("getInstrumentList() iterating", getInstrumentList().size());
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

    LOG_SIZE("waveIds", waveIds.size());
    LOG_SIZE("instruments (pre-write)", instruments.size());

    FileWriter writer(handle, stream);
    writer.openFile(mFormat == ArchiveFormat::BCSAR ? "CBNK" : "FBNK", 1, mVersion);

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
    LOG_U32("fileSize", fileSize);

    writer.closeFile();

    mBank = nullptr;
    mWaveArchive = nullptr;
    mWaveArchiveWaveFilesIndexes = nullptr;

    return fileSize;
}
