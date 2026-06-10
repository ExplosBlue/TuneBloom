#include <ui/UI.h>

#include <bfsar/Sound.h>

#include <cctype>



static Item* sDeleteItem = nullptr;
static Item* sDuplicateItem = nullptr;

bool Item::validateName(sead::BufferedSafeString& error) const
{
    if (!sBfsar.validName(getName()))
    {
        error = "Invalid name:\nCan only contain alphanumeric characters and underscores\nCan't start with a number";
        return false;
    }

    if (!sBfsar.validateName(*this))
    {
        error = "Duplicated name";
        return false;
    }

    return true;
}

InstanciateItemCallback CreateItemFunc(bool clear, InstanciateItemCallback instanciateItemCallback, ItemPropertiesCallback itemPropertiesCallback)
{
    SEAD_ASSERT(instanciateItemCallback);

    static bool sEnableName = true;
    static sead::FixedSafeString<256> sName;

    static InstanciateItemCallback sInstanciateItemCallback = nullptr;
    sInstanciateItemCallback = instanciateItemCallback;

    static ItemPropertiesCallback sItemPropertiesCallback = nullptr;
    sItemPropertiesCallback = itemPropertiesCallback;

    if (clear)
    {
        sEnableName = true;
        sName.clear();
    }

    // ImGui::Checkbox("Enable Name", &sEnableName);

    if (!sEnableName)
    {
        ImGui::BeginDisabled();
    }

    ImGui::InputText("Name", sName.getBuffer(), sName.getBufferSize());

    if (!sEnableName)
    {
        ImGui::EndDisabled();
    }

    WarningPopup("###InvalidName", "Invalid name:\nCan only contain alphanumeric characters and underscores\nCan't start with a number");
    WarningPopup("###EmptyName", "Name can't be empty");
    DupeNamePopup();

    if (sItemPropertiesCallback)
    {
        sItemPropertiesCallback(clear, nullptr, nullptr);
    }

    auto doCreate = []() -> Item*
    {
        SEAD_ASSERT(sInstanciateItemCallback);

        if (sName.isEmpty())
        {
            ImGui::OpenPopup("###EmptyName");
            return nullptr;
        }

        if (!sBfsar.validName(sName))
        {
            ImGui::OpenPopup("###InvalidName");
            return nullptr;
        }

        if (!sBfsar.validateName(sName))
        {
            ImGui::OpenPopup("###Dupe");
            return nullptr;
        }

        if (sItemPropertiesCallback)
        {
            bool validate = true;
            sItemPropertiesCallback(false, nullptr, &validate);

            if (!validate)
            {
                return nullptr;
            }
        }

        Item* item = sInstanciateItemCallback();

        item->setEnableName(sEnableName);
        if (sEnableName)
        {
            item->getName() = sName;
            SetUnsavedChanges(true);
        }

        if (sItemPropertiesCallback)
        {
            sItemPropertiesCallback(false, item, nullptr);
        }

        return item;
    };

    return doCreate;
}

void WarningPopup(const char* name, const char* content)
{
    SEAD_ASSERT(name);

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal(sead::FormatFixedSafeString<32>(ICON_LC_ALERT_TRIANGLE " Warning%s", name).cstr(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text(content);
        ImGui::Separator();

        if (ImGui::Button("OK", ImVec2(ImGui::GetWindowContentRegionMax().x - ImGui::GetStyle().WindowPadding.x, 0)))
        {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

static Item* sInsertAfterItem = nullptr;

static bool ItemContextMenu(Item* item, CreateItemCallback createCallback, ContextMenuCallback menuCallback, Item*& selectedItem)
{
    bool add = false;

    if (item ? ImGui::BeginPopupContextItem() : ImGui::BeginPopupContextWindow())
    {
        if (item)
        {
            selectedItem = item;

            if (&selectedItem == &sSubSelectedItem)
            {
                sSelectedItemIsSubWindow = true;
            }
            else
            {
                sSelectedItemIsSubWindow = false;
                sSubSelectedItem = nullptr;
            }
        }

        {
            bool disableAdd = createCallback == nullptr;
            if (disableAdd)
            {
                ImGui::BeginDisabled();
            }

            if (ImGui::MenuItem("Add"))
            {
                sInsertAfterItem = nullptr;
                add = true;
            }

            if (disableAdd)
            {
                ImGui::EndDisabled();
            }
        }

        if (item && createCallback)
        {
            if (ImGui::MenuItem("Insert After"))
            {
                sInsertAfterItem = item;
                add = true;
            }
        }

        if (item && item->getItemType() == Item::ItemType::Sound)
        {
            if (ImGui::MenuItem("Duplicate"))
            {
                sDuplicateItem = item;
            }
        }

        if (menuCallback)
        {
            menuCallback(item, false);
        }

        ImGui::Separator();

        {
            bool disableDelete = item == nullptr;
            if (disableDelete)
            {
                ImGui::BeginDisabled();
            }

            if (ImGui::MenuItem("Delete"))
            {
                sDeleteItem = item;
            }

            if (disableDelete)
            {
                ImGui::EndDisabled();
            }
        }

        if (menuCallback)
        {
            menuCallback(item, true);
        }

        ImGui::End();
    }

    return add;
}

static Item* sScrollItem = nullptr;

void DrawAllItemsUI(const char* listName, Item::List& list, CreateItemCallback createCallback, ItemNamePrefixCallback nameCallback, ContextMenuCallback menuCallback, ItemFilterCallback filterCallback, bool disableAddWindow)
{
    const bool cUseChild = true;

    bool isSubWindow = false;

    if (cUseChild)
    {
        ImGuiChildFlags flags = 0;

        ImGuiWindow* currentWindow = ImGui::GetCurrentWindow();
        if (currentWindow == ImGui::FindWindowByName("###InfoWindow"))
        {
            flags = ImGuiChildFlags_AlwaysUseWindowPadding;
        }
        else if (currentWindow == ImGui::FindWindowByName("###SubInfoWindow"))
        {
            flags = ImGuiChildFlags_AlwaysUseWindowPadding;
            isSubWindow = true;
        }

        ImGui::BeginChild(sead::FormatFixedSafeString<64>("%sInnerWindow", listName).cstr(), ImVec2(0.0f, 0.0f), flags);
    }

    // const bool canEdit = filterCallback == nullptr;
    const bool canEdit = true;

    bool reorder = false;
    Item* item1 = nullptr;
    Item* item2 = nullptr;

    bool add = false;

    Item*& selectedItem = isSubWindow ? sSubSelectedItem : sSelectedItem;

    //if (false)
    {
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && selectedItem && &list == selectedItem->list())
        {
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
            {
                Item::ListNode* prev = list.prev(selectedItem);
                if (prev)
                {
                    selectedItem = prev->val();
                    sScrollItem = selectedItem;

                    if (sSubSelectedItem && sSubSelectedItem->getItemType() != sScrollItem->getItemType())
                    {
                        sSubSelectedItem = nullptr;
                        sSelectedItemIsSubWindow = false;
                    }
                }
            }

            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
            {
                Item::ListNode* next = list.next(selectedItem);
                if (next)
                {
                    selectedItem = next->val();
                    sScrollItem = selectedItem;

                    if (sSubSelectedItem && sSubSelectedItem->getItemType() != sScrollItem->getItemType())
                    {
                        sSubSelectedItem = nullptr;
                        sSelectedItemIsSubWindow = false;
                    }
                }
            }

            if (ImGui::IsKeyPressed(ImGuiKey_PageUp))
            {
                Item* target = selectedItem;
                for (int i = 0; i < 20; i++)
                {
                    Item::ListNode* prev = list.prev(target);
                    if (prev)
                        target = prev->val();
                    else
                        break;
                }
                if (target != selectedItem)
                {
                    selectedItem = target;
                    sScrollItem = selectedItem;
                    if (sSubSelectedItem && sSubSelectedItem->getItemType() != sScrollItem->getItemType())
                    {
                        sSubSelectedItem = nullptr;
                        sSelectedItemIsSubWindow = false;
                    }
                }
            }

            if (ImGui::IsKeyPressed(ImGuiKey_PageDown))
            {
                Item* target = selectedItem;
                for (int i = 0; i < 20; i++)
                {
                    Item::ListNode* next = list.next(target);
                    if (next)
                        target = next->val();
                    else
                        break;
                }
                if (target != selectedItem)
                {
                    selectedItem = target;
                    sScrollItem = selectedItem;
                    if (sSubSelectedItem && sSubSelectedItem->getItemType() != sScrollItem->getItemType())
                    {
                        sSubSelectedItem = nullptr;
                        sSelectedItemIsSubWindow = false;
                    }
                }
            }

            if (ImGui::IsKeyPressed(ImGuiKey_Home))
            {
                Item::ListNode* first = list.front();
                if (first)
                {
                    selectedItem = first->val();
                    sScrollItem = selectedItem;
                    if (sSubSelectedItem && sSubSelectedItem->getItemType() != sScrollItem->getItemType())
                    {
                        sSubSelectedItem = nullptr;
                        sSelectedItemIsSubWindow = false;
                    }
                }
            }

            if (ImGui::IsKeyPressed(ImGuiKey_End))
            {
                Item::ListNode* last = list.back();
                if (last && last->val() != selectedItem)
                {
                    selectedItem = last->val();
                    sScrollItem = selectedItem;
                    if (sSubSelectedItem && sSubSelectedItem->getItemType() != sScrollItem->getItemType())
                    {
                        sSubSelectedItem = nullptr;
                        sSelectedItemIsSubWindow = false;
                    }
                }
            }

            if (ImGui::IsKeyPressed(ImGuiKey_G))
            {
                sScrollItem = selectedItem;
            }

            if (canEdit && ImGui::IsKeyPressed(ImGuiKey_Delete) && sSubSelectedItem == nullptr && sSelectedItemIsSubWindow == false)
            {
                sDeleteItem = selectedItem;
            }
        }
    }

    if (canEdit)
    {
        if (ItemContextMenu(nullptr, createCallback, menuCallback, selectedItem))
        {
            add = true;
        }
    }

    if (list.size() == 0)
    {
        sead::FormatFixedSafeString<64> str("No %ss", listName);
        CenteredText(str.cstr());
    }

    bool hasItem = false;
    for (auto it = list.robustBegin(); it != list.robustEnd(); ++it)
    {
        Item* item = static_cast<Item*>((*it).val());
        if (!item || (filterCallback && !filterCallback(item)))
            continue;

        hasItem = true;

        const char* namePrefix = "";
        if (nameCallback)
        {
            namePrefix = nameCallback(item);

            if (!namePrefix)
                namePrefix = "";
        }

        sead::FixedSafeString<256> name = item->getFormattedName();

        bool popColor = false;
        if (!isSubWindow && sSelectedItemIsSubWindow)
        {
            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(140.0f / 255.0f, 140.0f / 255.0f, 140.0f / 255.0f, 1.0f));
            popColor = true;
        }

        const char* postFix = "";
        if ((item == sSoundPlayer.getPlayingSound() || (!sSoundPlayer.getPlayingSound() && item == sSoundPlayer.getPlayingWaveFile())) && sSoundPlayer.isActive())
        {
            static u32 sAnim = 0;
            static u32 sStep = 2;
            static const u32 cSteps = 3;

            static const char* cAnims[cSteps] = {
                " " ICON_LC_VOLUME,
                " " ICON_LC_VOLUME_1,
                " " ICON_LC_VOLUME_2
            };

            if (!sSoundPlayer.isPause())
            {
                sAnim++;

                if (sAnim >= 20)
                {
                    sAnim = 0;
                    sStep++;

                    if (sStep >= cSteps)
                    {
                        sStep = 0;
                    }
                }
            }

            postFix = cAnims[sStep];
        }

        ImVec2 cursor = ImGui::GetCursorScreenPos();

        bool selected = selectedItem == item;
        sead::FormatFixedSafeString<512> selName("%s%s%s###%p", namePrefix, name.cstr(), postFix, item);
        if (ImGui::Selectable(selName.cstr(), selected))
        {
            selectedItem = item;
            FocusPropertiesWindow();

            if (isSubWindow)
            {
                sSelectedItemIsSubWindow = true;
            }
            else
            {
                sSelectedItemIsSubWindow = false;
                sSubSelectedItem = nullptr;
            }
        }

        selName.trim(selName.rfindIndex("###"));

        if (item->getItemType() == Item::ItemType::WaveFile)
        {
            WaveFile* waveFile = static_cast<WaveFile*>(item);
            if (waveFile->getIsLoopDirty())
            {
                f32 fontSize = ImGui::GetFontSize();
                f32 textSize = ImGui::CalcTextSize(selName.cstr()).x;
                f32 xPadding = ImGui::GetStyle().FramePadding.x + fontSize * 0.5f;
                f32 lineHeight = sead::Mathf::max(sead::Mathf::min(ImGui::GetCurrentWindow()->DC.CurrLineSize.y, fontSize + ImGui::GetStyle().FramePadding.y * 2.0f), fontSize);

                ImVec2 pos = ImVec2(cursor.x + textSize + xPadding, cursor.y + lineHeight * 0.67f);

                ImGui::RenderBullet(ImGui::GetWindowDrawList(), pos, ImGui::GetColorU32(ImGuiCol_Text));
            }
        }

        if (popColor)
        {
            ImGui::PopStyleColor();
        }

        if (sScrollItem == item)
        {
            sScrollItem = nullptr;
            ImGui::SetScrollHereY();
        }

        if (canEdit)
        {
            if (ItemContextMenu(item, createCallback, menuCallback, selectedItem))
            {
                add = true;
            }

            if (selected)
            {
                if (ImGui::BeginDragDropSource())
                {
                    ImGui::SetDragDropPayload("ITEM", &item, sizeof(item));

                    ImGui::Text("Moving '%s'", name.cstr());

                    ImGui::EndDragDropSource();
                }
            }

            if (ImGui::BeginDragDropTarget())
            {
                const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ITEM");
                if (payload)
                {
                    Item* data = *((Item**)payload->Data);
                    SEAD_ASSERT(data);
                    SEAD_ASSERT(data != item);

                    reorder = true;
                    item1 = data;
                    item2 = item;
                }

                ImGui::EndDragDropTarget();
            }
        }
    }

    if (list.size() > 0 && !hasItem)
    {
        sead::FormatFixedSafeString<64> str("No %ss", listName);
        CenteredText(str.cstr());
    }

    if (canEdit)
    {
        if (add)
        {
            ImGui::OpenPopup("###Add");
        }

        if (sDeleteItem)
        {
            if (sDeleteItem->getReferences().size() > 0)
            {
                ImGui::OpenPopup("###References");
            }
            else
            {
                for (size_t i = 0; i <= (size_t)UIType::Max; i++)
                {
                    if (sSelectedItemArr[i] == sDeleteItem)
                        sSelectedItemArr[i] = nullptr;
                    if (sSubSelectedItemArr[i] == sDeleteItem)
                        sSubSelectedItemArr[i] = nullptr;
                }

                delete sDeleteItem;
                selectedItem = nullptr;
                sBfsar.updateList(list);
                SetUnsavedChanges(true);

                sDeleteItem = nullptr;
            }
        }

        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

        if (ImGui::BeginPopupModal(sead::FormatFixedSafeString<64>(ICON_LC_PLUS " Add %s###Add", listName).cstr(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            SEAD_ASSERT(createCallback);
            InstanciateItemCallback instanciateItemCallback = createCallback(ImGui::IsWindowAppearing());
            if (!instanciateItemCallback)
            {
                sInsertAfterItem = nullptr;
                ImGui::CloseCurrentPopup();
            }

            ImGui::Separator();

            ImVec2 buttonSize((ImGui::GetWindowContentRegionMax().x - ImGui::GetStyle().WindowPadding.x * 2.0f) / 2.0f, 0.0f);

            if (disableAddWindow || ImGui::Button("Add", buttonSize))
            {
                SEAD_ASSERT(instanciateItemCallback);
                Item* addedItem = instanciateItemCallback();

                if (addedItem)
                {
                    sScrollItem = addedItem;
                    selectedItem = addedItem;

                    if (!isSubWindow)
                    {
                        sSelectedItemIsSubWindow = false;
                        sSubSelectedItem = nullptr;
                    }
                    else
                    {
                        sSelectedItemIsSubWindow = true;
                    }

                    if (sInsertAfterItem)
                    {
                        sInsertAfterItem->insertBack(addedItem);
                        sInsertAfterItem = nullptr;
                    }
                    else
                    {
                        list.pushBack(addedItem);
                    }

                    sBfsar.updateList(list);
                    SetUnsavedChanges(true);

                    ImGui::CloseCurrentPopup();
                }
            }

            ImGui::SameLine();

            if (ImGui::Button("Cancel", buttonSize))
            {
                sInsertAfterItem = nullptr;
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }

        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

        if (ImGui::BeginPopupModal(ICON_LC_ALERT_TRIANGLE " Warning###References", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("The Item '%s' is referenced by other Items\nDelete anyway ?", sDeleteItem->getFormattedName().cstr());
            ImGui::Separator();

            ImVec2 buttonSize((ImGui::GetWindowContentRegionMax().x - ImGui::GetStyle().WindowPadding.x * 2.0f) / 2.0f, 0.0f);

            if (ImGui::Button("Delete", buttonSize))
            {
                for (size_t i = 0; i <= (size_t)UIType::Max; i++)
                {
                    if (sSelectedItemArr[i] == sDeleteItem)
                        sSelectedItemArr[i] = nullptr;
                    if (sSubSelectedItemArr[i] == sDeleteItem)
                        sSubSelectedItemArr[i] = nullptr;
                }

                delete sDeleteItem;
                selectedItem = nullptr;
                sBfsar.updateList(list);
                SetUnsavedChanges(true);

                sDeleteItem = nullptr;

                ImGui::CloseCurrentPopup();
            }

            ImGui::SameLine();

            if (ImGui::Button("Cancel", buttonSize))
            {
                sDeleteItem = nullptr;

                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }

        if (reorder)
        {
            item1->erase();

            if (item1->getId() < item2->getId())
                item2->insertBack(item1);
            else
                item2->insertFront(item1);

            sBfsar.updateList(list);
            SetUnsavedChanges(true);
        }

        if (sDuplicateItem)
        {
            Sound* src = static_cast<Sound*>(sDuplicateItem);
            Sound* dup = new Sound();

            dup->setEnableName(src->isEnableName());
            {
                u32 copyNum = 1;
                sead::FixedSafeString<256> newName;
                do {
                    newName = src->getName();
                    newName.appendWithFormat("_%u", copyNum);
                    copyNum++;
                } while (!sBfsar.validateName(newName));
                dup->getName() = newName;
            }

            if (src->getPlayerRef().isAttached())
                dup->getPlayerRef().attach(src->getPlayerRef().getItem());

            dup->setVolume(src->getVolume());
            dup->setRemoteFilter(src->getRemoteFilter());
            dup->setSoundType(src->getSoundType());

            dup->setEnablePanParam(src->isEnablePanParam());
            dup->setPanMode(src->getPanMode());
            dup->setPanCurve(src->getPanCurve());

            dup->setEnablePlayerParam(src->isEnablePlayerParam());
            dup->setPlayerPriority(src->getPlayerPriority());
            dup->setActorPlayerId(src->getActorPlayerId());

            for (u32 i = 0; i < 4; i++)
            {
                dup->setEnableUserParam(i, src->isEnableUserParam(i));
                dup->setUserParam(i, src->getUserParam(i));
            }

            dup->setEnableIsFrontBypass(src->isEnableIsFrontBypass());
            dup->setIsFrontBypass(src->getIsFrontBypass());

            dup->setEnableSound3DInfo(src->isEnableSound3DInfo());
            {
                Sound::Sound3DInfo& dst3D = dup->getSound3DInfo();
                const Sound::Sound3DInfo& src3D = src->getSound3DInfo();
                dst3D.setFlags(src3D.getFlags());
                dst3D.setDecayRatio(src3D.getDecayRatio());
                dst3D.setDecayCurve(src3D.getDecayCurve());
                dst3D.setDopplerFactor(src3D.getDopplerFactor());
            }

            {
                Sound::SequenceSoundInfo& dstSeq = dup->getSequenceSoundInfo();
                Sound::SequenceSoundInfo& srcSeq = src->getSequenceSoundInfo();

                if (srcSeq.getSequenceFileRef().isAttached())
                    dstSeq.getSequenceFileRef().attach(srcSeq.getSequenceFileRef().getItem());

                for (u32 i = 0; i < 4; i++)
                {
                    if (srcSeq.getBankRef(i).isAttached())
                        dstSeq.getBankRef(i).attach(srcSeq.getBankRef(i).getItem());
                }

                dstSeq.setEnableStartOffset(srcSeq.isEnableStartOffset());
                dstSeq.getStartLabel() = srcSeq.getStartLabel();
                dstSeq.setEnablePriority(srcSeq.isEnablePriority());
                dstSeq.setIsReleasePriorityFix(srcSeq.getIsReleasePriorityFix());
            }

            {
                Sound::StreamSoundInfo& dstStrm = dup->getStreamSoundInfo();
                Sound::StreamSoundInfo& srcStrm = src->getStreamSoundInfo();

                dstStrm.getPath() = srcStrm.getPath();
                dstStrm.setPitch(srcStrm.getPitch());
                dstStrm.setMainSend(srcStrm.getMainSend());
                for (u32 i = 0; i < 3; i++)
                    dstStrm.setFxSend(i, srcStrm.getFxSend(i));
                dstStrm.setEnableStreamSoundExtension(srcStrm.isEnableStreamSoundExtension());
                dstStrm.setStreamType(srcStrm.getStreamType());
                dstStrm.setIsLoop(srcStrm.getIsLoop());
                dstStrm.setLoopStartFrame(srcStrm.getLoopStartFrame());
                dstStrm.setLoopEndFrame(srcStrm.getLoopEndFrame());

                if (srcStrm.getPrefetchFileRef().isAttached())
                    dstStrm.getPrefetchFileRef().attach(srcStrm.getPrefetchFileRef().getItem());

                u32 trackIdx = 0;
                for (auto it = srcStrm.getTrackList().begin(); it != srcStrm.getTrackList().end(); ++it, ++trackIdx)
                {
                    Sound::StreamSoundInfo::Track* srcTrack = static_cast<Sound::StreamSoundInfo::Track*>(*it);
                    Sound::StreamSoundInfo::Track* dstTrack = new Sound::StreamSoundInfo::Track();

                    if (srcTrack->getWaveFileRef().isAttached())
                        dstTrack->getWaveFileRef().attach(srcTrack->getWaveFileRef().getItem());

                    dstTrack->setId(trackIdx);
                    dstTrack->setEnableName(srcTrack->isEnableName());
                    dstTrack->getName() = srcTrack->getName();

                    dstTrack->setVolume(srcTrack->getVolume());
                    dstTrack->setPan(srcTrack->getPan());
                    dstTrack->setSPan(srcTrack->getSPan());
                    dstTrack->setFlags(srcTrack->getFlags());
                    dstTrack->setMainSend(srcTrack->getMainSend());
                    dstTrack->setLpfFreq(srcTrack->getLpfFreq());
                    dstTrack->setBiquadType(srcTrack->getBiquadType());
                    dstTrack->setBiquadValue(srcTrack->getBiquadValue());
                    for (u32 i = 0; i < 3; i++)
                        dstTrack->setFxSend(i, srcTrack->getFxSend(i));

                    dstStrm.getTrackList().pushBack(dstTrack);
                }
            }

            {
                Sound::WaveSoundInfo& dstWave = dup->getWaveSoundInfo();
                Sound::WaveSoundInfo& srcWave = src->getWaveSoundInfo();

                if (srcWave.getWaveFileRef().isAttached())
                    dstWave.getWaveFileRef().attach(srcWave.getWaveFileRef().getItem());

                dstWave.setAllocateTrackCount(srcWave.getAllocateTrackCount());
                dstWave.setEnablePriority(srcWave.isEnablePriority());
                dstWave.setChannelPriority(srcWave.getChannelPriority());
                dstWave.setIsReleasePriorityFix(srcWave.getIsReleasePriorityFix());
                dstWave.setEnablePan(srcWave.isEnablePan());
                dstWave.setPan(srcWave.getPan());
                dstWave.setSurroundPan(srcWave.getSurroundPan());
                dstWave.setEnablePitch(srcWave.isEnablePitch());
                dstWave.setPitch(srcWave.getPitch());
                dstWave.setEnableSend(srcWave.isEnableSend());
                dstWave.setMainSend(srcWave.getMainSend());
                for (u32 i = 0; i < 3; i++)
                    dstWave.setFxSend(i, srcWave.getFxSend(i));
                dstWave.setEnableEnvelope(srcWave.isEnableEnvelope());
                dstWave.setAdshrCurve(srcWave.getAdshrCurve());
                dstWave.setEnableFilter(srcWave.isEnableFilter());
                dstWave.setLpfFreq(srcWave.getLpfFreq());
                dstWave.setBiquadType(srcWave.getBiquadType());
                dstWave.setBiquadValue(srcWave.getBiquadValue());
            }

            src->insertBack(dup);
            sBfsar.updateList(list);
            SetUnsavedChanges(true);

            selectedItem = dup;
            if (!isSubWindow)
            {
                sSelectedItemIsSubWindow = false;
                sSubSelectedItem = nullptr;
            }
            else
            {
                sSelectedItemIsSubWindow = true;
            }

            sDuplicateItem = nullptr;
        }
    }

    if (cUseChild)
    {
        ImGui::EndChild();
    }
}

void DrawItemPropertiesUI()
{
    Item* item = sSelectedItem;

    //ImGui::Text("Id: %u", item->getId());

    // {
    //     for (const ItemReference* ref : item->getReferences())
    //     {
    //         const Item* owner = ref->getOwner();
    //         if (owner->getItemType() != Item::ItemType::Sound)
    //             continue;

    //         const Sound* sound = static_cast<const Sound*>(owner);
    //         if (sound->getSoundType() != Sound::SoundType::Seq)
    //             continue;

    //         for (const Item* soundSetItem : sBfsar.getSoundSetList())
    //         {
    //             const SoundSet* soundSet = static_cast<const SoundSet*>(soundSetItem);
    //           //if (soundSet->getSoundSetType() != SoundSet::SoundSetType::Seq)
    //           //    continue;

    //             if (!(soundSet->getStartId() <= sound->getId() && sound->getId() <= soundSet->getEndId()))
    //                 continue;

    //             for (const ItemReference* seqRef : soundSet->getReferences())
    //             {
    //                 const Item* seqOwner = seqRef->getOwner();
    //                 if (seqOwner->getItemType() == Item::ItemType::Group)
    //                 {
    //                     const Group* group = static_cast<const Group*>(seqOwner);
    //                     if (group->getId() == 5)
    //                     {
    //                         ImGui::Text("%s", sound->getFormattedName().cstr());
    //                         break;
    //                     }
    //                 }
    //             }
    //         }
    //     }
    // }

    bool enableName = item->isEnableName();
    // if (ImGui::Checkbox("Enable Name", &enableName))
    // {
    //     item->setEnableName(enableName);
    // }

    if (!enableName)
        ImGui::BeginDisabled();

    sead::FixedSafeString<256> name(item->getName());
    if (ImGui::InputText("Name", name.getBuffer(), name.getBufferSize(), ImGuiInputTextFlags_EnterReturnsTrue) || ImGui::IsItemDeactivatedAfterEdit())
    {
        if (name != item->getName())
        {
            if (name.isEmpty())
            {
                ImGui::OpenPopup("###EmptyName");
            }
            else
            {
                if (!sBfsar.validName(name))
                {
                    ImGui::OpenPopup("###InvalidName");
                }
                else if (!sBfsar.validateName(name))
                {
                    ImGui::OpenPopup("###Dupe");
                }
                else
                {
                    item->getName() = name;
                    SetUnsavedChanges(true);
                }
            }
        }
    }

    if (!enableName)
        ImGui::EndDisabled();

    WarningPopup("###InvalidName", "Invalid name:\nCan only contain alphanumeric characters and underscores\nCan't start with a number");
    WarningPopup("###EmptyName", "Name can't be empty");
    DupeNamePopup();
}

void SelectItem(Item* item)
{
    SEAD_ASSERT(item);

    sScrollItem = item;

    UIType tab = UIType::ProjectInfo;
    switch (item->getItemType())
    {
        case Item::ItemType::Sound:
            tab = UIType::AllSounds;
            break;

        case Item::ItemType::SoundSet:
            switch (sSelectedUIType)
            {
                case UIType::WaveSoundSets:
                case UIType::SequenceSoundSets:
                    tab = sSelectedUIType;
                    break;
                default:
                    tab = UIType::AllSoundSets;
                    break;
            }
            break;

        case Item::ItemType::Bank:
            tab = UIType::Banks;
            break;

        case Item::ItemType::WaveArchive:
            tab = UIType::WaveArchives;
            break;

        case Item::ItemType::Group:
            tab = UIType::Groups;
            break;

        case Item::ItemType::Player:
            tab = UIType::Players;
            break;

        case Item::ItemType::WaveFile:
            tab = UIType::WaveFiles;
            break;

        case Item::ItemType::SequenceFile:
            tab = UIType::SequenceFiles;
            break;

        case Item::ItemType::BankFile:
            tab = UIType::BankFiles;
            break;
    }

    size_t tabIdx = (size_t)tab;
    sSelectedItemArr[tabIdx] = item;
    sSubSelectedItemArr[tabIdx] = nullptr;
    sSelectedItemIsSubWindowArr[tabIdx] = false;

    SetUITab(tab);

    FocusInfoWindow();
    FocusPropertiesWindow();
}

bool ItemSelector(const char* name, const Item::List& list, Item** itemPtr, bool allowNone)
{
    SEAD_ASSERT(name);
    SEAD_ASSERT(itemPtr);

    sead::FixedSafeString<256> itemName;
    {
        Item* item = *itemPtr;

        if (item)
        {
            itemName = item->getFormattedName();
        }
        else
        {
            itemName.copy("[-] (none)");
        }
    }

    bool ret = false;

    ImGuiStyle& style = ImGui::GetStyle();
    f32 w = ImGui::CalcItemWidth();
    f32 spacing = style.ItemInnerSpacing.x;
    f32 buttonSize = ImGui::GetFrameHeight();

    ImGui::PushItemWidth(w - spacing * 2.0f - buttonSize * 2.0f);
    {
        sead::FixedSafeString<64> popupIdStr;
        popupIdStr.format("##%sSelectorPopup", name);

        // Draw the combo trigger (frame + preview + arrow, same visual as ImGui::BeginCombo)
        sead::FixedSafeString<64> triggerIdStr;
        triggerIdStr.format("##%sCombo", name);
        ImGuiID triggerId = ImGui::GetID(triggerIdStr.cstr());
        bool popupOpen = ImGui::IsPopupOpen(popupIdStr.cstr(), ImGuiPopupFlags_AnyPopupId);
        ImVec2 frameMin = ImGui::GetCursorScreenPos();
        float frameHeight = ImGui::GetFrameHeight();
        f32 triggerWidth = w - spacing * 2.0f - buttonSize * 2.0f;
        ImVec2 frameSize = ImVec2(triggerWidth, frameHeight);

        ImGui::ItemSize(frameSize);
        ImRect frameRect = ImRect(frameMin.x, frameMin.y, frameMin.x + frameSize.x, frameMin.y + frameSize.y);
        if (ImGui::ItemAdd(frameRect, triggerId))
        {
            bool hovered, held;
            bool pressed = ImGui::ButtonBehavior(frameRect, triggerId, &hovered, &held);

            float arrowSize = frameHeight;
            float valueX2 = frameMin.x + frameSize.x - arrowSize;

            ImU32 frameCol = ImGui::GetColorU32(hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg);
            ImGui::GetWindowDrawList()->AddRectFilled(frameMin, ImVec2(valueX2, frameMin.y + frameSize.y), frameCol, ImGui::GetStyle().FrameRounding, ImDrawFlags_RoundCornersLeft);

            ImU32 arrowBgCol = ImGui::GetColorU32((popupOpen || hovered) ? ImGuiCol_ButtonHovered : ImGuiCol_Button);
            ImU32 arrowTextCol = ImGui::GetColorU32(ImGuiCol_Text);
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(valueX2, frameMin.y), ImVec2(frameMin.x + frameSize.x, frameMin.y + frameSize.y), arrowBgCol, ImGui::GetStyle().FrameRounding, ImDrawFlags_RoundCornersRight);
            ImGui::RenderArrow(ImGui::GetWindowDrawList(), ImVec2(valueX2 + ImGui::GetStyle().FramePadding.y, frameMin.y + ImGui::GetStyle().FramePadding.y), arrowTextCol, ImGuiDir_Down, 1.0f);
            ImGui::RenderFrameBorder(ImVec2(frameMin.x, frameMin.y), ImVec2(frameMin.x + frameSize.x, frameMin.y + frameSize.y), ImGui::GetStyle().FrameRounding);

            ImGui::RenderTextClipped(ImVec2(frameMin.x + ImGui::GetStyle().FramePadding.x, frameMin.y + ImGui::GetStyle().FramePadding.y), ImVec2(valueX2, frameMin.y + frameSize.y - ImGui::GetStyle().FramePadding.y), itemName.cstr(), NULL, NULL);

            if (pressed)
                ImGui::OpenPopup(popupIdStr.cstr(), ImGuiPopupFlags_None);
        }

        // Position and size the popup like a standard combo (always set, even if popup not yet open on first frame)
        ImVec2 triggerMin = ImGui::GetItemRectMin();
        ImVec2 triggerMax = ImGui::GetItemRectMax();
        ImGui::SetNextWindowPos(ImVec2(triggerMin.x, triggerMax.y));

        int itemCount = (int)list.size() + (allowNone ? 1 : 0);
        int maxVisibleItems = itemCount < 15 ? itemCount : 15;
        float itemHeight = ImGui::GetFrameHeight() + style.ItemSpacing.y;
        float searchBoxHeight = ImGui::GetFrameHeight() + style.ItemSpacing.y;
        float fixedHeight = style.WindowPadding.y * 2.0f + searchBoxHeight + style.ItemSpacing.y;
        float listHeight = itemHeight * maxVisibleItems;
        float popupHeight = fixedHeight + listHeight;

        // Clamp to fit within the viewport below the trigger
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        float availableBelow = (viewport->Pos.y + viewport->Size.y) - triggerMax.y;
        if (popupHeight > availableBelow && availableBelow > fixedHeight + itemHeight)
        {
            popupHeight = availableBelow;
            float rawListHeight = availableBelow - fixedHeight;
            int rawItems = (int)(rawListHeight / itemHeight);
            listHeight = itemHeight * rawItems;
        }
        else if (popupHeight > availableBelow)
        {
            popupHeight = fixedHeight + itemHeight;
            listHeight = itemHeight;
        }
        ImGui::SetNextWindowSize(ImVec2(triggerMax.x - triggerMin.x, popupHeight));
        ImGui::SetNextWindowSizeConstraints(ImVec2(triggerMax.x - triggerMin.x, popupHeight), ImVec2(triggerMax.x - triggerMin.x, popupHeight));

        if (ImGui::BeginPopup(popupIdStr.cstr()))
        {
            static sead::FixedSafeString<256> sComboFilter;

            if (ImGui::IsWindowAppearing())
            {
                sComboFilter.clear();
                ImGui::SetKeyboardFocusHere();
            }

            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputTextWithHint("##comboFilter", ICON_LC_SEARCH " Search...", sComboFilter.getBuffer(), sComboFilter.getBufferSize());

            ImGui::Separator();

            bool filterActive = !sComboFilter.isEmpty();

            std::string filterStr;
            if (filterActive)
            {
                filterStr = sComboFilter.cstr();
                for (char& c : filterStr)
                    c = (char)std::tolower((u8)c);
            }

            if (ImGui::BeginChild("##comboList", ImVec2(0.0f, listHeight), ImGuiChildFlags_Border, ImGuiWindowFlags_AlwaysVerticalScrollbar))
            {
                bool hasMatch = false;

                if (allowNone && !filterActive)
                {
                    bool selected = *itemPtr == nullptr;
                    if (ImGui::Selectable("[-] (none)", selected))
                    {
                        *itemPtr = nullptr;
                        ret = true;
                        ImGui::CloseCurrentPopup();
                    }

                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }

                for (auto it = list.robustBegin(); it != list.robustEnd(); ++it)
                {
                    Item* item = (*it).val();

                    itemName = item->getFormattedName();

                    if (filterActive)
                    {
                        std::string nameLower = itemName.cstr();
                        for (char& c : nameLower)
                            c = (char)std::tolower((u8)c);

                        if (nameLower.find(filterStr) == std::string::npos)
                            continue;
                    }

                    hasMatch = true;

                    bool selected = *itemPtr == item;
                    if (ImGui::Selectable(itemName.cstr(), selected))
                    {
                        *itemPtr = item;
                        ret = true;
                        ImGui::CloseCurrentPopup();
                    }

                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }

                if (filterActive && !hasMatch)
                    ImGui::TextDisabled("No matches");
            }
            ImGui::EndChild();

            ImGui::EndPopup();
        }
    }
    ImGui::PopItemWidth();

    if (ImGui::BeginPopupContextItem())
    {
        bool disable = *itemPtr == nullptr;
        if (disable)
        {
            ImGui::BeginDisabled();
        }

        if (ImGui::MenuItem(ICON_LC_EXTERNAL_LINK " Go To"))
        {
            SelectItem(*itemPtr);
        }

        if (disable)
        {
            ImGui::EndDisabled();
        }

        ImGui::End();
    }

    ImGuiButtonFlags buttonFlags = ImGuiButtonFlags_Repeat | ImGuiButtonFlags_DontClosePopups;

    ImGui::SameLine(0.0f, spacing);
    if (ImGui::ArrowButtonEx(sead::FormatFixedSafeString<64>("##%sComboLeft", name).cstr(), ImGuiDir_Left, ImVec2(buttonSize, buttonSize), buttonFlags))
    {
        Item* item = *itemPtr;

        if (item)
        {
            Item::ListNode* node = list.prev(item);

            if (node)
            {
                *itemPtr = node->val();
                ret = true;
            }
            else if (allowNone && item == list.front())
            {
                *itemPtr = nullptr;
                ret = true;
            }
        }
        else if (!allowNone)
        {
            Item::ListNode* node = list.front();

            if (node)
            {
                *itemPtr = node->val();
                ret = true;
            }
        }
    }

    ImGui::SameLine(0.0f, spacing);
    if (ImGui::ArrowButtonEx(sead::FormatFixedSafeString<64>("##%sComboRight", name).cstr(), ImGuiDir_Right, ImVec2(buttonSize, buttonSize), buttonFlags))
    {
        Item* item = *itemPtr;

        if (item)
        {
            Item::ListNode* node = list.next(item);

            if (node)
            {
                *itemPtr = node->val();
                ret = true;
            }
            else if (allowNone && item != list.back())
            {
                *itemPtr = nullptr;
                ret = true;
            }
        }
        else
        {
            Item::ListNode* node = list.front();

            if (node)
            {
                *itemPtr = node->val();
                ret = true;
            }
        }
    }

    ImGui::SameLine(0, style.ItemInnerSpacing.x);

    bool redText = *itemPtr == nullptr && !allowNone;
    if (redText)
    {
        ImGui::PushStyleColor(ImGuiCol_Text,         ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_TextDisabled, ImVec4(0.6f, 0.0f, 0.0f, 1.0f));
    }

    ImGui::Text(name);

    if (redText)
    {
        ImGui::PopStyleColor();
        ImGui::PopStyleColor();
    }

    return ret;
}

bool WaveArchiveSelector(const char* name, WaveArchiveType* warcType, Item** warcPtr, const Item::List& warcList)
{
    SEAD_ASSERT(name);
    SEAD_ASSERT(warcType);
    SEAD_ASSERT(warcPtr);

    if (*warcType == WaveArchiveType::Explicit && *warcPtr == nullptr)
    {
        SEAD_ASSERT(false);
    }

    sead::FixedSafeString<256> itemName;
    {
        Item* item = *warcPtr;

        if (item)
        {
            itemName = item->getFormattedName();
        }
        else if (*warcType == WaveArchiveType::AutomaticShared)
        {
            itemName.copy("Automatic (Shared)");
        }
        else if (*warcType == WaveArchiveType::AutomaticIndividual)
        {
            itemName.copy("Automatic (Individual)");
        }
        else
        {
            itemName = "Invalid";
        }
    }

    bool ret = false;

    ImGuiStyle& style = ImGui::GetStyle();
    f32 w = ImGui::CalcItemWidth();
    f32 spacing = style.ItemInnerSpacing.x;
    f32 buttonSize = ImGui::GetFrameHeight();

    ImGui::PushItemWidth(w - spacing * 2.0f - buttonSize * 2.0f);
    if (ImGui::BeginCombo(sead::FormatFixedSafeString<64>("##%sCombo", name).cstr(), itemName.cstr()))
    {
        {
            bool selected = *warcType == WaveArchiveType::AutomaticShared;
            if (ImGui::Selectable("Automatic (Shared)", selected))
            {
                *warcType = WaveArchiveType::AutomaticShared;
                *warcPtr = nullptr;
                ret = true;
            }

            if (selected)
                ImGui::SetItemDefaultFocus();
        }

        {
            bool selected = *warcType == WaveArchiveType::AutomaticIndividual;
            if (ImGui::Selectable("Automatic (Individual)", selected))
            {
                *warcType = WaveArchiveType::AutomaticIndividual;
                *warcPtr = nullptr;
                ret = true;
            }

            if (selected)
                ImGui::SetItemDefaultFocus();
        }

        for (auto it = warcList.robustBegin(); it != warcList.robustEnd(); ++it)
        {
            Item* item = (*it).val();

            itemName = item->getFormattedName();

            bool selected = *warcPtr == item;
            if (ImGui::Selectable(itemName.cstr(), selected))
            {
                *warcType = WaveArchiveType::Explicit;
                *warcPtr = item;
                ret = true;
            }

            if (selected)
                ImGui::SetItemDefaultFocus();
        }

        ImGui::EndCombo();
    }
    ImGui::PopItemWidth();

    if (ImGui::BeginPopupContextItem())
    {
        bool disable = *warcType != WaveArchiveType::Explicit || *warcPtr == nullptr;
        if (disable)
        {
            ImGui::BeginDisabled();
        }

        if (ImGui::MenuItem(ICON_LC_EXTERNAL_LINK " Go To"))
        {
            SelectItem(*warcPtr);
        }

        if (disable)
        {
            ImGui::EndDisabled();
        }

        ImGui::End();
    }

    ImGuiButtonFlags buttonFlags = ImGuiButtonFlags_Repeat | ImGuiButtonFlags_DontClosePopups;

    ImGui::SameLine(0.0f, spacing);
    if (ImGui::ArrowButtonEx(sead::FormatFixedSafeString<64>("##%sComboLeft", name).cstr(), ImGuiDir_Left, ImVec2(buttonSize, buttonSize), buttonFlags))
    {
        if (*warcType == WaveArchiveType::AutomaticShared)
        {
            // do nothing
        }
        else if (*warcType == WaveArchiveType::AutomaticIndividual)
        {
            *warcType = WaveArchiveType::AutomaticShared;
            *warcPtr = nullptr;
            ret = true;
        }
        else
        {
            Item* item = *warcPtr;

            if (item)
            {
                Item::ListNode* node = warcList.prev(item);

                if (node)
                {
                    *warcType = WaveArchiveType::Explicit;
                    *warcPtr = node->val();
                    ret = true;
                }
                else if (item == warcList.front())
                {
                    *warcType = WaveArchiveType::AutomaticIndividual;
                    *warcPtr = nullptr;
                    ret = true;
                }
            }
            else
            {
                Item::ListNode* node = warcList.front();

                if (node)
                {
                    *warcType = WaveArchiveType::Explicit;
                    *warcPtr = node->val();
                    ret = true;
                }
            }
        }
    }

    ImGui::SameLine(0.0f, spacing);
    if (ImGui::ArrowButtonEx(sead::FormatFixedSafeString<64>("##%sComboRight", name).cstr(), ImGuiDir_Right, ImVec2(buttonSize, buttonSize), buttonFlags))
    {
        if (*warcType == WaveArchiveType::AutomaticShared)
        {
            *warcType = WaveArchiveType::AutomaticIndividual;
            *warcPtr = nullptr;
            ret = true;
        }
        else if (*warcType == WaveArchiveType::AutomaticIndividual)
        {
            Item::ListNode* node = warcList.front();

            if (node)
            {
                *warcType = WaveArchiveType::Explicit;
                *warcPtr = node->val();
                ret = true;
            }
        }
        else
        {
            Item* item = *warcPtr;

            if (item)
            {
                Item::ListNode* node = warcList.next(item);

                if (node)
                {
                    *warcType = WaveArchiveType::Explicit;
                    *warcPtr = node->val();
                    ret = true;
                }
                else if (item != warcList.back())
                {
                    *warcType = WaveArchiveType::AutomaticShared;
                    *warcPtr = nullptr;
                    ret = true;
                }
            }
            else
            {
                Item::ListNode* node = warcList.front();

                if (node)
                {
                    *warcType = WaveArchiveType::Explicit;
                    *warcPtr = node->val();
                    ret = true;
                }
            }
        }
    }

    ImGui::SameLine(0, style.ItemInnerSpacing.x);
    ImGui::Text(name);

    return ret;
}

void ItemIdTable(const char* name, IdTable& table, const Item::List& itemList)
{
    f32 spacing = ImGui::GetStyle().ItemInnerSpacing.x;

    ImGui::Text("%ss", name);
    ImGui::SameLine(0.0f, spacing);

    ImGui::Text("(%i)", table.size());
    ImGui::SameLine(0.0f, spacing);

    f32 buttonSize = ImGui::GetFrameHeight();

    if (ImGui::Button(sead::FormatFixedSafeString<64>("-##%s", name).cstr(), ImVec2(buttonSize, buttonSize)))
    {
        sead::TListNode<IdEntry*>* entry = table.popBack();
        if (entry)
        {
            delete entry->val();
        }
        SetUnsavedChanges(true);
    }

    ImGui::SameLine(0.0f, spacing);

    if (ImGui::Button(sead::FormatFixedSafeString<64>("+##%s", name).cstr(), ImVec2(buttonSize, buttonSize)))
    {
        table.pushBack(new IdEntry(table.getOwner(), nullptr));
        SetUnsavedChanges(true);
    }

    if (ImGui::BeginChild(name, ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border))
    {
        u32 i = 0;
        for (IdEntry* entry : table)
        {
            SEAD_ASSERT(entry);

            Item* item = entry->getIdRef().getItem();
            if (ItemSelector(sead::FormatFixedSafeString<32>("%s %u", name, i).cstr(), itemList, &item, true))
            {
                entry->getIdRef().attach(item);
                SetUnsavedChanges(true);
            }

            if (i + 1 != table.size())
            {
                ImGui::Separator();
            }

            i++;
        }
    }
    ImGui::EndChild();
}
