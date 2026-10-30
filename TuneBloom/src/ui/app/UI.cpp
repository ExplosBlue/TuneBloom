#include <ui/app/Workspace.h>

#include <ui/app/About.h>
#include <ui/backup/Backup.h>
#include <ui/browser/FileExportRun.h>
#include <ui/file/DocumentMgr.h>
#include <ui/file/DocumentState.h>
#include <ui/file/DocumentTabs.h>
#include <ui/export/Export.h>
#include <ui/file/File.h>
#include <ui/file/FileDrag.h>
#include <ui/file/FileFlow.h>
#include <ui/project/FileLists.h>
#include <ui/file/FileWindow.h>
#include <ui/file/StartPage.h>
#include <ui/items/ItemList.h>
#include <ui/project/ItemViews.h>
#include <ui/widgets/Knob.h>
#include <ui/widgets/ListFilter.h>
#include <ui/Messages.h>
#include <ui/app/Notifications.h>
#include <ui/player/OutputMeter.h>
#include <ui/player/PlayerFrame.h>
#include <ui/player/PlayerViews.h>
#include <ui/app/PopupMgr.h>
#include <ui/settings/Preferences.h>
#include <ui/project/ProjectViews.h>
#include <ui/project/Sidebar.h>
#include <ui/project/References.h>
#include <aal/BarsListFile.h>
#include <ui/resource/BarsListViews.h>
#include <ui/resource/ResourceViews.h>
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

static bool sShowSystemWindow = false;
static bool sResetLayoutRequest = false;
static bool sIsFileEditorShown = false;

static constexpr s32 cLayoutVersion = 2;
static s32 sSavedLayoutVersion = 0;

UIType GetUITab()
{
    return ActiveDocumentState().tab;
}

void SetUITab(UIType type)
{
    ActiveDocumentState().tab = type;
}

void FocusInfoWindow()
{
    ShowFileWindow(nullptr);

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

bool IsFileEditorShown()
{
    return sIsFileEditorShown;
}

void SetFileEditorShown(bool shown)
{
    sIsFileEditorShown = shown;
}

static void UpdateFileEditorShown()
{
    if (sIsFileEditorShown && (GetOpenDocuments().size() != 1 || !IsPlayerDocument(ActiveDocument())))
        sIsFileEditorShown = false;
}

void RequestResetLayout()
{
    sResetLayoutRequest = true;
}

static void* LayoutSettingsReadOpen(ImGuiContext*, ImGuiSettingsHandler*, const char*)
{
    return &sSavedLayoutVersion;
}

static void LayoutSettingsReadLine(ImGuiContext*, ImGuiSettingsHandler*, void*, const char* line)
{
    int value = 0;

    if (sscanf(line, "Version=%d", &value) == 1)
        sSavedLayoutVersion = value;
}

static void LayoutSettingsApplyAll(ImGuiContext*, ImGuiSettingsHandler*)
{
    if (sSavedLayoutVersion < cLayoutVersion)
        RequestResetLayout();

    sSavedLayoutVersion = cLayoutVersion;
}

static void LayoutSettingsWriteAll(ImGuiContext*, ImGuiSettingsHandler* handler, ImGuiTextBuffer* buf)
{
    buf->appendf("[%s][Dock]\n", handler->TypeName);
    buf->appendf("Version=%d\n\n", cLayoutVersion);
}

void RegisterLayoutSettings()
{
    ImGuiSettingsHandler handler;
    handler.TypeName = "Layout";
    handler.TypeHash = ImHashStr("Layout");
    handler.ReadOpenFn = LayoutSettingsReadOpen;
    handler.ReadLineFn = LayoutSettingsReadLine;
    handler.ApplyAllFn = LayoutSettingsApplyAll;
    handler.WriteAllFn = LayoutSettingsWriteAll;

    ImGui::AddSettingsHandler(&handler);
}

extern void HeapInfo();

static constexpr f32 cOutputMeterMaxRatio = 0.5f;

static constexpr const char* cOutputMeterWindowId = "###OutputMeterWindow";

static ImGuiID SplitOutputMeterNode(ImGuiID* nodeId)
{
    const ImGuiDockNode* node = ImGui::DockBuilderGetNode(*nodeId);
    const f32 ratio = ImMin(CalcOutputMeterWindowHeight() / ImMax(node->Size.y, 1.0f), cOutputMeterMaxRatio);

    return ImGui::DockBuilderSplitNode(*nodeId, ImGuiDir_Down, ratio, nullptr, nodeId);
}

static ImGuiID GetSavedDockId(const char* windowId)
{
    if (const ImGuiWindow* window = ImGui::FindWindowByName(windowId))
        return window->DockId;

    const ImGuiWindowSettings* settings = ImGui::FindWindowSettingsByID(ImHashStr(windowId));

    return settings ? settings->DockId : 0;
}

static void PlaceOutputMeterBelowSidebar()
{
    const ImGuiWindow* sidebar = ImGui::FindWindowByName(cSidebarWindowId);

    if (!sidebar || !sidebar->DockNode || sidebar->DockNode->IsSplitNode() || !ImGui::DockNodeGetRootNode(sidebar->DockNode)->IsDockSpace())
        return;

    const ImGuiID meterDockId = GetSavedDockId(cOutputMeterWindowId);

    if (meterDockId == 0)
        return;

    ImGuiDockNode* meterNode = ImGui::DockBuilderGetNode(meterDockId);

    if (meterNode && !ImGui::DockNodeGetRootNode(meterNode)->IsDockSpace())
        return;

    if (meterNode && meterNode->ParentNode && meterNode->ParentNode == sidebar->DockNode->ParentNode)
        return;

    ImGuiID sidebarNodeId = sidebar->DockNode->ID;

    ImGui::DockBuilderDockWindow(cOutputMeterWindowId, SplitOutputMeterNode(&sidebarNodeId));
}

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
    ImGuiID dock1Bottom = SplitOutputMeterNode(&dock1);

    ImGui::DockBuilderDockWindow(cSidebarWindowId, dock1);
    ImGui::DockBuilderDockWindow(cOutputMeterWindowId, dock1Bottom);
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

    if (!sIsFileEditorShown)
        DrawDocumentTabStrip();

    ImGuiID dockspace_id = ImGui::GetID("DockSpace");

    if (ImGui::DockBuilderGetNode(dockspace_id) == nullptr || sResetLayoutRequest)
    {
        DockBuilder(dockspace_id, viewport->WorkSize);
        sResetLayoutRequest = false;
    }
    else if (TakeOutputMeterPlaceRequest())
    {
        PlaceOutputMeterBelowSidebar();
    }

    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags, window_class);
    ImGui::End();

    return dockspace_id;
}

static void DrawSystemWindow()
{
    if (sShowSystemWindow)
    {
        if (ImGui::Begin(sead::FormatFixedSafeString<64>("%s###SystemWindow", messages::common::cSystem).cstr(), &sShowSystemWindow))
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
    Document* document = ActiveDocument();
    const bool showsFile = GetShownFileWindow() != nullptr;
    const bool notProjUI = document && (showsFile || GetUITab() != UIType::ProjectInfo);

    if (notProjUI)
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

    const ImGuiWindowFlags flags = showsFile ? ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse : notProjUI ? 0 : ImGuiWindowFlags_NoScrollbar;
    const bool windowOpen = ImGui::Begin(sead::FormatFixedSafeString<64>(ICON_LC_PEN " %s###InfoWindow", messages::project::cInfoWindowTitle).cstr(), nullptr, flags);

    if (notProjUI)
        ImGui::PopStyleVar();

    if (windowOpen && !DrawShownFileWindow())
    {
        if (document)
            document->drawInfo(GetUITab());
        else
            DrawStartPage();
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

StreamSource GetActiveStreamSource()
{
    Document* document = ActiveDocument();

    return document ? document->activeStreamSource() : StreamSource();
}

bool IsStreamPanelShown()
{
    if (!IsSoundArchiveOpen())
        return false;

    if (GetUITab() == UIType::StreamSounds)
        return true;

    return GetUITab() == UIType::AllSounds && GetSelectedStreamSound() != nullptr;
}

static void DrawSubInfoBody(const StreamSource& source)
{
    if (source.isValid())
    {
        ImGui::BeginChild("TrackInnerWindow", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding);
        DrawStreamTracksUI(source);
        ImGui::EndChild();
    }
    else if (sSelectedItem && sSelectedItem->getItemType() == Item::ItemType::Sound)
    {
        CenteredText(messages::stream::cSelectedNotStream);
    }
    else if (!sSelectedItem)
    {
        CenteredText(messages::item::cNoItemSelected);
    }
}

static void DrawSubInfoUI()
{
    if (!IsStreamPanelShown())
        return;

    const TabRowTitle title = { ICON_LC_LAYERS, messages::common::cTracks, "###SubInfoWindow", 1 };

    const StreamSource source = GetActiveStreamSource();
    const s32 trackNum = source.isValid() ? static_cast<s32>(source.stream->getTrackList().size()) : -1;

    sead::FixedSafeString<64> name;
    FormatTabRowTitle(&name, title, trackNum);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    bool windowOpen = ImGui::Begin(name.cstr(), nullptr, ImGuiWindowFlags_NoFocusOnAppearing);
    ImGui::PopStyleVar();

    DrawTabRowCount(title, trackNum);

    const bool folded = DrawStreamTracksTabRow(source);

    if (source.isValid())
        DrawStreamTrackImportPopup(source);

    if (windowOpen && !folded)
        DrawSubInfoBody(source);

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

        case Item::ItemType::AudioAsset:
            DrawAudioAssetPropertiesUI(static_cast<AudioAsset*>(sSelectedItem));
            break;

        case Item::ItemType::BarsListEntry:
            DrawBarsListEntryPropertiesUI(static_cast<BarsListEntry*>(sSelectedItem));
            break;

        default:
            break;
    }
}

static void DrawSelectedItemReferencesUI()
{
    if (!IsSoundArchiveOpen())
        return;

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
    if (!ActiveDocument())
    {
        ReleaseWaveFileView();
        ReleaseAudioAssetView();
        return;
    }

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

        Document* document = ActiveDocument();
        const bool showsItemInInfo = document && document->showsItemInInfo();

        if (!showsItemInInfo && (!sSelectedItem || sSelectedItemIsSubWindow || sSelectedItem->getItemType() != Item::ItemType::WaveFile))
            ReleaseWaveFileView();

        if (!sSelectedItem || sSelectedItemIsSubWindow || sSelectedItem->getItemType() != Item::ItemType::AudioAsset)
            ReleaseAudioAssetView();

        if (showsItemInInfo || !sSelectedItem)
        {
            if (!document || !document->drawFileProperties())
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

    UpdateFileWindows();
    UpdateFileEditorShown();

    DockSpaceOverViewport(nullptr, ImGuiDockNodeFlags_AutoHideTabBar);

    if (!sIsFileEditorShown)
        DrawSidebarUI();

    DrawInfoUI();
    DrawSubInfoUI();
    DrawStreamRegionsWindow();
    DrawPropertiesUI();
    DrawPlayerUI();

    if (!sIsFileEditorShown)
        DrawOutputMeterWindow();

    DrawAppDialogs();
    UpdateFileDropElsewhere();
}

void DrawAppDialogs()
{
    DrawPreferencesWindow();
    DrawUnsavedChangesDialog();
    DrawShortcutsDialog();

    backup::Update();
    backup::DrawDialogs();

    notify::Draw(!IsPlayerFrameShown());

    DrawWavRegionDropImport();

    DrawFileFlow();
    DrawSystemWindow();
    DrawAboutPopup();

    DrawSoundExportDialog();
    DrawFileExportDialogs();
    DrawExportResultPopup();
    DrawMidiExportDialog();
    DrawSoundExportProgress();
    DrawFileExportProgress();
    UpdateExportWindowTitle(IsSoundExportRunning() || IsFileExportRunning());
    DrawWaveFileReplacePopup();
    DrawAudioResourceDialogs();

    WaveImportTick();
}
