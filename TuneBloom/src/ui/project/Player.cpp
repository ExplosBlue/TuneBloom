#include <ui/Messages.h>
#include <ui/app/UI.h>

const Item* Player::validate(sead::BufferedSafeString& error) const
{
    if (!Item::validateName(error))
        return this;

    return nullptr;
}

static void PlayerCreatePropertiesCallback(bool clear, Item* item, bool* validate)
{
    static u32 sPlayableSoundMax = 1;
    static bool sEnablePlayerHeapSize = true;
    static u32 sPlayerHeapSize = 0;

    if (clear)
    {
        sPlayableSoundMax = 1;
        sEnablePlayerHeapSize = true;
        sPlayerHeapSize = 0;
        return;
    }

    if (!item && !validate)
    {
        DialogRow(messages::player::cSoundLimit);
        ImGui::InputScalar("##SoundLimit", ImGuiDataType_U32, &sPlayableSoundMax);
        DetailTooltip(messages::player::cSoundLimitDetail);

        DialogRowGap();
        PropertyToggleRow(messages::player::cHeapSize, &sEnablePlayerHeapSize);

        ImGui::BeginDisabled(!sEnablePlayerHeapSize);
        ImGui::SetNextItemWidth(-(ImGui::CalcTextSize(messages::player::cHeapSizeUnit).x + ImGui::GetStyle().ItemSpacing.x));
        ImGui::InputScalar("##HeapSize", ImGuiDataType_U32, &sPlayerHeapSize);
        ImGui::EndDisabled();

        DetailTooltip(messages::player::cHeapSizeDetail);

        ImGui::SameLine();
        ImGui::TextDisabled("%s", messages::player::cHeapSizeUnit);
    }
    else if (item && !validate)
    {
        Player* player = static_cast<Player*>(item);
        player->setPlayableSoundMax(sPlayableSoundMax);
        player->setEnablePlayerHeapSize(sEnablePlayerHeapSize);
        player->setPlayerHeapSize(sPlayerHeapSize);
    }
}

InstanciateItemCallback CreatePlayerFunc(bool clear)
{
    return CreateItemFunc(clear, []() -> Item* { return new Player(); }, &PlayerCreatePropertiesCallback);
}

void DrawPlayersUI()
{
    DrawTabFilterBar();

    DrawAllItemsUI("Player", messages::common::cPlayer,sBfsar.getPlayerList(),
                   &CreatePlayerFunc, nullptr, nullptr, GetItemFilterCallback());
}

static constexpr f32 cPlayerPanelEm = 6.5f;

void DrawPlayerPropertiesUI()
{
    Player* player = static_cast<Player*>(sSelectedItem);

    const f32 panelWidth = ImFloor(ImGui::GetFontSize() * cPlayerPanelEm);
    const f32 widths[] = { panelWidth, panelWidth };

    DrawSoundPanels("PlayerPanels", widths, IM_ARRAYSIZE(widths), [&](s32 index, f32)
    {
        if (index == 0)
        {
            const u32 min = 0;
            const u32 max = 255;
            u32 playableSoundMax = player->getPlayableSoundMax();

            RegionHeader(messages::player::cSoundLimit);
            DetailTooltip(messages::player::cSoundLimitDetail);

            ImGui::SetNextItemWidth(-FLT_MIN);
            if (DragField("##PlayableSoundMax", ImGuiDataType_U32, &playableSoundMax, cPropertyDragSpeed, &min, &max))
            {
                const u32 oldPlayableSoundMax = player->getPlayableSoundMax();
                player->setPlayableSoundMax(playableSoundMax);

                if (player->getPlayableSoundMax() != oldPlayableSoundMax)
                    SetUnsavedChanges(true);
            }

            return;
        }

        bool enablePlayerHeapSize = player->isEnablePlayerHeapSize();
        if (DrawPanelHeader(messages::player::cHeapSize, &enablePlayerHeapSize, messages::player::cHeapSizeDetail))
        {
            player->setEnablePlayerHeapSize(enablePlayerHeapSize);
            SetUnsavedChanges(true);
        }

        ImGui::BeginDisabled(!enablePlayerHeapSize);

        u32 playerHeapSize = player->getPlayerHeapSize();

        ImGui::SetNextItemWidth(-(ImGui::CalcTextSize(messages::player::cHeapSizeUnit).x + ImGui::GetStyle().ItemSpacing.x));
        if (ImGui::InputScalar("##PlayerHeapSize", ImGuiDataType_U32, &playerHeapSize))
        {
            player->setPlayerHeapSize(playerHeapSize);
            SetUnsavedChanges(true);
        }

        ImGui::SameLine();
        ImGui::TextDisabled("%s", messages::player::cHeapSizeUnit);

        ImGui::EndDisabled();
    });
}
