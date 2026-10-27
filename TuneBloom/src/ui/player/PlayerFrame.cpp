#include <ui/player/PlayerFrame.h>

#include <ui/app/MenuBar.h>
#include <ui/app/Workspace.h>
#include <ui/backup/Backup.h>
#include <ui/file/Document.h>
#include <ui/file/DocumentMgr.h>
#include <ui/file/DocumentState.h>
#include <ui/file/File.h>
#include <ui/file/FileFlow.h>
#include <ui/items/ItemColumns.h>
#include <ui/Messages.h>
#include <ui/player/PlayerViews.h>
#include <ui/project/FileLists.h>
#include <ui/resource/ResourceViews.h>
#include <ui/settings/UiColors.h>
#include <ui/stream/StreamTracks.h>
#include <ui/widgets/ContextMenu.h>
#include <ui/widgets/Dialogs.h>

#include <aal/AudioResource.h>
#include <app/Utilll.h>
#include <core/PlayerFile.h>
#include <core/TestHooks.h>

#include <framework/glfw/seadGameFrameworkBaseGlfw.h>
#include <imgui/imgui_internal.h>
#include <libs/IconsLucide.h>

#include <algorithm>

static constexpr f32 cPlayerWidthEm          = 46.0f;
static constexpr f32 cPlayerPadEm            = 0.8f;
static constexpr f32 cPlayerRowGapEm         = 0.6f;
static constexpr s32 cTrackRowsVisible       = 6;
static constexpr s32 cModalFirstMeasureFrame = 2;
static constexpr s32 cModalLastMeasureFrame  = 10;
static constexpr s32 cRevealStableFrames     = 2;
static constexpr s32 cRevealFrameLimit       = 90;

enum class LeaveTarget
{
    None,
    FileEditor,
    FullEditor
};

static bool sIsShown = false;
static bool sIsEnterPending = false;
static bool sIsSizePending = false;
static bool sIsRevealed = true;
static LeaveTarget sLeaveTarget = LeaveTarget::None;
static Document* sAutoplayDocument = nullptr;
static ImGuiID sSizedModalId = 0;
static s32 sModalFrameNum = 0;
static s32 sStableFrameNum = 0;
static s32 sHiddenFrameNum = 0;
static s32 sNormalWidth = 0;
static s32 sNormalHeight = 0;

static GLFWwindow* GetAppWindow()
{
    sead::GameFrameworkBaseGlfw* framework = sead::DynamicCast<sead::GameFrameworkBaseGlfw>(util::getFramework());

    return framework ? framework->getWindowHandle() : nullptr;
}

struct WorkArea
{
    s32 x = 0;
    s32 y = 0;
    s32 width = 0;
    s32 height = 0;
};

static bool FindWorkArea(s32 centerX, s32 centerY, WorkArea* outArea)
{
    s32 monitorNum = 0;
    GLFWmonitor** monitors = glfwGetMonitors(&monitorNum);

    for (s32 i = 0; i < monitorNum; i++)
    {
        WorkArea area;
        glfwGetMonitorWorkarea(monitors[i], &area.x, &area.y, &area.width, &area.height);

        if (centerX < area.x || centerX >= area.x + area.width || centerY < area.y || centerY >= area.y + area.height)
            continue;

        *outArea = area;
        return true;
    }

    return false;
}

static void ClampToWorkArea(s32* x, s32* y, s32 width, s32 height)
{
    WorkArea area;

    if (!FindWorkArea(*x + width / 2, *y + height / 2, &area))
        return;

    *x = std::max(area.x, std::min(*x, area.x + area.width - width));
    *y = std::max(area.y, std::min(*y, area.y + area.height - height));
}

static void ResizeAroundCenter(GLFWwindow* window, s32 width, s32 height)
{
    s32 x = 0;
    s32 y = 0;
    s32 oldWidth = 0;
    s32 oldHeight = 0;
    glfwGetWindowPos(window, &x, &y);
    glfwGetWindowSize(window, &oldWidth, &oldHeight);

    if (width == oldWidth && height == oldHeight)
        return;

    x += (oldWidth - width) / 2;
    y += (oldHeight - height) / 2;
    ClampToWorkArea(&x, &y, width, height);

    glfwSetWindowSize(window, width, height);

    if (!testhooks::IsEnabled())
        glfwSetWindowPos(window, x, y);
}

static void ClampToWorkAreaSize(GLFWwindow* window, s32* width, s32* height)
{
    s32 x = 0;
    s32 y = 0;
    s32 oldWidth = 0;
    s32 oldHeight = 0;
    glfwGetWindowPos(window, &x, &y);
    glfwGetWindowSize(window, &oldWidth, &oldHeight);

    WorkArea area;

    if (!FindWorkArea(x + oldWidth / 2, y + oldHeight / 2, &area))
        return;

    *width = std::min(*width, area.width);
    *height = std::min(*height, area.height);
}

static ImVec2 CalcModalFitSize(const ImGuiWindow* modal)
{
    const f32 margin = ImFloor(ImGui::GetFontSize() * cPlayerPadEm) * 2.0f;

    if (!(modal->Flags & ImGuiWindowFlags_AlwaysAutoResize))
        return modal->SizeFull + ImVec2(margin, margin);

    const ImVec2 decoration(modal->DecoOuterSizeX1 + modal->DecoOuterSizeX2, modal->DecoOuterSizeY1 + modal->DecoOuterSizeY2);

    return modal->ContentSizeIdeal + modal->WindowPadding * 2.0f + decoration + ImVec2(margin, margin);
}

static void RevealWindow(GLFWwindow* window)
{
    if (sIsRevealed)
        return;

    sIsRevealed = true;
    glfwSetWindowOpacity(window, 1.0f);
}

static void HideUntilSized(GLFWwindow* window)
{
    if (testhooks::IsEnabled())
        return;

    sIsRevealed = false;
    sStableFrameNum = 0;
    sHiddenFrameNum = 0;
    glfwSetWindowOpacity(window, 0.0f);
}

static void CountStableFrame(GLFWwindow* window, bool isStable)
{
    sStableFrameNum = isStable ? sStableFrameNum + 1 : 0;

    if (sStableFrameNum >= cRevealStableFrames || ++sHiddenFrameNum > cRevealFrameLimit)
        RevealWindow(window);
}

static void FitWindowToModal(GLFWwindow* window, ImGuiWindow* modal, s32 width, s32 height)
{
    if (++sModalFrameNum < cModalFirstMeasureFrame || sModalFrameNum > cModalLastMeasureFrame)
        return;

    ImGui::SetWindowPos(modal, ImFloor(ImGui::GetMainViewport()->GetCenter() - modal->Size * 0.5f));

    s32 oldWidth = 0;
    s32 oldHeight = 0;
    glfwGetWindowSize(window, &oldWidth, &oldHeight);

    const ImVec2 fitSize = CalcModalFitSize(modal);

    width = std::max(oldWidth, std::max(width, static_cast<s32>(ImCeil(fitSize.x))));
    height = std::max(oldHeight, std::max(height, static_cast<s32>(ImCeil(fitSize.y))));
    ClampToWorkAreaSize(window, &width, &height);

    ResizeAroundCenter(window, width, height);
    RevealWindow(window);
}

static void FitWindowToContent(f32 contentHeight)
{
    GLFWwindow* window = GetAppWindow();

    if (!window)
        return;

    if (contentHeight <= 0.0f)
    {
        CountStableFrame(window, false);
        return;
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    contentHeight += viewport->WorkPos.y - viewport->Pos.y;

    ImGuiWindow* modal = AreDialogsDetached() ? nullptr : ImGui::GetTopMostPopupModal();
    const ImGuiID modalId = modal ? modal->ID : 0;

    if (modalId != sSizedModalId)
    {
        sModalFrameNum = 0;
        sIsSizePending |= sSizedModalId != 0;
        sSizedModalId = modalId;
    }

    const s32 width = static_cast<s32>(ImCeil(ImGui::GetFontSize() * cPlayerWidthEm));
    const s32 height = static_cast<s32>(ImCeil(contentHeight));

    if (modal)
    {
        FitWindowToModal(window, modal, width, height);
        return;
    }

    s32 oldWidth = 0;
    s32 oldHeight = 0;
    glfwGetWindowSize(window, &oldWidth, &oldHeight);

    const bool isStable = oldWidth == width && oldHeight == height;

    if (!isStable)
    {
        if (sIsSizePending)
            ResizeAroundCenter(window, width, height);
        else
            glfwSetWindowSize(window, width, height);
    }

    sIsSizePending = false;
    CountStableFrame(window, isStable);
}

static void FocusAppWindow()
{
#if defined(SEAD_PLATFORM_WINDOWS)
    GLFWwindow* window = GetAppWindow();

    if (!window || testhooks::IsEnabled())
        return;

    if (glfwGetWindowAttrib(window, GLFW_ICONIFIED))
        glfwRestoreWindow(window);

    glfwFocusWindow(window);
#endif
}

void ApplyPlayerFrameStart()
{
    GLFWwindow* window = GetAppWindow();

    if (!sIsEnterPending || !window)
        return;

    sIsEnterPending = false;
    sIsSizePending = true;

    glfwGetWindowSize(window, &sNormalWidth, &sNormalHeight);
    glfwSetWindowAttrib(window, GLFW_RESIZABLE, GLFW_FALSE);
    SetDetachedDialogArea(ImVec2(static_cast<f32>(sNormalWidth), static_cast<f32>(sNormalHeight)));
    HideUntilSized(window);

    const f32 em = ImGui::GetFontSize();
    const f32 baseHeight = ImFloor(em * cPlayerPadEm) * 2.0f + CalcTransportRowHeight() + ImFloor(em * cPlayerRowGapEm) + ImGui::GetFrameHeight() * 2.0f;

    ResizeAroundCenter(window, static_cast<s32>(ImCeil(em * cPlayerWidthEm)), static_cast<s32>(ImCeil(baseHeight)));

    RefreshWindowTitle();
}

bool IsPlayerFrameShown()
{
    return sIsShown;
}

bool IsPlayerDocument(Document* document)
{
    return document && document->playableFile().isValid();
}

void PreparePlayerFrame(const openqueue::Request& launch)
{
    if (launch.origin != openqueue::Origin::System || !playerfile::IsPlayerFile(launch.path.c_str()))
        return;

    sIsShown = true;
    sIsEnterPending = true;
}

void NotifyPlayerFrameOpen(const openqueue::Request& request, Document* previous, Document* opened)
{
    if (request.origin == openqueue::Origin::System)
        FocusAppWindow();

    if (!sIsShown || request.mode == openqueue::Mode::Reload || !IsPlayerDocument(opened))
        return;

    sAutoplayDocument = opened;

    if (IsPlayerDocument(previous) && previous != opened)
        RequestCloseDocument(previous);
}

void RequestFullEditor()
{
    if (sIsShown)
        sLeaveTarget = LeaveTarget::FullEditor;
    else
        SetFileEditorShown(false);
}

static void LeavePlayerFrame()
{
    GLFWwindow* window = GetAppWindow();

    if (!sIsShown || !window)
        return;

    sIsShown = false;
    sIsSizePending = false;
    sAutoplayDocument = nullptr;

    if (sIsEnterPending)
    {
        sIsEnterPending = false;
        return;
    }

    RevealWindow(window);
    glfwSetWindowAttrib(window, GLFW_RESIZABLE, GLFW_TRUE);
    SetDetachedDialogArea(ImVec2());
    ResizeAroundCenter(window, sNormalWidth, sNormalHeight);

    RefreshWindowTitle();
}

static f32 CalcButtonWidth(const char* label)
{
    return ImGui::CalcTextSize(label, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2.0f;
}

static void RequestFileReplace(const PlayableFile& file, s32 trackIndex)
{
    if (file.wave)
    {
        RequestWaveFileReplace(file.wave);
        return;
    }

    if (file.asset)
    {
        RequestAudioAssetReplace(file.asset);
        return;
    }

    s32 index = 0;

    for (Item* item : file.stream.stream->getTrackList())
    {
        if (index++ != trackIndex)
            continue;

        if (Item* wave = static_cast<Sound::StreamSoundInfo::Track*>(item)->getWaveFileRef().getItem())
            RequestWaveFileReplace(static_cast<WaveFile*>(wave));

        return;
    }
}

static void DrawReplaceTrackMenu(const PlayableFile& file)
{
    if (!BeginContextMenu("###ReplaceTrack"))
        return;

    const s32 trackNum = static_cast<s32>(file.stream.stream->getTrackList().size());

    for (s32 i = 0; i < trackNum; i++)
    {
        const sead::FormatFixedSafeString<32> label(messages::stream::cTrackNameFormat, static_cast<u32>(i));

        if (ContextMenuItem(nullptr, label.cstr()))
            RequestFileReplace(file, i);
    }

    EndContextMenu();
}

static void DrawReplaceButton(const char* label, const PlayableFile& file)
{
    const bool isTrackMenu = file.stream.isValid() && file.stream.stream->getTrackList().size() > 1;
    const char* reason = nullptr;
    const bool canReplace = !file.asset || file.asset->getResource()->canReplaceAudio(*file.asset, &reason);

    ImGui::BeginDisabled(!canReplace);
    const bool pressed = ImGui::Button(label);
    ImGui::EndDisabled();

    if (!canReplace)
        ImGui::SetItemTooltip("%s", reason);
    else
        ImGui::SetItemTooltip("%s", isTrackMenu ? messages::playerframe::cReplaceTrackTip : messages::import::cReplaceAudioTip);

    if (pressed && isTrackMenu)
    {
        ImGui::SetNextWindowPos(ImVec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y));
        ImGui::OpenPopup("###ReplaceTrack");
    }
    else if (pressed)
    {
        RequestFileReplace(file, 0);
    }

    if (isTrackMenu)
        DrawReplaceTrackMenu(file);
}

static void DrawBackupFailureMark(const char* failure)
{
    const f32 size = ImGui::GetFrameHeight();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 iconSize = ImGui::CalcTextSize(ICON_LC_TRIANGLE_ALERT);

    ImGui::Dummy(ImVec2(size, size));
    ImGui::GetWindowDrawList()->AddText(ImFloor(pos + (ImVec2(size, size) - iconSize) * 0.5f), ImGui::GetColorU32(uicolors::Warning()), ICON_LC_TRIANGLE_ALERT);
    ImGui::SetItemTooltip("%s", failure);
}

static void DrawFileRow(Document& document, const PlayableFile& file)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const bool isUnsaved = document.state().unsaved;
    const char* backupFailure = backup::GetFailureTitle();

    const sead::FormatFixedSafeString<64> saveLabel(ICON_LC_SAVE " %s###Save", messages::common::cSave);
    const sead::FormatFixedSafeString<64> replaceLabel(ICON_LC_REPLACE " %s###Replace", messages::common::cReplace);
    const sead::FormatFixedSafeString<64> editLabel(ICON_LC_SQUARE_PEN " %s###Edit", messages::mainmenu::cEdit);

    f32 buttonsWidth = CalcButtonWidth(replaceLabel.cstr()) + style.ItemSpacing.x + CalcButtonWidth(editLabel.cstr());

    if (isUnsaved)
        buttonsWidth += CalcButtonWidth(saveLabel.cstr()) + style.ItemSpacing.x;

    if (backupFailure)
        buttonsWidth += ImGui::GetFrameHeight() + style.ItemSpacing.x;

    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const f32 availWidth = ImGui::GetContentRegionAvail().x;
    const f32 factsRight = pos.x + availWidth - buttonsWidth - style.ItemSpacing.x;
    const f32 textY = pos.y + style.FramePadding.y;

    sead::FixedSafeString<256> facts;
    document.formatFileFacts(&facts);

    ImGui::PushStyleColor(ImGuiCol_Text, GetItemValueColor());
    ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), ImVec2(pos.x, textY), ImVec2(factsRight, textY + ImGui::GetFontSize()), factsRight, factsRight, facts.cstr(), nullptr, nullptr);
    ImGui::PopStyleColor();

    ImGui::SetCursorScreenPos(ImVec2(pos.x + availWidth - buttonsWidth, pos.y));

    if (backupFailure)
    {
        DrawBackupFailureMark(backupFailure);
        ImGui::SameLine();
    }

    if (isUnsaved)
    {
        if (ImGui::Button(saveLabel.cstr()))
            RequestSaveFile();

        ImGui::SameLine();
    }

    DrawReplaceButton(replaceLabel.cstr(), file);
    ImGui::SameLine();
    if (ImGui::Button(editLabel.cstr()))
        sLeaveTarget = LeaveTarget::FileEditor;

    ImGui::SetItemTooltip("%s", messages::playerframe::cEditTip);
}

static void DrawCenteredTrackParam(const TrackContext& ctx, s32 index, TrackParam param)
{
    const f32 size = ImGui::GetFrameHeight();
    const f32 offset = ImFloor((ImGui::GetContentRegionAvail().x - size) * 0.5f);

    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + sead::Mathf::max(offset, 0.0f));
    DrawTrackParam(ctx, index, param, size);
}

static void DrawTrackList(const StreamSource& source)
{
    const TrackContext ctx = MakeTrackContext(source);
    const s32 trackNum = static_cast<s32>(ctx.tracks.size());

    const ImGuiStyle& style = ImGui::GetStyle();
    const f32 rowHeight = ImGui::GetFrameHeight() + style.CellPadding.y * 2.0f;
    const bool isScrolled = trackNum > cTrackRowsVisible;
    const f32 tableHeight = isScrolled ? ImGui::GetTextLineHeight() + (rowHeight + 1.0f) * (cTrackRowsVisible + 0.5f) : 0.0f;

    ImGuiTableFlags flags = ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersInnerH;

    if (isScrolled)
        flags |= ImGuiTableFlags_ScrollY;

    if (!ImGui::BeginTable("PlayerTracks", 4, flags, ImVec2(0.0f, tableHeight)))
        return;

    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn(messages::stream::cTrackMute, ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn(messages::stream::cTrackSolo, ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn(messages::common::cChannels, ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();

    for (s32 i = 0; i < trackNum; i++)
    {
        const bool isAudible = source.stream->isTrackAudible(static_cast<u32>(i));

        ImGui::PushID(i);
        ImGui::TableNextRow(ImGuiTableRowFlags_None, rowHeight);

        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%d", i);

        ImGui::TableNextColumn();
        DrawCenteredTrackParam(ctx, i, TrackParam::Mute);

        ImGui::TableNextColumn();
        DrawCenteredTrackParam(ctx, i, TrackParam::Solo);

        ImGui::TableNextColumn();
        DrawTrackChannelText(ctx.tracks[i]->getChannelCount(source.stream->getStreamType()), false);

        if (!isAudible)
            DimTrackCell(true);

        ImGui::PopID();
    }

    ImGui::EndTable();
}

static bool BeginFrameWindow()
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const f32 pad = ImFloor(ImGui::GetFontSize() * cPlayerPadEm);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad, pad));
    const bool open = ImGui::Begin("###PlayerFrame", nullptr, flags);
    ImGui::PopStyleVar(3);

    return open;
}

static f32 DrawFrameContent(Document& document)
{
    const PlayableFile file = document.playableFile();
    const f32 gap = ImFloor(ImGui::GetFontSize() * cPlayerRowGapEm);

    DrawCompactPlayerBar();

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + gap);
    DrawFileRow(document, file);

    if (file.stream.isValid() && file.stream.stream->getTrackList().size() > 1)
    {
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + gap);
        DrawTrackList(file.stream);
    }

    return ImGui::GetCursorPosY() - ImGui::GetStyle().ItemSpacing.y + ImGui::GetCurrentWindow()->WindowPadding.y;
}

void DrawPlayerFrame()
{
    ApplyPlayerFrameStart();
    HandleFileShortcuts();

    Document* document = ActiveDocument();
    const bool isPlayer = IsPlayerDocument(document);

    if (!isPlayer && (document || !IsOpenPending()))
        sLeaveTarget = LeaveTarget::FullEditor;

    if (isPlayer && document == sAutoplayDocument)
    {
        sAutoplayDocument = nullptr;
        PlayActiveDocumentFile();
    }

    DrawMenuBar();

    f32 contentHeight = 0.0f;

    if (BeginFrameWindow() && isPlayer)
        contentHeight = DrawFrameContent(*document);

    ImGui::End();

    DrawAppDialogs();

    if (sLeaveTarget != LeaveTarget::None)
    {
        const bool isFileEditor = sLeaveTarget == LeaveTarget::FileEditor;

        sLeaveTarget = LeaveTarget::None;
        LeavePlayerFrame();
        SetFileEditorShown(isFileEditor);
        return;
    }

    FitWindowToContent(contentHeight);
}
