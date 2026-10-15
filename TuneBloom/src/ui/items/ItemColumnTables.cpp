#include <ui/items/ItemColumns.h>

#include <ui/Messages.h>

#include <bfsar/model/Bank.h>
#include <bfsar/files/BankFile.h>
#include <bfsar/model/Group.h>
#include <bfsar/model/Player.h>
#include <bfsar/files/SequenceFile.h>
#include <bfsar/model/Sound.h>
#include <bfsar/model/SoundSet.h>
#include <bfsar/files/WaveFile.h>

#include <libs/IconsLucide.h>
#include <imgui/imgui.h>

#include <algorithm>
#include <string>
#include <vector>

static constexpr u32 cItemTooltipNameMax = 16;

static constexpr const char* cLoopLengthSample = ICON_LC_REPEAT " 0:00.000";

static const std::vector<const Item*>& GetUniqueItems(std::vector<const Item*>* items)
{
    std::sort(items->begin(), items->end());
    items->erase(std::unique(items->begin(), items->end()), items->end());

    return *items;
}

static std::vector<const Item*>& CollectRefOwners(const Item* item, Item::ItemType ownerType)
{
    static std::vector<const Item*> sOwners;
    sOwners.clear();

    for (auto it = item->getReferences().robustBegin(); it != item->getReferences().robustEnd(); ++it)
    {
        const ItemReference* ref = it->val();

        if (!ref || !ref->getOwner() || ref->getOwner()->isFileWindow())
            continue;

        if (ownerType == Item::ItemType::Invalid || ref->getOwner()->getItemType() == ownerType)
            sOwners.push_back(ref->getOwner());
    }

    GetUniqueItems(&sOwners);

    return sOwners;
}

static s64 CountRefs(const Item* item, Item::ItemType ownerType)
{
    return static_cast<s64>(CollectRefOwners(item, ownerType).size());
}

static s64 GetRefsKey(const Item* item)
{
    return CountRefs(item, Item::ItemType::Invalid);
}

static s64 GetSoundRefsKey(const Item* item)
{
    return CountRefs(item, Item::ItemType::Sound);
}

static s64 GetSoundPlayerKey(const Item* item)
{
    const Item* player = static_cast<const Sound*>(item)->getPlayerRef().getItem();

    return player ? static_cast<s64>(player->getId()) : -1;
}

static s64 GetSoundTracksKey(const Item* item)
{
    const Sound* sound = static_cast<const Sound*>(item);

    if (sound->getSoundType() != Sound::SoundType::Strm)
        return -1;

    return static_cast<s64>(sound->getStreamSoundInfo().getTrackList().size());
}

static s64 GetSoundSetSoundsKey(const Item* item)
{
    const SoundSet* soundSet = static_cast<const SoundSet*>(item);

    if (soundSet->getIsEmpty())
        return -1;

    return static_cast<s64>(soundSet->getEndId()) - soundSet->getStartId() + 1;
}

static s64 GetSoundSetIdsKey(const Item* item)
{
    const SoundSet* soundSet = static_cast<const SoundSet*>(item);

    return soundSet->getIsEmpty() ? -1 : static_cast<s64>(soundSet->getStartId());
}

static s64 GetGroupItemsKey(const Item* item)
{
    return static_cast<const Group*>(item)->getItemInfoList().size();
}

static s64 GetPlayerLimitKey(const Item* item)
{
    return static_cast<const Player*>(item)->getPlayableSoundMax();
}

static s64 GetWaveFileLengthKey(const Item* item)
{
    const WaveFile* wave = static_cast<const WaveFile*>(item);

    if (wave->getSampleRate() == 0)
        return 0;

    return static_cast<s64>(wave->getSampleCount()) * 1000 / wave->getSampleRate();
}

static s64 GetWaveFileRateKey(const Item* item)
{
    return static_cast<const WaveFile*>(item)->getSampleRate();
}

static s64 GetWaveFileChannelsKey(const Item* item)
{
    return static_cast<s64>(static_cast<const WaveFile*>(item)->getChannels().size());
}

static s64 GetWaveFileSizeKey(const Item* item)
{
    return static_cast<const WaveFile*>(item)->getFileSize();
}

static const WaveFile* GetStreamWaveFile(const Item* item)
{
    const Sound* sound = static_cast<const Sound*>(item);

    if (sound->getSoundType() != Sound::SoundType::Strm)
        return nullptr;

    for (const Item* trackItem : sound->getStreamSoundInfo().getTrackList())
        return static_cast<const WaveFile*>(static_cast<const Sound::StreamSoundInfo::Track*>(trackItem)->getWaveFileRef().getItem());

    return nullptr;
}

static const WaveFile* GetWaveSoundWaveFile(const Item* item)
{
    const Sound* sound = static_cast<const Sound*>(item);

    if (sound->getSoundType() != Sound::SoundType::Wave)
        return nullptr;

    return static_cast<const WaveFile*>(sound->getWaveSoundInfo().getWaveFileRef().getItem());
}

static const Item* GetSoundSequenceFile(const Item* item)
{
    const Sound* sound = static_cast<const Sound*>(item);

    if (sound->getSoundType() != Sound::SoundType::Seq)
        return nullptr;

    return sound->getSequenceSoundInfo().getSequenceFileRef().getItem();
}

static bool IsStreamLoop(const Item* item)
{
    const Sound::StreamSoundInfo& strmInfo = static_cast<const Sound*>(item)->getStreamSoundInfo();

    if (strmInfo.getStreamType() != Sound::StreamSoundInfo::StreamType::NwStreamBinary)
        return strmInfo.getIsLoop();

    const WaveFile* wave = GetStreamWaveFile(item);

    return wave && wave->getIsLoop();
}

static s64 GetStreamLengthKey(const Item* item)
{
    const WaveFile* wave = GetStreamWaveFile(item);

    return wave ? GetWaveFileLengthKey(wave) : -1;
}

static s64 GetWaveSoundLengthKey(const Item* item)
{
    const WaveFile* wave = GetWaveSoundWaveFile(item);

    return wave ? GetWaveFileLengthKey(wave) : -1;
}

static s64 GetWaveSoundWaveFileKey(const Item* item)
{
    const WaveFile* wave = GetWaveSoundWaveFile(item);

    return wave ? static_cast<s64>(wave->getId()) : -1;
}

static s64 GetSoundSequenceFileKey(const Item* item)
{
    const Item* sequenceFile = GetSoundSequenceFile(item);

    return sequenceFile ? static_cast<s64>(sequenceFile->getId()) : -1;
}

static s64 GetSequenceLabelsKey(const Item* item)
{
    return static_cast<s64>(static_cast<const SequenceFile*>(item)->getLabels().size());
}

static s64 GetSequenceSizeKey(const Item* item)
{
    return static_cast<const SequenceFile*>(item)->getSeqBytesSize();
}

static s64 GetBankFileInstrumentsKey(const Item* item)
{
    return static_cast<const BankFile*>(item)->getInstrumentList().size();
}

static void CollectInstrumentWaves(const Item* item, std::vector<const Item*>* waves)
{
    ForEachVelocityRegion(*static_cast<const BankFile::Instrument*>(item), [&](const BankFile::KeyRegion&, const BankFile::VelocityRegion& velocityRegion)
    {
        const Item* wave = velocityRegion.getWaveFileRef().getItem();

        if (wave)
            waves->push_back(wave);
    });
}

static s64 GetBankFileWavesKey(const Item* item)
{
    static std::vector<const Item*> sWaves;
    sWaves.clear();

    for (const Item* instrument : static_cast<const BankFile*>(item)->getInstrumentList())
        CollectInstrumentWaves(instrument, &sWaves);

    return GetUniqueItems(&sWaves).size();
}

static s64 GetInstrumentRegionsKey(const Item* item)
{
    s64 count = 0;

    ForEachVelocityRegion(*static_cast<const BankFile::Instrument*>(item), [&](const BankFile::KeyRegion&, const BankFile::VelocityRegion&)
    {
        count++;
    });

    return count;
}

static s64 GetBankInstrumentsKey(const Item* item)
{
    const Item* bankFile = static_cast<const Bank*>(item)->getFileRef().getItem();

    if (!bankFile)
        return -1;

    return GetBankFileInstrumentsKey(bankFile);
}

void FormatItemSize(sead::BufferedSafeString* out, u32 size)
{
    if (size >= 1024 * 1024)
        out->format(messages::unit::cMegabytesFormat, static_cast<f32>(size) / (1024.0f * 1024.0f));
    else if (size >= 1024)
        out->format(messages::unit::cKilobytesFormat, static_cast<f32>(size) / 1024.0f);
    else
        out->format(messages::unit::cBytesFormat, size);
}

void FormatWaveFileLength(const Item* item, sead::BufferedSafeString* out)
{
    const WaveFile* wave = static_cast<const WaveFile*>(item);

    if (wave->getSampleRate() == 0)
    {
        out->copy("-");
        return;
    }

    const f64 seconds = static_cast<f64>(wave->getSampleCount()) / wave->getSampleRate();
    const u32 minutes = static_cast<u32>(seconds / 60.0);

    out->format("%u:%06.3f", minutes, seconds - minutes * 60.0);
}

static void FormatCount(sead::BufferedSafeString* out, s64 count)
{
    if (count < 0)
        out->copy("-");
    else
        out->format("%lld", count);
}

static void FormatRefsColumn(const Item* item, sead::BufferedSafeString* out)
{
    FormatCount(out, GetRefsKey(item));
}

static void FormatSoundRefsColumn(const Item* item, sead::BufferedSafeString* out)
{
    FormatCount(out, GetSoundRefsKey(item));
}

static void FormatSoundPlayerColumn(const Item* item, sead::BufferedSafeString* out)
{
    const Item* player = static_cast<const Sound*>(item)->getPlayerRef().getItem();

    out->copy(player ? player->getNameOrNull().cstr() : "-");
}

static void FormatSoundTracksColumn(const Item* item, sead::BufferedSafeString* out)
{
    FormatCount(out, GetSoundTracksKey(item));
}

static void FormatLoopLength(const WaveFile* wave, bool loop, sead::BufferedSafeString* out)
{
    sead::FixedSafeString<32> length("-");

    if (wave)
        FormatWaveFileLength(wave, &length);

    if (loop)
        out->format(ICON_LC_REPEAT " %s", length.cstr());
    else
        out->copy(length.cstr());
}

static void FormatStreamLengthColumn(const Item* item, sead::BufferedSafeString* out)
{
    FormatLoopLength(GetStreamWaveFile(item), IsStreamLoop(item), out);
}

static void FormatWaveSoundLengthColumn(const Item* item, sead::BufferedSafeString* out)
{
    const WaveFile* wave = GetWaveSoundWaveFile(item);

    FormatLoopLength(wave, wave && wave->getIsLoop(), out);
}

static void FormatWaveFileLengthColumn(const Item* item, sead::BufferedSafeString* out)
{
    const WaveFile* wave = static_cast<const WaveFile*>(item);

    FormatLoopLength(wave, wave->getIsLoop(), out);
}

static void FormatWaveSoundWaveFileColumn(const Item* item, sead::BufferedSafeString* out)
{
    const WaveFile* wave = GetWaveSoundWaveFile(item);

    out->copy(wave ? wave->getNameOrNull().cstr() : "-");
}

static void FormatSoundSequenceFileColumn(const Item* item, sead::BufferedSafeString* out)
{
    const Item* sequenceFile = GetSoundSequenceFile(item);

    out->copy(sequenceFile ? sequenceFile->getNameOrNull().cstr() : "-");
}

static void FormatSoundSetSoundsColumn(const Item* item, sead::BufferedSafeString* out)
{
    FormatCount(out, GetSoundSetSoundsKey(item));
}

static void FormatSoundSetIdsColumn(const Item* item, sead::BufferedSafeString* out)
{
    const SoundSet* soundSet = static_cast<const SoundSet*>(item);

    if (soundSet->getIsEmpty())
        out->copy("-");
    else
        out->format("%u-%u", soundSet->getStartId(), soundSet->getEndId());
}

static void FormatGroupItemsColumn(const Item* item, sead::BufferedSafeString* out)
{
    FormatCount(out, GetGroupItemsKey(item));
}

static void FormatPlayerLimitColumn(const Item* item, sead::BufferedSafeString* out)
{
    FormatCount(out, GetPlayerLimitKey(item));
}

static void FormatWaveFileRateColumn(const Item* item, sead::BufferedSafeString* out)
{
    out->format(messages::unit::cHertzFormat, static_cast<const WaveFile*>(item)->getSampleRate());
}

static void FormatChannelCount(sead::BufferedSafeString* out, s64 channels)
{
    if (channels == 1)
        out->copy(messages::common::cMono);
    else if (channels == 2)
        out->copy(messages::common::cStereo);
    else
        FormatCount(out, channels);
}

void FormatWaveFileChannels(const Item* item, sead::BufferedSafeString* out)
{
    FormatChannelCount(out, GetWaveFileChannelsKey(item));
}

static void FormatWaveFileSizeColumn(const Item* item, sead::BufferedSafeString* out)
{
    FormatItemSize(out, static_cast<const WaveFile*>(item)->getFileSize());
}

static void FormatSequenceLabelsColumn(const Item* item, sead::BufferedSafeString* out)
{
    out->format("%lld", GetSequenceLabelsKey(item));
}

static void FormatSequenceSizeColumn(const Item* item, sead::BufferedSafeString* out)
{
    FormatItemSize(out, static_cast<const SequenceFile*>(item)->getSeqBytesSize());
}

static void FormatBankFileInstrumentsColumn(const Item* item, sead::BufferedSafeString* out)
{
    out->format("%lld", GetBankFileInstrumentsKey(item));
}

static void FormatBankFileWavesColumn(const Item* item, sead::BufferedSafeString* out)
{
    out->format("%lld", GetBankFileWavesKey(item));
}

static void FormatInstrumentRegionsColumn(const Item* item, sead::BufferedSafeString* out)
{
    out->format("%lld", GetInstrumentRegionsKey(item));
}

static void FormatBankInstrumentsColumn(const Item* item, sead::BufferedSafeString* out)
{
    FormatCount(out, GetBankInstrumentsKey(item));
}

static s64 GetSoundTypeKey(const Item* item)
{
    return static_cast<s64>(static_cast<const Sound*>(item)->getSoundType());
}

static void FormatSoundTypeColumn(const Item* item, sead::BufferedSafeString* out)
{
    switch (static_cast<const Sound*>(item)->getSoundType())
    {
        case Sound::SoundType::Strm:
            out->copy(messages::common::cStream);
            return;

        case Sound::SoundType::Wave:
            out->copy(messages::common::cWave);
            return;

        case Sound::SoundType::Seq:
            out->copy(messages::common::cSequence);
            return;

        default:
            break;
    }

    out->copy("-");
}

static s64 GetStreamChannelsKey(const Item* item)
{
    const Sound* sound = static_cast<const Sound*>(item);

    if (sound->getSoundType() != Sound::SoundType::Strm)
        return -1;

    return sound->getStreamSoundInfo().getAllocateChannelCount();
}

static void FormatStreamChannelsColumn(const Item* item, sead::BufferedSafeString* out)
{
    FormatChannelCount(out, GetStreamChannelsKey(item));
}

static s64 GetWaveFileEncodingKey(const Item* item)
{
    return static_cast<s64>(static_cast<const WaveFile*>(item)->getEncoding());
}

static void FormatWaveFileEncodingColumn(const Item* item, sead::BufferedSafeString* out)
{
    const s64 encoding = GetWaveFileEncodingKey(item);

    out->copy(encoding >= 0 && encoding < IM_ARRAYSIZE(WaveFile::sEncodingTypes) ? WaveFile::sEncodingTypes[encoding] : "-");
}

static s64 GetBankFileKey(const Item* item)
{
    const Item* bankFile = static_cast<const Bank*>(item)->getFileRef().getItem();

    return bankFile ? static_cast<s64>(bankFile->getId()) : -1;
}

static void FormatBankFileColumn(const Item* item, sead::BufferedSafeString* out)
{
    const Item* bankFile = static_cast<const Bank*>(item)->getFileRef().getItem();

    out->copy(bankFile ? bankFile->getNameOrNull().cstr() : "-");
}

static void DrawTooltipMore(u32 nameCount)
{
    if (nameCount > cItemTooltipNameMax)
        ImGui::TextDisabled(messages::item::cMoreTooltipFormat, nameCount - cItemTooltipNameMax);
}

static void DrawSequenceLabelsTooltip(const Item* item)
{
    const std::vector<std::string>& labels = static_cast<const SequenceFile*>(item)->getLabels();
    const u32 nameCount = static_cast<u32>(labels.size());

    if (nameCount == 0 || !ImGui::BeginTooltip())
        return;

    for (u32 i = 0; i < nameCount && i < cItemTooltipNameMax; i++)
        ImGui::TextUnformatted(labels[i].c_str());

    DrawTooltipMore(nameCount);

    ImGui::EndTooltip();
}

static void DrawSoundUsesTooltip(const Item* item)
{
    std::vector<const Item*>& sounds = CollectRefOwners(item, Item::ItemType::Sound);
    const u32 nameCount = static_cast<u32>(sounds.size());

    if (nameCount == 0 || !ImGui::BeginTooltip())
        return;

    std::sort(sounds.begin(), sounds.end(), [](const Item* a, const Item* b)
    {
        return a->getId() < b->getId();
    });

    for (u32 i = 0; i < nameCount && i < cItemTooltipNameMax; i++)
        ImGui::TextUnformatted(sounds[i]->getFormattedName().cstr());

    DrawTooltipMore(nameCount);

    ImGui::EndTooltip();
}

static void DrawBankFileInstrumentsTooltip(const Item* item)
{
    const Item::List& instruments = static_cast<const BankFile*>(item)->getInstrumentList();

    if (instruments.size() == 0 || !ImGui::BeginTooltip())
        return;

    u32 i = 0;
    for (const Item* instrument : instruments)
    {
        if (i++ >= cItemTooltipNameMax)
            break;

        ImGui::TextUnformatted(instrument->getFormattedName().cstr());
    }

    DrawTooltipMore(static_cast<u32>(instruments.size()));

    ImGui::EndTooltip();
}

static void DrawLoopTooltip(const WaveFile* wave, bool loop)
{
    if (!loop)
        return;

    if (!wave || wave->getSampleRate() == 0)
    {
        ImGui::SetTooltip("%s", messages::item::cLoopTooltip);
        return;
    }

    const f64 seconds = static_cast<f64>(wave->getOriginalLoopStartFrame()) / wave->getSampleRate();
    const u32 minutes = static_cast<u32>(seconds / 60.0);

    ImGui::SetTooltip(messages::item::cLoopStartTooltipFormat, minutes, seconds - minutes * 60.0);
}

static void DrawStreamLoopTooltip(const Item* item)
{
    const WaveFile* wave = GetStreamWaveFile(item);

    DrawLoopTooltip(wave && wave->getIsLoop() ? wave : nullptr, IsStreamLoop(item));
}

static void DrawWaveSoundLoopTooltip(const Item* item)
{
    const WaveFile* wave = GetWaveSoundWaveFile(item);

    DrawLoopTooltip(wave, wave && wave->getIsLoop());
}

static void DrawWaveFileLoopTooltip(const Item* item)
{
    const WaveFile* wave = static_cast<const WaveFile*>(item);

    DrawLoopTooltip(wave, wave->getIsLoop());
}

static const ItemColumn cSoundColumns[] = {
    { "Type",messages::common::cType,   "PLAYER_SE_OBJ_ENAB", &FormatSoundTypeColumn,   &GetSoundTypeKey,   cItemColumnFlag_Text | cItemColumnFlag_Hidden },
    { "Player", messages::common::cPlayer, "PLAYER_SE_OBJ_ENAB", &FormatSoundPlayerColumn, &GetSoundPlayerKey, cItemColumnFlag_Text | cItemColumnFlag_Hidden }
};

static const ItemColumn cStreamSoundColumns[] = {
    { "Tracks",   messages::common::cTracks,   "0",                             &FormatSoundTracksColumn,    &GetSoundTracksKey,    cItemColumnFlag_Hidden },
    { "Channels", messages::common::cChannels, messages::common::cStereo, &FormatStreamChannelsColumn, &GetStreamChannelsKey, cItemColumnFlag_Hidden },
    { "Length",   messages::common::cLength,   cLoopLengthSample,               &FormatStreamLengthColumn,   &GetStreamLengthKey,   cItemColumnFlag_None, &DrawStreamLoopTooltip },
    { "Player",   messages::common::cPlayer,   "PLAYER_SE_OBJ_ENAB",            &FormatSoundPlayerColumn,    &GetSoundPlayerKey,    cItemColumnFlag_Text | cItemColumnFlag_Hidden }
};

static const ItemColumn cWaveSoundColumns[] = {
    { "Length",    messages::common::cLength,   cLoopLengthSample,    &FormatWaveSoundLengthColumn,   &GetWaveSoundLengthKey,   cItemColumnFlag_None, &DrawWaveSoundLoopTooltip },
    { "Wave File", messages::common::cWaveFile, "PLAYER_SE_OBJ_ENAB", &FormatWaveSoundWaveFileColumn, &GetWaveSoundWaveFileKey, cItemColumnFlag_Text | cItemColumnFlag_TextEnd | cItemColumnFlag_Hidden },
    { "Player",    messages::common::cPlayer,   "PLAYER_SE_OBJ_ENAB", &FormatSoundPlayerColumn,       &GetSoundPlayerKey,       cItemColumnFlag_Text | cItemColumnFlag_Hidden }
};

static const ItemColumn cSequenceSoundColumns[] = {
    { "Sequence File", messages::common::cSequenceFile, "PLAYER_SE_OBJ_ENAB", &FormatSoundSequenceFileColumn, &GetSoundSequenceFileKey, cItemColumnFlag_Text | cItemColumnFlag_Hidden },
    { "Player",        messages::common::cPlayer,   "PLAYER_SE_OBJ_ENAB", &FormatSoundPlayerColumn,       &GetSoundPlayerKey,       cItemColumnFlag_Text | cItemColumnFlag_Hidden }
};

static const ItemColumn cSoundSetColumns[] = {
    { "Sounds", messages::common::cSounds, "0000",      &FormatSoundSetSoundsColumn, &GetSoundSetSoundsKey },
    { "IDs",    messages::item::cColumnIds,    "0000-0000", &FormatSoundSetIdsColumn,    &GetSoundSetIdsKey, cItemColumnFlag_Hidden }
};

static const ItemColumn cGroupColumns[] = {
    { "Items", messages::common::cItems, "0000", &FormatGroupItemsColumn, &GetGroupItemsKey }
};

static const ItemColumn cPlayerColumns[] = {
    { "Sound Limit", messages::player::cSoundLimit, "000",  &FormatPlayerLimitColumn, &GetPlayerLimitKey },
    { "SoundUses",   messages::item::cColumnUses,   "0000", &FormatSoundRefsColumn,   &GetSoundRefsKey, cItemColumnFlag_WarnZero | cItemColumnFlag_Hidden, &DrawSoundUsesTooltip }
};

static const ItemColumn cWaveArchiveColumns[] = {
    { "Uses", messages::item::cColumnUses, "0000", &FormatRefsColumn, &GetRefsKey, cItemColumnFlag_WarnZero }
};

static const ItemColumn cWaveFileColumns[] = {
    { "Uses",     messages::item::cColumnUses,     "0000",     &FormatRefsColumn,             &GetRefsKey, cItemColumnFlag_WarnZero | cItemColumnFlag_Hidden },
    { "Length",   messages::common::cLength,   cLoopLengthSample, &FormatWaveFileLengthColumn, &GetWaveFileLengthKey, cItemColumnFlag_None, &DrawWaveFileLoopTooltip },
    { "Channels", messages::common::cChannels, messages::common::cStereo, &FormatWaveFileChannels,      &GetWaveFileChannelsKey, cItemColumnFlag_Hidden },
    { "Rate",     messages::item::cColumnRate,     messages::unit::cHertzSample, &FormatWaveFileRateColumn,    &GetWaveFileRateKey,     cItemColumnFlag_Hidden },
    { "Encoding", messages::common::cEncoding, messages::wavefile::cEncodingDspAdpcm, &FormatWaveFileEncodingColumn, &GetWaveFileEncodingKey, cItemColumnFlag_Hidden },
    { "Size",     messages::common::cSize,     messages::unit::cKilobytesSample, &FormatWaveFileSizeColumn,   &GetWaveFileSizeKey,     cItemColumnFlag_Hidden }
};

static const ItemColumn cSequenceFileColumns[] = {
    { "Labels",    messages::common::cLabels,   "0000", &FormatSequenceLabelsColumn, &GetSequenceLabelsKey, cItemColumnFlag_None, &DrawSequenceLabelsTooltip },
    { "SoundUses", messages::item::cColumnUses, "0000", &FormatSoundRefsColumn,      &GetSoundRefsKey,      cItemColumnFlag_WarnZero | cItemColumnFlag_Hidden, &DrawSoundUsesTooltip },
    { "Size",   messages::common::cSize,   messages::unit::cSizeSample, &FormatSequenceSizeColumn,  &GetSequenceSizeKey, cItemColumnFlag_Hidden }
};

static const ItemColumn cBankFileColumns[] = {
    { "Instruments", messages::common::cInstruments, "0000", &FormatBankFileInstrumentsColumn, &GetBankFileInstrumentsKey, cItemColumnFlag_None, &DrawBankFileInstrumentsTooltip },
    { "Waves",       messages::item::cColumnWaves,       "0000", &FormatBankFileWavesColumn,       &GetBankFileWavesKey, cItemColumnFlag_Hidden }
};

static const ItemColumn cBankColumns[] = {
    { "Instruments", messages::common::cInstruments, "0000", &FormatBankInstrumentsColumn, &GetBankInstrumentsKey },
    { "SoundUses",   messages::item::cColumnUses,    "0000", &FormatSoundRefsColumn,       &GetSoundRefsKey, cItemColumnFlag_WarnZero | cItemColumnFlag_Hidden, &DrawSoundUsesTooltip },
    { "Bank File",   messages::common::cBankFile,    "PLAYER_SE_OBJ_ENAB", &FormatBankFileColumn, &GetBankFileKey, cItemColumnFlag_Text | cItemColumnFlag_Hidden }
};

static const ItemColumn cInstrumentColumns[] = {
    { "Regions", messages::item::cColumnRegions, "0000", &FormatInstrumentRegionsColumn, &GetInstrumentRegionsKey, cItemColumnFlag_WarnZero }
};

const char* GetItemTypeName(Item::ItemType itemType)
{
    switch (itemType)
    {
        case Item::ItemType::Sound:
            return messages::common::cSound;

        case Item::ItemType::SoundSet:
            return messages::item::cTypeSoundSet;

        case Item::ItemType::Bank:
            return messages::common::cBank;

        case Item::ItemType::WaveArchive:
            return messages::common::cWaveArchive;

        case Item::ItemType::Group:
            return messages::common::cGroup;

        case Item::ItemType::Player:
            return messages::common::cPlayer;

        case Item::ItemType::WaveFile:
            return messages::common::cWaveFile;

        case Item::ItemType::SequenceFile:
            return messages::common::cSequenceFile;

        case Item::ItemType::BankFile:
            return messages::common::cBankFile;

        case Item::ItemType::BankFileInstrument:
            return messages::common::cInstrument;

        default:
            break;
    }

    return messages::common::cItem;
}

const ItemColumn* GetItemColumns(Item::ItemType itemType, u32* outCount)
{
    switch (itemType)
    {
        case Item::ItemType::WaveFile:
            *outCount = IM_ARRAYSIZE(cWaveFileColumns);
            return cWaveFileColumns;

        case Item::ItemType::SequenceFile:
            *outCount = IM_ARRAYSIZE(cSequenceFileColumns);
            return cSequenceFileColumns;

        case Item::ItemType::BankFile:
            *outCount = IM_ARRAYSIZE(cBankFileColumns);
            return cBankFileColumns;

        case Item::ItemType::Bank:
            *outCount = IM_ARRAYSIZE(cBankColumns);
            return cBankColumns;

        case Item::ItemType::Sound:
            *outCount = IM_ARRAYSIZE(cSoundColumns);
            return cSoundColumns;

        case Item::ItemType::SoundSet:
            *outCount = IM_ARRAYSIZE(cSoundSetColumns);
            return cSoundSetColumns;

        case Item::ItemType::WaveArchive:
            *outCount = IM_ARRAYSIZE(cWaveArchiveColumns);
            return cWaveArchiveColumns;

        case Item::ItemType::Player:
            *outCount = IM_ARRAYSIZE(cPlayerColumns);
            return cPlayerColumns;

        case Item::ItemType::Group:
            *outCount = IM_ARRAYSIZE(cGroupColumns);
            return cGroupColumns;

        case Item::ItemType::GroupItemInfo:
            *outCount = 0;
            return cGroupColumns;

        case Item::ItemType::BankFileInstrument:
            *outCount = IM_ARRAYSIZE(cInstrumentColumns);
            return cInstrumentColumns;

        default:
            break;
    }

    *outCount = 0;
    return nullptr;
}

const ItemColumn* GetSoundTypeColumns(Sound::SoundType soundType, u32* outCount)
{
    switch (soundType)
    {
        case Sound::SoundType::Strm:
            *outCount = IM_ARRAYSIZE(cStreamSoundColumns);
            return cStreamSoundColumns;

        case Sound::SoundType::Wave:
            *outCount = IM_ARRAYSIZE(cWaveSoundColumns);
            return cWaveSoundColumns;

        case Sound::SoundType::Seq:
            *outCount = IM_ARRAYSIZE(cSequenceSoundColumns);
            return cSequenceSoundColumns;

        default:
            break;
    }

    return GetItemColumns(Item::ItemType::Sound, outCount);
}
