#include <ui/player/PlayerFrame.h>

#include <ui/app/MenuBar.h>
#include <ui/app/Workspace.h>
#include <ui/backup/Backup.h>
#include <ui/file/Document.h>
#include <ui/file/DocumentMgr.h>
#include <ui/file/DocumentState.h>
#include <ui/file/File.h>
#include <ui/file/FileFlow.h>
#include <messages/Messages.h>
#include <ui/player/PlaybackDrawer.h>
#include <ui/player/PlayerFileFacts.h>
#include <ui/player/PlayerViews.h>
#include <ui/project/FileLists.h>
#include <ui/resource/ResourceViews.h>
#include <ui/settings/ConfigFile.h>
#include <ui/settings/UiColors.h>
#include <ui/widgets/ContextMenu.h>
#include <ui/widgets/Dialogs.h>
#include <ui/widgets/Knob.h>

#include <aal/AudioResource.h>
#include <app/Utilll.h>
#include <core/PlayerFile.h>
#include <core/TestHooks.h>
#include <snd/StreamSoundPlayer.h>
#include <platform/WindowFrame.h>
#include <platform/WindowZoom.h>

#include <framework/glfw/seadGameFrameworkBaseGlfw.h>
#include <imgui/imgui_internal.h>
#include <libs/IconsLucide.h>

#include <algorithm>
#include <vector>

static constexpr f32 cPlayerWidthEm          = 46.0f;
static constexpr f32 cPlayerMinWidthEm       = 32.0f;
static constexpr f32 cPlayerPadEm            = 0.8f;
static constexpr f32 cPlayerRowGapEm         = 0.6f;
static constexpr s32 cModalFirstMeasureFrame = 2;
static constexpr s32 cModalLastMeasureFrame  = 10;
static constexpr s32 cRevealStableFrames     = 2;
static constexpr s32 cRevealFrameLimit       = 90;
static constexpr s32 cWidthSaveDelayFrames   = 30;

static constexpr const char* cConfigSection = "player";
static constexpr const char* cWidthKey      = "widthEm";

struct SizeLimits
{
    s32 minWidth = GLFW_DONT_CARE;
    s32 minHeight = GLFW_DONT_CARE;
    s32 maxWidth = GLFW_DONT_CARE;
    s32 maxHeight = GLFW_DONT_CARE;

    bool operator==(const SizeLimits& other) const
    {
        return minWidth == other.minWidth && minHeight == other.minHeight && maxWidth == other.maxWidth && maxHeight == other.maxHeight;
    }

    bool operator!=(const SizeLimits& other) const
    {
        return !(*this == other);
    }
};

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
static bool sCanBeEmpty = false;
static LeaveTarget sLeaveTarget = LeaveTarget::None;
static Document* sAutoplayDocument = nullptr;
static ImGuiID sSizedModalId = 0;
static s32 sModalFrameNum = 0;
static s32 sStableFrameNum = 0;
static s32 sHiddenFrameNum = 0;
static s32 sNormalWidth = 0;
static s32 sNormalHeight = 0;
static s32 sWidthSaveFrameNum = 0;
static f32 sWidthEm = cPlayerWidthEm;
static SizeLimits sSizeLimits;

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

    windowframe::SetSize(window, width, height);

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

static s32 CalcMinWidth()
{
    return static_cast<s32>(ImCeil(ImGui::GetFontSize() * cPlayerMinWidthEm));
}

static s32 CalcMaxWidth()
{
    const f32 pad = ImFloor(ImGui::GetFontSize() * cPlayerPadEm);

    return static_cast<s32>(ImCeil(CalcCompactPlayerBarMaxWidth() + pad * 2.0f));
}

static s32 CalcUserWidth()
{
    return std::clamp(static_cast<s32>(sWidthEm * ImGui::GetFontSize() + 0.5f), CalcMinWidth(), CalcMaxWidth());
}

static void LoadUserWidth()
{
    if (testhooks::IsEnabled())
        return;

    json::Value section;
    LoadConfigSection(cStateFile, cConfigSection, &section);

    sWidthEm = section.getFloat(cWidthKey, cPlayerWidthEm);
}

static void SaveUserWidth(bool isForced)
{
    if (sWidthSaveFrameNum == 0)
        return;

    if (!isForced && --sWidthSaveFrameNum > 0)
        return;

    sWidthSaveFrameNum = 0;

    if (testhooks::IsEnabled())
        return;

    json::Value section;
    section.setFloat(cWidthKey, sWidthEm);

    SaveConfigSection(cStateFile, cConfigSection, section);
}

static void RememberUserWidth(s32 width)
{
    if (width == CalcUserWidth())
        return;

    sWidthEm = static_cast<f32>(width) / ImGui::GetFontSize();
    sWidthSaveFrameNum = cWidthSaveDelayFrames;
}

static void ApplySizeLimits(GLFWwindow* window, const SizeLimits& limits)
{
    if (limits == sSizeLimits)
        return;

    sSizeLimits = limits;
    windowframe::SetSizeLimits(window, limits.minWidth, limits.minHeight, limits.maxWidth, limits.maxHeight);
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

    const s32 height = static_cast<s32>(ImCeil(contentHeight));

    if (modal)
    {
        ApplySizeLimits(window, SizeLimits());
        FitWindowToModal(window, modal, CalcUserWidth(), height);
        return;
    }

    const SizeLimits limits = { CalcMinWidth(), height, CalcMaxWidth(), height };

    s32 oldWidth = 0;
    s32 oldHeight = 0;
    glfwGetWindowSize(window, &oldWidth, &oldHeight);

    const s32 width = sIsSizePending ? CalcUserWidth() : std::clamp(oldWidth, limits.minWidth, limits.maxWidth);
    const bool isStable = oldWidth == width && oldHeight == height;

    if (limits != sSizeLimits)
        ApplySizeLimits(window, SizeLimits());

    if (!isStable)
    {
        if (sIsSizePending)
            ResizeAroundCenter(window, width, height);
        else
            windowframe::SetSize(window, width, height);
    }

    ApplySizeLimits(window, limits);

    if (isStable && !sIsSizePending)
        RememberUserWidth(width);

    sIsSizePending = false;
    SaveUserWidth(false);
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
    windowzoom::SetEnabled(ImGui::GetMainViewport()->PlatformHandleRaw, false);
    SetDetachedDialogArea(ImVec2(static_cast<f32>(sNormalWidth), static_cast<f32>(sNormalHeight)));
    HideUntilSized(window);
    LoadUserWidth();

    const f32 em = ImGui::GetFontSize();
    const f32 baseHeight = ImFloor(em * cPlayerPadEm) * 2.0f + CalcTransportRowHeight() + ImFloor(em * cPlayerRowGapEm) + ImGui::GetFrameHeight() * 2.0f;

    ResizeAroundCenter(window, CalcUserWidth(), static_cast<s32>(ImCeil(baseHeight)));

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

void PrepareEmptyPlayerFrame()
{
    sIsShown = true;
    sIsEnterPending = true;
    sCanBeEmpty = true;
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

void ForgetPlayerFrameDocument(const Document* document)
{
    if (sAutoplayDocument == document)
        sAutoplayDocument = nullptr;
}

void RequestFullEditor()
{
    if (sIsShown)
        sLeaveTarget = LeaveTarget::FullEditor;
    else
        SetFileEditorShown(false);
}

static void ClosePlayerOnlyDocuments()
{
    const std::vector<Document*> documents = GetOpenDocuments();

    for (Document* document : documents)
    {
        if (document->isPlayerOnly())
            CloseDocument(document);
    }
}

static void LeavePlayerFrame()
{
    GLFWwindow* window = GetAppWindow();

    if (!sIsShown || !window)
        return;

    ClosePlayerOnlyDocuments();

    sIsShown = false;
    sIsSizePending = false;
    sCanBeEmpty = false;
    sAutoplayDocument = nullptr;

    if (sIsEnterPending)
    {
        sIsEnterPending = false;
        return;
    }

    RevealWindow(window);
    SaveUserWidth(true);
    ApplySizeLimits(window, SizeLimits());
    windowzoom::SetEnabled(ImGui::GetMainViewport()->PlatformHandleRaw, true);
    SetDetachedDialogArea(ImVec2());
    ResizeAroundCenter(window, sNormalWidth, sNormalHeight);

    RefreshWindowTitle();
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

static f32 DrawEditButton(f32 right, f32 top, f32 bottom)
{
    if (MenuBarIconButton("##Edit", ICON_LC_SQUARE_PEN, right, top, bottom))
        sLeaveTarget = LeaveTarget::FileEditor;

    SetMenuBarTooltip(messages::playerframe::cOpenInEditor);

    return CalcMenuBarIconButtonWidth(ICON_LC_SQUARE_PEN);
}

static f32 DrawReplaceButton(const PlayableFile& file, f32 right, f32 top, f32 bottom)
{
    const bool isTrackMenu = file.stream.isValid() && file.stream.stream->getTrackList().size() > 1;
    const bool isMenuOpen = ImGui::IsPopupOpen("###ReplaceTrack");
    const char* reason = nullptr;
    const bool canReplace = !file.asset || file.asset->getResource()->canReplaceAudio(*file.asset, &reason);
    const ImVec4 disabledColor = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);

    ImGui::BeginDisabled(!canReplace);
    const bool pressed = MenuBarIconButton("##Replace", ICON_LC_ARROW_DOWN_UP, right, top, bottom, isMenuOpen, canReplace ? nullptr : &disabledColor);
    ImGui::EndDisabled();

    if (!canReplace)
        SetMenuBarTooltip(reason);
    else if (!isMenuOpen)
        SetMenuBarTooltip(isTrackMenu ? messages::playerframe::cReplaceTrack : messages::common::cReplaceAudio);

    if (pressed && isTrackMenu)
    {
        ImGui::SetNextWindowPos(ImVec2(right, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
        ImGui::OpenPopup("###ReplaceTrack");
    }
    else if (pressed)
    {
        RequestFileReplace(file, 0);
    }

    if (isTrackMenu)
        DrawReplaceTrackMenu(file);

    return CalcMenuBarIconButtonWidth(ICON_LC_ARROW_DOWN_UP);
}

static f32 DrawSaveButton(f32 right, f32 top, f32 bottom)
{
    const ImVec4 unsavedColor = uicolors::Warning();

    if (MenuBarIconButton("##Save", ICON_LC_SAVE, right, top, bottom, false, &unsavedColor))
        RequestSaveFile();

    SetMenuBarTooltip(messages::common::cSave);

    return CalcMenuBarIconButtonWidth(ICON_LC_SAVE);
}

static f32 DrawBackupFailureMark(const char* failure, f32 right, f32 top, f32 bottom)
{
    const f32 width = CalcMenuBarIconButtonWidth(ICON_LC_TRIANGLE_ALERT);
    const ImVec2 iconSize = ImGui::CalcTextSize(ICON_LC_TRIANGLE_ALERT);
    const ImVec2 min(right - width, top);

    ImGui::SetCursorScreenPos(min);
    ImGui::Dummy(ImVec2(width, bottom - top));
    ImGui::GetWindowDrawList()->AddText(ImFloor(min + (ImVec2(width, bottom - top) - iconSize) * 0.5f), ImGui::GetColorU32(uicolors::Warning()), ICON_LC_TRIANGLE_ALERT);
    SetMenuBarTooltip(failure);

    return width;
}

f32 DrawPlayerFrameTools(f32 right, f32 top, f32 bottom)
{
    Document* document = ActiveDocument();

    if (!IsPlayerDocument(document))
        return right;

    const PlayableFile file = document->playableFile();
    const f32 gap = ImGui::GetStyle().ItemInnerSpacing.x;

    if (!document->isPlayerOnly())
    {
        right -= DrawEditButton(right, top, bottom) + gap;
        right -= DrawReplaceButton(file, right, top, bottom) + gap;
    }

    right -= DrawFileDetailsButton(*document, right, top, bottom) + gap;

    if (document->state().unsaved)
        right -= DrawSaveButton(right, top, bottom) + gap;

    if (const char* backupFailure = backup::GetFailureTitle())
        return right - DrawBackupFailureMark(backupFailure, right, top, bottom);

    return right + gap;
}

static void DrawTrackVolume(u32 trackNum)
{
    if (trackNum > cStrmTrackNum)
        trackNum = cStrmTrackNum;

    RegionHeader(messages::playback::cPanelTrackVolume);
    DrawTrackVolumeStrip(trackNum, CalcTrackVolumeStripHeight(false));
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

static bool DrawDocumentRows(Document& document)
{
    const PlayableFile file = document.playableFile();
    const f32 gap = ImFloor(ImGui::GetFontSize() * cPlayerRowGapEm);
    const u32 trackNum = file.stream.isValid() ? static_cast<u32>(file.stream.stream->getTrackList().size()) : 0;

    if (trackNum > 1)
    {
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + gap);
        DrawTrackVolume(trackNum);
    }

    return DrawFileFactsLine(document, gap);
}

static f32 DrawFrameContent(Document* document)
{
    DrawCompactPlayerBar();

    const bool hasStatusBar = document && DrawDocumentRows(*document);
    const f32 bottomPad = hasStatusBar ? 0.0f : ImGui::GetCurrentWindow()->WindowPadding.y;

    return ImGui::GetCursorPosY() - ImGui::GetStyle().ItemSpacing.y + bottomPad;
}

void DrawPlayerFrame()
{
    ApplyPlayerFrameStart();
    HandleFileShortcuts();

    Document* document = ActiveDocument();
    const bool isPlayer = IsPlayerDocument(document);
    const bool isEmpty = !document && sCanBeEmpty;

    if (!isPlayer && !isEmpty && (document || !IsOpenPending()))
        sLeaveTarget = LeaveTarget::FullEditor;

    if (isPlayer && document == sAutoplayDocument)
    {
        sAutoplayDocument = nullptr;
        PlayActiveDocumentFile();
    }

    DrawMenuBar();

    f32 contentHeight = 0.0f;

    if (BeginFrameWindow() && (isPlayer || isEmpty))
        contentHeight = DrawFrameContent(isPlayer ? document : nullptr);

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
