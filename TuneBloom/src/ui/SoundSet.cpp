#include <ui/UI.h>

const Item* SoundSet::validate(sead::BufferedSafeString& error) const
{
    if (!Item::validateName(error))
    {
        return this;
    }

    switch (getSoundSetType())
    {
        case SoundSetType::Wave:
            switch (getWaveArchiveType())
            {
                case WaveArchiveType::AutomaticShared:
                case WaveArchiveType::AutomaticIndividual:
                    break;

                case WaveArchiveType::Explicit:
                    if (getWaveArchiveRef().isAttached())
                    {
                        break;
                    }

                //! Fallthrough

                default:
                    error = "Invalid Wave Archive";
                    return this;
            }

            break;

        case SoundSetType::Seq:
            break;

        default:
            error = "Invalid Sound Type";
            return this;
    }

    if (getIsEmpty())
    {
        return nullptr;
    }

    if (getEndId() < getStartId())
    {
        error = "Invalid Start and End ids";
        return this;
    }

    if (getStartId() >= sBfsar.getSoundList().size())
    {
        error = "Start Id exceeds sound count";
        return this;
    }

    if (getEndId() >= sBfsar.getSoundList().size())
    {
        error = "End Id exceeds sound count";
        return this;
    }

    for (const Item::ListNode* itemNode = sBfsar.getItem(getStartId(), sBfsar.getSoundList()); itemNode && itemNode->val()->getId() <= getEndId(); itemNode = sBfsar.getSoundList().next(itemNode))
    {
        SEAD_ASSERT(itemNode->val()->getItemType() == Item::ItemType::Sound);
        const Sound* sound = static_cast<const Sound*>(itemNode->val());

        if (sound->mOwnerSet)
        {
            error.format("Sound '%s' is already in Sound Set '%s'", sound->getFormattedName().cstr(), sound->mOwnerSet->getFormattedName().cstr());
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

// Uniform offset for all preceding sets (used when startId increases / shrinking on left)
static bool CanOffsetBefore(const SoundSet* current, s32 delta, const Item::List& list, u32 soundCount)
{
    if (delta == 0) return true;
    for (auto it = list.robustBegin(); it != list.robustEnd(); ++it)
    {
        SoundSet* ss = static_cast<SoundSet*>(it->val());
        if (ss == current) break;
        if (ss->getIsEmpty()) continue;
        s32 newStart = static_cast<s32>(ss->getStartId()) + delta;
        s32 newEnd = static_cast<s32>(ss->getEndId()) + delta;
        if (newStart < 0 || newEnd >= static_cast<s32>(soundCount) || newStart > newEnd)
            return false;
    }
    return true;
}

// Uniform offset for all following sets (used when endId decreases / shrinking on right)
static bool CanOffsetAfter(const SoundSet* current, s32 delta, const Item::List& list, u32 soundCount)
{
    if (delta == 0) return true;
    bool found = false;
    for (auto it = list.robustBegin(); it != list.robustEnd(); ++it)
    {
        SoundSet* ss = static_cast<SoundSet*>(it->val());
        if (ss == current) { found = true; continue; }
        if (!found) continue;
        if (ss->getIsEmpty()) continue;
        s32 newStart = static_cast<s32>(ss->getStartId()) + delta;
        s32 newEnd = static_cast<s32>(ss->getEndId()) + delta;
        if (newStart < 0 || newEnd >= static_cast<s32>(soundCount) || newStart > newEnd)
            return false;
    }
    return true;
}

static void ApplyOffsetBefore(const SoundSet* current, s32 delta, Item::List& list)
{
    if (delta == 0) return;
    for (auto it = list.robustBegin(); it != list.robustEnd(); ++it)
    {
        SoundSet* ss = static_cast<SoundSet*>(it->val());
        if (ss == current) break;
        if (ss->getIsEmpty()) continue;
        ss->setStartId(static_cast<u32>(static_cast<s32>(ss->getStartId()) + delta));
        ss->setEndId(static_cast<u32>(static_cast<s32>(ss->getEndId()) + delta));
    }
}

static void ApplyOffsetAfter(const SoundSet* current, s32 delta, Item::List& list)
{
    if (delta == 0) return;
    bool found = false;
    for (auto it = list.robustBegin(); it != list.robustEnd(); ++it)
    {
        SoundSet* ss = static_cast<SoundSet*>(it->val());
        if (ss == current) { found = true; continue; }
        if (!found) continue;
        if (ss->getIsEmpty()) continue;
        ss->setStartId(static_cast<u32>(static_cast<s32>(ss->getStartId()) + delta));
        ss->setEndId(static_cast<u32>(static_cast<s32>(ss->getEndId()) + delta));
    }
}

// Cascade: absorb gaps when expanding forward (endId increases)
static bool CanCascadeAfter(const SoundSet* current, u32 newEndId, const Item::List& list, u32 soundCount)
{
    u32 prevEnd = newEndId;
    bool found = false;
    for (auto it = list.robustBegin(); it != list.robustEnd(); ++it)
    {
        SoundSet* ss = static_cast<SoundSet*>(it->val());
        if (ss == current) { found = true; continue; }
        if (!found) continue;
        if (ss->getIsEmpty()) continue;

        u32 neededStart = prevEnd + 1;
        if (ss->getStartId() >= neededStart) break;

        u32 shift = neededStart - ss->getStartId();
        u32 newStart = ss->getStartId() + shift;
        u32 newEnd = ss->getEndId() + shift;

        if (newStart > newEnd) return false;
        if (newEnd >= soundCount) return false;

        prevEnd = newEnd;
    }
    return true;
}

// Cascade: absorb gaps when expanding backward (startId decreases)
static bool CanCascadeBefore(const SoundSet* current, u32 newStartId, const Item::List& list)
{
    u32 adjustedStart = newStartId;

    std::vector<const SoundSet*> preceding;
    for (auto it = list.robustBegin(); it != list.robustEnd(); ++it)
    {
        const SoundSet* ss = static_cast<const SoundSet*>(it->val());
        if (ss == current) break;
        if (!ss->getIsEmpty()) preceding.push_back(ss);
    }

    for (auto it = preceding.rbegin(); it != preceding.rend(); ++it)
    {
        const SoundSet* ss = *it;
        u32 neededEnd = adjustedStart - 1;
        if (ss->getEndId() <= neededEnd) break;

        u32 shift = ss->getEndId() - neededEnd;
        if (ss->getStartId() < shift) return false;
        u32 newStart = ss->getStartId() - shift;
        u32 newEnd = ss->getEndId() - shift;
        if (newStart > newEnd) return false;

        adjustedStart = newStart;
    }
    return true;
}

static void CascadeOffsetAfter(const SoundSet* current, u32 newEndId, Item::List& list)
{
    u32 prevEnd = newEndId;
    bool found = false;
    for (auto it = list.robustBegin(); it != list.robustEnd(); ++it)
    {
        SoundSet* ss = static_cast<SoundSet*>(it->val());
        if (ss == current) { found = true; continue; }
        if (!found) continue;
        if (ss->getIsEmpty()) continue;

        u32 neededStart = prevEnd + 1;
        if (ss->getStartId() >= neededStart) break;

        u32 shift = neededStart - ss->getStartId();
        ss->setStartId(ss->getStartId() + shift);
        ss->setEndId(ss->getEndId() + shift);
        prevEnd = ss->getEndId();
    }
}

static void CascadeOffsetBefore(const SoundSet* current, u32 newStartId, Item::List& list)
{
    u32 adjustedStart = newStartId;

    std::vector<SoundSet*> preceding;
    for (auto it = list.robustBegin(); it != list.robustEnd(); ++it)
    {
        SoundSet* ss = static_cast<SoundSet*>(it->val());
        if (ss == current) break;
        if (!ss->getIsEmpty()) preceding.push_back(ss);
    }

    for (auto it = preceding.rbegin(); it != preceding.rend(); ++it)
    {
        SoundSet* ss = *it;
        u32 neededEnd = adjustedStart - 1;
        if (ss->getEndId() <= neededEnd) break;

        u32 shift = ss->getEndId() - neededEnd;
        ss->setStartId(ss->getStartId() - shift);
        ss->setEndId(ss->getEndId() - shift);
        adjustedStart = ss->getStartId();
    }
}

void DrawSoundSetPropertiesUI()
{
    SoundSet* soundSet = static_cast<SoundSet*>(sSelectedItem);

    const ImU32 cStepU32 = 1;

    {
        static const char* sSoundSetTypes[] = { "Wave", "Sequence" };

        SoundSet::SoundSetType soundSetType = soundSet->getSoundSetType();
        if (ImGui::Combo("Sound Type", (s32*)&soundSetType, sSoundSetTypes, IM_ARRAYSIZE(sSoundSetTypes)))
        {
            soundSet->setSoundSetType(soundSetType);
        }
    }

    {
        bool enableWaveArchives = soundSet->getSoundSetType() == SoundSet::SoundSetType::Wave;
        if (!enableWaveArchives)
        {
            ImGui::BeginDisabled();
        }

        Item* warc = soundSet->getWaveArchiveRef().getItem();
        WaveArchiveType warcType = soundSet->getWaveArchiveType();
        if (WaveArchiveSelector("Wave Archive", &warcType, &warc, sBfsar.getWaveArchiveList()))
        {
            soundSet->getWaveArchiveRef().attach(warc);
            soundSet->setWaveArchiveType(warcType);
        }

        if (!enableWaveArchives)
        {
            ImGui::EndDisabled();
        }
    }

    {
        bool isEmpty = soundSet->getIsEmpty();
        if (ImGui::Checkbox("Is Empty", &isEmpty))
        {
            soundSet->setIsEmpty(isEmpty);
        }

        bool stickyEdit = sSoundSetStickyEdit && !isEmpty;
        Item::List& soundSetList = sBfsar.getSoundSetList();
        u32 soundCount = static_cast<u32>(sBfsar.getSoundList().size());

        if (isEmpty)
        {
            ImGui::BeginDisabled();
        }

        // Start Id
        {
            f32 spacing = ImGui::GetStyle().ItemInnerSpacing.x;
            f32 buttonSize = ImGui::GetFrameHeight();

            ImGui::Text("Start Id");
            ImGui::SameLine();

            if (stickyEdit)
            {
                ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.85f, 0.35f, 0.1f, 0.8f));
            }
            u32 startId = soundSet->getStartId();
            bool startChanged = ImGui::InputScalar("##StartId", ImGuiDataType_U32, &startId, nullptr);
            if (stickyEdit)
            {
                ImGui::PopStyleColor();
                ImGui::PopStyleVar();
            }
            if (stickyEdit && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                ImGui::SetTooltip("Sticky Edit enabled: shifting this value offsets adjacent touched sets.");
            if (startChanged)
            {
                if (stickyEdit)
                {
                    s32 delta = static_cast<s32>(startId) - static_cast<s32>(soundSet->getStartId());
                    if (IsTouchingPrev(soundSet, soundSetList))
                    {
                        if (delta < 0)
                        {
                            if (CanCascadeBefore(soundSet, startId, soundSetList))
                            {
                                CascadeOffsetBefore(soundSet, startId, soundSetList);
                                soundSet->setStartId(startId);
                            }
                        }
                        else if (delta > 0)
                        {
                            if (CanOffsetBefore(soundSet, delta, soundSetList, soundCount))
                            {
                                ApplyOffsetBefore(soundSet, delta, soundSetList);
                                soundSet->setStartId(startId);
                            }
                        }
                    }
                    else
                    {
                        if (startId <= soundSet->getEndId())
                            soundSet->setStartId(startId);
                    }
                }
                else
                {
                    if (startId <= soundSet->getEndId())
                        soundSet->setStartId(startId);
                }
            }

            ImGui::SameLine(0, spacing);
            // - button: expand backward (decrement startId)
            bool canDec = soundSet->getStartId() > 0;
            if (stickyEdit)
                canDec = canDec && (!IsTouchingPrev(soundSet, soundSetList) || CanCascadeBefore(soundSet, soundSet->getStartId() - 1, soundSetList));
            if (!canDec) ImGui::BeginDisabled();
            if (ImGui::Button("-##StartIdDec", ImVec2(buttonSize, buttonSize)))
            {
                u32 newId = soundSet->getStartId() - 1;
                if (stickyEdit && IsTouchingPrev(soundSet, soundSetList))
                    CascadeOffsetBefore(soundSet, newId, soundSetList);
                soundSet->setStartId(newId);
            }
            if (!canDec) ImGui::EndDisabled();

            ImGui::SameLine(0, spacing);
            // + button: shrink on left (increment startId)
            bool canInc = soundSet->getStartId() < soundSet->getEndId();
            if (stickyEdit)
                canInc = canInc && (!IsTouchingPrev(soundSet, soundSetList) || CanOffsetBefore(soundSet, +1, soundSetList, soundCount));
            if (!canInc) ImGui::BeginDisabled();
            if (ImGui::Button("+##StartIdInc", ImVec2(buttonSize, buttonSize)))
            {
                u32 newId = soundSet->getStartId() + 1;
                s32 delta = +1;
                if (stickyEdit && IsTouchingPrev(soundSet, soundSetList))
                    ApplyOffsetBefore(soundSet, delta, soundSetList);
                soundSet->setStartId(newId);
            }
            if (!canInc) ImGui::EndDisabled();
        }

        // End Id
        {
            f32 spacing = ImGui::GetStyle().ItemInnerSpacing.x;
            f32 buttonSize = ImGui::GetFrameHeight();

            ImGui::Text("End Id");
            ImGui::SameLine();

            if (stickyEdit)
            {
                ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.85f, 0.35f, 0.1f, 0.8f));
            }
            u32 endId = soundSet->getEndId();
            bool endChanged = ImGui::InputScalar("##EndId", ImGuiDataType_U32, &endId, nullptr);
            if (stickyEdit)
            {
                ImGui::PopStyleColor();
                ImGui::PopStyleVar();
            }
            if (stickyEdit && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                ImGui::SetTooltip("Sticky Edit enabled: shifting this value offsets adjacent touched sets.");
            if (endChanged)
            {
                if (stickyEdit)
                {
                    s32 delta = static_cast<s32>(endId) - static_cast<s32>(soundSet->getEndId());
                    if (IsTouchingNext(soundSet, soundSetList))
                    {
                        if (delta > 0)
                        {
                            if (CanCascadeAfter(soundSet, endId, soundSetList, soundCount))
                            {
                                CascadeOffsetAfter(soundSet, endId, soundSetList);
                                soundSet->setEndId(endId);
                            }
                        }
                        else if (delta < 0)
                        {
                            if (CanOffsetAfter(soundSet, delta, soundSetList, soundCount))
                            {
                                ApplyOffsetAfter(soundSet, delta, soundSetList);
                                soundSet->setEndId(endId);
                            }
                        }
                    }
                    else
                    {
                        if (endId >= soundSet->getStartId())
                            soundSet->setEndId(endId);
                    }
                }
                else
                {
                    if (endId >= soundSet->getStartId())
                        soundSet->setEndId(endId);
                }
            }

            ImGui::SameLine(0, spacing);
            // - button: shrink on right (decrement endId)
            bool canDec = soundSet->getEndId() > soundSet->getStartId();
            if (stickyEdit)
                canDec = canDec && (!IsTouchingNext(soundSet, soundSetList) || CanOffsetAfter(soundSet, -1, soundSetList, soundCount));
            if (!canDec) ImGui::BeginDisabled();
            if (ImGui::Button("-##EndIdDec", ImVec2(buttonSize, buttonSize)))
            {
                u32 newId = soundSet->getEndId() - 1;
                s32 delta = -1;
                if (stickyEdit && IsTouchingNext(soundSet, soundSetList))
                    ApplyOffsetAfter(soundSet, delta, soundSetList);
                soundSet->setEndId(newId);
            }
            if (!canDec) ImGui::EndDisabled();

            ImGui::SameLine(0, spacing);
            // + button: expand forward (increment endId) — uses cascade
            bool canInc = soundSet->getEndId() + 1 < soundCount;
            if (stickyEdit)
                canInc = canInc && (!IsTouchingNext(soundSet, soundSetList) || CanCascadeAfter(soundSet, soundSet->getEndId() + 1, soundSetList, soundCount));
            if (!canInc) ImGui::BeginDisabled();
            if (ImGui::Button("+##EndIdInc", ImVec2(buttonSize, buttonSize)))
            {
                u32 newId = soundSet->getEndId() + 1;
                if (stickyEdit && IsTouchingNext(soundSet, soundSetList))
                    CascadeOffsetAfter(soundSet, newId, soundSetList);
                soundSet->setEndId(newId);
            }
            if (!canInc) ImGui::EndDisabled();
        }

        if (isEmpty)
        {
            ImGui::EndDisabled();
        }
    }

    if (ImGui::BeginChild("ChildSounds", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border))
    {
        for (Item::ListNode* itemNode = sBfsar.getItem(soundSet->getStartId(), sBfsar.getSoundList()); itemNode && itemNode->val()->getId() <= soundSet->getEndId(); itemNode = sBfsar.getSoundList().next(itemNode))
        {
            SEAD_ASSERT(itemNode->val()->getItemType() == Item::ItemType::Sound);
            Sound* sound = static_cast<Sound*>(itemNode->val());

            if (ImGui::Button(sead::FormatFixedSafeString<32>(ICON_LC_PLAY "###%u", sound->getId()).cstr()))
            {
                sSoundPlayer.playSound(sound);
                sSelectedItem = soundSet;
            }

            ImGui::SameLine();

            bool isError =
                (sound->getSoundType() == Sound::SoundType::Seq  && soundSet->getSoundSetType() != SoundSet::SoundSetType::Seq) ||
                (sound->getSoundType() == Sound::SoundType::Wave && soundSet->getSoundSetType() != SoundSet::SoundSetType::Wave) ||
                (sound->getSoundType() == Sound::SoundType::Strm);

            sead::FixedSafeString<516> name(sound->getFormattedName().cstr());
            if (isError)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
                name.format("%s (?)", sound->getFormattedName().cstr());
            }

            if (ImGui::Selectable(name.cstr()))
            {
            }

            if (isError)
            {
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                {
                    ImGui::SetTooltip(
                        "The type of this Sound does not match the Sound Set type.\n"
                        "The tool allows this case, except for Wave Sounds.\n"
                        "However, please note that if your game tries to explicitly\n"
                        "load this Sound Set, then all following Sounds will NOT load,\n"
                        "including this one !"
                    );
                }
            }

            if (ImGui::BeginPopupContextItem())
            {
                if (ImGui::MenuItem(ICON_LC_EXTERNAL_LINK " Go To"))
                {
                    SelectItem(sound);
                }

                ImGui::End();
            }
        }
    }
    ImGui::EndChild();
}
