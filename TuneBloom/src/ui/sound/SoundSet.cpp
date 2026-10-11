#include <ui/sound/SoundViews.h>

#include <ui/file/File.h>
#include <ui/items/ItemSelector.h>
#include <ui/widgets/Knob.h>
#include <ui/Messages.h>
#include <ui/widgets/Properties.h>
#include <ui/items/Selection.h>
#include <ui/file/UnsavedChanges.h>
#include <ui/widgets/Widgets.h>

const Item* SoundSet::validate(sead::BufferedSafeString& error) const
{
    if (!Item::validateName(error))
    {
        return this;
    }

    switch (getSoundSetType())
    {
        case SoundSetType::Wave:
            if (!Item::validateWaveArchive(getWaveArchiveType(), getWaveArchiveRef(), error))
                return this;

            break;

        case SoundSetType::Seq:
            break;

        default:
            error = messages::validation::cInvalidSoundType;
            return this;
    }

    if (getIsEmpty())
    {
        return nullptr;
    }

    if (getEndId() < getStartId())
    {
        error = messages::soundset::cErrorInvalidRange;
        return this;
    }

    if (getStartId() >= sBfsar.getSoundList().size())
    {
        error = messages::soundset::cErrorStartExceedsCount;
        return this;
    }

    if (getEndId() >= sBfsar.getSoundList().size())
    {
        error = messages::soundset::cErrorEndExceedsCount;
        return this;
    }

    for (const Item::ListNode* itemNode = sBfsar.getItem(getStartId(), sBfsar.getSoundList()); itemNode && itemNode->val()->getId() <= getEndId(); itemNode = sBfsar.getSoundList().next(itemNode))
    {
        SEAD_ASSERT(itemNode->val()->getItemType() == Item::ItemType::Sound);
        const Sound* sound = static_cast<const Sound*>(itemNode->val());

        if (sound->mOwnerSet)
        {
            error.format(messages::soundset::cErrorSoundInOtherSetFormat,sound->getFormattedName().cstr(), sound->mOwnerSet->getFormattedName().cstr());
            return this;
        }

        sound->mOwnerSet = this;
    }

    return nullptr;
}

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

void InsertSoundIntoSet(SoundSet *targetSet, Sound *newSound, Item *afterItem)
{
    Item::List &soundList = sBfsar.getSoundList();
    Item::List &soundSetList = sBfsar.getSoundSetList();

    Item *insertAfterItem = afterItem;

    if (targetSet->getIsEmpty())
    {
        SEAD_ASSERT_MSG(!afterItem, "Cannot insert after a specific sound in an empty set as there isn't one");

        for (Item::ListNode *node = soundSetList.prev(targetSet); node; node = soundSetList.prev(node->val()))
        {
            SoundSet *earlierSet = static_cast<SoundSet *>(node->val());
            if (!earlierSet->getIsEmpty())
            {
                insertAfterItem = sBfsar.getItem(earlierSet->getEndId(), soundList);
                break;
            }
        }

        if (insertAfterItem)
            insertAfterItem->insertBack(newSound);
        else
            soundList.pushFront(newSound);

        sBfsar.updateList(soundList);

        targetSet->setIsEmpty(false);
        targetSet->setStartId(newSound->getId());
        targetSet->setEndId(newSound->getId());
    }
    else
    {
        if (!insertAfterItem)
        {
            u32 endId = targetSet->getEndId();
            Item::ListNode *lastNode = nullptr;

            for (Item::ListNode *node = sBfsar.getItem(targetSet->getStartId(), soundList); node && node->val()->getId() <= endId; node = soundList.next(node))
                lastNode = node;

            SEAD_ASSERT(lastNode);
            insertAfterItem = lastNode->val();
        }

        SEAD_ASSERT_MSG(
            insertAfterItem->getId() >= targetSet->getStartId() && insertAfterItem->getId() <= targetSet->getEndId(), 
            "InsertSoundIntoSet: afterItem must be inside targetSet's range"
        );

        insertAfterItem->insertBack(newSound);
        sBfsar.updateList(soundList);

        targetSet->setEndId(targetSet->getEndId() + 1);
    }

    u32 soundCount = static_cast<u32>(soundList.size());

    const std::vector<SoundSet*> setsAfter = GetFilledSets(targetSet, soundSetList, true);

    if (OffsetSets(setsAfter, +1, soundCount, false))
        OffsetSets(setsAfter, +1, soundCount, true);
    else
        SEAD_ASSERT_MSG(false, "Cascade offset after insert should always be in-bounds");

    SetUnsavedChanges(true);
    sBfsar.validate_(false);
}

static SoundSet *FindContainingSet(u32 soundId, const Item::List &soundSetList)
{
    for (auto it = soundSetList.robustBegin(); it != soundSetList.robustEnd(); ++it)
    {
        SoundSet *set = static_cast<SoundSet *>(it->val());
        if (!set->getIsEmpty() && soundId >= set->getStartId() && soundId <= set->getEndId())
            return set;
    }

    return nullptr;
}

static void ShiftSetsAtOrAfter(u32 thresholdId, s32 delta, Item::List &soundSetList, u32 soundCount)
{
    if (delta == 0)
        return;

    for (auto it = soundSetList.robustBegin(); it != soundSetList.robustEnd(); ++it)
    {
        SoundSet *set = static_cast<SoundSet *>(it->val());

        if (set->getIsEmpty() || set->getStartId() < thresholdId)
            continue;

        s32 newStart = static_cast<s32>(set->getStartId()) + delta;
        s32 newEnd = static_cast<s32>(set->getEndId()) + delta;

        SEAD_ASSERT_MSG(
            newStart >= 0 && newEnd < static_cast<s32>(soundCount) && newStart <= newEnd,
            "Set shift after structural sound edit went out of bounds");

        set->setStartId(static_cast<u32>(newStart));
        set->setEndId(static_cast<u32>(newEnd));

        SetUnsavedChanges(true);
    }
}

static void InsertSoundAt(Item *afterItem, Sound *newSound)
{
    Item::List &soundList = sBfsar.getSoundList();
    Item::List &soundSetList = sBfsar.getSoundSetList();

    SoundSet *containingSet = afterItem ? FindContainingSet(afterItem->getId(), soundSetList) : nullptr;

    if (afterItem)
        afterItem->insertBack(newSound);
    else
        soundList.pushBack(newSound);

    sBfsar.updateList(soundList);

    if (containingSet)
        containingSet->setEndId(containingSet->getEndId() + 1);

    u32 soundCount = static_cast<u32>(soundList.size());
    ShiftSetsAtOrAfter(newSound->getId(), +1, soundSetList, soundCount);

    SetUnsavedChanges(true);
    sBfsar.validate_(false);
}

void InsertSoundAtHook(Item *afterItem, Item *newItem)
{
    InsertSoundAt(afterItem, static_cast<Sound *>(newItem));
}

void RemoveSoundWithCascade(Item *item)
{
    Sound *sound = static_cast<Sound *>(item);
    Item::List &soundList = sBfsar.getSoundList();
    Item::List &soundSetList = sBfsar.getSoundSetList();

    u32 removedId = sound->getId();
    SoundSet *containingSet = FindContainingSet(removedId, soundSetList);

    delete sound;
    sBfsar.updateList(soundList);

    if (containingSet)
    {
        if (containingSet->getStartId() == containingSet->getEndId())
            containingSet->setIsEmpty(true);
        else
            containingSet->setEndId(containingSet->getEndId() - 1);
    }

    u32 soundCount = static_cast<u32>(soundList.size());
    ShiftSetsAtOrAfter(removedId + 1, -1, soundSetList, soundCount);

    SetUnsavedChanges(true);
    sBfsar.validate_(false);
}

static bool sSoundSetStickyEdit = false;

static void DrawSoundSetTypeBox(SoundSet* soundSet)
{
    if (!BeginPropertyBox("SoundSetType"))
        return;

    static const char *sSoundSetTypes[] = {messages::sound::cTypeWave, messages::sound::cTypeSequence};

    const bool isWave = soundSet->getSoundSetType() == SoundSet::SoundSetType::Wave;

    s32 soundSetType = static_cast<s32>(soundSet->getSoundSetType());
    if (DrawPanelHeaderCombo(messages::soundset::cSoundTypeLabel, "##SoundType", &soundSetType, sSoundSetTypes, nullptr, IM_ARRAYSIZE(sSoundSetTypes), nullptr, isWave))
    {
        soundSet->setSoundSetType(static_cast<SoundSet::SoundSetType>(soundSetType));
        SetUnsavedChanges(true);
    }

    if (isWave)
    {
        Item* warc = soundSet->getWaveArchiveRef().getItem();
        WaveArchiveType warcType = soundSet->getWaveArchiveType();

        PropertyRow(messages::soundset::cWaveArchiveLabel);
        if (WaveArchiveSelector("##WaveArchive", &warcType, &warc, sBfsar.getWaveArchiveList()))
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
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.85f, 0.35f, 0.1f, 0.8f));
    }

    const bool changed = DragField(id, ImGuiDataType_U32, value, cPropertyDragSpeed);

    if (stickyEdit)
    {
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
    }

    if (stickyEdit && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip("%s", messages::soundset::cStickyEditTooltip);

    return changed;
}

static bool MoveStickyStartId(SoundSet* soundSet, u32 startId)
{
    Item::List& soundSetList = sBfsar.getSoundSetList();
    const u32 soundCount = static_cast<u32>(sBfsar.getSoundList().size());
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
    Item::List& soundSetList = sBfsar.getSoundSetList();
    const u32 soundCount = static_cast<u32>(sBfsar.getSoundList().size());
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
    PropertyRow(messages::soundset::cEmptyLabel);
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
