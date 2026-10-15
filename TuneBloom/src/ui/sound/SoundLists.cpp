#include <ui/sound/SoundViews.h>

#include <ui/widgets/ContextMenu.h>
#include <ui/widgets/Dialogs.h>
#include <ui/export/Export.h>
#include <ui/file/File.h>
#include <ui/project/FileLists.h>
#include <ui/items/ItemColumns.h>
#include <ui/items/ItemList.h>
#include <ui/widgets/ListFilter.h>
#include <ui/Messages.h>
#include <ui/app/Notifications.h>
#include <ui/player/PlayerViews.h>
#include <ui/project/References.h>
#include <ui/items/Selection.h>
#include <ui/settings/Theme.h>
#include <ui/settings/UiColors.h>
#include <ui/widgets/Widgets.h>
#include <ui/app/Workspace.h>

#include <libs/IconsLucide.h>

#include <algorithm>
#include <vector>

static void SequenceSoundSetContextMenuFunc(Item* item);

static void DrawSoundPlayButton(Sound* sound)
{
    sead::FixedSafeString<SoundPlayer::cPlaybackErrorLength> playbackError;
    const bool canPlay = !SoundPlayer::GetSoundPlaybackError(*sound, &playbackError);

    const bool playing = sSoundPlayer.isActive() && sSoundPlayer.getPlayingSound() == sound;

    if (!DrawRowPlayButton(sound->getId(), canPlay ? nullptr : playbackError.cstr(), playing))
        return;

    if (sound != sSelectedItem)
    {
        sSubSelectedItem = nullptr;
        sSelectedItemIsSubWindow = false;
    }

    sSoundPlayer.playSound(sound);
}

static void SoundRowActionFunc(Item* item)
{
    DrawSoundPlayButton(static_cast<Sound*>(item));
}

const char* SoundNamePrefixFunc(Item* item)
{
    return GetSoundIcon(static_cast<const Sound*>(item));
}

static void ReloadStreamFiles(const std::vector<Sound*>& sounds)
{
    u32 streamNum = 0;
    u32 reloaded = 0;

    for (Sound* sound : sounds)
    {
        if (sound->getSoundType() != Sound::SoundType::Strm)
            continue;

        streamNum++;

        if (ReloadStreamFile(sound))
            reloaded++;
    }

    if (streamNum > 1)
    {
        sead::FormatFixedSafeString<128> msg(messages::stream::cReloadBatchDoneFormat, reloaded, streamNum);
        notify::Add(reloaded == streamNum ? notify::Kind::Success : notify::Kind::Warning, msg.cstr());
    }
}

SoundExportCheck CheckSoundExport(const std::vector<Sound*>& sounds)
{
    SoundExportCheck check;

    for (const Sound* target : sounds)
    {
        const Sound::SoundType type = target->getSoundType();
        const bool hasExportType = type == Sound::SoundType::Wave || type == Sound::SoundType::Strm || type == Sound::SoundType::Seq;

        check.onlySequence &= type == Sound::SoundType::Seq;
        check.canExport &= hasExportType && SoundPlayer::CanPlaySound(*target);
    }

    check.reason.copy(messages::exporting::cSelectionNotExportable);

    if (sounds.size() == 1)
    {
        sead::FixedSafeString<SoundPlayer::cPlaybackErrorLength> playbackError;
        const bool hasError = SoundPlayer::GetSoundPlaybackError(*sounds.front(), &playbackError) && !playbackError.isEmpty();

        check.reason.copy(hasError ? playbackError.cstr() : messages::exporting::cWaveNotExportable);
    }

    return check;
}

void SoundContextMenuFunc(Item* item)
{
    if (!item)
        return;

    Sound* sound = static_cast<Sound*>(item);
    const std::vector<Sound*> sounds = CollectForAction<Sound>(item);
    const bool single = sounds.size() == 1;

    bool hasStream = false;
    bool onlyWave = true;

    for (Sound* target : sounds)
    {
        hasStream |= target->getSoundType() == Sound::SoundType::Strm;
        onlyWave &= target->getSoundType() == Sound::SoundType::Wave;
    }

    const SoundExportCheck exportCheck = CheckSoundExport(sounds);

    // Files
    ContextMenuSeparator();

    if (hasStream && ContextMenuItem(ICON_LC_REFRESH_CW, messages::stream::cReloadMenuLabel))
        ReloadStreamFiles(sounds);

    if (onlyWave)
    {
        WaveFile* wave = single ? static_cast<WaveFile*>(sound->getWaveSoundInfo().getWaveFileRef().getItem()) : nullptr;
        const char* reason = single ? messages::reference::cNoWaveFile : messages::import::cWaveFileNotSingleSound;

        if (ContextMenuItem(ICON_LC_REPLACE, messages::import::cReplaceWaveFileMenuLabel, wave != nullptr, reason))
            RequestWaveFileReplace(wave);

        if (ContextMenuItem(ICON_LC_SLIDERS_HORIZONTAL, messages::import::cReimportWaveFileMenuLabel, wave != nullptr, reason))
            RequestWaveFileReimport(wave);
    }

    // Export
    ContextMenuSeparator();

    if (ContextMenuItem(ICON_LC_FILE_AUDIO, messages::menu::cExportWav, exportCheck.canExport, exportCheck.reason.cstr()))
        RequestExportSounds(sounds);

    if (exportCheck.onlySequence && ContextMenuItem(ICON_LC_FILE_MUSIC, messages::menu::cExportMidi, exportCheck.canExport, exportCheck.reason.cstr()))
        RequestExportMidi(sounds);
}

void DrawAllSoundsUI()
{
    DrawTabFilterBar();

    SetNextItemListAction(&SoundRowActionFunc);

    DrawAllItemsUI("Sound", messages::common::cSound, sBfsar.getSoundList(),
        &CreateSoundFunc, &SoundNamePrefixFunc, &SoundContextMenuFunc, GetItemFilterCallback()
    );
}

void DrawStreamSoundsUI()
{
    DrawTabFilterBar();

    u32 columnCount = 0;
    const ItemColumn* columns = GetSoundTypeColumns(Sound::SoundType::Strm, &columnCount);
    SetNextItemListColumns(columns, columnCount);
    SetNextItemListAction(&SoundRowActionFunc);

    DrawAllItemsUI("Stream Sound", messages::item::cTypeStreamSound,sBfsar.getSoundList(), &CreateStreamSoundFunc, nullptr, &SoundContextMenuFunc,
        [](const Item* item)
        {
            const Sound* sound = static_cast<const Sound*>(item);
            return sound->getSoundType() == Sound::SoundType::Strm && ItemMatchesFilter(sound);
        },
        false, &InsertSoundAtHook, &RemoveSoundWithCascade
    );
}

static bool SoundIsInAnySet(const Sound *sound)
{
    u32 id = sound->getId();
    for (auto it = sBfsar.getSoundSetList().robustBegin(); it != sBfsar.getSoundSetList().robustEnd(); ++it)
    {
        const SoundSet *set = static_cast<const SoundSet *>(it->val());
        if (set->getIsEmpty())
            continue;

        if (id >= set->getStartId() && id <= set->getEndId())
            return true;
    }

    return false;
}

template <Sound::SoundType Type>
static void DrawTypedSoundsUI(const char* id, const char* title, CreateItemCallback createCallback)
{
    static bool sIncludeInSet = true;

    if (BeginListOptions(!sIncludeInSet))
    {
        if (ContextMenuCheckItem(messages::item::cShowSoundsInSet, sIncludeInSet))
            sIncludeInSet = !sIncludeInSet;

        EndContextMenu();
    }

    if (!sIncludeInSet)
        SetNextItemListNote(messages::item::cSoundsInSetHidden);

    u32 columnCount = 0;
    const ItemColumn* columns = GetSoundTypeColumns(Type, &columnCount);
    SetNextItemListColumns(columns, columnCount);
    SetNextItemListAction(&SoundRowActionFunc);

    DrawAllItemsUI(id, title, sBfsar.getSoundList(), createCallback, nullptr, &SoundContextMenuFunc,
        [](const Item* item)
        {
            const Sound* sound = static_cast<const Sound*>(item);

            if (sound->getSoundType() != Type || !ItemMatchesFilter(sound))
                return false;

            return sIncludeInSet || !SoundIsInAnySet(sound);
        },
        false, &InsertSoundAtHook, &RemoveSoundWithCascade
    );
}

void DrawWaveSoundsUI()
{
    DrawTypedSoundsUI<Sound::SoundType::Wave>("Wave Sound", messages::common::cWaveSound, &CreateWaveSoundFunc);
}

void DrawSequenceSoundsUI()
{
    DrawTypedSoundsUI<Sound::SoundType::Seq>("Sequence Sound", messages::item::cTypeSequenceSound, &CreateSequenceSoundFunc);
}

const char* SoundSetNamePrefixFunc(Item* item)
{
    SoundSet* sound = (SoundSet*)item;

    const char* icon = ICON_LC_FILE_QUESTION " ";
    switch (sound->getSoundSetType())
    {
        case SoundSet::SoundSetType::Wave:
            icon = ICON_LC_AUDIO_WAVEFORM " ";
            break;

        case SoundSet::SoundSetType::Seq:
            icon = ICON_LC_MUSIC_2 " ";
            break;
    }

    return icon;
}

InstanciateItemCallback CreateSoundSetFunc(bool clear)
{
    auto innerFunc = [](bool clear, Item *item, bool *validate)
    {
        static int sSoundSetType = 0;

        if (clear)
        {
            sSoundSetType = 0;
        }

        if (!item && !validate)
        {
            DialogRow(messages::common::cType);
            ImGui::RadioButton(messages::common::cWave, &sSoundSetType, 0);
            ImGui::SameLine();
            ImGui::RadioButton(messages::common::cSequence, &sSoundSetType, 1);
        }
        else if (!validate)
        {
            SoundSet *soundSet = static_cast<SoundSet *>(item);

            soundSet->setSoundSetType(static_cast<SoundSet::SoundSetType>(sSoundSetType));
        }
    };

    return CreateItemFunc(clear, []() -> Item* { return new SoundSet(); }, innerFunc);
}

template <SoundSet::SoundSetType FixedType>
static InstanciateItemCallback CreateFixedSoundSetTypeFunc(bool clear)
{
    auto innerFunc = [](bool clear, Item* item, bool* validate)
    {
        if (item && !validate)
            static_cast<SoundSet*>(item)->setSoundSetType(FixedType);
    };

    return CreateItemFunc(clear, []() -> Item* { return new SoundSet(); }, innerFunc);
}

InstanciateItemCallback CreateWaveSoundSetFunc(bool clear)
{
    return CreateFixedSoundSetTypeFunc<SoundSet::SoundSetType::Wave>(clear);
}

InstanciateItemCallback CreateSequenceSoundSetFunc(bool clear)
{
    return CreateFixedSoundSetTypeFunc<SoundSet::SoundSetType::Seq>(clear);
}

static constexpr u8  cRangeSetAlpha         = 160;
static constexpr u8  cRangeViewedSetAlpha   = 255;
static constexpr u8  cRangeTextDimAlpha     = 190;
static constexpr u8  cRangeOverlapAlpha     = 180;
static constexpr u8  cRangeHoverAlpha       = 35;
static constexpr u8  cRangeOutlineAlpha     = 160;
static constexpr f32 cRangeViewedMarkHeight = 3.0f;
static constexpr f32 cRangeBarHeight        = 28.0f;
static constexpr f32 cRangeBarOuterPad      = 2.0f;

namespace
{

struct RangeSet
{
    SoundSet* set;
    u32 start;
    u32 end;
    u32 index;
};

struct RangeBar
{
    ImVec2 min;
    ImVec2 max;
    f32 originX;
    f32 originY;
    f32 width;
    u32 firstId;
    f32 idRange;

    f32 calcX(u32 id) const
    {
        return originX + ((f32)(id - firstId) / idRange) * width;
    }

    f32 calcEndX(f32 startX, u32 endId) const
    {
        const f32 endX = calcX(endId + 1);
        return endX - startX < 1.0f ? startX + 1.0f : endX;
    }
};

}

static std::vector<RangeSet> CollectRangeSets(const SoundSet::List& list, bool filterByType, SoundSet::SoundSetType filterType)
{
    std::vector<RangeSet> sets;
    u32 index = 0;

    for (auto it = list.begin(); it != list.end(); ++it, index++)
    {
        SoundSet* soundSet = static_cast<SoundSet*>(*it);

        if (filterByType && soundSet->getSoundSetType() != filterType)
            continue;

        if (soundSet->getIsEmpty())
            continue;

        const u32 start = soundSet->getStartId();
        const u32 end = soundSet->getEndId();

        if (start == Item::cInvalidId || end == Item::cInvalidId || end < start)
            continue;

        sets.push_back({ soundSet, start, end, index });
    }

    return sets;
}

static RangeBar CalcRangeBar(const std::vector<RangeSet>& sets, u32 totalSounds)
{
    u32 globalMin = totalSounds;
    u32 globalMax = 0;
    for (const RangeSet& s : sets)
    {
        if (s.start < globalMin) globalMin = s.start;
        if (s.end > globalMax) globalMax = s.end;
    }
    if (globalMax < globalMin) globalMax = globalMin;
    f32 floatRange = (f32)(globalMax - globalMin + 1);
    if (floatRange <= 0.0f) floatRange = 1.0f;

    ImVec2 cursorPos = ImGui::GetCursorScreenPos();
    cursorPos.x += ImGui::GetStyle().WindowPadding.x;
    f32 width = ImGui::GetContentRegionAvail().x - ImGui::GetStyle().WindowPadding.x * 2.0f;
    if (width < 10.0f) width = 10.0f;

    RangeBar bar;
    bar.min = ImVec2(cursorPos.x, cursorPos.y + cRangeBarOuterPad);
    bar.max = ImVec2(cursorPos.x + width, cursorPos.y + cRangeBarOuterPad + cRangeBarHeight);
    bar.originX = cursorPos.x;
    bar.originY = cursorPos.y;
    bar.width = width;
    bar.firstId = globalMin;
    bar.idRange = floatRange;

    return bar;
}

static void DrawRangeSet(ImDrawList* drawList, const RangeBar& bar, const RangeSet& rangeSet, bool viewed)
{
    const f32 x1 = bar.calcX(rangeSet.start);
    const f32 x2 = bar.calcEndX(x1, rangeSet.end);

    const ImU32 ink = ImGui::GetColorU32(ImGuiCol_Text);
    const ImU32 color = uicolors::ToU32(GetSeriesColor(rangeSet.index), viewed ? cRangeViewedSetAlpha : cRangeSetAlpha);
    const ImU32 textColor = viewed ? ink : uicolors::WithAlpha(ink, cRangeTextDimAlpha);

    drawList->AddRectFilled(ImVec2(x1, bar.min.y), ImVec2(x2, bar.max.y), color);

    if (viewed)
        drawList->AddRectFilled(ImVec2(x1, bar.max.y - cRangeViewedMarkHeight), ImVec2(x2, bar.max.y), ink);

    if (x2 - x1 > 3.0f)
        drawList->AddLine(ImVec2(x1 + 0.5f, bar.min.y), ImVec2(x1 + 0.5f, bar.max.y), ImGui::GetColorU32(ImGuiCol_FrameBg));

    const char* name = rangeSet.set->getNameOrNull().cstr();
    ImVec2 textSize = ImGui::CalcTextSize(name);
    if (x2 - x1 > textSize.x + 6.0f)
    {
        f32 textX = x1 + (x2 - x1 - textSize.x) * 0.5f;
        if (textX < bar.min.x + 2.0f)
            textX = bar.min.x + 2.0f;

        if (textX + textSize.x > bar.max.x - 2.0f)
            textX = bar.max.x - textSize.x - 2.0f;

        drawList->AddText(ImVec2(textX, bar.min.y + (cRangeBarHeight - textSize.y) * 0.5f), textColor, name);
        return;
    }

    sead::FormatFixedSafeString<16> idStr("%u", rangeSet.set->getId());
    ImVec2 idSize = ImGui::CalcTextSize(idStr.cstr());
    if (x2 - x1 > idSize.x + 4.0f)
    {
        f32 textX = x1 + (x2 - x1 - idSize.x) * 0.5f;
        drawList->AddText(ImVec2(textX, bar.min.y + (cRangeBarHeight - idSize.y) * 0.5f), textColor, idStr.cstr());
    }
}

static void DrawRangeOverlaps(ImDrawList* drawList, const RangeBar& bar, const std::vector<RangeSet>& sets)
{
    for (size_t i = 0; i < sets.size(); i++)
    {
        for (size_t j = i + 1; j < sets.size(); j++)
        {
            u32 ovStart = sets[i].start > sets[j].start ? sets[i].start : sets[j].start;
            u32 ovEnd = sets[i].end < sets[j].end ? sets[i].end : sets[j].end;
            if (ovStart <= ovEnd)
            {
                const f32 ox1 = bar.calcX(ovStart);
                const f32 ox2 = bar.calcEndX(ox1, ovEnd);
                drawList->AddRectFilled(ImVec2(ox1, bar.min.y), ImVec2(ox2, bar.max.y), uicolors::ToU32(uicolors::Marker(), cRangeOverlapAlpha));
            }
        }
    }
}

static const RangeSet* FindHoveredRangeSet(const RangeBar& bar, const std::vector<RangeSet>& sets)
{
    if (!ImGui::IsItemHovered())
        return nullptr;

    const f32 relX = ImGui::GetMousePos().x - bar.min.x;
    if (relX < 0.0f || relX > bar.width)
        return nullptr;

    const f32 hoveredId = (f32)bar.firstId + (relX / bar.width) * bar.idRange;

    for (const RangeSet& s : sets)
    {
        if (hoveredId >= (f32)s.start && hoveredId < (f32)(s.end + 1))
            return &s;
    }

    return nullptr;
}

static void DrawSoundSetRangeVisualizer(const SoundSet::List& list, bool filterByType, SoundSet::SoundSetType filterType, const SoundSet* viewedSet)
{
    u32 totalSounds = sBfsar.getSoundList().size();
    if (totalSounds == 0)
        return;

    const std::vector<RangeSet> sets = CollectRangeSets(list, filterByType, filterType);
    if (sets.empty())
        return;

    const RangeBar bar = CalcRangeBar(sets, totalSounds);

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    drawList->AddRectFilled(bar.min, bar.max, ImGui::GetColorU32(ImGuiCol_FrameBg));

    for (const RangeSet& rangeSet : sets)
        DrawRangeSet(drawList, bar, rangeSet, rangeSet.set == viewedSet);

    DrawRangeOverlaps(drawList, bar, sets);

    drawList->AddRect(bar.min, bar.max, ImGui::GetColorU32(ImGuiCol_Border));

    ImGui::SetCursorScreenPos(ImVec2(bar.originX, bar.originY));
    ImGui::InvisibleButton("##rangeVis", ImVec2(bar.width, cRangeBarHeight + cRangeBarOuterPad * 2.0f));

    const RangeSet* hoveredSet = FindHoveredRangeSet(bar, sets);
    if (!hoveredSet)
        return;

    const f32 hx1 = bar.calcX(hoveredSet->start);
    const f32 hx2 = bar.calcEndX(hx1, hoveredSet->end);
    const ImU32 ink = ImGui::GetColorU32(ImGuiCol_Text);

    drawList->AddRectFilled(ImVec2(hx1, bar.min.y), ImVec2(hx2, bar.max.y), uicolors::WithAlpha(ink, cRangeHoverAlpha));
    drawList->AddRect(ImVec2(hx1, bar.min.y), ImVec2(hx2, bar.max.y), uicolors::WithAlpha(ink, cRangeOutlineAlpha), 0.0f, 0, 1.5f);

    if (ImGui::IsItemActivated())
        SelectItem(hoveredSet->set);

    ImGui::BeginTooltip();
    ImGui::Text("%s [%u, %u]", hoveredSet->set->getNameOrNull().cstr(), hoveredSet->start, hoveredSet->end);
    ImGui::EndTooltip();
}

static SoundSet* sScopedSoundsViewedSet = nullptr;

static bool SoundInScopedSetFilter(const Item* item)
{
    if (!sScopedSoundsViewedSet)
        return false;

    const Sound* sound = static_cast<const Sound*>(item);
    u32 id = sound->getId();
    return id >= sScopedSoundsViewedSet->getStartId() && id <= sScopedSoundsViewedSet->getEndId() && ItemMatchesDetailFilter(sound);
}

static void ScopedSoundInsertHook(Item* afterItem, Item* newItem)
{
    InsertSoundIntoSet(sScopedSoundsViewedSet, static_cast<Sound*>(newItem), afterItem);
}

static bool ValidateViewedSet(SoundSet** viewedSet)
{
    if (!*viewedSet)
        return false;

    for (auto it = sBfsar.getSoundSetList().robustBegin(); it != sBfsar.getSoundSetList().robustEnd(); ++it)
    {
        if (it->val() == *viewedSet)
            return true;
    }

    *viewedSet = nullptr;
    return false;
}

static u32 CountSetPlayers(const SoundSet* set, const Item** outPlayer)
{
    static std::vector<const Item*> sPlayers;
    sPlayers.clear();

    for (auto it = sBfsar.getSoundList().begin(); it != sBfsar.getSoundList().end(); ++it)
    {
        const Sound* sound = static_cast<const Sound*>(*it);

        if (sound->getId() < set->getStartId() || sound->getId() > set->getEndId())
            continue;

        const Item* player = sound->getPlayerRef().getItem();
        if (!player)
            continue;

        if (std::find(sPlayers.begin(), sPlayers.end(), player) == sPlayers.end())
            sPlayers.push_back(player);
    }

    if (!sPlayers.empty())
        *outPlayer = sPlayers.front();

    return static_cast<u32>(sPlayers.size());
}

static void DrawSoundSetScopedSoundsUI(SoundSet* viewedSet)
{
    static sead::FixedSafeString<320> sNote;

    if (!viewedSet)
    {
        DrawEmptyDetailPane(messages::soundset::cNoneSelected);
        return;
    }

    DrawDetailTabFilterBar();

    u32 columnCount = 0;
    const ItemColumn* columns = GetItemColumns(Item::ItemType::Sound, &columnCount);
    const Item* sharedPlayer = nullptr;

    const u32 playerNum = CountSetPlayers(viewedSet, &sharedPlayer);

    if (playerNum == 1)
        sNote.format(messages::item::cCountInPlayerFormat, viewedSet->getFormattedName().cstr(), sharedPlayer->getNameOrNull().cstr());
    else if (playerNum > 1)
        sNote.format(messages::item::cCountInPlayersFormat, viewedSet->getFormattedName().cstr(), playerNum);
    else
        sNote.format(messages::item::cCountInFormat, viewedSet->getFormattedName().cstr());

    SetNextItemListNote(sNote.cstr());
    SetNextItemListColumns(columns, columnCount, true);
    SetNextItemListRemoveText(messages::menu::cDeleteFromArchive, messages::menu::cDeleteItemsFromArchiveFormat, true);
    SetNextItemListAction(&SoundRowActionFunc);

    sScopedSoundsViewedSet = viewedSet;

    InstanciateItemCallback (*create)(bool) = viewedSet->getSoundSetType() == SoundSet::SoundSetType::Seq ? &CreateSequenceSoundFunc : &CreateWaveSoundFunc;

    DrawAllItemsUI("Sound", messages::common::cSound, sBfsar.getSoundList(), create, nullptr, &SoundContextMenuFunc,
        &SoundInScopedSetFilter,
        false, &ScopedSoundInsertHook, &RemoveSoundWithCascade
    );
}

static bool WaveSoundSetListFilter(const Item* item)
{
    const SoundSet* set = static_cast<const SoundSet*>(item);
    return set->getSoundSetType() == SoundSet::SoundSetType::Wave && ItemMatchesFilter(set);
}

static bool SeqSoundSetListFilter(const Item* item)
{
    const SoundSet* set = static_cast<const SoundSet*>(item);
    return set->getSoundSetType() == SoundSet::SoundSetType::Seq && ItemMatchesFilter(set);
}

static void DrawSoundSetSplitView(
    const char* id,
    const char* listName,
    CreateItemCallback createFunc,
    ItemNamePrefixCallback namePrefix, 
    ContextMenuCallback contextMenu, 
    ItemFilterCallback listFilter,
    bool filterByType, 
    SoundSet::SoundSetType type,
    SoundSet*& viewedSet
)
{
    const f32 cVisHeight = 32.0f;
    f32 barHeight = ImGui::GetStyle().ItemSpacing.y + cVisHeight + ImGui::GetStyle().WindowPadding.y;

    auto adoptSelection = [&]()
    {
        if (sSelectedItem && sSelectedItem->getItemType() == Item::ItemType::SoundSet)
        {
            SoundSet* sel = static_cast<SoundSet*>(sSelectedItem);
            if (!filterByType || sel->getSoundSetType() == type)
                viewedSet = sel;
        }
    };

    ValidateViewedSet(&viewedSet);
    adoptSelection();

    DrawSplitView(
        listName, barHeight,
        [&]()
        {
            DrawTabFilterBar();
            DrawAllItemsUI(id, listName, sBfsar.getSoundSetList(), createFunc, namePrefix, contextMenu, listFilter, false, nullptr, nullptr, false, /*highlightItem*/ viewedSet);
            ValidateViewedSet(&viewedSet);
            adoptSelection();
        },
        [&]()
        {
            DrawSoundSetScopedSoundsUI(viewedSet);
        });

    DrawSoundSetRangeVisualizer(sBfsar.getSoundSetList(), filterByType, type, viewedSet);
}

void DrawWaveSoundSetsUI()
{
    static SoundSet* sViewedSet = nullptr;

    DrawSoundSetSplitView("Wave Sound Set", messages::common::cWaveSoundSet,&CreateWaveSoundSetFunc, nullptr, nullptr, &WaveSoundSetListFilter,
        true, SoundSet::SoundSetType::Wave, sViewedSet);
}

void DrawSequenceSoundSetsUI()
{
    static SoundSet* sViewedSet = nullptr;

    DrawSoundSetSplitView("Sequence Sound Set", messages::item::cTypeSequenceSoundSet,&CreateSequenceSoundSetFunc, nullptr, &SequenceSoundSetContextMenuFunc, &SeqSoundSetListFilter,
        true, SoundSet::SoundSetType::Seq, sViewedSet);
}

void DrawAllSoundSetsUI()
{
    static SoundSet* sViewedSet = nullptr;

    DrawSoundSetSplitView("Sound Set", messages::item::cTypeSoundSet,&CreateSoundSetFunc, &SoundSetNamePrefixFunc, nullptr, GetItemFilterCallback(),
        false, SoundSet::SoundSetType::Wave, sViewedSet);
}

static bool SoundSetHasSequences(const SoundSet* soundSet)
{
    if (!soundSet || soundSet->getIsEmpty() || soundSet->getSoundSetType() != SoundSet::SoundSetType::Seq)
        return false;

    const u32 startId = soundSet->getStartId();
    const u32 endId = soundSet->getEndId();

    for (auto it = sBfsar.getSoundList().begin(); it != sBfsar.getSoundList().end(); ++it)
    {
        Sound* sound = static_cast<Sound*>(*it);
        if (sound->getId() >= startId && sound->getId() <= endId && sound->getSoundType() == Sound::SoundType::Seq)
            return true;
    }

    return false;
}

bool CanExportSoundSetMidi(const std::vector<SoundSet*>& soundSets)
{
    return std::all_of(soundSets.begin(), soundSets.end(), SoundSetHasSequences);
}

static void SequenceSoundSetContextMenuFunc(Item* item)
{
    SoundSet* soundSet = static_cast<SoundSet*>(item);

    if (!soundSet || soundSet->getIsEmpty() || soundSet->getSoundSetType() != SoundSet::SoundSetType::Seq)
        return;

    const std::vector<SoundSet*> targets = CollectForAction<SoundSet>(item);

    ContextMenuSeparator();

    if (ContextMenuItem(ICON_LC_FILE_MUSIC, messages::menu::cExportMidi, CanExportSoundSetMidi(targets), messages::exporting::cSoundSetMidiNeedsSequences))
        RequestExportMidi(targets);
}
