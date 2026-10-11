#define IMGUI_DEFINE_MATH_OPERATORS
#include <ui/app/Workspace.h>

#include <ui/app/About.h>
#include <ui/backup/Backup.h>
#include <ui/export/Export.h>
#include <ui/file/File.h>
#include <ui/file/FileFlow.h>
#include <ui/project/FileLists.h>
#include <ui/file/FileWindow.h>
#include <ui/items/ItemList.h>
#include <ui/project/ItemViews.h>
#include <ui/widgets/Knob.h>
#include <ui/widgets/ListFilter.h>
#include <ui/Messages.h>
#include <ui/app/Notifications.h>
#include <ui/player/OutputMeter.h>
#include <ui/player/PlayerViews.h>
#include <ui/app/PopupMgr.h>
#include <ui/settings/Preferences.h>
#include <ui/project/ProjectViews.h>
#include <ui/project/References.h>
#include <ui/items/Selection.h>
#include <ui/app/Shortcuts.h>
#include <ui/app/ShortcutsDialog.h>
#include <ui/sound/SoundViews.h>
#include <ui/stream/StreamRegions.h>
#include <ui/stream/StreamTrackImport.h>
#include <ui/widgets/TabRowTools.h>
#include <ui/settings/Theme.h>
#include <ui/file/UnsavedChanges.h>
#include <ui/wave/WaveImportPanel.h>
#include <ui/widgets/Widgets.h>

#include <libs/IconsLucide.h>
#include <imgui/imgui_internal.h>

#include <framework/seadProcessMeter.h>

#include <bfsar/files/BankFile.h>

UIType sSelectedUIType = UIType::ProjectInfo;

static bool sShowSystemWindow = false;
static bool sResetLayoutRequest = false;

void SetUITab(UIType type)
{
    sSelectedUIType = type;
}

void FocusInfoWindow()
{
    ImGuiWindow* w = ImGui::FindWindowByName("###InfoWindow");
    if (w && w->DockIsActive && w->Hidden)
    {
        ImGui::SetWindowFocus("###InfoWindow");
    }
}

void FocusPropertiesWindow()
{
    ImGuiWindow* w = ImGui::FindWindowByName("###PropertiesWindow");
    if (w && w->DockIsActive && w->Hidden)
    {
        ImGui::SetWindowFocus("###PropertiesWindow");
    }
}

bool IsSystemWindowShown()
{
    return sShowSystemWindow;
}

void SetSystemWindowShown(bool shown)
{
    sShowSystemWindow = shown;
}

void RequestResetLayout()
{
    sResetLayoutRequest = true;
}

extern void HeapInfo();

static void DockBuilder(ImGuiID dockspaceId, const ImVec2& dockspaceSize)
{
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceId, dockspaceSize);

    ImGuiID mainDockId = dockspaceId;

    ImGuiID dockDown = ImGui::DockBuilderSplitNode(mainDockId, ImGuiDir_Down, 0.1f, nullptr, &mainDockId);

    ImGuiID dock1 = ImGui::DockBuilderSplitNode(mainDockId, ImGuiDir_Left, 0.20f, nullptr, &mainDockId);
    ImGuiID dock2 = ImGui::DockBuilderSplitNode(mainDockId, ImGuiDir_Right, 0.50f, nullptr, &mainDockId);
    ImGuiID dockDownMain = ImGui::DockBuilderSplitNode(mainDockId, ImGuiDir_Down, 0.35f, nullptr, &mainDockId);
    ImGuiID dock1Bottom = ImGui::DockBuilderSplitNode(dock1, ImGuiDir_Down, 0.50f, nullptr, &dock1);

    ImGui::DockBuilderDockWindow("###ProjectWindow", dock1);
    ImGui::DockBuilderDockWindow("###OutputMeterWindow", dock1Bottom);
    ImGui::DockBuilderDockWindow("###InfoWindow", mainDockId);
    ImGui::DockBuilderDockWindow("###SubInfoWindow", dockDownMain);
    ImGui::DockBuilderDockWindow("###StreamRegionsWindow", dockDownMain);
    ImGui::DockBuilderDockWindow("###PropertiesWindow", dock2);
    ImGui::DockBuilderDockWindow("###PlayerWindow", dockDown);

    ImGui::DockBuilderFinish(dockspaceId);
}

static ImGuiID DockSpaceOverViewport(const ImGuiViewport* viewport = nullptr, ImGuiDockNodeFlags dockspace_flags = 0, const ImGuiWindowClass* window_class = nullptr)
{
    if (viewport == NULL)
        viewport = ImGui::GetMainViewport();

    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags host_window_flags = 0;
    host_window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking;
    host_window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
    if (dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode)
        host_window_flags |= ImGuiWindowFlags_NoBackground;

    char label[32];
    ImFormatString(label, IM_ARRAYSIZE(label), "DockSpaceViewport_%08X", viewport->ID);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin(label, NULL, host_window_flags);
    ImGui::PopStyleVar(3);

    ImGuiID dockspace_id = ImGui::GetID("DockSpace");

    if (ImGui::DockBuilderGetNode(dockspace_id) == nullptr || sResetLayoutRequest)
    {
        DockBuilder(dockspace_id, viewport->WorkSize);
        sResetLayoutRequest = false;
    }

    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags, window_class);
    ImGui::End();

    return dockspace_id;
}

static void DrawSystemWindow()
{
    if (sShowSystemWindow)
    {
        if (ImGui::Begin(sead::FormatFixedSafeString<64>("%s###SystemWindow", messages::preferences::cSystemWindowTitle).cstr(), &sShowSystemWindow))
        {
            HeapInfo();

            if (sead::ProcessMeter::instance())
            {
                bool isEnable = sead::ProcessMeter::instance()->isVisible();
                if (ImGui::Checkbox(messages::preferences::cProcessMeterLabel, &isEnable))
                    sead::ProcessMeter::instance()->setVisible(isEnable);
            }
        }
        ImGui::End();
    }
}

static void DrawInfoUI()
{
    bool notProjUI = sSelectedUIType != UIType::ProjectInfo;
    if (notProjUI)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    }

    bool windowOpen = ImGui::Begin(sead::FormatFixedSafeString<64>(ICON_LC_PEN " %s###InfoWindow", messages::project::cInfoWindowTitle).cstr(), nullptr, sSelectedUIType == UIType::ProjectInfo ? ImGuiWindowFlags_NoScrollbar : 0);

    if (notProjUI)
    {
        ImGui::PopStyleVar();
    }

    if (windowOpen)
    {
        if (!sBfsar.isOpen())
        {
            CenteredText(messages::item::cNoFileOpen);

            ImGui::End();
            return;
        }

        switch (sSelectedUIType)
        {
            case UIType::ProjectInfo:
                DrawProjectInfoUI();
                break;

            case UIType::AllSounds:
                DrawAllSoundsUI();
                break;

            case UIType::StreamSounds:
                DrawStreamSoundsUI();
                break;

            case UIType::WaveSounds:
                DrawWaveSoundsUI();
                break;

            case UIType::SequenceSounds:
                DrawSequenceSoundsUI();
                break;

            case UIType::AllSoundSets:
                DrawAllSoundSetsUI();
                break;

            case UIType::WaveSoundSets:
                DrawWaveSoundSetsUI();
                break;

            case UIType::SequenceSoundSets:
                DrawSequenceSoundSetsUI();
                break;

            case UIType::Banks:
                DrawBanksUI();
                break;

            case UIType::WaveArchives:
                DrawWaveArchivesUI();
                break;

            case UIType::Groups:
                DrawGroupsUI();
                break;

            case UIType::Players:
                DrawPlayersUI();
                break;

            case UIType::WaveFiles:
                DrawWaveFilesUI();
                break;

            case UIType::SequenceFiles:
                DrawSequenceFilesUI();
                break;

            case UIType::BankFiles:
                DrawBankFilesUI();
                break;

            case UIType::FileStatistics:
                DrawFileStatisticsUI();
                break;

            default:
                break;
        }
    }

    ImGui::End();
}

Sound* GetSelectedStreamSound()
{
    if (!sSelectedItem || sSelectedItem->getItemType() != Item::ItemType::Sound)
        return nullptr;

    Sound* sound = static_cast<Sound*>(sSelectedItem);

    return sound->getSoundType() == Sound::SoundType::Strm ? sound : nullptr;
}

bool IsStreamPanelShown()
{
    if (!sBfsar.isOpen())
        return false;

    if (sSelectedUIType == UIType::StreamSounds)
        return true;

    return sSelectedUIType == UIType::AllSounds && GetSelectedStreamSound() != nullptr;
}

static void DrawSubInfoUI()
{
    if (!IsStreamPanelShown())
        return;

    const TabRowTitle title = { ICON_LC_LAYERS, messages::stream::cTracksWindowTitle, "###SubInfoWindow", 1 };

    Sound* streamSound = GetSelectedStreamSound();
    const s32 trackNum = streamSound ? static_cast<s32>(streamSound->getStreamSoundInfo().getTrackList().size()) : -1;

    sead::FixedSafeString<64> name;
    FormatTabRowTitle(&name, title, trackNum);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    bool windowOpen = ImGui::Begin(name.cstr(), nullptr, ImGuiWindowFlags_NoFocusOnAppearing);
    ImGui::PopStyleVar();

    DrawTabRowCount(title, trackNum);

    const bool folded = DrawStreamTracksTabRow(streamSound);

    if (streamSound)
        DrawStreamTrackImportPopup(streamSound);

    if (windowOpen && !folded)
    {
        if (sSelectedItem)
        {
            switch (sSelectedItem->getItemType())
            {
                    default:
                        break;

                    case Item::ItemType::Sound:
                    {
                        Sound* sound = static_cast<Sound*>(sSelectedItem);

                        if (sound->getSoundType() == Sound::SoundType::Strm)
                        {
                            ImGui::BeginChild("TrackInnerWindow", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding);
                            DrawStreamTracksUI(sound);
                            ImGui::EndChild();
                        }
                        else
                        {
                        CenteredText(messages::stream::cSelectedNotStream);
                    }
                    break;
                }
            }
        }
        else
        {
            CenteredText(messages::item::cNoItemSelected);
        }
    }

    ImGui::End();
}

void DrawEmptyDetailPane(const char* text)
{
    ImGui::BeginDisabled();
    DrawDetailTabFilterBar();
    ImGui::EndDisabled();

    CenteredText(text);
}

void DrawSplitDivider(f32 listWidth, f32 gap)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImDrawList* drawList = window->DrawList;

    const f32 lineWidth = ImGui::GetStyle().DockingSeparatorSize;
    const f32 lineX = ImFloor(ImGui::GetCursorScreenPos().x + listWidth + (gap - lineWidth) * 0.5f);
    const ImRect& rect = window->Rect();

    drawList->AddRectFilled(ImVec2(lineX + lineWidth, rect.Min.y), rect.Max, GetRecessedPaneColorU32());
    drawList->AddRectFilled(ImVec2(lineX, rect.Min.y), ImVec2(lineX + lineWidth, rect.Max.y), ImGui::GetColorU32(ImGuiCol_Border));
}

static constexpr f32 cPropertiesRightMarginEm = 0.6f;

static void DrawSubItemPropertiesUI()
{
    if (!sSubSelectedItem)
    {
        CenteredText(messages::item::cNoItemSelected);
        return;
    }

    switch (sSubSelectedItem->getItemType())
    {
        case Item::ItemType::BankFileVelocityRegion:
        {
            if (sSelectedItem->getItemType() == Item::ItemType::BankFileInstrument)
                static_cast<BankFile::Instrument*>(sSelectedItem)->drawUI();
            break;
        }

        case Item::ItemType::GroupItemInfo:
        {
            Group::ItemInfo* itemInfo = static_cast<Group::ItemInfo*>(sSubSelectedItem);
            itemInfo->drawUI();
            break;
        }

        case Item::ItemType::BankFileInstrument:
        {
            BankFile::Instrument* instrument = static_cast<BankFile::Instrument*>(sSubSelectedItem);
            instrument->drawUI();
            break;
        }

        default:
            break;
    }
}

static void DrawSelectedItemPropertiesUI()
{
    switch (sSelectedItem->getItemType())
    {
        case Item::ItemType::Sound:
        case Item::ItemType::SoundSet:
        case Item::ItemType::Bank:
        case Item::ItemType::WaveArchive:
        case Item::ItemType::Group:
        case Item::ItemType::Player:
            DrawItemPropertiesUI(true);
            break;

        default:
            break;
    }

    switch (sSelectedItem->getItemType())
    {
        case Item::ItemType::Sound:
            DrawSoundPropertiesUI();
            break;

        case Item::ItemType::SoundSet:
            DrawSoundSetPropertiesUI();
            break;

        case Item::ItemType::Bank:
            DrawBankPropertiesUI();
            break;

        case Item::ItemType::WaveArchive:
            DrawWaveArchivePropertiesUI();
            break;

        case Item::ItemType::Group:
            DrawGroupPropertiesUI();
            break;

        case Item::ItemType::Player:
            DrawPlayerPropertiesUI();
            break;

        case Item::ItemType::WaveFile:
        {
            WaveFile* wave = static_cast<WaveFile*>(sSelectedItem);
            wave->drawUI();
            break;
        }

        case Item::ItemType::SequenceFile:
        {
            SequenceFile* sequenceFile = static_cast<SequenceFile*>(sSelectedItem);
            sequenceFile->drawUI();
            break;
        }

        case Item::ItemType::BankFileInstrument:
        {
            BankFile::Instrument* instrument = static_cast<BankFile::Instrument*>(sSelectedItem);
            instrument->drawUI();
            break;
        }

        default:
            break;
    }
}

static void DrawSelectedItemReferencesUI()
{
    const Item::ItemType itemType = sSelectedItem->getItemType();

    switch (itemType)
    {
        case Item::ItemType::Player:
        case Item::ItemType::Bank:
            DrawReferencesUI(sSelectedItem);
            break;

        case Item::ItemType::WaveFile:
        case Item::ItemType::SequenceFile:
        case Item::ItemType::BankFile:
            DrawReferencesUI(sSelectedItem, ImGui::GetFrameHeightWithSpacing() + ImGui::GetFontSize());
            DrawArchiveInfoUI(itemType);
            break;

        default:
            break;
    }
}

static void DrawPropertiesUI()
{
    static bool sStart = true;
    if (sStart)
    {
        ImGui::SetNextWindowFocus();
        ImGuiWindow* win = ImGui::FindWindowByName("###PropertiesWindow");
        if (win && win->Appearing)
        {
            sStart = false;
        }
    }

    if (ImGui::Begin(sead::FormatFixedSafeString<64>(ICON_LC_TABLE_PROPERTIES " %s###PropertiesWindow", messages::project::cPropertiesWindowTitle).cstr()))
    {
        {
            ImGuiWindow* window = ImGui::GetCurrentWindow();
            const f32 margin = ImFloor(ImGui::GetFontSize() * cPropertiesRightMarginEm);

            window->WorkRect.Max.x -= margin;
            window->ContentRegionRect.Max.x -= margin;
        }

        if (!sSelectedItem || sSelectedItemIsSubWindow || sSelectedItem->getItemType() != Item::ItemType::WaveFile)
            ReleaseWaveFileView();

        if (!sSelectedItem)
        {
            CenteredText(messages::item::cNoItemSelected);
        }
        else if (sSelectedItemIsSubWindow)
        {
            DrawSubItemPropertiesUI();
        }
        else
        {
            DrawSelectedItemPropertiesUI();
            DrawSelectedItemReferencesUI();
        }
    }
    ImGui::End();
}

void DrawUI()
{
    UpdateKnobCursor();

    HandleFileShortcuts();

    ImGuiID dockspaceId = DockSpaceOverViewport(nullptr, ImGuiDockNodeFlags_AutoHideTabBar);

    DrawProjectUI();
    DrawInfoUI();
    DrawSubInfoUI();
    DrawStreamRegionsWindow();
    DrawFileWindows(dockspaceId);
    DrawPropertiesUI();
    DrawPlayerUI();
    DrawOutputMeterWindow();

    DrawPreferencesWindow();
    DrawUnsavedChangesDialog();
    DrawShortcutsDialog();

    backup::Update();
    backup::DrawDialogs();

    notify::Draw();

    DrawWavRegionDropImport();

    DrawFileFlow();
    DrawSystemWindow();
    DrawAboutPopup();

    DrawSoundExportDialog();
    DrawFileExportDialogs();
    DrawExportResultPopup();
    DrawMidiExportDialog();
    DrawSoundExportProgress();
    DrawWaveFileReplacePopup();

    WaveImportTick();
}
