#define IMGUI_DEFINE_MATH_OPERATORS
#include <ui/UI.h>

#include <ui/PopupMgr.h>

//#include <snd/SoundThread.h>
#include <snd/SoundSystem.h>

#include <filedevice/seadPath.h>
#include <filedevice/seadFileDeviceMgr.h>
#include <framework/glfw/seadGameFrameworkBaseGlfw.h>
#include <framework/seadProcessMeter.h>
#include <gfx/gl/seadTextureGL.h>

#include <Utilll.h>

#if defined(SEAD_PLATFORM_WINDOWS)
#include <basis/win/seadWindows.h>
#include <shellapi.h>
#include <direct.h>
#define strcasecmp _stricmp
#define getcwd _getcwd
#elif defined(SEAD_PLATFORM_LINUX) || defined(SEAD_PLATFORM_MACOSX)
#include <cstdlib>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <midi/SeqMidiExporter.h>

#include <cstdio>
#include <cctype>
#include <vector>
#include <algorithm>
#include <unordered_set>
#include <filesystem>

UIType sSelectedUIType = UIType::ProjectInfo;

ImVec4 gAccentColor = ImVec4(0.24f, 0.50f, 0.88f, 1.00f);
f32 gThemeBrightness = 1.0f;

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

bool sShowSystemWindow = false;
bool sShowDemoWindow = false;
bool sSoundSetStickyEdit = false;

sead::FixedSafeString<512> sDroppedFilePath;
sead::FixedSafeString<512> sRecentFileClick;

static bool sPendingExport = false;
static Sound* sPendingExportSound = nullptr;
static Sound::SoundType sPendingExportType;
static int sPendingExportDurationMin = 2;
static int sPendingExportDurationSec = 0;

static SequenceFile* sPendingExportSequenceFile = nullptr;
static bool sPendingImportSequenceFile = false;
static WaveFile* sPendingExportWaveFile = nullptr;
static WaveFile* sPendingExportWaveToWav = nullptr;
static Sound* sPendingExportMidiSound = nullptr;
static BankFile* sPendingExportBankBundle = nullptr;
static bool sPendingImportBankBundle = false;

static int sStrmMultiChannel = 1;
static bool sStrmLoop = true;
static int sStrmLoopCount = 2;
static float sStrmFadeSec = 12.0f;
static int sStrmSampleRateIdx = 0;
static int sSeqSampleRateIdx = 0;

u32 gOutputSampleRate = 48000;

static const char* sOutSampleRateItems[] = {
    "8000 Hz",
    "11025 Hz",
    "16000 Hz",
    "22050 Hz",
    "32000 Hz",
    "44100 Hz",
    "48000 Hz",
    "96000 Hz"
};
static const u32 sOutSampleRateValues[] = {
    8000,
    11025,
    16000,
    22050,
    32000,
    44100,
    48000,
    96000
};

static bool sShowExportConfirm = false;

static const char* sSampleRateItems[] = {
    "Original",
    "8000 Hz",
    "11025 Hz",
    "16000 Hz",
    "22050 Hz",
    "32000 Hz",
    "44100 Hz",
    "48000 Hz",
    "96000 Hz"
};
static const u32 sSampleRateValues[] = {
    0,
    8000,
    11025,
    16000,
    22050,
    32000,
    44100,
    48000,
    96000
};

ItemList sFileWindows;

// Windows
void DrawProjectUI();
void DrawInfoUI();
void DrawSubInfoUI();
void DrawFileUI(ImGuiID dockspaceId);
void DrawPropertiesUI();
void DrawExportDialog();
static void DrawFileExportDialogs();
void DrawExportConfirmPopup();

void DrawAllSoundsUI();
void DrawStreamSoundsUI();
void DrawWaveSoundsUI();
void DrawSequenceSoundsUI();
void DrawAllSoundSetsUI();
void DrawWaveSoundSetsUI();
void DrawSequenceSoundSetsUI();
void SequenceSoundSetContextMenuFunc(Item* item, bool afterDelete);
void DrawWaveFilesUI();
void DrawSequenceFilesUI();
void DrawBankFilesUI();
void DrawFileStatisticsUI();

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

    ImGui::DockBuilderDockWindow("###ProjectWindow", dock1);
    ImGui::DockBuilderDockWindow("###InfoWindow", mainDockId);
    ImGui::DockBuilderDockWindow("###SubInfoWindow", dockDownMain);
    ImGui::DockBuilderDockWindow("###PropertiesWindow", dock2);
    ImGui::DockBuilderDockWindow("###PlayerParamWindow", dock2);
    ImGui::DockBuilderDockWindow("###SequenceVarWindow", dock2);
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

    if (ImGui::DockBuilderGetNode(dockspace_id) == nullptr)
    {
        DockBuilder(dockspace_id, viewport->WorkSize);
    }

    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags, window_class);
    ImGui::End();

    return dockspace_id;
}

bool gUnsavedChanges = false;

void SetUnsavedChanges(bool dirty)
{
    if (gUnsavedChanges != dirty)
    {
        gUnsavedChanges = dirty;
        util::refreshTitleDirty(dirty);
    }
}

static bool sWantsNew = false;
static bool sWantsOpen = false;
static bool sWantsClose = false;
static bool sWantsExit = false;
static bool sWantsAbout = false;
static bool sNeedsNewFileFormat = false;

static bool NewFileFormatTrigger()
{
    sNeedsNewFileFormat = true;
    return true;
}

bool TryExit()
{
    if (sBfsar.isOpen())
    {
        sWantsExit = true;
        return false;
    }
    else
    {
        Exit();
        return true;
    }
}

void DrawMenuBar()
{
    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            bool bfsarOpen = sBfsar.isOpen();
            if (ImGui::MenuItem(ICON_LC_FILE " New", "Ctrl+Shift+N"))
            {
                if (bfsarOpen)
                {
                    sWantsNew = true;
                }
                else
                {
                    sNeedsNewFileFormat = true;
                }
            }

            if (ImGui::MenuItem(ICON_LC_FOLDER_OPEN " Open", "Ctrl+O"))
            {
                if (bfsarOpen)
                {
                    sWantsOpen = true;
                }
                else
                {
                    OpenFile();
                }
            }

            if (ImGui::BeginMenu(ICON_LC_CLOCK " Open Recent"))
            {
                for (size_t i = 0; i < GetRecentFiles().size(); i++)
                {
                    const char* path = GetRecentFiles()[i].c_str();
                    const char* name = path;
                    if (const char* sep = strrchr(path, '/'))
                        name = sep + 1;
                    else if (const char* sep = strrchr(path, '\\'))
                        name = sep + 1;
                    if (ImGui::MenuItem(name, nullptr))
                    {
                        if (bfsarOpen)
                        {
                            sWantsOpen = true;
                            sRecentFileClick = path;
                        }
                        else
                        {
                            OpenFile(path);
                        }
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s", path);
                }
                if (GetRecentFiles().empty())
                {
                    ImGui::BeginDisabled();
                    ImGui::MenuItem("(empty)");
                    ImGui::EndDisabled();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Clear recents"))
                {
                    GetRecentFiles().clear();
                    SaveRecentFiles();
                }
                ImGui::EndMenu();
            }

            if (!bfsarOpen)
            {
                ImGui::BeginDisabled();
            }

            if (ImGui::MenuItem(ICON_LC_SAVE " Save", "Ctrl+S"))
            {
                SaveFile();
            }

            if (ImGui::MenuItem(ICON_LC_SAVE_ALL " Save As", "Ctrl+Shift+S"))
            {
                SaveFileAs();
            }

            if (ImGui::MenuItem(ICON_LC_FILE_OUTPUT " Close"))
            {
                sWantsClose = true;
            }

            if (!bfsarOpen)
            {
                ImGui::EndDisabled();
            }

            if (ImGui::MenuItem(ICON_LC_DOOR_OPEN " Exit"))
            {
                TryExit();
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("More"))
        {
            ImGui::MenuItem(ICON_LC_CPU " System Window", nullptr, &sShowSystemWindow);
            // ImGui::MenuItem("Demo Window", nullptr, &sShowDemoWindow);

            int outRateIdx = 3;
            for (u32 i = 0; i < sizeof(sOutSampleRateValues) / sizeof(sOutSampleRateValues[0]); i++)
            {
                if (sOutSampleRateValues[i] == gOutputSampleRate)
                { outRateIdx = static_cast<int>(i); break; }
            }
            if (ImGui::Combo("Output Sample Rate", &outRateIdx, sOutSampleRateItems, IM_ARRAYSIZE(sOutSampleRateItems)))
            {
                gOutputSampleRate = sOutSampleRateValues[outRateIdx];
                snd::SoundSystem::setOutputSampleRate(gOutputSampleRate);
            }

            ImGui::Separator();
            {
                float h, s, v;
                ImGui::ColorConvertRGBtoHSV(gAccentColor.x, gAccentColor.y, gAccentColor.z, h, s, v);

                bool changed = false;
                bool done = false;

                ImVec4 ctPreview;
                ImGui::ColorConvertHSVtoRGB(h, 0.50f, 0.60f, ctPreview.x, ctPreview.y, ctPreview.z);
                ctPreview.w = 1.0f;
                ImGui::ColorButton("##ct", ctPreview, ImGuiColorEditFlags_NoTooltip, ImVec2(14, 14));
                ImGui::SameLine();
                if (ImGui::SliderFloat("Color", &h, 0.0f, 1.0f, ""))
                {
                    ImVec4 c;
                    ImGui::ColorConvertHSVtoRGB(h, 0.50f, 0.60f, c.x, c.y, c.z);
                    c.w = 1.0f;
                    gAccentColor = c;
                    changed = true;
                }
                if (ImGui::IsItemDeactivatedAfterEdit())
                    done = true;

                if (ImGui::SliderFloat("Brightness", &gThemeBrightness, 0.3f, 1.7f, "%.1f"))
                    changed = true;
                if (ImGui::IsItemDeactivatedAfterEdit())
                    done = true;

                if (changed)
                    ApplyThemeFromAccent(gAccentColor);

                if (done)
                    SaveAccentColor();
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Help"))
        {
            if (ImGui::MenuItem(ICON_LC_INFO " About"))
            {
                sWantsAbout = true;
            }

            ImGui::EndMenu();
        }

        if (sBfsar.isOpen())
        {
            const char* statusText = gUnsavedChanges ? "  Unsaved " ICON_LC_FILE : "  Saved " ICON_LC_CHECK_CHECK;
            ImVec4 statusColor = gUnsavedChanges ? ImVec4(0.85f, 0.65f, 0.15f, 1.0f) : ImVec4(0.45f, 0.45f, 0.45f, 1.0f);
            float textWidth = ImGui::CalcTextSize(statusText).x;
            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - textWidth - ImGui::GetStyle().FramePadding.x);
            ImGui::TextColored(statusColor, "%s", statusText);
        }

        ImGui::EndMainMenuBar();
    }
}

void OpenURL(const char* url)
{
#if defined(SEAD_PLATFORM_WINDOWS)
    ShellExecuteA(nullptr, "open", url, nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(SEAD_PLATFORM_LINUX)
    std::string command = "xdg-open \"" + std::string(url) + "\"";
    system(command.c_str());
#elif defined(SEAD_PLATFORM_MACOSX)
    std::string command = "open \"" + std::string(url) + "\"";
    system(command.c_str());
#else
    #error "Unsupported platform"
#endif
}

static ImVec4 HSV(float h, float s, float v, float a = 1.0f)
{
    v *= gThemeBrightness;
    if (v > 1.0f) v = 1.0f;
    ImVec4 c;
    ImGui::ColorConvertHSVtoRGB(h, s, v, c.x, c.y, c.z);
    c.w = a;
    return c;
}

void ApplyThemeFromAccent(ImVec4 accent)
{
    float h, s, v;
    ImGui::ColorConvertRGBtoHSV(accent.x, accent.y, accent.z, h, s, v);

    if (s < 0.05f)
        h = 0.0f;

    ImVec4* colors = ImGui::GetStyle().Colors;

    colors[ImGuiCol_Text] = ImVec4(0.92f, 0.92f, 0.93f, 0.90f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.52f, 1.00f);
    colors[ImGuiCol_WindowBg] = HSV(h, 0.40f, 0.17f);
    colors[ImGuiCol_ChildBg] = HSV(h, 0.35f, 0.13f);
    colors[ImGuiCol_PopupBg] = HSV(h, 0.38f, 0.15f, 0.85f);
    colors[ImGuiCol_Border] = HSV(h, 0.30f, 0.40f, 0.65f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg] = HSV(h, 0.42f, 0.22f);
    colors[ImGuiCol_FrameBgHovered] = HSV(h, 0.45f, 0.32f, 0.40f);
    colors[ImGuiCol_FrameBgActive] = HSV(h, 0.48f, 0.38f, 0.45f);
    colors[ImGuiCol_TitleBg] = HSV(h, 0.30f, 0.12f, 0.83f);
    colors[ImGuiCol_TitleBgActive] = HSV(h, 0.32f, 0.14f, 0.87f);
    colors[ImGuiCol_TitleBgCollapsed] = HSV(h, 0.35f, 0.20f, 0.20f);
    colors[ImGuiCol_MenuBarBg] = HSV(h, 0.35f, 0.13f, 0.80f);
    colors[ImGuiCol_ScrollbarBg] = HSV(h, 0.38f, 0.20f, 0.60f);
    colors[ImGuiCol_ScrollbarGrab] = HSV(h, 0.40f, 0.38f, 0.51f);
    colors[ImGuiCol_ScrollbarGrabHovered] = HSV(h, 0.45f, 0.48f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive] = HSV(h, 0.50f, 0.56f, 0.91f);
    colors[ImGuiCol_CheckMark] = HSV(h, 0.60f, 0.80f, 0.83f);
    colors[ImGuiCol_SliderGrab] = HSV(h, 0.40f, 0.45f, 0.62f);
    colors[ImGuiCol_SliderGrabActive] = HSV(h, 0.50f, 0.65f, 0.84f);
    colors[ImGuiCol_Button] = HSV(h, 0.50f, 0.60f, 0.49f);
    colors[ImGuiCol_ButtonHovered] = HSV(h, 0.50f, 0.70f, 0.68f);
    colors[ImGuiCol_ButtonActive] = HSV(h, 0.55f, 0.50f, 1.00f);
    colors[ImGuiCol_Header] = HSV(h, 0.45f, 0.50f, 0.53f);
    colors[ImGuiCol_HeaderHovered] = HSV(h, 0.50f, 0.65f, 1.00f);
    colors[ImGuiCol_HeaderActive] = HSV(h, 0.55f, 0.55f, 1.00f);
    colors[ImGuiCol_Separator] = HSV(h, 0.15f, 0.30f, 0.50f);
    colors[ImGuiCol_SeparatorHovered] = HSV(h, 0.40f, 0.55f, 0.78f);
    colors[ImGuiCol_SeparatorActive] = HSV(h, 0.50f, 0.65f, 1.00f);
    colors[ImGuiCol_ResizeGrip] = ImVec4(1.00f, 1.00f, 1.00f, 0.85f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(1.00f, 1.00f, 1.00f, 0.60f);
    colors[ImGuiCol_ResizeGripActive] = ImVec4(1.00f, 1.00f, 1.00f, 0.90f);
    colors[ImGuiCol_Tab] = HSV(h, 0.35f, 0.18f);
    colors[ImGuiCol_TabHovered] = HSV(h, 0.50f, 0.55f, 0.80f);
    colors[ImGuiCol_TabActive] = HSV(h, 0.38f, 0.24f);
    colors[ImGuiCol_TabUnfocused] = HSV(h, 0.30f, 0.14f);
    colors[ImGuiCol_TabUnfocusedActive] = HSV(h, 0.32f, 0.20f);
    colors[ImGuiCol_DockingPreview] = HSV(h, 0.50f, 0.65f, 0.70f);
    colors[ImGuiCol_DockingEmptyBg] = HSV(h, 0.00f, 0.20f);
    colors[ImGuiCol_PlotLines] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
    colors[ImGuiCol_PlotHistogram] = HSV(h, 0.55f, 0.65f, 1.00f);
    colors[ImGuiCol_PlotHistogramHovered] = HSV(h, 0.55f, 0.80f, 1.00f);
    colors[ImGuiCol_TableHeaderBg] = HSV(h, 0.35f, 0.22f);
    colors[ImGuiCol_TableBorderStrong] = HSV(h, 0.20f, 0.25f);
    colors[ImGuiCol_TableBorderLight] = HSV(h, 0.15f, 0.18f);
    colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.00f, 1.00f, 1.00f, 0.06f);
    colors[ImGuiCol_TextSelectedBg] = HSV(h, 0.40f, 0.40f, 0.35f);
    colors[ImGuiCol_DragDropTarget] = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
    colors[ImGuiCol_NavHighlight] = HSV(h, 0.50f, 0.60f, 1.00f);
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);
}

static std::string GetConfigDir()
{
#if defined(SEAD_PLATFORM_WINDOWS)
    const char* appdata = getenv("APPDATA");
    if (appdata && appdata[0])
        return std::string(appdata) + "\\TuneBloom";
    return ".\\TuneBloom";
#elif defined(SEAD_PLATFORM_LINUX)
    const char* xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0])
        return std::string(xdg) + "/TuneBloom";
    const char* home = getenv("HOME");
    if (home && home[0])
        return std::string(home) + "/.config/TuneBloom";
    return "./.config/TuneBloom";
#else
    const char* home = getenv("HOME");
    if (home && home[0])
        return std::string(home) + "/Library/Application Support/TuneBloom";
    return "./TuneBloom";
#endif
}

void SaveAccentColor()
{
    std::string path = GetConfigDir() + "/theme.cfg";

#if defined(SEAD_PLATFORM_WINDOWS)
    CreateDirectoryA(GetConfigDir().c_str(), NULL);
#else
    mkdir(GetConfigDir().c_str(), 0755);
#endif

    FILE* f = fopen(path.c_str(), "w");
    if (f)
    {
        fprintf(f, "%f %f %f %f %f\n", gAccentColor.x, gAccentColor.y, gAccentColor.z, gAccentColor.w, gThemeBrightness);
        fclose(f);
    }
}

void LoadAccentColor()
{
    std::string path = GetConfigDir() + "/theme.cfg";

    FILE* f = fopen(path.c_str(), "r");
    if (f)
    {
        int n = fscanf(f, "%f %f %f %f %f", &gAccentColor.x, &gAccentColor.y, &gAccentColor.z, &gAccentColor.w, &gThemeBrightness);
        if (n < 5)
            gThemeBrightness = 1.0f;
        fclose(f);
    }
}

std::vector<std::string>& GetRecentFiles()
{
    static std::vector<std::string>* files = new std::vector<std::string>();
    return *files;
}

void SaveRecentFiles()
{
    std::string path = GetConfigDir() + "/recent.cfg";

    FILE* f = fopen(path.c_str(), "w");
    if (f)
    {
        for (size_t i = 0; i < GetRecentFiles().size(); i++)
            fprintf(f, "%s\n", GetRecentFiles()[i].c_str());
        fclose(f);
    }
}

void LoadRecentFiles()
{
    GetRecentFiles().clear();

    std::string path = GetConfigDir() + "/recent.cfg";

    FILE* f = fopen(path.c_str(), "r");
    if (f)
    {
        char buf[1024];
        while (fgets(buf, sizeof(buf), f))
        {
            size_t len = strlen(buf);
            if (len > 0 && buf[len - 1] == '\n')
                buf[len - 1] = '\0';
            if (buf[0])
                GetRecentFiles().push_back(buf);
        }
        fclose(f);
    }
}

void DrawTuneBloomSplash(ImTextureID logoTex, ImVec2 logoSize)
{
    ImGuiIO& io = ImGui::GetIO();

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 8.0f));

    if (ImGui::BeginPopupModal("##TuneBloomSplash",
        nullptr,
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_AlwaysAutoResize
    ))
    {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        ImVec2 winPos = ImGui::GetWindowPos();
        ImVec2 winSize = ImGui::GetWindowSize();

        ImU32 topCol    = IM_COL32(15, 15, 20, 255);
        ImU32 bottomCol = IM_COL32(30, 30, 45, 255);
        ImVec4 hyperCol = ImVec4(0.3f, 0.6f, 1.0f, 1.0f);

        s32 vtxIdxBegin = draw->VtxBuffer.Size;
        draw->AddRectFilled(winPos, winPos + winSize, IM_COL32_WHITE, 12.0f);
        s32 vtxIdxEnd = draw->VtxBuffer.Size;

        ImGui::ShadeVertsLinearColorGradientKeepAlpha(draw,
            vtxIdxBegin, vtxIdxEnd,
            winPos, winPos + winSize,
            topCol, bottomCol
        );

        auto CenterText = [](const char* text)
        {
            f32 width = ImGui::CalcTextSize(text).x;
            ImGui::SetCursorPosX((ImGui::GetWindowSize().x - width) * 0.5f);
            ImGui::TextUnformatted(text);
        };

        auto CenterItem = [](f32 width)
        {
            ImGui::SetCursorPosX((ImGui::GetWindowSize().x - width) * 0.5f);
        };

        CenterItem(logoSize.x);
        ImGui::Image(logoTex, logoSize);

        // ImGui::Dummy(ImVec2(0.0f, 10.0f));

        ImGui::PushFont(io.Fonts->Fonts[0]); // Big font
        CenterText(util::cAppName.cstr());
        ImGui::PopFont();

        ImGui::SameLine();


#if defined(COMMIT_SHA)
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), " %s", util::cAppVersion.cstr());
#else
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), " v%s", util::cAppVersion.cstr());
#endif

        ImGui::Dummy(ImVec2(0.0f, 5.0f));

        CenterItem(ImGui::CalcTextSize("by stupidestmodder & more").x);

        ImGui::Text("by");
        ImGui::SameLine();

        ImGui::TextColored(hyperCol, "stupidestmodder");
        if (ImGui::IsItemHovered())
        {
            ImVec2 min = ImGui::GetItemRectMin();
            ImVec2 max = ImGui::GetItemRectMax();
            min.y = max.y;
            draw->AddLine(min, max, ImGui::GetColorU32(hyperCol));

            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                OpenURL("https://github.com/stupidestmodder");
            }
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 5.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
            ImGui::SetTooltip("https://github.com/stupidestmodder");
            ImGui::PopStyleVar(2);
        }

        ImGui::SameLine();
        ImGui::Text("& more");

        ImGui::Dummy(ImVec2(0.0f, 10.0f));

        CenterText("The NintendoWare sound archive editor");

        CenterItem(ImGui::CalcTextSize("Made in Brazil").x);
        ImGui::Text("Made in");
        ImGui::SameLine();
        ImGui::Text("Brazil");

        ImGui::Dummy(ImVec2(0.0f, 10.0f));

        const char* linkFull = "   Support: https://go.nsmbu.net/discord   ";
        const char* link = "https://go.nsmbu.net/discord   ";
        CenterItem(ImGui::CalcTextSize(linkFull).x);

        ImGui::Text("   Support:");
        ImGui::SameLine();
        ImGui::TextColored(hyperCol, "%s", link);
        if (ImGui::IsItemHovered())
        {
            ImVec2 min = ImGui::GetItemRectMin();
            ImVec2 max = ImGui::GetItemRectMax();
            max.x -= ImGui::CalcTextSize("   ").x;
            min.y = max.y;
            draw->AddLine(min, max, ImGui::GetColorU32(hyperCol));

            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                OpenURL(link);
            }
        }

        ImGui::Separator();
        f32 buttonSize = 100.0f;
        CenterItem(buttonSize);

        if (ImGui::Button("Close", ImVec2(buttonSize, 0.0f)))
        {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    ImGui::PopStyleVar(2);
}

void DrawUI()
{
    if (!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId))
    {
        if (ImGui::IsKeyDown(ImGuiKey_ModCtrl) && !ImGui::IsKeyDown(ImGuiKey_ModShift))
        {
            if (ImGui::IsKeyPressed(ImGuiKey_S) && sBfsar.isOpen())
                SaveFile();

            if (ImGui::IsKeyPressed(ImGuiKey_O))
            {
                if (sBfsar.isOpen())
                    sWantsOpen = true;
                else
                    OpenFile();
            }
        }

        if (ImGui::IsKeyDown(ImGuiKey_ModCtrl) && ImGui::IsKeyDown(ImGuiKey_ModShift))
        {
            if (ImGui::IsKeyPressed(ImGuiKey_N))
            {
                if (sBfsar.isOpen())
                    sWantsNew = true;
                else
                    sNeedsNewFileFormat = true;
            }

            if (ImGui::IsKeyPressed(ImGuiKey_S) && sBfsar.isOpen())
                SaveFileAs();
        }
    }

    ImGuiID dockspaceId = DockSpaceOverViewport();

    DrawProjectUI();
    DrawInfoUI();
    DrawSubInfoUI();
    DrawFileUI(dockspaceId);
    DrawPropertiesUI();
    DrawPlayerUI();

    if (!sDroppedFilePath.isEmpty())
    {
        if (!ImGui::IsPopupOpen("###Save"))
        {
            sead::GameFrameworkBaseGlfw* fw = sead::DynamicCast<sead::GameFrameworkBaseGlfw>(util::getFramework());
            if (fw)
            {
                glfwFocusWindow(fw->getWindowHandle());
            }
        }

        if (sBfsar.isOpen())
        {
            sWantsOpen = true; // open with save prompt
        }
        else
        {
            OpenFile(); // open directly
        }
    }

    static bool (*sFileAction)() = nullptr;
    if (sWantsNew)
    {
        sWantsNew = false;
        if (!gUnsavedChanges)
        {
            NewFileFormatTrigger();
        }
        else
        {
            ImGui::OpenPopup("###Save");
            sFileAction = &NewFileFormatTrigger;
        }
    }
    else if (sWantsOpen)
    {
        sWantsOpen = false;
        if (!gUnsavedChanges)
        {
            OpenFile();
        }
        else
        {
            ImGui::OpenPopup("###Save");
            sFileAction = &OpenFile;
        }
    }
    else if (sWantsClose)
    {
        sWantsClose = false;
        if (!gUnsavedChanges)
        {
            CloseFile();
        }
        else
        {
            ImGui::OpenPopup("###Save");
            sFileAction = &CloseFile;
        }
    }
    else if (sWantsExit)
    {
        sWantsExit = false;
        if (!gUnsavedChanges)
        {
            Exit();
        }
        else
        {
            ImGui::OpenPopup("###Save");
            sFileAction = &Exit;
        }
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal(ICON_LC_SAVE " Save ?###Save", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Do you want to save the current file ?");
        ImGui::Separator();

        ImVec2 buttonSize((ImGui::GetWindowContentRegionMax().x - ImGui::GetStyle().WindowPadding.x * 3.0f) / 3.0f, 0.0f);

        if (ImGui::Button("Yes", buttonSize))
        {
            if (SaveFile())
            {
                sFileAction();
            }
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();

        if (ImGui::Button("No", buttonSize))
        {
            sFileAction();
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();

        if (ImGui::Button("Cancel", buttonSize))
        {
            ImGui::CloseCurrentPopup();

            sDroppedFilePath.clear(); // clear dropped file path to prevent multiple popups
        }

        ImGui::EndPopup();
    }

    if (sNeedsNewFileFormat)
    {
        ImGui::OpenPopup("###NewFileFormat");
        sNeedsNewFileFormat = false;
    }

    {
        ImVec2 c = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(c, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

        if (ImGui::BeginPopupModal("New Archive###NewFileFormat", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Select the archive format:");
            ImGui::Separator();

            ImVec2 buttonSize((ImGui::GetWindowContentRegionMax().x - ImGui::GetStyle().WindowPadding.x * 3.0f) / 2.0f, 0.0f);

            if (ImGui::Button("BFSAR", buttonSize))
            {
                NewFile(ArchiveFormat::BFSAR);
                ImGui::CloseCurrentPopup();
            }

            ImGui::SameLine();

            if (ImGui::Button("BCSAR", buttonSize))
            {
                NewFile(ArchiveFormat::BCSAR);
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }

    if (sShowSystemWindow)
    {
        if (ImGui::Begin("System", &sShowSystemWindow))
        {
            HeapInfo();

            if (sead::ProcessMeter::instance())
            {
                bool isEnable = sead::ProcessMeter::instance()->isVisible();
                if (ImGui::Checkbox("ProcessMeter", &isEnable))
                    sead::ProcessMeter::instance()->setVisible(isEnable);
            }
        }
        ImGui::End();
    }

    if (sShowDemoWindow)
        ImGui::ShowDemoWindow(&sShowDemoWindow);

    if (sWantsAbout)
    {
        ImGui::OpenPopup("##TuneBloomSplash");
        sWantsAbout = false;
    }

    if (ImGui::IsPopupOpen("##TuneBloomSplash"))
    {
        ImTextureID icon = 0;
        if (util::getIcon())
        {
            sead::TextureGL* tex = sead::DynamicCast<sead::TextureGL>(util::getIcon());
            if (tex)
            {
                icon = reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(tex->getID()));
            }
        }

        DrawTuneBloomSplash(icon, ImVec2(130, 130));
    }

    DrawExportDialog();
    DrawFileExportDialogs();
    DrawExportConfirmPopup();
}

static void BuildDefaultExportPath(sead::BufferedSafeString* outPath, const Sound* sound, const char* ext)
{
    const char* rawName = sound->getName().cstr();
    sead::FixedSafeString<256> upperName;
    upperName.format("%s", rawName);
    for (s32 i = 0; i < upperName.calcLength(); i++)
        upperName.getBuffer()[i] = ::toupper((unsigned char)upperName.getBuffer()[i]);

    std::string cwd = std::filesystem::current_path().string();
    outPath->format("%s/%s.%s", cwd.c_str(), upperName.cstr(), ext);
}

static void DecodeWaveFileChannels(const WaveFile* wave, std::vector<std::vector<s16>>& outChannels)
{
    u32 sampleCount = wave->getSampleCount();
    const auto& channels = wave->getChannels();
    u32 numChannels = channels.size();
    WaveFile::Encoding encoding = wave->getEncoding();

    outChannels.resize(numChannels);

    for (u32 ch = 0; ch < numChannels; ch++)
    {
        const WaveFile::Channel* channel = channels.nth(ch);
        outChannels[ch].resize(sampleCount);
        s16* dst = outChannels[ch].data();

        switch (encoding)
        {
            case WaveFile::Encoding::Pcm8:
            {
                const s8* src = static_cast<const s8*>(channel->getData());
                for (u32 i = 0; i < sampleCount; i++)
                    dst[i] = static_cast<s16>(src[i]) << 8;
                break;
            }
            case WaveFile::Encoding::Pcm16:
            {
                const s16* src = static_cast<const s16*>(channel->getData());
                for (u32 i = 0; i < sampleCount; i++)
                    dst[i] = sead::Endian::convertS16(wave->getDataEndian(), sead::Endian::eLittle, src[i]);
                break;
            }
            case WaveFile::Encoding::DspAdpcm:
            {
                ADPCMINFO adpcmInfo;
                sead::MemUtil::fillZero(&adpcmInfo, sizeof(adpcmInfo));
                FillAdpcmInfo(&adpcmInfo, channel->getAdpcmParam(false), channel->getAdpcmLoopParam(false));
                decode(const_cast<u8*>(static_cast<const u8*>(channel->getData())), dst, &adpcmInfo, sampleCount);
                break;
            }
        }
    }
}

static void ApplyLoopAndFade(std::vector<std::vector<s16>>& channels, u32 sampleRate, bool loop, u32 loopCount, float fadeSec)
{
    if (!loop || channels.empty())
        return;

    u32 numChannels = channels.size();
    u32 baseLen = channels[0].size();

    for (u32 ch = 1; ch < numChannels; ch++)
        if (channels[ch].size() > baseLen)
            baseLen = channels[ch].size();

    if (baseLen == 0)
        return;

    u32 fadeSamples = static_cast<u32>(fadeSec * sampleRate);
    u32 totalLen = baseLen * loopCount + sead::Mathu::min(baseLen, fadeSamples);

    for (u32 ch = 0; ch < numChannels; ch++)
    {
        std::vector<s16> original = std::move(channels[ch]);
        channels[ch].resize(totalLen);

        for (u32 loop = 0; loop < loopCount; loop++)
        {
            sead::MemUtil::copy(&channels[ch][loop * baseLen], original.data(), baseLen * sizeof(s16));
        }

        s32 fadeStart = static_cast<s32>(baseLen * loopCount);
        fadeSamples = sead::Mathu::min(fadeSamples, totalLen - fadeStart);

        for (u32 i = 0; i < fadeSamples; i++)
        {
            float t = static_cast<float>(i) / fadeSamples;
            float gain = 1.0f - t;
            channels[ch][fadeStart + i] = static_cast<s16>(original[i] * gain);
        }
    }
}

static void ResampleChannels(std::vector<std::vector<s16>>& channels, u32 srcRate, u32 dstRate)
{
    if (srcRate == dstRate || channels.empty())
        return;

    u32 numChannels = channels.size();
    u32 srcLen = channels[0].size();
    u32 dstLen = static_cast<u32>((static_cast<u64>(srcLen) * dstRate) / srcRate);
    if (dstLen < 1) dstLen = 1;

    for (u32 ch = 0; ch < numChannels; ch++)
    {
        std::vector<s16> src = std::move(channels[ch]);
        channels[ch].resize(dstLen);

        for (u32 i = 0; i < dstLen; i++)
        {
            f32 pos = (static_cast<f32>(i) * srcLen) / dstLen;
            u32 idx = static_cast<u32>(pos);
            f32 frac = pos - idx;

            s16 s0 = src[sead::Mathu::min(idx, srcLen - 1)];
            s16 s1 = src[sead::Mathu::min(idx + 1, srcLen - 1)];
            channels[ch][i] = static_cast<s16>(s0 + (s1 - s0) * frac);
        }
    }
}

static bool WriteWavCustom(const sead::SafeString& path, u32 sampleRate, const std::vector<std::vector<s16>>& channels)
{
    u32 numChannels = channels.size();
    if (numChannels == 0)
        return false;

    u32 sampleCount = channels[0].size();
    for (u32 ch = 1; ch < numChannels; ch++)
        if (channels[ch].size() < sampleCount)
            sampleCount = channels[ch].size();

    sead::FileDevice* device = sead::FileDeviceMgr::instance()->findDevice("native");
    if (!device)
        return false;

    sead::FileHandle handle;
    device->tryOpen(&handle, path, sead::FileDevice::FileOpenFlag::eWriteOnly, 0);
    if (!handle.getDevice())
    {
        device->tryOpen(&handle, path, sead::FileDevice::FileOpenFlag::eCreate, 0);
        if (!handle.getDevice())
            return false;
    }

    sead::FileDeviceWriteStream stream(&handle, sead::Stream::Modes::eBinary);
    stream.setBinaryEndian(sead::Endian::eLittle);

    u32 bitsPerSample = 16;
    u32 blockAlign = numChannels * (bitsPerSample / 8);
    u32 byteRate = sampleRate * blockAlign;
    u32 dataSize = sampleCount * blockAlign;

    stream.writeString("RIFF", 4);
    stream.writeU32(36 + dataSize);
    stream.writeString("WAVE", 4);
    stream.writeString("fmt ", 4);
    stream.writeU32(16);
    stream.writeU16(1);
    stream.writeU16(numChannels);
    stream.writeU32(sampleRate);
    stream.writeU32(byteRate);
    stream.writeU16(blockAlign);
    stream.writeU16(bitsPerSample);
    stream.writeString("data", 4);
    stream.writeU32(dataSize);

    for (u32 i = 0; i < sampleCount; i++)
    {
        for (u32 ch = 0; ch < numChannels; ch++)
        {
            stream.writeS16(channels[ch][i]);
        }
    }

    return true;
}

void DrawExportDialog()
{
    if (!sPendingExport)
        return;

    if (sPendingExportType == Sound::SoundType::Seq)
    {
        ImGui::OpenPopup("Export Sequence Duration");

        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

        if (ImGui::BeginPopupModal("Export Sequence Duration", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Set maximum duration for sequence export:");
            ImGui::Text("The export will stop after 2 loops or this duration, whichever comes first.");
            ImGui::Separator();

            ImGui::SetNextItemWidth(100);
            ImGui::InputInt("min", &sPendingExportDurationMin, 1, 5);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100);
            ImGui::InputInt("sec", &sPendingExportDurationSec, 1, 15);
            if (sPendingExportDurationMin < 0) sPendingExportDurationMin = 0;
            if (sPendingExportDurationMin > 60) sPendingExportDurationMin = 60;
            if (sPendingExportDurationSec < 0) sPendingExportDurationSec = 0;
            if (sPendingExportDurationSec > 59) sPendingExportDurationSec = 59;

            ImGui::Separator();
            ImGui::SetNextItemWidth(140);
            ImGui::Combo("Sample rate", &sSeqSampleRateIdx, sSampleRateItems, IM_ARRAYSIZE(sSampleRateItems));
            ImGui::Separator();

            if (ImGui::Button("Export", ImVec2(120, 0)))
            {
                u32 totalSecs = (u32)(sPendingExportDurationMin * 60 + sPendingExportDurationSec);
                if (totalSecs == 0) totalSecs = 1;

                sead::FixedSafeString<512> path;
                const u32 filterCount = 1;
                FileFilter filters[filterCount] = { { "Wave (*.wav)", "*.wav" } };

                sead::FixedSafeString<512> defaultPath;
                BuildDefaultExportPath(&defaultPath, sPendingExportSound, "wav");

                u32 targetRate = sSampleRateValues[sSeqSampleRateIdx];
                if (SaveFileDialog(&path, nullptr, filterCount, filters, "wav", defaultPath.cstr()))
                {
                    sSoundPlayer.exportSeqToWav(path, sPendingExportSound, totalSecs, targetRate);
                    sPendingExport = false;
                    sPendingExportSound = nullptr;
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
            {
                sPendingExport = false;
                sPendingExportSound = nullptr;
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }
    else if (sPendingExportType == Sound::SoundType::Strm)
    {
        ImGui::OpenPopup("Export Stream Options");

        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

        if (ImGui::BeginPopupModal("Export Stream Options", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Stream export options:");
            ImGui::Separator();

            ImGui::RadioButton("Multi-channel WAV (one file)", &sStrmMultiChannel, 1);
            ImGui::RadioButton("Split per-track WAV files", &sStrmMultiChannel, 0);

            ImGui::Separator();
            ImGui::Checkbox("Loop", &sStrmLoop);

            if (sStrmLoop)
            {
                ImGui::Indent();
                ImGui::SetNextItemWidth(80);
                ImGui::InputInt("Loop count", &sStrmLoopCount, 1, 2);
                if (sStrmLoopCount < 1) sStrmLoopCount = 1;
                if (sStrmLoopCount > 99) sStrmLoopCount = 99;

                ImGui::SetNextItemWidth(80);
                ImGui::InputFloat("Fade (sec)", &sStrmFadeSec, 0.5f, 2.0f, "%.1f");
                if (sStrmFadeSec < 0.0f) sStrmFadeSec = 0.0f;
                ImGui::Unindent();
            }

            ImGui::Separator();
            ImGui::SetNextItemWidth(140);
            ImGui::Combo("Sample rate", &sStrmSampleRateIdx, sSampleRateItems, IM_ARRAYSIZE(sSampleRateItems));
            ImGui::Separator();

            if (ImGui::Button("Export", ImVec2(120, 0)))
            {
                sead::FixedSafeString<512> path;
                const u32 filterCount = 1;
                FileFilter filters[filterCount] = { { "Wave (*.wav)", "*.wav" } };

                sead::FixedSafeString<512> defaultPath;
                BuildDefaultExportPath(&defaultPath, sPendingExportSound, "wav");

                if (SaveFileDialog(&path, nullptr, filterCount, filters, "wav", defaultPath.cstr()))
                {
                    auto& trackList = sPendingExportSound->getStreamSoundInfo().getTrackList();
                    u32 sampleRate = 0;

                    std::vector<std::vector<std::vector<s16>>> trackChannels;
                    for (auto it = trackList.robustBegin(); it != trackList.robustEnd(); ++it)
                    {
                        Sound::StreamSoundInfo::Track* track = static_cast<Sound::StreamSoundInfo::Track*>(it->val());
                        Item* waveItem = track->getWaveFileRef().getItem();
                        if (!waveItem)
                            continue;

                        const WaveFile* wave = static_cast<const WaveFile*>(waveItem);
                        if (sampleRate == 0)
                            sampleRate = wave->getSampleRate();

                        trackChannels.emplace_back();
                        DecodeWaveFileChannels(wave, trackChannels.back());
                    }

                    if (!trackChannels.empty())
                    {
                        u32 targetRate = sSampleRateValues[sStrmSampleRateIdx];
                        if (targetRate == 0)
                            targetRate = sampleRate;

                        std::vector<std::vector<s16>> allChannels;
                        std::vector<u32> trackChannelOffset;

                        for (auto& tc : trackChannels)
                        {
                            trackChannelOffset.push_back(allChannels.size());
                            for (auto& ch : tc)
                                allChannels.push_back(std::move(ch));
                        }

                        if (targetRate != sampleRate)
                            ResampleChannels(allChannels, sampleRate, targetRate);
                        sampleRate = targetRate;

                        if (sStrmLoop)
                        {
                            u32 loopCount = static_cast<u32>(sStrmLoopCount);
                            ApplyLoopAndFade(allChannels, sampleRate, true, loopCount, sStrmFadeSec);
                        }

                        if (sStrmMultiChannel != 0)
                        {
                            if (!WriteWavCustom(path, sampleRate, allChannels))
                            {
                                PopupMgr::instance()->addPopup({ "Failed to write multi-channel WAV file", nullptr });
                            }
                        }
                        else
                        {
                            bool anyFailed = false;
                            u32 trackIdx = 0;
                            const char* dot = strrchr(path.cstr(), '.');
                            for (auto& tc : trackChannels)
                            {
                                u32 offset = trackChannelOffset[trackIdx];
                                std::vector<std::vector<s16>> trackChs;
                                for (u32 i = 0; i < tc.size(); i++)
                                {
                                    if (offset + i < allChannels.size())
                                        trackChs.push_back(allChannels[offset + i]);
                                }

                                sead::FixedSafeString<512> trackPath;
                                if (dot)
                                    trackPath.format("%.*s_track%u%s", (s32)(dot - path.cstr()), path.cstr(), trackIdx, dot);
                                else
                                    trackPath.format("%s_track%u.wav", path.cstr(), trackIdx);

                                if (!WriteWavCustom(trackPath, sampleRate, trackChs))
                                    anyFailed = true;
                                trackIdx++;
                            }
                            if (anyFailed)
                            {
                                PopupMgr::instance()->addPopup({ "Failed to write some per-track WAV files", nullptr });
                            }
                        }

                        sShowExportConfirm = true;
                    }

                    sPendingExport = false;
                    sPendingExportSound = nullptr;
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
            {
                sPendingExport = false;
                sPendingExportSound = nullptr;
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }
    else if (sPendingExportType == Sound::SoundType::Wave)
    {
        sead::FixedSafeString<512> path;

        const u32 filterCount = 1;
        FileFilter filters[filterCount] = {
            { "Wave (*.wav)", "*.wav" }
        };

        sead::FixedSafeString<512> defaultPath;
        BuildDefaultExportPath(&defaultPath, sPendingExportSound, "wav");

        if (SaveFileDialog(&path, nullptr, filterCount, filters, "wav", defaultPath.cstr()))
        {
            Item* waveItem = sPendingExportSound->getWaveSoundInfo().getWaveFileRef().getItem();
            if (waveItem)
                static_cast<WaveFile*>(waveItem)->writeWavFile(path);
        }

        sPendingExport = false;
        sPendingExportSound = nullptr;
    }

}

static void DrawFileExportDialogs()
{
    if (sPendingExportSequenceFile)
    {
        SequenceFile* seq = sPendingExportSequenceFile;
        sPendingExportSequenceFile = nullptr;

        bool isBcsar = sBfsar.getFormat() == ArchiveFormat::BCSAR;
        const char* ext = isBcsar ? "bcseq" : "bfseq";
        const char* name = isBcsar ? "BCSEQ" : "BFSEQ";

        sead::FixedSafeString<512> path;
        sead::FixedSafeString<64> filterName;
        filterName.format("%s file (*.%s)", name, ext);
        sead::FixedSafeString<32> filterPattern;
        filterPattern.format("*.%s", ext);
        const u32 filterCount = 1;
        FileFilter filters[filterCount] = {
            { filterName.cstr(), filterPattern.cstr() }
        };

        sead::FixedSafeString<512> defaultPath;
        {
            const char* rawName = seq->getNameOrNull().cstr();
            std::string cwd = std::filesystem::current_path().string();
            defaultPath.format("%s/%s.%s", cwd.c_str(), rawName, ext);
        }

        if (SaveFileDialog(&path, nullptr, filterCount, filters, ext, defaultPath.cstr()))
        {
            sead::FileDevice* device = sead::FileDeviceMgr::instance()->findDevice("native");
            if (device)
            {
                sead::FileHandle handle;
                device->tryOpen(&handle, path, sead::FileDevice::FileOpenFlag::eCreate, 0);
                if (handle.getDevice())
                {
                    sead::FileDeviceWriteStream stream(&handle, sead::Stream::Modes::eBinary);
                    seq->write(&handle, &stream, sead::Endian::eBig, true);
                }
            }
        }
    }

    if (sPendingExportWaveFile)
    {
        WaveFile* wave = sPendingExportWaveFile;
        sPendingExportWaveFile = nullptr;

        bool isBcsar = sBfsar.getFormat() == ArchiveFormat::BCSAR;
        const char* ext = isBcsar ? "bcwav" : "bfwav";
        const char* name = isBcsar ? "BCWAV" : "BFWAV";

        sead::FixedSafeString<512> path;
        sead::FixedSafeString<64> filterName;
        filterName.format("%s file (*.%s)", name, ext);
        sead::FixedSafeString<32> filterPattern;
        filterPattern.format("*.%s", ext);
        const u32 filterCount = 1;
        FileFilter filters[filterCount] = {
            { filterName.cstr(), filterPattern.cstr() }
        };

        sead::FixedSafeString<512> defaultPath;
        {
            const char* rawName = wave->getNameOrNull().cstr();
            std::string cwd = std::filesystem::current_path().string();
            defaultPath.format("%s/%s.%s", cwd.c_str(), rawName, ext);
        }

        if (SaveFileDialog(&path, nullptr, filterCount, filters, ext, defaultPath.cstr()))
        {
            sead::FileDevice* device = sead::FileDeviceMgr::instance()->findDevice("native");
            if (device)
            {
                sead::FileHandle handle;
                device->tryOpen(&handle, path, sead::FileDevice::FileOpenFlag::eCreate, 0);
                if (handle.getDevice())
                {
                    sead::FileDeviceWriteStream stream(&handle, sead::Stream::Modes::eBinary);
                    wave->write(&handle, &stream, sead::Endian::eBig, true);
                }
            }
        }
    }

    if (sPendingExportBankBundle)
    {
        BankFile* bank = sPendingExportBankBundle;
        sPendingExportBankBundle = nullptr;

        const char* ext = "bbnk";

        sead::FixedSafeString<512> path;
        sead::FixedSafeString<64> filterName;
        filterName.format("Bank Bundle (*.%s)", ext);
        sead::FixedSafeString<32> filterPattern;
        filterPattern.format("*.%s", ext);
        const u32 filterCount = 1;
        FileFilter filters[filterCount] = {
            { filterName.cstr(), filterPattern.cstr() }
        };

        sead::FixedSafeString<512> defaultPath;
        {
            const char* rawName = bank->getNameOrNull().cstr();
            std::string cwd = std::filesystem::current_path().string();
            defaultPath.format("%s/%s.%s", cwd.c_str(), rawName, ext);
        }

        if (SaveFileDialog(&path, nullptr, filterCount, filters, ext, defaultPath.cstr()))
        {
            sead::FileDevice* device = sead::FileDeviceMgr::instance()->findDevice("native");
            if (device)
            {
                sead::FileHandle handle;
                device->tryOpen(&handle, path, sead::FileDevice::FileOpenFlag::eCreate, 0);
                if (handle.getDevice())
                {
                    // Collect unique wave files referenced by the bank
                    std::vector<const WaveFile*> referencedWaves;
                    std::unordered_set<const WaveFile*> seenWaves;

                    for (const Item* instrItem : bank->getInstrumentList())
                    {
                        const auto* instr = static_cast<const BankFile::Instrument*>(instrItem);
                        for (const Item* krItem : instr->getKeyRegionList())
                        {
                            const auto* kr = static_cast<const BankFile::KeyRegion*>(krItem);
                            for (const Item* vrItem : kr->getVelocityRegionList())
                            {
                                const auto* vr = static_cast<const BankFile::VelocityRegion*>(vrItem);
                                const Item* waveItem = vr->getWaveFileRef().getItem();
                                if (waveItem)
                                {
                                    const WaveFile* wave = static_cast<const WaveFile*>(waveItem);
                                    if (!seenWaves.contains(wave))
                                    {
                                        seenWaves.insert(wave);
                                        referencedWaves.push_back(wave);
                                    }
                                }
                            }
                        }
                    }

                    sead::FileDeviceWriteStream stream(&handle, sead::Stream::Modes::eBinary);
                    stream.setBinaryEndian(sead::Endian::eBig);

                    // Magic: "BBND"
                    stream.writeU32(0x42424E44);
                    // Version
                    stream.writeU32(1);

                    // Bank name
                    const char* bankName = bank->getNameOrNull().cstr();
                    u32 nameLen = strlen(bankName);
                    stream.writeU32(nameLen);
                    stream.writeMemBlock(bankName, nameLen);

                    // Wave count
                    stream.writeU32(referencedWaves.size());

                    // Write each wave binary via temp file
                    for (const WaveFile* wave : referencedWaves)
                    {
                        const char* waveName = wave->getNameOrNull().cstr();
                        u32 waveNameLen = strlen(waveName);
                        stream.writeU32(waveNameLen);
                        stream.writeMemBlock(waveName, waveNameLen);

                        static u64 sTempCounter = 0;
                        std::string tmpDir = std::filesystem::temp_directory_path().string();
                        sead::FixedSafeString<512> tempPath;
                        tempPath.format("%s/tb_bbnk_%llu.tmp",
                                        tmpDir.c_str(), (unsigned long long)sTempCounter++);

                        // Write wave to temp file
                        sead::FileHandle tempHandle;
                        if (device->tryOpen(&tempHandle, tempPath, sead::FileDevice::FileOpenFlag::eCreate, 0))
                        {
                            sead::FileDeviceWriteStream tempStream(&tempHandle, sead::Stream::Modes::eBinary);
                            wave->write(&tempHandle, &tempStream, sead::Endian::eBig, true);
                            tempHandle.close();

                            // Read temp file back
                            sead::FileDevice::LoadArg loadArg;
                            loadArg.path = tempPath;
                            u8* waveBuf = device->tryLoad(loadArg);
                            if (waveBuf)
                            {
                                u32 waveSize = loadArg.read_size;
                                stream.writeU32(waveSize);
                                stream.writeMemBlock(waveBuf, waveSize);
                                device->unload(waveBuf);
                            }
                            else
                            {
                                stream.writeU32(0);
                            }

                            ::remove(tempPath.cstr());
                        }
                        else
                        {
                            stream.writeU32(0);
                        }
                    }

                    // Instrument count
                    stream.writeU32(bank->getInstrumentList().size());
                    for (const Item* instrItem : bank->getInstrumentList())
                    {
                        const auto* instr = static_cast<const BankFile::Instrument*>(instrItem);
                        stream.writeU16(instr->getProgramNo());
                        stream.writeU16(instr->getKeyRegionList().size());

                        for (const Item* krItem : instr->getKeyRegionList())
                        {
                            const auto* kr = static_cast<const BankFile::KeyRegion*>(krItem);
                            stream.writeU8(kr->getKeyMin());
                            stream.writeU8(kr->getKeyMax());
                            stream.writeU16(kr->getVelocityRegionList().size());

                            for (const Item* vrItem : kr->getVelocityRegionList())
                            {
                                const auto* vr = static_cast<const BankFile::VelocityRegion*>(vrItem);

                                const Item* refWaveItem = vr->getWaveFileRef().getItem();
                                const char* refWaveName = refWaveItem ? refWaveItem->getNameOrNull().cstr() : "";
                                u32 refNameLen = strlen(refWaveName);
                                stream.writeU32(refNameLen);
                                stream.writeMemBlock(refWaveName, refNameLen);

                                stream.writeU8(vr->getVelocityMin());
                                stream.writeU8(vr->getVelocityMax());
                                stream.writeU8(vr->getOriginalKey());
                                stream.writeU8(vr->getVolume());
                                stream.writeU8(vr->getPan());
                                stream.writeF32(vr->getPitch());
                                stream.writeU8(vr->getIsIgnoreNoteOff() ? 1 : 0);
                                stream.writeU8(vr->getKeyGroup());
                                stream.writeU8(vr->getInterpolationType());
                                const auto& adsr = vr->getAdshrCurve();
                                stream.writeU8(adsr.attack);
                                stream.writeU8(adsr.decay);
                                stream.writeU8(adsr.sustain);
                                stream.writeU8(adsr.hold);
                                stream.writeU8(adsr.release);
                            }
                        }
                    }

                    handle.close();
                }
            }
        }
    }

    if (sPendingImportSequenceFile)
    {
        sPendingImportSequenceFile = false;

        bool isBcsar = sBfsar.getFormat() == ArchiveFormat::BCSAR;
        const char* ext = isBcsar ? "bcseq" : "bfseq";
        const char* name = isBcsar ? "BCSEQ" : "BFSEQ";

        sead::FixedSafeString<512> path;
        sead::FixedSafeString<64> filterName;
        filterName.format("%s file (*.%s)", name, ext);
        sead::FixedSafeString<32> filterPattern;
        filterPattern.format("*.%s", ext);
        const u32 filterCount = 1;
        FileFilter filters[filterCount] = {
            { filterName.cstr(), filterPattern.cstr() }
        };

        if (OpenFileDialog(&path, nullptr, filterCount, filters))
        {
            sead::FileDevice* device = sead::FileDeviceMgr::instance()->findDevice("native");
            if (device)
            {
                sead::FileDevice::LoadArg arg;
                arg.path = path;
                u8* fileData = device->tryLoad(arg);
                if (fileData)
                {
                    sead::FixedSafeString<256> fileName;
                    sead::Path::getFileName(&fileName, path);
                    s32 dotPos = fileName.rfindIndex(".");
                    if (dotPos != -1)
                        fileName.trim(dotPos);

                    SequenceFile* newSeq = new SequenceFile();
                    newSeq->setEnableName(true);
                    newSeq->getName() = fileName;
                    newSeq->setFormat(sBfsar.getFormat());
                    newSeq->setVersion(sBfsar.getVersionForBfseq());
                    if (newSeq->read(fileData))
                    {
                        newSeq->setFormat(sBfsar.getFormat());
                        Item* insertAfter = GetInsertAfterItem();
                        if (insertAfter)
                            insertAfter->insertBack(newSeq);
                        else
                            sBfsar.getSequenceFileList().pushBack(newSeq);
                        ClearInsertAfterItem();
                        sBfsar.updateList(sBfsar.getSequenceFileList());
                        SetUnsavedChanges(true);
                        SelectItem(newSeq);
                    }
                    else
                    {
                        delete newSeq;
                    }

                    device->unload(fileData);
                }
            }
        }
    }

    if (sPendingImportBankBundle)
    {
        sPendingImportBankBundle = false;

        const char* ext = "bbnk";

        sead::FixedSafeString<512> path;
        sead::FixedSafeString<64> filterName;
        filterName.format("Bank Bundle (*.%s)", ext);
        sead::FixedSafeString<32> filterPattern;
        filterPattern.format("*.%s", ext);
        const u32 filterCount = 1;
        FileFilter filters[filterCount] = {
            { filterName.cstr(), filterPattern.cstr() }
        };

        if (OpenFileDialog(&path, nullptr, filterCount, filters))
        {
            sead::FileDevice* device = sead::FileDeviceMgr::instance()->findDevice("native");
            if (device)
            {
                sead::FileDevice::LoadArg loadArg;
                loadArg.path = path;
                u8* fileData = device->tryLoad(loadArg);
                if (fileData)
                {
                    u32 offset = 0;

                    auto readU32 = [&]() -> u32
                    {
                        u32 val;
                        sead::MemUtil::copy(&val, fileData + offset, 4);
                        offset += 4;
                        return sead::Endian::toHostU32(sead::Endian::eBig, val);
                    };

                    auto readU16 = [&]() -> u16
                    {
                        u16 val;
                        sead::MemUtil::copy(&val, fileData + offset, 2);
                        offset += 2;
                        return sead::Endian::toHostU16(sead::Endian::eBig, val);
                    };

                    auto readU8 = [&]() -> u8
                    {
                        return fileData[offset++];
                    };

                    auto readF32 = [&]() -> f32
                    {
                        u32 val;
                        sead::MemUtil::copy(&val, fileData + offset, 4);
                        offset += 4;
                        val = sead::Endian::toHostU32(sead::Endian::eBig, val);
                        f32 result;
                        sead::MemUtil::copy(&result, &val, 4);
                        return result;
                    };

                    auto readString = [&]() -> std::string
                    {
                        u32 len = readU32();
                        std::string s;
                        if (len > 0)
                            s.assign(reinterpret_cast<const char*>(fileData + offset), len);
                        offset += len;
                        return s;
                    };

                    // Verify magic
                    u32 magic = readU32();
                    if (magic != 0x42424E44)
                    {
                        PopupMgr::instance()->pushCurrentItemError("Invalid bank bundle file");
                        device->unload(fileData);
                        return;
                    }

                    // Version
                    u32 version = readU32();
                    if (version != 1)
                    {
                        PopupMgr::instance()->pushCurrentItemError("Unsupported bank bundle version");
                        device->unload(fileData);
                        return;
                    }

                    // Bank name
                    std::string bankName = readString();

                    // Read waves
                    u32 waveCount = readU32();
                    std::unordered_map<std::string, WaveFile*> waveMap;

                    for (u32 i = 0; i < waveCount; i++)
                    {
                        std::string waveName = readString();
                        u32 waveSize = readU32();

                        if (waveSize == 0)
                            continue;

                        // Version-patch the wave binary to match current archive
                        {
                            nw::ut::BinaryFileHeader* hdr = reinterpret_cast<nw::ut::BinaryFileHeader*>(fileData + offset);
                            u16 bom;
                            sead::MemUtil::copy(&bom, fileData + offset + 4, 2);
                            bool le = *reinterpret_cast<u8*>(&bom) == 0xFF;
                            u32 ver = sBfsar.getVersionForBfwav();
                            if (le)
                            {
                                fileData[offset + 8] = (ver >> 0) & 0xFF;
                                fileData[offset + 9] = (ver >> 8) & 0xFF;
                                fileData[offset + 10] = (ver >> 16) & 0xFF;
                                fileData[offset + 11] = (ver >> 24) & 0xFF;
                            }
                            else
                            {
                                fileData[offset + 8] = (ver >> 24) & 0xFF;
                                fileData[offset + 9] = (ver >> 16) & 0xFF;
                                fileData[offset + 10] = (ver >> 8) & 0xFF;
                                fileData[offset + 11] = (ver >> 0) & 0xFF;
                            }
                            // Fix signature
                            const char* wantSig = sBfsar.getFormat() == ArchiveFormat::BCSAR ? "CWAV" : "FWAV";
                            hdr->signature[0] = wantSig[0];
                            hdr->signature[1] = wantSig[1];
                            hdr->signature[2] = wantSig[2];
                            hdr->signature[3] = wantSig[3];
                        }

                        // Check if a wave with this name already exists
                        WaveFile* existingWave = nullptr;
                        for (const auto& node : sBfsar.getWaveFileList())
                        {
                            if (node->getName() == waveName.c_str())
                            {
                                existingWave = static_cast<WaveFile*>(node);
                                break;
                            }
                        }

                        if (existingWave)
                        {
                            waveMap[waveName.c_str()] = existingWave;
                            offset += waveSize;
                            continue;
                        }

                        WaveFile* newWave = new WaveFile();
                        newWave->setEnableName(true);
                        newWave->getName() = waveName.c_str();
                        if (newWave->read(fileData + offset))
                        {
                            newWave->setFormat(sBfsar.getFormat());
                            newWave->setVersion(sBfsar.getVersionForBfwav());
                            sBfsar.getWaveFileList().pushBack(newWave);
                            sBfsar.updateList(sBfsar.getWaveFileList());
                            SetUnsavedChanges(true);
                            waveMap[waveName.c_str()] = newWave;
                        }
                        else
                        {
                            delete newWave;
                            PopupMgr::instance()->pushCurrentItemError("Failed to read bundled wave file");
                        }

                        offset += waveSize;
                    }

                    // Read instruments
                    if (offset >= loadArg.read_size)
                    {
                        PopupMgr::instance()->pushCurrentItemError("Truncated bank bundle");
                        device->unload(fileData);
                        return;
                    }

                    u32 instrumentCount = readU32();
                    if (instrumentCount == 0)
                    {
                        PopupMgr::instance()->pushCurrentItemError("Bank bundle has no instruments");
                        device->unload(fileData);
                        return;
                    }

                    BankFile* newBank = new BankFile();
                    newBank->setEnableName(true);
                    newBank->getName() = bankName.c_str();
                    newBank->setup(sBfsar.getEndian(), sBfsar.getFormat());

                    for (u32 i = 0; i < instrumentCount; i++)
                    {
                        s16 programNo = readU16();
                        u16 krCount = readU16();

                        BankFile::Instrument* instr = new BankFile::Instrument();
                        instr->setProgramNo(programNo);

                        for (u16 k = 0; k < krCount; k++)
                        {
                            u8 keyMin = readU8();
                            u8 keyMax = readU8();
                            u16 vrCount = readU16();

                            BankFile::KeyRegion* kr = new BankFile::KeyRegion(keyMin, keyMax);

                            for (u16 v = 0; v < vrCount; v++)
                            {
                                std::string refWaveName = readString();
                                u8 velMin = readU8();
                                u8 velMax = readU8();
                                u8 originalKey = readU8();
                                u8 volume = readU8();
                                u8 pan = readU8();
                                f32 pitch = readF32();
                                bool ignoreNoteOff = readU8() != 0;
                                u8 keyGroup = readU8();
                                u8 interpolationType = readU8();
                                u8 attack = readU8();
                                u8 decay = readU8();
                                u8 sustain = readU8();
                                u8 hold = readU8();
                                u8 release = readU8();

                                BankFile::VelocityRegion* vr = new BankFile::VelocityRegion(velMin, velMax);
                                vr->setOriginalKey(originalKey);
                                vr->setVolume(volume);
                                vr->setPan(pan);
                                vr->setPitch(pitch);
                                vr->setIsIgnoreNoteOff(ignoreNoteOff);
                                vr->setKeyGroup(keyGroup);
                                vr->setInterpolationType(interpolationType);

                                snd::AdshrCurve adsr;
                                adsr.attack = attack;
                                adsr.decay = decay;
                                adsr.sustain = sustain;
                                adsr.hold = hold;
                                adsr.release = release;
                                vr->setAdshrCurve(adsr);

                                // Attach wave reference
                                WaveFile* foundWave = nullptr;
                                auto waveIt = waveMap.find(refWaveName.c_str());
                                if (waveIt != waveMap.end())
                                {
                                    foundWave = waveIt->second;
                                }
                                else if (!refWaveName.empty())
                                {
                                    for (const auto& node : sBfsar.getWaveFileList())
                                    {
                                        if (node->getName() == refWaveName.c_str())
                                        {
                                            foundWave = static_cast<WaveFile*>(node);
                                            break;
                                        }
                                    }
                                }
                                if (foundWave)
                                {
                                    vr->getWaveFileRef().attach(foundWave);
                                }

                                kr->getVelocityRegionList().pushBack(vr);
                            }

                            instr->getKeyRegionList().pushBack(kr);
                        }

                        newBank->getInstrumentList().pushBack(instr);
                    }

                    sBfsar.getBankFileList().pushBack(newBank);
                    sBfsar.updateList(sBfsar.getBankFileList());
                    SetUnsavedChanges(true);
                    SelectItem(newBank);

                    device->unload(fileData);
                }
            }
        }
    }

    if (sPendingExportWaveToWav)
    {
        WaveFile* wave = sPendingExportWaveToWav;
        sPendingExportWaveToWav = nullptr;

        sead::FixedSafeString<512> defaultPath;
        {
            const char* rawName = wave->getNameOrNull().cstr();
            std::string cwd = std::filesystem::current_path().string();
            defaultPath.format("%s/%s.wav", cwd.c_str(), rawName);
        }

        sead::FixedSafeString<512> path;
        FileFilter filters[] = {
            { "Wave (*.wav)", "*.wav" }
        };

        if (SaveFileDialog(&path, nullptr, 1, filters, "wav", defaultPath.cstr()))
        {
            if (!wave->writeWavFile(path))
            {
                PopupMgr::instance()->addPopup({ "Failed to write WAV file", nullptr });
            }
        }
    }

    if (sPendingExportMidiSound)
    {
        Sound* sound = sPendingExportMidiSound;
        sPendingExportMidiSound = nullptr;

        sead::FixedSafeString<512> path;
        const u32 filterCount = 1;
        FileFilter filters[filterCount] = { { "MIDI (*.midi)", "*.midi" } };

        sead::FixedSafeString<512> defaultPath;
        BuildDefaultExportPath(&defaultPath, sound, "midi");

        if (SaveFileDialog(&path, nullptr, filterCount, filters, "midi", defaultPath.cstr()))
        {
            if (!exportSeqToMidi(path, *sound))
            {
                PopupMgr::instance()->addPopup({ "Failed to export MIDI file", nullptr });
            }
        }
    }
}

void DrawExportConfirmPopup()
{
    if (!sShowExportConfirm)
        return;

    ImGui::OpenPopup("Export Complete");

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("Export Complete", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Stream exported successfully.");
        ImGui::Separator();

        if (ImGui::Button("OK", ImVec2(120, 0)))
        {
            sShowExportConfirm = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void DrawProjectUI()
{
    if (ImGui::Begin(ICON_LC_FOLDER " Project###ProjectWindow"))
    {
        bool selected = false;

        if (ImGui::Selectable(ICON_LC_INFO " Project Info", sSelectedUIType == UIType::ProjectInfo))
        {
            sSelectedUIType = UIType::ProjectInfo;
            selected = true;
        }

        ImGui::Separator();

        if (ImGui::Selectable(ICON_LC_LIST_MUSIC " All Sounds", sSelectedUIType == UIType::AllSounds))
        {
            sSelectedUIType = UIType::AllSounds;
            selected = true;
        }

        if (ImGui::Selectable(ICON_LC_DISC_3 " Stream Sounds", sSelectedUIType == UIType::StreamSounds, ImGuiSelectableFlags_SpanAvailWidth))
        {
            sSelectedUIType = UIType::StreamSounds;
            selected = true;
        }

        if (ImGui::Selectable(ICON_LC_AUDIO_LINES " Wave Sounds", sSelectedUIType == UIType::WaveSounds))
        {
            sSelectedUIType = UIType::WaveSounds;
            selected = true;
        }

        if (ImGui::Selectable(ICON_LC_MUSIC_3 " Sequence Sounds", sSelectedUIType == UIType::SequenceSounds))
        {
            sSelectedUIType = UIType::SequenceSounds;
            selected = true;
        }

        ImGui::Separator();

        if (ImGui::Selectable(ICON_LC_MUSIC_4 " All Sound Sets", sSelectedUIType == UIType::AllSoundSets))
        {
            sSelectedUIType = UIType::AllSoundSets;
            selected = true;
        }

        if (ImGui::Selectable(ICON_LC_AUDIO_LINES " Wave Sound Sets", sSelectedUIType == UIType::WaveSoundSets))
        {
            sSelectedUIType = UIType::WaveSoundSets;
            selected = true;
        }

        if (ImGui::Selectable(ICON_LC_MUSIC_2 " Sequence Sound Sets", sSelectedUIType == UIType::SequenceSoundSets))
        {
            sSelectedUIType = UIType::SequenceSoundSets;
            selected = true;
        }

        ImGui::Separator();

        if (ImGui::Selectable(ICON_LC_PIANO " Banks", sSelectedUIType == UIType::Banks))
        {
            sSelectedUIType = UIType::Banks;
            selected = true;
        }

        if (ImGui::Selectable(ICON_LC_FILE_MUSIC " Wave Archives", sSelectedUIType == UIType::WaveArchives))
        {
            sSelectedUIType = UIType::WaveArchives;
            selected = true;
        }

        if (ImGui::Selectable(ICON_LC_FOLDERS " Groups", sSelectedUIType == UIType::Groups))
        {
            sSelectedUIType = UIType::Groups;
            selected = true;
        }

        if (ImGui::Selectable(ICON_LC_VOLUME_2 " Players", sSelectedUIType == UIType::Players))
        {
            sSelectedUIType = UIType::Players;
            selected = true;
        }

        ImGui::Separator();

        if (ImGui::Selectable(ICON_LC_AUDIO_LINES " Wave Files", sSelectedUIType == UIType::WaveFiles))
        {
            sSelectedUIType = UIType::WaveFiles;
            selected = true;
        }

        if (ImGui::Selectable(ICON_LC_MUSIC_3 " Sequence Files", sSelectedUIType == UIType::SequenceFiles))
        {
            sSelectedUIType = UIType::SequenceFiles;
            selected = true;
        }

        if (ImGui::Selectable(ICON_LC_PIANO " Bank Files", sSelectedUIType == UIType::BankFiles))
        {
            sSelectedUIType = UIType::BankFiles;
            selected = true;
        }

        ImGui::Separator();

        if (ImGui::Selectable(ICON_LC_CHART_AREA " File Statistics", sSelectedUIType == UIType::FileStatistics, ImGuiSelectableFlags_SpanAvailWidth))
        {
            sSelectedUIType = UIType::FileStatistics;
            selected = true;
        }

        if (ImGui::IsWindowFocused())
        {
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
            {
                if (sSelectedUIType > UIType::Min)
                {
                    sSelectedUIType = UIType(u32(sSelectedUIType) - 1);
                }
            }

            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
            {
                if (sSelectedUIType < UIType::Max)
                {
                    sSelectedUIType = UIType(u32(sSelectedUIType) + 1);
                }
            }
        }

        if (selected)
        {
            ImGuiWindow* w = ImGui::FindWindowByName("###InfoWindow");
            if (w && w->DockIsActive && w->Hidden)
            {
                ImGui::SetWindowFocus("###InfoWindow");
            }
        }
    }
    ImGui::End();
}

struct TabFilterState
{
    sead::FixedSafeString<256> filter;
    bool active = false;
    bool caseSensitive = false;
};

static TabFilterState sTabFilterState[(size_t)UIType::Max + 1];

static TabFilterState& GetCurrentTabFilter()
{
    return sTabFilterState[(size_t)sSelectedUIType];
}

void CloseFilter()
{
    TabFilterState& state = GetCurrentTabFilter();
    state.filter.clear();
    state.active = false;
}

bool ItemMatchesFilter(const Item* item)
{
    TabFilterState& state = GetCurrentTabFilter();

    if (state.filter.isEmpty())
    {
        return true;
    }

    std::string name = item->getFormattedName().cstr();
    std::string filter = state.filter.cstr();

    if (!state.caseSensitive)
    {
        for (char& c : name)
        {
            c = std::tolower(c);
        }

        for (char& c : filter)
        {
            c = std::tolower(c);
        }
    }

    return name.find(filter) != std::string::npos;
}

ItemFilterCallback GetItemFilterCallback()
{
    TabFilterState& state = GetCurrentTabFilter();
    return !state.filter.isEmpty() ? &ItemMatchesFilter : nullptr;
}

void DrawInfoUI()
{
    bool notProjUI = sSelectedUIType != UIType::ProjectInfo;
    if (notProjUI)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    }

    bool windowOpen = ImGui::Begin(ICON_LC_PEN " Info###InfoWindow", nullptr, sSelectedUIType == UIType::ProjectInfo ? ImGuiWindowFlags_NoScrollbar : 0);

    if (notProjUI)
    {
        ImGui::PopStyleVar();
    }

    if (windowOpen)
    {
        if (!sBfsar.isOpen())
        {
            CenteredText("No File Open");

            ImGui::End();
            return;
        }

        TabFilterState& tabFilter = GetCurrentTabFilter();

        bool setFocus = false;
        if (notProjUI && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) && ImGui::IsKeyDown(ImGuiKey_ModCtrl) && ImGui::IsKeyDown(ImGuiKey_F))
        {
            tabFilter.active = true;
            setFocus = true;
        }

        if (notProjUI && tabFilter.active)
        {
            if (ImGui::BeginChild("##Filter", ImVec2(0, 0), ImGuiChildFlags_AlwaysAutoResize | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Border))
            {
                if (setFocus)
                {
                    ImGui::SetKeyboardFocusHere();
                }

                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("   ").x * 2.0f - ImGui::GetStyle().ItemSpacing.x * 2.0f);
                ImGui::InputTextWithHint("##Search", "Search...", tabFilter.filter.getBuffer(), tabFilter.filter.getBufferSize(), ImGuiInputTextFlags_CharsNoBlank);

                ImGui::SameLine();

                bool popColor = false;
                if (!tabFilter.caseSensitive)
                {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.2f, 0.5f));
                    popColor = true;
                }

                if (ImGui::Button(ICON_LC_CASE_SENSITIVE "##CaseSensitive"))
                {
                    tabFilter.caseSensitive = !tabFilter.caseSensitive;
                }

                if (popColor)
                {
                    ImGui::PopStyleColor();
                }

                ImGui::SameLine();

                if (ImGui::Button(ICON_LC_X "##CloseFilter"))
                {
                    CloseFilter();
                }
            }
            ImGui::EndChild();
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

InstanciateItemCallback CreateStreamTrackFunc(bool clear)
{
    static Item* sWaveFileRef = nullptr;

    if (clear)
    {
        //sWaveFileRef = sBfsar.getItem(0, sBfsar.getWaveFileList());
        sWaveFileRef = nullptr;
    }

    ItemSelector("Wave File", sBfsar.getWaveFileList(), &sWaveFileRef, false);

    WarningPopup("###WaveFile", "Select a valid Wave File !");

    auto doCreate = []() -> Item*
    {
        SEAD_ASSERT(sSelectedItem);
        SEAD_ASSERT(sSelectedItem->getItemType() == Item::ItemType::Sound);

        if (!sWaveFileRef)
        {
            ImGui::OpenPopup("###WaveFile");
            return nullptr;
        }

        Sound::StreamSoundInfo::Track* track = new Sound::StreamSoundInfo::Track();
        track->setEnableName(true);
        track->getName() = "Track";

        track->getWaveFileRef().attach(sWaveFileRef);

        return track;
    };

    return doCreate;
}

InstanciateItemCallback CreateGroupItemFunc(bool clear)
{
    static Item::ItemType sItemRefType = Item::ItemType::Invalid;
    static u32 sLoadItem = 0;
    static Item* sItemRef = nullptr;

    static sead::FixedSafeString<256> sError;

    if (clear)
    {
        sItemRefType = Item::ItemType::Invalid;
        sLoadItem = 0;
        sItemRef = nullptr;

        sError.clear();
    }

    {
        u32 itemRefType = (u32)sItemRefType - 1;
        if (ImGui::Combo("Item Type", (s32*)&itemRefType, Group::ItemInfo::sItemIdTypes, IM_ARRAYSIZE(Group::ItemInfo::sItemIdTypes)))
        {
            sItemRefType = static_cast<Item::ItemType>(itemRefType + 1);
            sItemRef = nullptr;

            sLoadItem = 0; // All
        }
    }

    {
        Item* oldItem = sItemRef;
        Item* item = oldItem;
        if (ItemSelector("Item", sBfsar.getItemList(sItemRefType), &item, false))
        {
            sItemRef = item;

            if (oldItem && item)
            {
                switch (sItemRefType)
                {
                    case Item::ItemType::Sound:
                    {
                        Sound* oldSound = static_cast<Sound*>(oldItem);
                        Sound* sound = static_cast<Sound*>(item);
                        if (oldSound->getSoundType() != sound->getSoundType())
                        {
                            sLoadItem = 0; // All
                        }
                    }

                    case Item::ItemType::SoundSet:
                    {
                        SoundSet* oldSoundSet = static_cast<SoundSet*>(oldItem);
                        SoundSet* soundSet = static_cast<SoundSet*>(item);
                        if (oldSoundSet->getSoundSetType() != soundSet->getSoundSetType())
                        {
                            sLoadItem = 0; // All
                        }
                    }

                    default:
                        break;
                }
            }
        }
    }

    {
        u32 itemCount = 0;
        const char** items = Group::ItemInfo::GetLoadItems(sItemRef, sItemRefType, &itemCount);

        bool enable = sItemRef && items;

        u32 oldLoadItem = sLoadItem;
        u32 loadItem = oldLoadItem;
        if (!enable)
        {
            ImGui::BeginDisabled();

            static const char* all = "All";

            itemCount = 1;
            items = &all;
            oldLoadItem = 0;
            loadItem = 0;
        }

        if (ImGui::Combo("Load Items", (s32*)&loadItem, items, itemCount))
        {
            sLoadItem = loadItem;
        }

        if (!enable)
        {
            ImGui::EndDisabled();
        }
    }

    WarningPopup("###LoadItemAdd", sError.cstr());

    auto doCreate = []() -> Item*
    {
        SEAD_ASSERT(sSelectedItem);
        SEAD_ASSERT(sSelectedItem->getItemType() == Item::ItemType::Group);

        Group::ItemInfo* info = new Group::ItemInfo(static_cast<Group*>(sSelectedItem));
        info->setItemRefType_(sItemRefType);
        info->getItemRef().attach(sItemRef);
        info->setLoadItem_(sLoadItem);

        if (info->validate(sError))
        {
            ImGui::OpenPopup("###LoadItemAdd");
            delete info;
            return nullptr;
        }

        return info;
    };

    return doCreate;
}

const char* GroupItemPrefixFunc(Item* item)
{
    Group::ItemInfo* groupItem = (Group::ItemInfo*)item;

    const char* icon = ICON_LC_FILE_QUESTION " ";

    if (!groupItem->getIsDisabled())
    {
        switch (groupItem->getItemRefType())
        {
            case Item::ItemType::Sound:
                if (groupItem->getItemRef().isAttached())
                {
                    Sound* sound = (Sound*)groupItem->getItemRef().getItem();
                    switch (sound->getSoundType())
                    {
                        case Sound::SoundType::Seq:
                            icon = ICON_LC_MUSIC_3 " ";
                            break;

                        case Sound::SoundType::Strm:
                            icon = ICON_LC_DISC_3 " ";
                            break;

                        case Sound::SoundType::Wave:
                            icon = ICON_LC_AUDIO_LINES " ";
                            break;

                        default:
                            break;
                    }
                }

                break;

            case Item::ItemType::SoundSet:
                if (groupItem->getItemRef().isAttached())
                {
                    SoundSet* soundSet = (SoundSet*)groupItem->getItemRef().getItem();
                    switch (soundSet->getSoundSetType())
                    {
                        case SoundSet::SoundSetType::Wave:
                            icon = ICON_LC_AUDIO_LINES " ";
                            break;

                        case SoundSet::SoundSetType::Seq:
                            icon = ICON_LC_MUSIC_2 " ";
                            break;

                        default:
                            break;
                    }
                }

                break;

            case Item::ItemType::Bank:
                icon = ICON_LC_PIANO " ";
                break;

            case Item::ItemType::WaveArchive:
                icon = ICON_LC_FILE_MUSIC " ";
                break;

            default:
                break;
        }
    }

    return icon;
}

void DrawSubInfoUI()
{
    if (!sBfsar.isOpen())
    {
        return;
    }

    sead::FixedSafeString<512> name(ICON_LC_PEN " ");

    switch (sSelectedUIType)
    {
        case UIType::AllSounds:
        {
            if (!sSelectedItem || sSelectedItem->getItemType() != Item::ItemType::Sound)
            {
                return;
            }

            const Sound* sound = static_cast<const Sound*>(sSelectedItem);
            if (sound->getSoundType() != Sound::SoundType::Strm)
            {
                return;
            }

            name.append("Tracks");
            break;
        }

        case UIType::StreamSounds:
            name.append("Tracks");
            break;

        case UIType::Groups:
            name.append("Items");
            break;

        default:
            return;
    }

    name.append("###SubInfoWindow");

    //ImGui::SetNextWindowDockID();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    //bool windowOpen = ImGui::Begin(name.cstr(), nullptr, ImGuiWindowFlags_NoMove);
    bool windowOpen = ImGui::Begin(name.cstr(), nullptr, ImGuiWindowFlags_NoFocusOnAppearing);
    ImGui::PopStyleVar();

    if (windowOpen)
    {
        ImGuiWindow* win = ImGui::GetCurrentWindow();
        if (win && win->DockNode)
            win->DockNode->LocalFlags &= ~ImGuiDockNodeFlags_NoResize;

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
                            DrawAllItemsUI("Track", sound->getStreamSoundInfo().getTrackList(), &CreateStreamTrackFunc);
                        }
                        else
                        {
                        CenteredText("Selected Sound Is Not A Stream");
                    }
                    break;
                }

                case Item::ItemType::Group:
                {
                    Group* group = static_cast<Group*>(sSelectedItem);

                    DrawAllItemsUI("Item", group->getItemInfoList(), &CreateGroupItemFunc, &GroupItemPrefixFunc);
                    break;
                }
            }
        }
        else
        {
            CenteredText("No Item Selected");
        }
    }

    ImGui::End();
}

static const char* GetItemIcon(const Item* item)
{
    if (item->getItemType() == Item::ItemType::Sound)
    {
        const Sound* sound = static_cast<const Sound*>(item);
        switch (sound->getSoundType())
        {
            case Sound::SoundType::Seq:  return ICON_LC_MUSIC_3 " ";
            case Sound::SoundType::Strm: return ICON_LC_DISC_3 " ";
            case Sound::SoundType::Wave: return ICON_LC_AUDIO_LINES " ";
            default: break;
        }
    }

    switch (item->getItemType())
    {
        case Item::ItemType::SoundSet:     return ICON_LC_MUSIC_4 " ";
        case Item::ItemType::Bank:         return ICON_LC_PIANO " ";
        case Item::ItemType::WaveArchive:  return ICON_LC_FILE_MUSIC " ";
        case Item::ItemType::Group:        return ICON_LC_FOLDERS " ";
        case Item::ItemType::Player:       return ICON_LC_VOLUME_2 " ";
        case Item::ItemType::WaveFile:     return ICON_LC_AUDIO_LINES " ";
        case Item::ItemType::SequenceFile: return ICON_LC_MUSIC_3 " ";
        case Item::ItemType::BankFile:     return ICON_LC_PIANO " ";
        case Item::ItemType::StreamTrack:  return ICON_LC_MUSIC_2 " ";
        default:                           return "";
    }
}

static sead::FixedSafeString<512> BuildRefLabel(const Item* owner)
{
    sead::FixedSafeString<512> label;
    const char* icon = GetItemIcon(owner);

    if (owner->getItemType() == Item::ItemType::BankFileVelocityRegion)
    {
        for (const Item* bfItem : sBfsar.getBankFileList())
        {
            const BankFile* bankFile = static_cast<const BankFile*>(bfItem);
            for (const Item* instItem : bankFile->getInstrumentList())
            {
                const BankFile::Instrument* inst = static_cast<const BankFile::Instrument*>(instItem);
                for (const Item* krItem : inst->getKeyRegionList())
                {
                    const BankFile::KeyRegion* kr = static_cast<const BankFile::KeyRegion*>(krItem);
                    for (const Item* vrItem : kr->getVelocityRegionList())
                    {
                        if (vrItem == owner)
                        {
                            label.appendWithFormat("%s%s \xe2\x86\x92 %s",
                                icon, bfItem->getFormattedName().cstr(),
                                instItem->getFormattedName().cstr());
                            return label;
                        }
                    }
                }
            }
        }
    }

    if (owner->getItemType() == Item::ItemType::StreamTrack)
    {
        Sound::StreamSoundInfo::Track* track = const_cast<Sound::StreamSoundInfo::Track*>(static_cast<const Sound::StreamSoundInfo::Track*>(owner));
        for (const Item* soundItem : sBfsar.getSoundList())
        {
            const Sound* sound = static_cast<const Sound*>(soundItem);
            if (sound->getSoundType() != Sound::SoundType::Strm)
                continue;
            for (const Item* trackItem : sound->getStreamSoundInfo().getTrackList())
            {
                if (trackItem == track)
                {
                    label.appendWithFormat("%s%s - %s",
                        icon, soundItem->getFormattedName().cstr(),
                        owner->getFormattedName().cstr());
                    return label;
                }
            }
        }
    }

    label.appendWithFormat("%s%s", icon, owner->getFormattedName().cstr());
    return label;
}

static bool DrawReferencesUI(Item* item)
{
    ImGui::SeparatorText("References");

    const ItemReference::List& refs = item->getReferences();
    if (refs.size() == 0)
    {
        ImGui::TextDisabled("No references");
        return false;
    }

    struct RefEntry {
        sead::FixedSafeString<512> label;
        Item* navigateItem;
        Item* selectSubItem = nullptr;
    };

    RefEntry entries[1024];
    int entryCount = 0;

    for (auto it = refs.robustBegin(); it != refs.robustEnd(); ++it)
    {
        ItemReference* ref = it->val();
        if (!ref)
            continue;

        Item* owner = ref->getOwner();
        if (!owner)
            continue;

        if (owner->isFileWindow())
            continue;

        sead::FixedSafeString<512> label;

        if (item->getItemType() == Item::ItemType::WaveFile && owner->getItemType() == Item::ItemType::BankFileVelocityRegion)
        {
            bool found = false;
            for (Item* bfItem : sBfsar.getBankFileList())
            {
                BankFile* bankFile = static_cast<BankFile*>(bfItem);
                for (Item* instItem : bankFile->getInstrumentList())
                {
                    BankFile::Instrument* inst = static_cast<BankFile::Instrument*>(instItem);
                    for (Item* krItem : inst->getKeyRegionList())
                    {
                        BankFile::KeyRegion* kr = static_cast<BankFile::KeyRegion*>(krItem);
                        for (Item* vrItem : kr->getVelocityRegionList())
                        {
                            if (vrItem == owner)
                            {
                                for (Item* bankItem : sBfsar.getBankList())
                                {
                                    Bank* bank = static_cast<Bank*>(bankItem);
                                    if (bank->getFileRef().getItem() == bfItem)
                                    {
                                        label.appendWithFormat("%s%s - %s",
                                            ICON_LC_PIANO " ",
                                            bankItem->getFormattedName().cstr(),
                                            instItem->getFormattedName().cstr());
                                        for (int i = 0; i < entryCount; i++)
                                        {
                                            if (entries[i].label == label)
                                                found = true;
                                        }
                                        if (!found && entryCount < 1024)
                                        {
                                            entries[entryCount].label = label;
                                            entries[entryCount].navigateItem = bfItem;
                                            entries[entryCount].selectSubItem = instItem;
                                            entryCount++;
                                        }
                                        found = true;
                                        break;
                                    }
                                }
                                if (found) break;
                            }
                        }
                        if (found) break;
                    }
                    if (found) break;
                }
                if (found) break;
            }
        }
        else
        {
            bool dup = false;

            Item* navigateItem = owner;
            Item* selectSubItem = nullptr;

            if (owner->getItemType() == Item::ItemType::StreamTrack)
            {
                Sound::StreamSoundInfo::Track* track = static_cast<Sound::StreamSoundInfo::Track*>(owner);
                for (Item* soundItem : sBfsar.getSoundList())
                {
                    Sound* sound = static_cast<Sound*>(soundItem);
                    if (sound->getSoundType() != Sound::SoundType::Strm)
                        continue;
                    for (Item* trackItem : sound->getStreamSoundInfo().getTrackList())
                    {
                        if (trackItem == track)
                        {
                            navigateItem = sound;
                            selectSubItem = track;
                            break;
                        }
                    }
                    if (selectSubItem) break;
                }
            }

            label = BuildRefLabel(owner);
            for (int i = 0; i < entryCount; i++)
            {
                if (entries[i].label == label)
                {
                    dup = true;
                    break;
                }
            }
            if (!dup && entryCount < 1024)
            {
                entries[entryCount].label = label;
                entries[entryCount].navigateItem = navigateItem;
                entries[entryCount].selectSubItem = selectSubItem;
                entryCount++;
            }
        }
    }

    bool clicked = false;
    if (ImGui::BeginChild("###ReferencesList", ImVec2(0, ImGui::GetContentRegionAvail().y), ImGuiChildFlags_Border))
    {
        for (int i = 0; i < entryCount; i++)
        {
            if (ImGui::Selectable(entries[i].label.cstr()))
            {
                SelectItem(entries[i].navigateItem);

                if (entries[i].selectSubItem)
                {
                    if (entries[i].selectSubItem->getItemType() == Item::ItemType::BankFileInstrument)
                    {
                        sSelectedItem = entries[i].selectSubItem;
                        sSubSelectedItem = nullptr;
                        OpenFileWindow(entries[i].navigateItem);
                    }
                    else
                    {
                        sSubSelectedItem = entries[i].selectSubItem;
                        sSelectedItemIsSubWindow = true;
                    }
                }

                if (entries[i].navigateItem->getItemType() == Item::ItemType::Sound)
                {
                    const Sound* sound = static_cast<const Sound*>(entries[i].navigateItem);
                    switch (sound->getSoundType())
                    {
                        case Sound::SoundType::Seq:  SetUITab(UIType::SequenceSounds); break;
                        case Sound::SoundType::Strm: SetUITab(UIType::StreamSounds); break;
                        case Sound::SoundType::Wave: SetUITab(UIType::WaveSounds); break;
                        default: break;
                    }

                    size_t newIdx = (size_t)sSelectedUIType;
                    sSelectedItemArr[newIdx] = entries[i].navigateItem;
                    sSubSelectedItemArr[newIdx] = entries[i].selectSubItem;
                    sSelectedItemIsSubWindowArr[newIdx] = entries[i].selectSubItem != nullptr;
                }

                clicked = true;
            }
        }
    }
    ImGui::EndChild();

    return clicked;
}

void DrawPropertiesUI()
{
    sead::FixedSafeString<512> name(ICON_LC_TABLE_PROPERTIES " Properties");

    if (sSelectedItem && sSelectedItem->getItemType() < Item::ItemType::InternalFileTypes)
    {
        name.appendWithFormat(" - %s", sSelectedItem->getFormattedName().cstr());
    }

    name.append("###PropertiesWindow");

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

    if (ImGui::Begin(name.cstr()))
    {
        if (!sSelectedItem)
        {
            CenteredText("No Item Selected");

            ImGui::End();
            return;
        }

        if (sSelectedItemIsSubWindow)
        {
            if (!sSubSelectedItem)
            {
                CenteredText("No Item Selected");

                ImGui::End();
                return;
            }

            switch (sSubSelectedItem->getItemType())
            {
                case Item::ItemType::BankFileVelocityRegion:
                {
                    BankFile::VelocityRegion* velRegion = static_cast<BankFile::VelocityRegion*>(sSubSelectedItem);
                    velRegion->drawUI();
                    break;
                }

                case Item::ItemType::StreamTrack:
                {
                    Sound::StreamSoundInfo::Track* track = static_cast<Sound::StreamSoundInfo::Track*>(sSubSelectedItem);
                    track->drawUI();
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

            ImGui::End();
            return;
        }

        switch (sSelectedItem->getItemType())
        {
            case Item::ItemType::Sound:
            case Item::ItemType::SoundSet:
            case Item::ItemType::Bank:
            case Item::ItemType::WaveArchive:
            case Item::ItemType::Group:
            case Item::ItemType::Player:
                DrawItemPropertiesUI();
                ImGui::Separator();

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

            case Item::ItemType::BankFile:
            {
                BankFile* bankFile = static_cast<BankFile*>(sSelectedItem);
                bankFile->drawUI();
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

        switch (sSelectedItem->getItemType())
        {
            case Item::ItemType::Player:
            case Item::ItemType::WaveFile:
            case Item::ItemType::SequenceFile:
            case Item::ItemType::BankFile:
            case Item::ItemType::Bank:
                DrawReferencesUI(sSelectedItem);
                break;

            default:
                break;
        }
    }
    ImGui::End();
}

void DrawFileUI(ImGuiID dockspaceId)
{
    for (auto it = sFileWindows.robustBegin(); it != sFileWindows.robustEnd(); ++it)
    {
        FileWindow* window = static_cast<FileWindow*>(it->val());
        SEAD_ASSERT(window);

        if (!window->isOpen() || !window->getFileRef().isAttached())
        {
            auto* prevNode = sFileWindows.prev(window);
            if (prevNode)
            {
                FileWindow* prev = static_cast<FileWindow*>(prevNode->val());
                ImGui::SetWindowFocus(sead::FormatFixedSafeString<512>(ICON_LC_FILE " %s###%u",
                    prev->getFileRef().getItem()->getFormattedName().cstr(),
                    prev->getFileRef().getItem()).cstr());
            }
            else
            {
                auto* nextNode = sFileWindows.next(window);
                if (nextNode)
                {
                    FileWindow* next = static_cast<FileWindow*>(nextNode->val());
                    ImGui::SetWindowFocus(sead::FormatFixedSafeString<512>(ICON_LC_FILE " %s###%u",
                        next->getFileRef().getItem()->getFormattedName().cstr(),
                        next->getFileRef().getItem()).cstr());
                }
            }

            delete window;
        }
    }

    for (Item* item : sFileWindows)
    {
        FileWindow* window = static_cast<FileWindow*>(item);

        Item* file = window->getFileRef().getItem();

        ImGui::SetNextWindowDockID(dockspaceId, ImGuiCond_Appearing);

        bool dirty = false;
        ImGuiWindowFlags flags = ImGuiWindowFlags_None;
        if (file->getItemType() == Item::ItemType::SequenceFile)
        {
            SequenceFile* seq = static_cast<SequenceFile*>(file);
            flags = ImGuiWindowFlags_MenuBar;

            dirty = seq->isDirty();
            flags |= dirty ? ImGuiWindowFlags_UnsavedDocument : 0;
        }

        bool visible = ImGui::Begin(sead::FormatFixedSafeString<512>(ICON_LC_FILE " %s###%u", file->getFormattedName().cstr(), file).cstr(), window->getOpenPtr(), flags);

        if (visible && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyDown(ImGuiKey_ModCtrl) && ImGui::IsKeyPressed(ImGuiKey_W) && !dirty)
            *window->getOpenPtr() = false;

        if (!window->isOpen() && dirty)
        {
            *window->getOpenPtr() = true;
            PopupMgr::instance()->addPopup({ "Please compile your Sequence data first" });
        }

        if (visible)
        {
            switch (file->getItemType())
            {
                case Item::ItemType::BankFile:
                {
                    BankFile* bank = static_cast<BankFile*>(file);
                    bank->drawFileUI();
                    break;
                }

                case Item::ItemType::SequenceFile:
                {
                    SequenceFile* seq = static_cast<SequenceFile*>(file);
                    seq->drawFileUI();
                    break;
                }

                default:
                    break;
            }
        }
        ImGui::End();
    }
}

void DrawProjectInfoUI()
{
    const ImU8 stepU8 = 1;
    const ImU16 stepU16 = 1;
    const ImU32 stepU32 = 1;

    {
        static const char* sEndianTypes[] = {
            "Big Endian",
            "Little Endian"
        };

        sead::Endian::Types endian = sBfsar.getEndian();
        if (ImGui::Combo("Byte Order", (s32*)&endian, sEndianTypes, IM_ARRAYSIZE(sEndianTypes)))
        {
            sBfsar.setEndian(endian);
            SetUnsavedChanges(true);
        }
    }

    {
        CenteredTextX("Version");

        u32 version = sBfsar.getVersion();
        if (DrawVersionUI(&version, sBfsar.getFormat() == ArchiveFormat::BCSAR ? 4 : 3))
        {
            sBfsar.setVersion(version);
            SetUnsavedChanges(true);
        }
    }

    //ImGui::SeparatorText("");
    {
        bool stringTable = sBfsar.isIncludeStringTable();
        if (ImGui::Checkbox("Include String Table", &stringTable))
        {
            sBfsar.setIncludeStringTable(stringTable);
            SetUnsavedChanges(true);
        }
    }

    ImGui::SeparatorText("Sound Archive Player");

    {
        SoundArchivePlayerInfo& playerInfo = sBfsar.getSoundArchivePlayerInfo();

        if (ImGui::InputScalar("Sequence Sound Max", ImGuiDataType_U16, &playerInfo.sequenceSoundMax, &stepU16))
        {
            playerInfo.sequenceSoundMax = sead::MathCalcCommon<u16>::clamp2(0, playerInfo.sequenceSoundMax, 999);
            SetUnsavedChanges(true);
        }
        if (ImGui::InputScalar("Sequence Track Max", ImGuiDataType_U16, &playerInfo.sequenceTrackMax, &stepU16))
        {
            playerInfo.sequenceTrackMax = sead::MathCalcCommon<u16>::clamp2(0, playerInfo.sequenceTrackMax, 999);
            SetUnsavedChanges(true);
        }
        if (ImGui::InputScalar("Stream Sound Max", ImGuiDataType_U16, &playerInfo.streamSoundMax, &stepU16))
        {
            playerInfo.streamSoundMax = sead::MathCalcCommon<u16>::clamp2(0, playerInfo.streamSoundMax, 999);
            SetUnsavedChanges(true);
        }
        if (ImGui::InputScalar("Stream Track Max", ImGuiDataType_U16, &playerInfo.streamTrackMax, &stepU16))
        {
            playerInfo.streamTrackMax = sead::MathCalcCommon<u16>::clamp2(0, playerInfo.streamTrackMax, 999);
            SetUnsavedChanges(true);
        }
        if (ImGui::InputScalar("Stream Channel Max", ImGuiDataType_U16, &playerInfo.streamChannelMax, &stepU16))
        {
            playerInfo.streamChannelMax = sead::MathCalcCommon<u16>::clamp2(0, playerInfo.streamChannelMax, 32);
            SetUnsavedChanges(true);
        }
        if (ImGui::InputScalar("Wave Sound Max", ImGuiDataType_U16, &playerInfo.waveSoundMax, &stepU16))
        {
            playerInfo.waveSoundMax = sead::MathCalcCommon<u16>::clamp2(0, playerInfo.waveSoundMax, 999);
            SetUnsavedChanges(true);
        }
        if (ImGui::InputScalar("Wave Track Max", ImGuiDataType_U16, &playerInfo.waveTrackMax, &stepU16))
        {
            playerInfo.waveTrackMax = sead::MathCalcCommon<u16>::clamp2(0, playerInfo.waveTrackMax, 999);
            SetUnsavedChanges(true);
        }

        {
            bool enable = sBfsar.isStreamPrefetchAvailable();
            if (!enable)
                ImGui::BeginDisabled();

            u8 streamBufferTimes = enable ? playerInfo.streamBufferTimes : 0;
            if (ImGui::InputScalar(enable ? "Stream Buffer Times" : "##", ImGuiDataType_U8, &streamBufferTimes, &stepU8))
            {
                streamBufferTimes = sead::MathCalcCommon<u8>::clamp2(1, streamBufferTimes, 4);
                playerInfo.streamBufferTimes = streamBufferTimes;
                SetUnsavedChanges(true);
            }

            if (!enable)
                ImGui::EndDisabled();

            if (!enable)
            {
                ImGui::SameLine();
                HelpMarker("Need version to be >= 0.2.2.0");
            }
        }

        if (ImGui::InputScalar("Options", ImGuiDataType_U32, &playerInfo.options, &stepU32))
            SetUnsavedChanges(true);
    }
}

InstanciateItemCallback CreateSoundFunc(bool clear)
{
    auto innerFunc = [](bool clear, Item* item, bool* validate)
    {
        static Item* sPlayer = nullptr;
        static Sound::SoundType sSoundType = Sound::SoundType::Invalid;

        if (clear)
        {
            // sPlayer = sBfsar.getItem(0, sBfsar.getPlayerList());
            sPlayer = nullptr;
            sSoundType = Sound::SoundType::Invalid;
        }

        if (!item && !validate)
        {
            ImGui::Separator();

            ItemSelector("Player", sBfsar.getPlayerList(), &sPlayer, false);

            static const char* sSoundTypes[] = { "Invalid", "Sequence", "Stream", "Wave" };
            ImGui::Combo("Sound Type", (s32*)&sSoundType, sSoundTypes, IM_ARRAYSIZE(sSoundTypes));

            WarningPopup("###Player", "Select a valid Player !" "\nIf there are none please add one first.");
            WarningPopup("###SoundType", "Select a valid Sound Type !");
        }
        else if (validate)
        {
            if (!sPlayer)
            {
                ImGui::OpenPopup("###Player");
                *validate = false;
            }
            else if (sSoundType == Sound::SoundType::Invalid)
            {
                ImGui::OpenPopup("###SoundType");
                *validate = false;
            }
        }
        else
        {
            Sound* sound = static_cast<Sound*>(item);

            sound->getPlayerRef().attach(sPlayer);
            sound->setSoundType(sSoundType);

            if (sSoundType == Sound::SoundType::Strm)
            {
                sound->setPanMode(snd::PanMode::Balance); // Default for streams
            }
        }
    };

    return CreateItemFunc(clear, []() -> Item* { return new Sound(); }, innerFunc);
}

template <Sound::SoundType FixedType>
static InstanciateItemCallback CreateFixedSoundTypeFunc(bool clear)
{
    auto innerFunc = [](bool clear, Item* item, bool* validate)
    {
        static Item* sPlayer = nullptr;

        if (clear)
        {
            sPlayer = nullptr;
        }

        if (!item && !validate)
        {
            ImGui::Separator();

            ItemSelector("Player", sBfsar.getPlayerList(), &sPlayer, false);

            WarningPopup("###Player", "Select a valid Player !" "\nIf there are none please add one first.");
        }
        else if (validate)
        {
            if (!sPlayer)
            {
                ImGui::OpenPopup("###Player");
                *validate = false;
            }
        }
        else
        {
            Sound* sound = static_cast<Sound*>(item);

            sound->getPlayerRef().attach(sPlayer);
            sound->setSoundType(FixedType);

            if (FixedType == Sound::SoundType::Strm)
            {
                sound->setPanMode(snd::PanMode::Balance); // Default for streams
            }
        }
    };

    return CreateItemFunc(clear, []() -> Item* { return new Sound(); }, innerFunc);
}

InstanciateItemCallback CreateStreamSoundFunc(bool clear)
{
    return CreateFixedSoundTypeFunc<Sound::SoundType::Strm>(clear);
}

InstanciateItemCallback CreateWaveSoundFunc(bool clear)
{
    return CreateFixedSoundTypeFunc<Sound::SoundType::Wave>(clear);
}

InstanciateItemCallback CreateSequenceSoundFunc(bool clear)
{
    return CreateFixedSoundTypeFunc<Sound::SoundType::Seq>(clear);
}

const char* SoundNamePrefixFunc(Item* item)
{
    Sound* sound = (Sound*)item;

    if (ImGui::Button(sead::FormatFixedSafeString<32>(ICON_LC_PLAY "###%u", sound->getId()).cstr()))
    {
        if (sound != sSelectedItem)
        {
            sSubSelectedItem = nullptr;
            sSelectedItemIsSubWindow = false;
        }

        sSoundPlayer.playSound(sound);
    }

    ImGui::SameLine();

    const char* icon = ICON_LC_FILE_QUESTION " ";
    switch (sound->getSoundType())
    {
        case Sound::SoundType::Seq:
            icon = ICON_LC_MUSIC_3 " ";
            break;

        case Sound::SoundType::Strm:
            icon = ICON_LC_DISC_3 " ";
            break;

        case Sound::SoundType::Wave:
            icon = ICON_LC_AUDIO_LINES " ";
            break;

        default:
            break;
    }

    return icon;
}

void SoundContextMenuFunc(Item* item, bool afterDelete)
{
    if (afterDelete)
        return;

    Sound* sound = static_cast<Sound*>(item);
    bool canExport = false;
    bool isSeq = false;
    if (sound)
    {
        Sound::SoundType type = sound->getSoundType();
        canExport = (type == Sound::SoundType::Wave || type == Sound::SoundType::Strm || type == Sound::SoundType::Seq);
        isSeq = (type == Sound::SoundType::Seq);
    }

    if (!canExport)
        ImGui::BeginDisabled();

    if (ImGui::MenuItem("Export to WAV"))
    {
        sShowExportConfirm = false;
        sPendingExportSound = sound;
        sPendingExportType = sound->getSoundType();
        sPendingExport = true;
    }

    if (!canExport)
        ImGui::EndDisabled();

    ImGui::Separator();

    if (!isSeq)
        ImGui::BeginDisabled();

    if (ImGui::MenuItem("Export to MIDI"))
    {
        sPendingExportMidiSound = sound;
    }

    if (!isSeq)
        ImGui::EndDisabled();
}

void DrawAllSoundsUI()
{
    DrawAllItemsUI("Sound", sBfsar.getSoundList(),
        &CreateSoundFunc, &SoundNamePrefixFunc, &SoundContextMenuFunc, GetItemFilterCallback()
    );
}

const char* SoundNamePrefixFunc2(Item* item)
{
    Sound* sound = (Sound*)item;

    if (ImGui::Button(sead::FormatFixedSafeString<32>(ICON_LC_PLAY "###%u", sound->getId()).cstr()))
    {
        if (sound != sSelectedItem)
        {
            sSubSelectedItem = nullptr;
            sSelectedItemIsSubWindow = false;
        }

        sSoundPlayer.playSound(sound);
    }

    ImGui::SameLine();

    return nullptr;
}

void DrawStreamSoundsUI()
{
    DrawAllItemsUI("Stream Sound", sBfsar.getSoundList(), &CreateStreamSoundFunc, &SoundNamePrefixFunc2, &SoundContextMenuFunc,
        [](const Item* item)
        {
            const Sound* sound = static_cast<const Sound*>(item);
            return sound->getSoundType() == Sound::SoundType::Strm && ItemMatchesFilter(sound);
        }
    );
}

void DrawWaveSoundsUI()
{
    DrawAllItemsUI("Wave Sound", sBfsar.getSoundList(), &CreateWaveSoundFunc, &SoundNamePrefixFunc2, &SoundContextMenuFunc,
        [](const Item* item)
        {
            const Sound* sound = static_cast<const Sound*>(item);
            return sound->getSoundType() == Sound::SoundType::Wave && ItemMatchesFilter(sound);
        }
    );
}

void DrawSequenceSoundsUI()
{
    DrawAllItemsUI("Sequence Sound", sBfsar.getSoundList(), &CreateSequenceSoundFunc, &SoundNamePrefixFunc2, &SoundContextMenuFunc,
        [](const Item* item)
        {
            const Sound* sound = static_cast<const Sound*>(item);
            return sound->getSoundType() == Sound::SoundType::Seq && ItemMatchesFilter(sound);
        }
    );
}

const char* SoundSetNamePrefixFunc(Item* item)
{
    SoundSet* sound = (SoundSet*)item;

    const char* icon = ICON_LC_FILE_QUESTION " ";
    switch (sound->getSoundSetType())
    {
        case SoundSet::SoundSetType::Wave:
            icon = ICON_LC_AUDIO_LINES " ";
            break;

        case SoundSet::SoundSetType::Seq:
            icon = ICON_LC_MUSIC_3 " ";
            break;
    }

    return icon;
}

InstanciateItemCallback CreateSoundSetFunc(bool clear)
{
    auto innerFunc = [](bool clear, Item* item, bool* validate)
    {
        static SoundSet::SoundSetType sSoundSetType = SoundSet::SoundSetType::Wave;

        if (clear)
        {
            sSoundSetType = SoundSet::SoundSetType::Wave;
        }

        if (!item && !validate)
        {
            ImGui::Separator();

            static const char* sSoundSetTypes[] = { "Wave", "Sequence" };
            ImGui::Combo("Sound Type", (s32*)&sSoundSetType, sSoundSetTypes, IM_ARRAYSIZE(sSoundSetTypes));
        }
        else if (validate)
        {
        }
        else
        {
            SoundSet* soundSet = static_cast<SoundSet*>(item);

            soundSet->setSoundSetType(sSoundSetType);
        }
    };

    return CreateItemFunc(clear, []() -> Item* { return new SoundSet(); }, innerFunc);
}

template <SoundSet::SoundSetType FixedType>
static InstanciateItemCallback CreateFixedSoundSetTypeFunc(bool clear)
{
    auto innerFunc = [](bool clear, Item* item, bool* validate)
    {
        if (!item && !validate)
        {
        }
        else if (validate)
        {
        }
        else
        {
            SoundSet* soundSet = static_cast<SoundSet*>(item);
            soundSet->setSoundSetType(FixedType);
        }
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

static void DrawSoundSetRangeVisualizer(const SoundSet::List& list, bool filterByType, SoundSet::SoundSetType filterType)
{
    u32 totalSounds = sBfsar.getSoundList().size();
    if (totalSounds == 0)
        return;

    struct SetVisual
    {
        SoundSet* set;
        u32 start;
        u32 end;
        u32 index;
    };
    std::vector<SetVisual> sets;
    u32 index = 0;
    for (auto it = list.begin(); it != list.end(); ++it)
    {
        SoundSet* soundSet = static_cast<SoundSet*>(*it);
        if (filterByType && soundSet->getSoundSetType() != filterType)
        {
            index++;
            continue;
        }
        if (soundSet->getIsEmpty())
        {
            index++;
            continue;
        }
        u32 start = soundSet->getStartId();
        u32 end = soundSet->getEndId();
        if (start == Item::cInvalidId || end == Item::cInvalidId || end < start)
        {
            index++;
            continue;
        }
        sets.push_back({ soundSet, start, end, index });
        index++;
    }

    if (sets.empty())
        return;

    u32 globalMin = totalSounds;
    u32 globalMax = 0;
    for (auto& s : sets)
    {
        if (s.start < globalMin) globalMin = s.start;
        if (s.end > globalMax) globalMax = s.end;
    }
    if (globalMax < globalMin) globalMax = globalMin;
    f32 floatRange = (f32)(globalMax - globalMin + 1);
    if (floatRange <= 0.0f) floatRange = 1.0f;

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 cursorPos = ImGui::GetCursorScreenPos();
    f32 width = ImGui::GetContentRegionAvail().x;
    if (width < 10.0f) width = 10.0f;
    const f32 height = 28.0f;
    const f32 outerPad = 2.0f;

    ImVec2 barMin(cursorPos.x, cursorPos.y + outerPad);
    ImVec2 barMax(cursorPos.x + width, cursorPos.y + outerPad + height);

    drawList->AddRectFilled(barMin, barMax, IM_COL32(25, 25, 25, 255));

    static const ImU32 sColors[] = {
        IM_COL32(70, 130, 180, 200),
        IM_COL32(60, 179, 113, 200),
        IM_COL32(218, 165, 32, 200),
        IM_COL32(147, 112, 219, 200),
        IM_COL32(205, 92, 92, 200),
        IM_COL32(0, 139, 139, 200),
        IM_COL32(255, 140, 0, 200),
        IM_COL32(100, 149, 237, 200),
    };
    const u32 colorCount = IM_ARRAYSIZE(sColors);

    for (size_t i = 0; i < sets.size(); i++)
    {
        f32 x1 = cursorPos.x + ((f32)(sets[i].start - globalMin) / floatRange) * width;
        f32 x2 = cursorPos.x + ((f32)(sets[i].end - globalMin + 1) / floatRange) * width;
        if (x2 - x1 < 1.0f) x2 = x1 + 1.0f;

        ImU32 color = sColors[sets[i].index % colorCount];
        drawList->AddRectFilled(ImVec2(x1, barMin.y), ImVec2(x2, barMax.y), color);

        const char* name = sets[i].set->getNameOrNull().cstr();
        ImVec2 textSize = ImGui::CalcTextSize(name);
        if (x2 - x1 > textSize.x + 6.0f)
        {
            f32 textX = x1 + (x2 - x1 - textSize.x) * 0.5f;
            if (textX < barMin.x + 2.0f) textX = barMin.x + 2.0f;
            if (textX + textSize.x > barMax.x - 2.0f) textX = barMax.x - textSize.x - 2.0f;
            drawList->AddText(ImVec2(textX, barMin.y + (height - textSize.y) * 0.5f), IM_COL32(255, 255, 255, 220), name);
        }
        else
        {
            sead::FormatFixedSafeString<16> idStr("%u", sets[i].set->getId());
            ImVec2 idSize = ImGui::CalcTextSize(idStr.cstr());
            if (x2 - x1 > idSize.x + 4.0f)
            {
                f32 textX = x1 + (x2 - x1 - idSize.x) * 0.5f;
                drawList->AddText(ImVec2(textX, barMin.y + (height - idSize.y) * 0.5f), IM_COL32(255, 255, 255, 180), idStr.cstr());
            }
        }
    }

    // Overlap detection
    for (size_t i = 0; i < sets.size(); i++)
    {
        for (size_t j = i + 1; j < sets.size(); j++)
        {
            u32 ovStart = sets[i].start > sets[j].start ? sets[i].start : sets[j].start;
            u32 ovEnd = sets[i].end < sets[j].end ? sets[i].end : sets[j].end;
            if (ovStart <= ovEnd)
            {
                f32 ox1 = cursorPos.x + ((f32)(ovStart - globalMin) / floatRange) * width;
                f32 ox2 = cursorPos.x + ((f32)(ovEnd - globalMin + 1) / floatRange) * width;
                if (ox2 - ox1 < 1.0f) ox2 = ox1 + 1.0f;
                drawList->AddRectFilled(ImVec2(ox1, barMin.y), ImVec2(ox2, barMax.y), IM_COL32(255, 40, 40, 180));
            }
        }
    }

    drawList->AddRect(barMin, barMax, IM_COL32(60, 60, 60, 255));

    ImGui::SetCursorScreenPos(ImVec2(cursorPos.x, cursorPos.y));
    ImGui::InvisibleButton("##rangeVis", ImVec2(width, height + outerPad * 2.0f));

    SetVisual* hoveredSet = nullptr;
    if (ImGui::IsItemHovered())
    {
        ImVec2 mousePos = ImGui::GetMousePos();
        f32 relX = mousePos.x - barMin.x;
        if (relX >= 0.0f && relX <= width)
        {
            f32 hoveredId = (f32)globalMin + (relX / width) * floatRange;
            for (auto& s : sets)
            {
                if (hoveredId >= (f32)s.start && hoveredId < (f32)(s.end + 1))
                {
                    hoveredSet = &s;
                    break;
                }
            }
        }
    }

    if (hoveredSet)
    {
        f32 hx1 = cursorPos.x + ((f32)(hoveredSet->start - globalMin) / floatRange) * width;
        f32 hx2 = cursorPos.x + ((f32)(hoveredSet->end - globalMin + 1) / floatRange) * width;
        if (hx2 - hx1 < 1.0f) hx2 = hx1 + 1.0f;
        drawList->AddRectFilled(ImVec2(hx1, barMin.y), ImVec2(hx2, barMax.y), IM_COL32(255, 255, 255, 35));
        drawList->AddRect(ImVec2(hx1, barMin.y), ImVec2(hx2, barMax.y), IM_COL32(255, 255, 255, 160), 0.0f, 0, 1.5f);

        if (ImGui::IsItemActivated())
            SelectItem(hoveredSet->set);

        ImGui::BeginTooltip();
        ImGui::Text("%s [%u, %u]", hoveredSet->set->getNameOrNull().cstr(), hoveredSet->start, hoveredSet->end);
        ImGui::EndTooltip();
    }
}

void DrawAllSoundSetsUI()
{
    const f32 cVisHeight = 32.0f;
    f32 barHeight = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y * 3.0f + cVisHeight;

    ImGui::BeginChild("SoundSetList", ImVec2(0, -barHeight), ImGuiChildFlags_AlwaysUseWindowPadding);
    DrawAllItemsUI("Sound Set", sBfsar.getSoundSetList(),
        &CreateSoundSetFunc, &SoundSetNamePrefixFunc, nullptr, GetItemFilterCallback()
    );
    ImGui::EndChild();

    ImGui::Separator();
    ImGui::Checkbox("Sticky Edit", &sSoundSetStickyEdit);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip("Sticky Edit: start/end IDs stick to adjacent sound sets.\nEditing shifts neighbors on the touching side.");

    DrawSoundSetRangeVisualizer(sBfsar.getSoundSetList(), false, SoundSet::SoundSetType::Wave);
}

void DrawWaveSoundSetsUI()
{
    const f32 cVisHeight = 32.0f;
    f32 barHeight = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y * 3.0f + cVisHeight;

    ImGui::BeginChild("SoundSetList", ImVec2(0, -barHeight), ImGuiChildFlags_AlwaysUseWindowPadding);
    DrawAllItemsUI("Wave Sound Set", sBfsar.getSoundSetList(), &CreateWaveSoundSetFunc, nullptr, nullptr,
        [](const Item* item)
        {
            const SoundSet* soundSet = static_cast<const SoundSet*>(item);
            return soundSet->getSoundSetType() == SoundSet::SoundSetType::Wave && ItemMatchesFilter(soundSet);
        }
    );
    ImGui::EndChild();

    ImGui::Separator();
    ImGui::Checkbox("Sticky Edit", &sSoundSetStickyEdit);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip("Sticky Edit: start/end IDs stick to adjacent sound sets.\nEditing shifts neighbors on the touching side.");

    DrawSoundSetRangeVisualizer(sBfsar.getSoundSetList(), true, SoundSet::SoundSetType::Wave);
}

void DrawSequenceSoundSetsUI()
{
    const f32 cVisHeight = 32.0f;
    f32 barHeight = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y * 3.0f + cVisHeight;

    ImGui::BeginChild("SoundSetList", ImVec2(0, -barHeight), ImGuiChildFlags_AlwaysUseWindowPadding);
    DrawAllItemsUI("Sequence Sound Set", sBfsar.getSoundSetList(), &CreateSequenceSoundSetFunc, nullptr, &SequenceSoundSetContextMenuFunc,
        [](const Item* item)
        {
            const SoundSet* soundSet = static_cast<const SoundSet*>(item);
            return soundSet->getSoundSetType() == SoundSet::SoundSetType::Seq && ItemMatchesFilter(soundSet);
        }
    );
    ImGui::EndChild();

    ImGui::Separator();
    ImGui::Checkbox("Sticky Edit", &sSoundSetStickyEdit);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip("Sticky Edit: start/end IDs stick to adjacent sound sets.\nEditing shifts neighbors on the touching side.");

    DrawSoundSetRangeVisualizer(sBfsar.getSoundSetList(), true, SoundSet::SoundSetType::Seq);
}

void SequenceSoundSetContextMenuFunc(Item* item, bool afterDelete)
{
    if (afterDelete)
        return;

    SoundSet* soundSet = static_cast<SoundSet*>(item);
    if (!soundSet || soundSet->getIsEmpty() || soundSet->getSoundSetType() != SoundSet::SoundSetType::Seq)
        return;

    u32 startId = soundSet->getStartId();
    u32 endId = soundSet->getEndId();

    bool hasSeq = false;
    for (auto it = sBfsar.getSoundList().begin(); it != sBfsar.getSoundList().end(); ++it)
    {
        Sound* s = static_cast<Sound*>(*it);
        if (s->getId() >= startId && s->getId() <= endId && s->getSoundType() == Sound::SoundType::Seq)
        {
            hasSeq = true;
            break;
        }
    }

    if (!hasSeq)
        ImGui::BeginDisabled();

    if (ImGui::MenuItem("Export to MIDI"))
    {
        sead::FixedSafeString<512> dirPath;
        if (SaveFileDialog(&dirPath, "Select directory for MIDI export"))
        {
            const char* p = dirPath.cstr();
            const char* lastSlash = nullptr;
            for (const char* c = p; *c; c++)
                if (*c == '/' || *c == '\\')
                    lastSlash = c;

            sead::FixedSafeString<512> exportDir;
            if (lastSlash)
                exportDir.format("%.*s", (s32)(lastSlash - p), p);
            else
                exportDir = ".";

            exportSeqSoundSetToMidiDir(exportDir, *soundSet);
        }
    }

    if (!hasSeq)
        ImGui::EndDisabled();
}

void DrawWaveImportInfo(WaveFile::Encoding* encoding, WaveFile::RiffWaveInfo* info)
{
    if (ImGui::Combo("Encoding", (s32*)encoding, WaveFile::sEncodingTypes, IM_ARRAYSIZE(WaveFile::sEncodingTypes)))
    {
    }

    ImGui::Separator();

    DrawWaveLoopInfo(info->isLoop, info->loopStartFrame, info->loopEndFrame, info->sampleCount, info->sampleRate, true, nullptr, true);
}

InstanciateItemCallback CreateWaveFileFunc(bool clear)
{
    static WaveFile::RiffWaveInfo sRiffWaveInfo;
    static sead::FixedSafeString<512> sFileName;
    static bool sAskForPath = true;
    static WaveFile::Encoding sEncoding = WaveFile::Encoding::DspAdpcm;
    static bool sIsNative;

    if (clear)
    {
        sRiffWaveInfo.clear();
        sFileName.clear();
        sAskForPath = true;
        sEncoding = WaveFile::Encoding::DspAdpcm;
        sIsNative = false;
    }

    if (sAskForPath)
    {
        const u32 filterCount = 2;
        FileFilter filters[filterCount] = {
            { "All supported audio (*.wav, *.bcwav, *.bfwav)", "*.wav *.bcwav *.bfwav" },
            { "Wave (*.wav)", "*.wav" }
        };

        sAskForPath = false;

        if (OpenFileDialog(&sRiffWaveInfo.path, nullptr, filterCount, filters))
        {
            sEncoding = WaveFile::Encoding::DspAdpcm;
            sead::Path::getFileName(&sFileName, sRiffWaveInfo.path);
            s32 dotPos = sFileName.rfindIndex(".");
            if (dotPos != -1)
                sFileName.trim(dotPos);

            const char* pathStr = sRiffWaveInfo.path.cstr();
            const char* dot = strrchr(pathStr, '.');
            sIsNative = dot && (strcasecmp(dot, ".bcwav") == 0 || strcasecmp(dot, ".bfwav") == 0);

            if (sIsNative)
            {
                sead::FileDevice* device = sead::FileDeviceMgr::instance()->findDevice("native");
                if (device)
                {
                    sead::FileDevice::LoadArg arg;
                    arg.path = sRiffWaveInfo.path;
                    u8* fileData = device->tryLoad(arg);
                    if (fileData)
                    {
                        // Patch file header so reader accepts any version
                        {
                            nw::ut::BinaryFileHeader* hdr = reinterpret_cast<nw::ut::BinaryFileHeader*>(fileData);
                            u16 bom;
                            sead::MemUtil::copy(&bom, fileData + 4, 2);
                            bool le = *reinterpret_cast<u8*>(&bom) == 0xFF;
                            u32 ver = sBfsar.getVersionForBfwav();
                            if (le)
                            {
                                fileData[8] = (ver >> 0) & 0xFF;
                                fileData[9] = (ver >> 8) & 0xFF;
                                fileData[10] = (ver >> 16) & 0xFF;
                                fileData[11] = (ver >> 24) & 0xFF;
                            }
                            else
                            {
                                fileData[8] = (ver >> 24) & 0xFF;
                                fileData[9] = (ver >> 16) & 0xFF;
                                fileData[10] = (ver >> 8) & 0xFF;
                                fileData[11] = (ver >> 0) & 0xFF;
                            }
                            // Fix signature to match archive format
                            const char* wantSig = sBfsar.getFormat() == ArchiveFormat::BCSAR ? "CWAV" : "FWAV";
                            hdr->signature[0] = wantSig[0];
                            hdr->signature[1] = wantSig[1];
                            hdr->signature[2] = wantSig[2];
                            hdr->signature[3] = wantSig[3];
                        }

                        WaveFile* newWave = new WaveFile();
                        newWave->setEnableName(true);
                        newWave->getName() = sFileName;
                        if (newWave->read(fileData))
                        {
                            newWave->setFormat(sBfsar.getFormat());
                            newWave->setVersion(sBfsar.getVersionForBfwav());
                            device->unload(fileData);

                            Item* insertAfter = GetInsertAfterItem();
                            if (insertAfter)
                                insertAfter->insertBack(newWave);
                            else
                                sBfsar.getWaveFileList().pushBack(newWave);
                            ClearInsertAfterItem();
                            sBfsar.updateList(sBfsar.getWaveFileList());
                            SetUnsavedChanges(true);
                            SelectItem(newWave);

                            return nullptr;
                        }
                        else
                        {
                            delete newWave;
                            device->unload(fileData);
                            PopupMgr::instance()->pushCurrentItemError("Failed to read wave file");
                            return nullptr;
                        }
                    }
                    else
                    {
                        PopupMgr::instance()->pushCurrentItemError("Failed to load wave file");
                        return nullptr;
                    }
                }
                else
                {
                    PopupMgr::instance()->pushCurrentItemError("Native file device not available");
                    return nullptr;
                }
            }
            else
            {
                bool success = WaveFile::readRiffWavInfo(&sRiffWaveInfo);
                if (!success)
                {
                    return nullptr;
                }
            }
        }
        else
        {
            return nullptr;
        }
    }

    if (!sFileName.isEmpty())
    {
        ImGui::Text("Import '%s'", sFileName.cstr());
        DrawWaveImportInfo(&sEncoding, &sRiffWaveInfo);
    }

    return []() -> Item*
    {
        WaveFile* waveFile = new WaveFile();
        waveFile->setEnableName(true);
        waveFile->getName() = sFileName;

        if (!sRiffWaveInfo.isLoop)
        {
            sRiffWaveInfo.loopStartFrame = 0;
        }

        bool success = waveFile->readWavFile(sRiffWaveInfo, sEncoding);
        if (!success)
        {
            delete waveFile;
            return nullptr;
        }

        return waveFile;
    };
}

static const char* WaveFileNamePrefixFunc(Item* item)
{
    WaveFile* wave = static_cast<WaveFile*>(item);

    if (ImGui::Button(sead::FormatFixedSafeString<32>(ICON_LC_PLAY "###%u", wave->getId()).cstr()))
    {
        sSoundPlayer.playWaveFile(*wave);
    }

    ImGui::SameLine();

    return nullptr;
}

static WaveFile::Encoding sEncoding = WaveFile::Encoding::DspAdpcm;
static WaveFile* sImportWaveFile = nullptr;
static WaveFile::RiffWaveInfo sRiffWaveInfo;
static sead::FixedSafeString<512> sWavFileName;

void WaveFileContextMenuFunc(Item* item, bool afterDelete)
{
    WaveFile* wave = nullptr;
    if (item)
        wave = static_cast<WaveFile*>(item);

    bool disableMenu = wave == nullptr;
    if (disableMenu)
        ImGui::BeginDisabled();

    if (!afterDelete)
    {
        if (ImGui::MenuItem("Replace"))
        {
            sImportWaveFile = nullptr;
            sRiffWaveInfo.clear();
            sWavFileName.clear();

            FileFilter filters[] = {
                { "Wave (*.wav)", "*.wav" }
            };

            if (OpenFileDialog(&sRiffWaveInfo.path, nullptr, 1, filters))
            {
                bool success = WaveFile::readRiffWavInfo(&sRiffWaveInfo);
                if (success)
                {
                    sEncoding = WaveFile::Encoding::DspAdpcm;
                    sImportWaveFile = wave;
                    sead::Path::getFileName(&sWavFileName, sRiffWaveInfo.path);
                    s32 dotPos = sWavFileName.rfindIndex(".");
                    if (dotPos != -1)
                        sWavFileName.trim(dotPos);
                }
            }
        }
    }
    else
    {
        ImGui::Separator();

        if (ImGui::MenuItem("Export"))
        {
            sPendingExportWaveFile = wave;
        }

        if (ImGui::MenuItem("Export to WAV"))
        {
            sPendingExportWaveToWav = wave;
        }
    }

    if (disableMenu)
        ImGui::EndDisabled();
}

void SequenceFileContextMenuFunc(Item* item, bool afterDelete)
{
    SequenceFile* seq = nullptr;
    if (item)
        seq = static_cast<SequenceFile*>(item);

    if (afterDelete)
        return;

    ImGui::Separator();

    if (ImGui::MenuItem("Import"))
    {
        sPendingImportSequenceFile = true;
    }

    bool disabled = seq == nullptr || !seq->isValid();
    if (disabled) ImGui::BeginDisabled();

    if (ImGui::MenuItem("Export"))
    {
        sPendingExportSequenceFile = seq;
    }

    if (disabled) ImGui::EndDisabled();
}

void BankFileContextMenuFunc(Item* item, bool afterDelete)
{
    BankFile* bank = nullptr;
    if (item)
        bank = static_cast<BankFile*>(item);

    if (afterDelete)
        return;

    ImGui::Separator();

    if (ImGui::MenuItem("Import Bank Bundle"))
    {
        sPendingImportBankBundle = true;
    }

    {
        bool disabled = bank == nullptr;
        if (disabled) ImGui::BeginDisabled();

        if (ImGui::MenuItem("Export Bank Bundle"))
        {
            sPendingExportBankBundle = bank;
        }

        if (disabled) ImGui::EndDisabled();
    }
}

void DrawWaveFilesUI()
{
    DrawAllItemsUI("Wave File", sBfsar.getWaveFileList(),
        &CreateWaveFileFunc, &WaveFileNamePrefixFunc, &WaveFileContextMenuFunc, GetItemFilterCallback()
    );

    if (sImportWaveFile && !sRiffWaveInfo.path.isEmpty())
    {
        ImGui::OpenPopup("WavImport");
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("WavImport", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar))
    {
        ImGui::Text("Replace with '%s'", sWavFileName.cstr());

        DrawWaveImportInfo(&sEncoding, &sRiffWaveInfo);

        ImGui::Separator();

        ImVec2 buttonSize((ImGui::GetWindowContentRegionMax().x - ImGui::GetStyle().WindowPadding.x * 2.0f) / 2.0f, 0.0f);

        if (ImGui::Button("Replace", buttonSize))
        {
            if (!sRiffWaveInfo.isLoop)
            {
                sRiffWaveInfo.loopStartFrame = 0;
            }

            bool success = sImportWaveFile->readWavFile(sRiffWaveInfo, sEncoding);
            if (success)
            {
                sImportWaveFile->getName() = sWavFileName;
            }

            sImportWaveFile = nullptr;
            sRiffWaveInfo.clear();
            sWavFileName.clear();

            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();

        if (ImGui::Button("Cancel", buttonSize))
        {
            sImportWaveFile = nullptr;
            sRiffWaveInfo.clear();
            sWavFileName.clear();

            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

FileWindow* OpenFileWindow(Item* item)
{
    sead::FormatFixedSafeString<512> windowName("###%u", item);

    ImGuiWindow* imguiWindow = ImGui::FindWindowByName(windowName.cstr());
    if (!imguiWindow || !imguiWindow->WasActive)
    {
        FileWindow* window = new FileWindow(item);

        sFileWindows.pushBack(window);

        return window;
    }
    else if (imguiWindow)
    {
        ImGui::SetWindowFocus(windowName.cstr());

        return nullptr;
    }

    return nullptr;
}

InstanciateItemCallback CreateSequenceFileFunc(bool clear)
{
    auto doCreate = []() -> Item*
    {
        SequenceFile* seq = new SequenceFile();
        seq->setEnableName(true);
        seq->getName() = "Sequence";
        return seq;
    };

    return doCreate;
}

const char* SequenceFileNamePrefixFunc(Item* item)
{
    SequenceFile* seq = static_cast<SequenceFile*>(item);

    if (ImGui::Button(sead::FormatFixedSafeString<32>(ICON_LC_FILE_PEN "###%u", seq->getId()).cstr()))
    {
        OpenFileWindow(seq);
        SelectItem(seq);
    }

    ImGui::SameLine();

    return nullptr;
}

void DrawSequenceFilesUI()
{
    DrawAllItemsUI("Sequence File", sBfsar.getSequenceFileList(),
        &CreateSequenceFileFunc, &SequenceFileNamePrefixFunc, &SequenceFileContextMenuFunc, GetItemFilterCallback(), true
    );
}

InstanciateItemCallback CreateBankFileFunc(bool clear)
{
    auto doCreate = []() -> Item*
    {
        BankFile* bank = new BankFile();
        bank->setEnableName(true);
        bank->getName() = "Bank";

        return bank;
    };

    return doCreate;
}

const char* BankFileNamePrefixFunc(Item* item)
{
    BankFile* bank = static_cast<BankFile*>(item);

    if (ImGui::Button(sead::FormatFixedSafeString<32>(ICON_LC_FILE_PEN "###%u", bank->getId()).cstr()))
    {
        OpenFileWindow(bank);
    }

    ImGui::SameLine();

    return nullptr;
}

void DrawBankFilesUI()
{
    DrawAllItemsUI("Bank File", sBfsar.getBankFileList(),
        &CreateBankFileFunc, &BankFileNamePrefixFunc, &BankFileContextMenuFunc, GetItemFilterCallback(), true
    );
}

static void FormatFileSize(sead::BufferedSafeString& out, u32 bytes)
{
    if (bytes >= 1024 * 1024)
    {
        f32 mb = (f32)bytes / (1024.0f * 1024.0f);
        out.format("%.2f MB", mb);
    }
    else if (bytes >= 1024)
    {
        f32 kb = (f32)bytes / 1024.0f;
        out.format("%.1f KB", kb);
    }
    else
    {
        out.format("%u B", bytes);
    }
}

static ImU32 HeatMapColor(f32 t)
{
    // Green (small) -> Yellow (medium) -> Red (large)
    t = sead::Mathf::clamp2(0.0f, t, 1.0f);
    if (t < 0.5f)
    {
        f32 u = t / 0.5f;
        return IM_COL32(
            (int)(u * 255),
            255,
            (int)((1.0f - u) * 128),
            200
        );
    }
    else
    {
        f32 u = (t - 0.5f) / 0.5f;
        return IM_COL32(
            255,
            (int)((1.0f - u) * 255),
            (int)((1.0f - u) * 64),
            200
        );
    }
}

struct FileStatBlock
{
    WaveFile* wave;
    u32 size;
    sead::FixedSafeString<256> name;
    sead::FixedSafeString<32> sizeStr;
    ImRect rect;
};

static void LayoutTreemapRow(std::vector<FileStatBlock>& blocks, size_t start, size_t end, ImRect area)
{
    if (start >= end) return;

    if (end - start == 1)
    {
        blocks[start].rect = area;
        return;
    }

    u32 totalSize = 0;
    for (size_t i = start; i < end; i++)
        totalSize += blocks[i].size;

    if (totalSize == 0)
    {
        f32 w = area.GetWidth() / (end - start);
        for (size_t i = start; i < end; i++)
        {
            blocks[i].rect = ImRect(
                area.Min.x + (i - start) * w, area.Min.y,
                area.Min.x + (i - start + 1) * w, area.Max.y
            );
        }
        return;
    }

    // Lay out items along the longer dimension of the available area
    bool horizontal = area.GetWidth() >= area.GetHeight();

    u32 cumSize = 0;
    size_t bestSplit = start + 1;
    f32 bestWorst = FLT_MAX;

    for (size_t i = start; i < end; i++)
    {
        cumSize += blocks[i].size;
        size_t count = i - start + 1;

        f32 cumRatio = (f32)cumSize / (f32)totalSize;
        f32 rowSpan = horizontal ? area.GetHeight() : area.GetWidth();
        f32 rowThick = rowSpan * cumRatio;
        f32 itemSpan = horizontal ? area.GetWidth() : area.GetHeight();

        f32 worst = 0.0f;
        for (size_t j = start; j <= i; j++)
        {
            f32 itemRatio = (f32)blocks[j].size / (f32)cumSize;
            f32 itemW = horizontal ? itemSpan * itemRatio : rowThick;
            f32 itemH = horizontal ? rowThick : itemSpan * itemRatio;
            f32 aspect = (itemW > itemH) ? itemW / itemH : itemH / itemW;
            if (aspect > worst) worst = aspect;
        }

        if (count == 1 || worst < bestWorst)
        {
            bestWorst = worst;
            bestSplit = i + 1;
        }
        else
        {
            break;
        }
    }

    u32 firstSize = 0;
    for (size_t i = start; i < bestSplit; i++)
        firstSize += blocks[i].size;

    f32 splitRatio = (f32)firstSize / (f32)totalSize;

    ImRect rowRect, restRect;
    if (horizontal)
    {
        // Row takes a fraction of the HEIGHT at the top; items within stretch full width
        f32 splitY = area.Min.y + area.GetHeight() * splitRatio;
        rowRect = ImRect(area.Min, ImVec2(area.Max.x, splitY));
        restRect = ImRect(ImVec2(area.Min.x, splitY), area.Max);
    }
    else
    {
        // Row takes a fraction of the WIDTH at the left; items within stretch full height
        f32 splitX = area.Min.x + area.GetWidth() * splitRatio;
        rowRect = ImRect(area.Min, ImVec2(splitX, area.Max.y));
        restRect = ImRect(ImVec2(splitX, area.Min.y), area.Max);
    }

    if (horizontal)
    {
        f32 itemSpan = rowRect.GetWidth();
        f32 rowPos = rowRect.Min.x;
        for (size_t i = start; i < bestSplit; i++)
        {
            f32 itemRatio = (f32)blocks[i].size / (f32)firstSize;
            f32 itemW = itemSpan * itemRatio;
            blocks[i].rect = ImRect(rowPos, rowRect.Min.y, rowPos + itemW, rowRect.Max.y);
            rowPos += itemW;
        }
    }
    else
    {
        f32 itemSpan = rowRect.GetHeight();
        f32 rowPos = rowRect.Min.y;
        for (size_t i = start; i < bestSplit; i++)
        {
            f32 itemRatio = (f32)blocks[i].size / (f32)firstSize;
            f32 itemH = itemSpan * itemRatio;
            blocks[i].rect = ImRect(rowRect.Min.x, rowPos, rowRect.Max.x, rowPos + itemH);
            rowPos += itemH;
        }
    }

    LayoutTreemapRow(blocks, bestSplit, end, restRect);
}

void DrawFileStatisticsUI()
{
    static bool sIncludeStreamWaves = true;

    ImGui::Checkbox("Include Stream Waves", &sIncludeStreamWaves);
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip())
    {
        ImGui::Text("When enabled, includes wave files derived from stream sound data.");
        ImGui::EndTooltip();
    }

    ImGui::Separator();

    std::vector<FileStatBlock> blocks;
    u32 totalSize = 0;
    u32 maxSize = 0;

    const auto& waveList = sBfsar.getWaveFileList();
    for (const auto& node : waveList)
    {
        WaveFile* wave = static_cast<WaveFile*>(node->val());

        if (!sIncludeStreamWaves && wave->getIsStreamExtended())
            continue;

        u32 size = wave->getFileSize();
        if (size == 0) size = 1;

        FileStatBlock block;
        block.wave = wave;
        block.size = size;
        block.name = wave->getName();
        FormatFileSize(block.sizeStr, size);
        blocks.push_back(block);
        totalSize += size;
        if (size > maxSize) maxSize = size;
    }

    if (blocks.empty())
    {
        ImGui::Text("No wave files to display.");
        return;
    }

    std::sort(blocks.begin(), blocks.end(), [](const FileStatBlock& a, const FileStatBlock& b) {
        return a.size > b.size;
    });

    {
        sead::FixedSafeString<32> totalSizeStr;
        FormatFileSize(totalSizeStr, totalSize);
        ImGui::Text("Total: %s (%zu files)", totalSizeStr.cstr(), blocks.size());
    }
    ImGui::Separator();

    ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    if (canvasSize.x < 10.0f || canvasSize.y < 10.0f) return;

    f32 minHeight = ImGui::GetFontSize() * 24.0f;
    if (canvasSize.y < minHeight) canvasSize.y = minHeight;

    ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    ImRect canvasRect(canvasPos, canvasPos + canvasSize);

    ImGui::InvisibleButton("##treemap", canvasSize, ImGuiButtonFlags_MouseButtonLeft);
    bool canvasHovered = ImGui::IsItemHovered();

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    LayoutTreemapRow(blocks, 0, blocks.size(), canvasRect);

    FileStatBlock* hoveredBlock = nullptr;
    FileStatBlock* clickedBlock = nullptr;

    for (auto& block : blocks)
    {
        ImRect rect = block.rect;

        f32 t = maxSize > 0 ? (f32)block.size / (f32)maxSize : 0.0f;
        ImU32 color = HeatMapColor(t);
        ImU32 borderColor = IM_COL32(0, 0, 0, 100);

        bool darkBg = t < 0.65f;
        ImU32 textColor = darkBg ? IM_COL32(20, 20, 20, 230) : IM_COL32(255, 255, 255, 230);
        ImU32 sizeColor = darkBg ? IM_COL32(20, 20, 20, 180) : IM_COL32(255, 255, 255, 180);

        drawList->AddRectFilled(rect.Min, rect.Max, color, 1.0f);
        drawList->AddRect(rect.Min, rect.Max, borderColor, 1.0f);

        if (rect.GetWidth() > 50.0f && rect.GetHeight() > 30.0f)
        {
            ImVec2 nameSize = ImGui::CalcTextSize(block.name.cstr());
            if (nameSize.x < rect.GetWidth() - 8.0f)
            {
                ImVec2 textPos = ImVec2(
                    rect.Min.x + (rect.GetWidth() - nameSize.x) * 0.5f,
                    rect.Min.y + 4.0f
                );
                drawList->AddText(textPos, textColor, block.name.cstr());
            }

            ImVec2 sizeTextSize = ImGui::CalcTextSize(block.sizeStr.cstr());
            if (sizeTextSize.x < rect.GetWidth() - 8.0f)
            {
                ImVec2 sizeTextPos = ImVec2(
                    rect.Min.x + (rect.GetWidth() - sizeTextSize.x) * 0.5f,
                    rect.Max.y - sizeTextSize.y - 4.0f
                );
                drawList->AddText(sizeTextPos, sizeColor, block.sizeStr.cstr());
            }
        }

        if (canvasHovered && ImGui::IsMouseHoveringRect(rect.Min, rect.Max))
        {
            hoveredBlock = &block;
            drawList->AddRect(rect.Min, rect.Max, IM_COL32(255, 255, 255, 220), 1.0f, 0, 2.0f);
        }
    }

    if (canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && hoveredBlock)
    {
        clickedBlock = hoveredBlock;
    }

    if (hoveredBlock)
    {
        f32 pct = totalSize > 0 ? (f32)hoveredBlock->size / (f32)totalSize * 100.0f : 0.0f;
        ImGui::BeginTooltip();
        ImGui::Text("%s", hoveredBlock->name.cstr());
        ImGui::Text("Size: %s", hoveredBlock->sizeStr.cstr());
        ImGui::Text("Percentage: %.2f%%", pct);
        ImGui::EndTooltip();
    }

    if (clickedBlock)
        SelectItem(clickedBlock->wave);
}

bool DrawVersionUI(u32* versionPtr, u32 versionByteNum)
{
    const ImU8 stepU8 = 1;

    //ImGui::Text("0x%08X", *versionPtr);

    bool updated = false;
    for (u32 i = 0; i < versionByteNum; i++)
    {
        ImGui::PushID(i);
        ImGui::PushItemWidth(ImGui::GetWindowContentRegionMax().x / static_cast<f32>(versionByteNum) - 7.0f);

        u32 version = *versionPtr;
        u32 versionByteOffset = ((versionByteNum - 1) - i) * 0x8;

        u8 versionByte = (version >> versionByteOffset) & 0xFF;
        if (ImGui::InputScalar("", ImGuiDataType_U8, &versionByte, &stepU8))
        {
            *versionPtr = (version & ~(0xFF << versionByteOffset)) | (versionByte << versionByteOffset);
            updated = true;
        }

        if (i + 1 < versionByteNum)
        {
            ImGui::SameLine();
        }

        ImGui::PopItemWidth();
        ImGui::PopID();
    }

    return updated;
}

void CenteredText(const char* text, const ImVec2& sizeArg)
{
    ImVec2 textSize = ImGui::CalcTextSize(text);

    if (sizeArg.x != -1)
        ImGui::SetCursorPosX((sizeArg.x / 2.0f) - (textSize.x / 2.0f));

    if (sizeArg.y != -1)
        ImGui::SetCursorPosY((sizeArg.y / 2.0f) - (textSize.y / 2.0f));

    ImGui::Text("%s", text);
}

// From ImGui Demo
void HelpMarker(const char* desc)
{
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip())
    {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(desc);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}
