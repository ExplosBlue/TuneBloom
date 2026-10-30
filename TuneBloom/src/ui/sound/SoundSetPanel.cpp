#include <ui/sound/SoundViews.h>

#include <bfsar/model/MessageLog.h>
#include <ui/file/File.h>
#include <ui/items/ItemSelector.h>
#include <ui/widgets/Knob.h>
#include <messages/Messages.h>
#include <ui/widgets/Properties.h>
#include <ui/items/Selection.h>
#include <ui/settings/UiColors.h>
#include <ui/file/UnsavedChanges.h>
#include <ui/widgets/Widgets.h>

#include <bfsar/model/SoundSetMembership.h>

// Chain-edit helpers

static bool IsTouchingPrev(const SoundSet* ss, const Item::List& list)
{
    Item::ListNode* node = list.prev(ss);
    if (!node) return false;
    SoundSet* prev = static_cast<SoundSet*>(node->val());
    if (prev->getIsEmpty() || ss->getIsEmpty()) return false;
    return prev->getEndId() + 1 == ss->getStartId();
}

static bool IsTouchingNext(const SoundSet* ss, const Item::List& list)
{
    Item::ListNode* node = list.next(ss);
    if (!node) return false;
    SoundSet* next = static_cast<SoundSet*>(node->val());
    if (next->getIsEmpty() || ss->getIsEmpty()) return false;
    return ss->getEndId() + 1 == next->getStartId();
}

static std::vector<SoundSet*> GetFilledSets(const SoundSet* current, const Item::List& list, bool after)
{
    std::vector<SoundSet*> sets;
    bool found = false;

    for (auto it = list.robustBegin(); it != list.robustEnd(); ++it)
    {
        SoundSet* ss = static_cast<SoundSet*>(it->val());
        if (ss == current)
        {
            if (!after)
                break;

            found = true;
            continue;
        }

        if ((!after || found) && !ss->getIsEmpty())
            sets.push_back(ss);
    }

    return sets;
}

static void ShiftSet(SoundSet* ss, s32 shift)
{
    ss->setStartId(static_cast<u32>(static_cast<s32>(ss->getStartId()) + shift));
    ss->setEndId(static_cast<u32>(static_cast<s32>(ss->getEndId()) + shift));
    SetUnsavedChanges(true);
}

// Uniform offset
static bool OffsetSets(const std::vector<SoundSet*>& sets, s32 delta, u32 soundCount, bool apply)
{
    if (delta == 0)
        return true;

    for (SoundSet* ss : sets)
    {
        const s32 newStart = static_cast<s32>(ss->getStartId()) + delta;
        const s32 newEnd = static_cast<s32>(ss->getEndId()) + delta;
        if (newStart < 0 || newEnd >= static_cast<s32>(soundCount) || newStart > newEnd)
            return false;

        if (apply)
            ShiftSet(ss, delta);
    }

    return true;
}

// Cascade forward
static bool CascadeSetsAfter(const std::vector<SoundSet*>& sets, u32 newEndId, u32 soundCount, bool apply)
{
    u32 prevEnd = newEndId;

    for (SoundSet* ss : sets)
    {
        const u32 neededStart = prevEnd + 1;
        if (ss->getStartId() >= neededStart)
            break;

        const u32 shift = neededStart - ss->getStartId();
        const u32 newStart = ss->getStartId() + shift;
        const u32 newEnd = ss->getEndId() + shift;
        if (newStart > newEnd || newEnd >= soundCount)
            return false;

        if (apply)
            ShiftSet(ss, static_cast<s32>(shift));

        prevEnd = newEnd;
    }

    return true;
}

// Cascade backward
static bool CascadeSetsBefore(const std::vector<SoundSet*>& sets, u32 newStartId, bool apply)
{
    u32 adjustedStart = newStartId;

    for (auto it = sets.rbegin(); it != sets.rend(); ++it)
    {
        SoundSet* ss = *it;
        const u32 neededEnd = adjustedStart - 1;
        if (ss->getEndId() <= neededEnd)
            break;

        const u32 shift = ss->getEndId() - neededEnd;
        if (ss->getStartId() < shift)
            return false;

        const u32 newStart = ss->getStartId() - shift;
        if (newStart > ss->getEndId() - shift)
            return false;

        if (apply)
            ShiftSet(ss, -static_cast<s32>(shift));

        adjustedStart = newStart;
    }

    return true;
}

static const char* GetSoundSetTypeName(const SoundSet& soundSet)
{
    return soundSet.getSoundSetType() == SoundSet::SoundSetType::Seq ? messages::item::cTypeSequenceSoundSet : messages::common::cWaveSoundSet;
}

static void ReportSoundListRefusal(const SoundSetEditResult& result)
{
    const sead::FixedSafeString<256> setName = result.set ? result.set->getFormattedName() : sead::FixedSafeString<256>();

    sead::FixedSafeString<1024> text;
    Item* item = nullptr;

    switch (result.refusal)
    {
        case SoundSetEditRefusal::SharedSounds:
            text.format(messages::soundset::cListSharedSoundsFormat, setName.cstr(), result.otherSet->getFormattedName().cstr());
            item = result.set;
            break;

        case SoundSetEditRefusal::InvalidRange:
            text.format(messages::soundset::cListInvalidRangeFormat, setName.cstr());
            item = result.set;
            break;

        case SoundSetEditRefusal::WrongType:
            text.format(messages::soundset::cMoveWrongTypeFormat, result.sound->getFormattedName().cstr(), GetSoundSetTypeName(*result.set), setName.cstr());
            break;

        case SoundSetEditRefusal::SplitSet:
            text.format(messages::soundset::cListSplitsSetFormat, setName.cstr());
            break;

        default:
            return;
    }

    MessageLog::instance()->addPopup({ text, item });
}

static bool ApplySoundListEdit(Bfsar& archive, const SoundSetEditResult& result)
{
    if (result.refusal != SoundSetEditRefusal::None)
    {
        ReportSoundListRefusal(result);
        return false;
    }

    if (!result.changed)
        return true;

    SetUnsavedChanges(true);
    archive.validate(false);

    return true;
}

bool InsertSoundHook(Item* afterItem, Item* newItem)
{
    Bfsar& archive = ActiveSoundArchive();

    return ApplySoundListEdit(archive, InsertSoundAfter(archive, static_cast<Sound*>(afterItem), static_cast<Sound*>(newItem)));
}

bool InsertSoundIntoSetHook(SoundSet* soundSet, Item* afterItem, Item* newItem)
{
    Bfsar& archive = ActiveSoundArchive();

    return ApplySoundListEdit(archive, InsertSoundIntoSet(archive, soundSet, static_cast<Sound*>(afterItem), static_cast<Sound*>(newItem)));
}

bool RemoveSoundHook(Item* item)
{
    Bfsar& archive = ActiveSoundArchive();

    return ApplySoundListEdit(archive, RemoveSound(archive, static_cast<Sound*>(item)));
}

void MoveSoundsHook(Item* target, const std::vector<Item*>& items)
{
    Bfsar& archive = ActiveSoundArchive();
    std::vector<Sound*> sounds;

    for (Item* item : items)
        sounds.push_back(static_cast<Sound*>(item));

    ApplySoundListEdit(archive, MoveSoundsAfter(archive, static_cast<Sound*>(target), sounds));
}

static bool sSoundSetStickyEdit = false;

static void ResetGroupLoadItems(const SoundSet* soundSet)
{
    for (Item* groupItem : ActiveSoundArchive().getGroupList())
    {
        for (Item* infoItem : static_cast<Group*>(groupItem)->getItemInfoList())
        {
            Group::ItemInfo* info = static_cast<Group::ItemInfo*>(infoItem);

            if (info->getItemRef().getItem() == soundSet)
                info->setLoadItem_(0);
        }
    }
}

static void DrawSoundSetTypeBox(SoundSet* soundSet)
{
    if (!BeginPropertyBox("SoundSetType"))
        return;

    static const char *sSoundSetTypes[] = {messages::common::cWave, messages::common::cSequence};

    const bool isWave = soundSet->getSoundSetType() == SoundSet::SoundSetType::Wave;

    s32 soundSetType = static_cast<s32>(soundSet->getSoundSetType());
    if (DrawPanelHeaderCombo(messages::soundset::cSoundTypeLabel, "##SoundType", &soundSetType, sSoundSetTypes, nullptr, IM_ARRAYSIZE(sSoundSetTypes), nullptr, isWave))
    {
        const SoundSet::SoundSetType newType = static_cast<SoundSet::SoundSetType>(soundSetType);

        if (newType != soundSet->getSoundSetType())
            ResetGroupLoadItems(soundSet);

        soundSet->setSoundSetType(newType);
        SetUnsavedChanges(true);
    }

    if (isWave)
    {
        Item* warc = soundSet->getWaveArchiveRef().getItem();
        WaveArchiveType warcType = soundSet->getWaveArchiveType();

        PropertyRow(messages::common::cWaveArchive);
        if (WaveArchiveSelector("##WaveArchive", &warcType, &warc, ActiveSoundArchive().getWaveArchiveList()))
        {
            soundSet->getWaveArchiveRef().attach(warc);
            soundSet->setWaveArchiveType(warcType);
            SetUnsavedChanges(true);
        }
    }

    EndPropertyBox();
}

static bool DrawRangeIdField(const char* id, u32* value, bool stickyEdit)
{
    if (stickyEdit)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Border, uicolors::EditBorder());
    }

    const bool changed = DragField(id, ImGuiDataType_U32, value, cPropertyDragSpeed);

    if (stickyEdit)
    {
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
    }

    if (stickyEdit)
        ImGui::SetItemTooltip("%s", messages::soundset::cStickyEditDetail);

    return changed;
}

static bool MoveStickyStartId(SoundSet* soundSet, u32 startId)
{
    Bfsar& archive = ActiveSoundArchive();

    Item::List& soundSetList = archive.getSoundSetList();
    const u32 soundCount = static_cast<u32>(archive.getSoundList().size());
    const s32 delta = static_cast<s32>(startId) - static_cast<s32>(soundSet->getStartId());

    if (!IsTouchingPrev(soundSet, soundSetList))
        return startId <= soundSet->getEndId();

    const std::vector<SoundSet*> setsBefore = GetFilledSets(soundSet, soundSetList, false);

    if (delta < 0 && CascadeSetsBefore(setsBefore, startId, false))
    {
        CascadeSetsBefore(setsBefore, startId, true);
        return true;
    }

    if (delta > 0 && OffsetSets(setsBefore, delta, soundCount, false))
    {
        OffsetSets(setsBefore, delta, soundCount, true);
        return true;
    }

    return false;
}

static bool MoveStickyEndId(SoundSet* soundSet, u32 endId)
{
    Bfsar& archive = ActiveSoundArchive();

    Item::List& soundSetList = archive.getSoundSetList();
    const u32 soundCount = static_cast<u32>(archive.getSoundList().size());
    const s32 delta = static_cast<s32>(endId) - static_cast<s32>(soundSet->getEndId());

    if (!IsTouchingNext(soundSet, soundSetList))
        return endId >= soundSet->getStartId();

    const std::vector<SoundSet*> setsAfter = GetFilledSets(soundSet, soundSetList, true);

    if (delta > 0 && CascadeSetsAfter(setsAfter, endId, soundCount, false))
    {
        CascadeSetsAfter(setsAfter, endId, soundCount, true);
        return true;
    }

    if (delta < 0 && OffsetSets(setsAfter, delta, soundCount, false))
    {
        OffsetSets(setsAfter, delta, soundCount, true);
        return true;
    }

    return false;
}

static void DrawSoundSetRangeBox(SoundSet* soundSet)
{
    if (!BeginPropertyBox("SoundSetRange"))
        return;

    RegionHeader(messages::soundset::cIdRangeTitle);

    bool isEmpty = soundSet->getIsEmpty();
    PropertyRow(messages::common::cEmpty);
    DetailTooltip(messages::soundset::cEmptyDetail);

    if (ImGui::Checkbox("##IsEmpty", &isEmpty))
    {
        soundSet->setIsEmpty(isEmpty);
        SetUnsavedChanges(true);
    }

    DetailTooltip(messages::soundset::cEmptyDetail);

    const bool stickyEdit = sSoundSetStickyEdit && !isEmpty;

    if (isEmpty)
    {
        EndPropertyBox();
        return;
    }

    PropertyRow(messages::soundset::cStickyEdit);
    DetailTooltip(messages::soundset::cStickyEditDetail);

    ImGui::Checkbox("##StickyEdit", &sSoundSetStickyEdit);
    DetailTooltip(messages::soundset::cStickyEditDetail);

    // Start Id
    PropertyRow(messages::soundset::cStartIdLabel);

    u32 startId = soundSet->getStartId();

    if (DrawRangeIdField("##StartId", &startId, stickyEdit) && (stickyEdit ? MoveStickyStartId(soundSet, startId) : startId <= soundSet->getEndId()))
    {
        soundSet->setStartId(startId);
        SetUnsavedChanges(true);
    }

    // End Id
    PropertyRow(messages::soundset::cEndIdLabel);

    u32 endId = soundSet->getEndId();

    if (DrawRangeIdField("##EndId", &endId, stickyEdit) && (stickyEdit ? MoveStickyEndId(soundSet, endId) : endId >= soundSet->getStartId()))
    {
        soundSet->setEndId(endId);
        SetUnsavedChanges(true);
    }

    EndPropertyBox();
}

void DrawSoundSetPropertiesUI()
{
    SoundSet* soundSet = static_cast<SoundSet*>(sSelectedItem);

    DrawSoundSetTypeBox(soundSet);
    DrawSoundSetRangeBox(soundSet);
}
