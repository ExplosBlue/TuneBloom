#define IMGUI_DEFINE_MATH_OPERATORS
#include <ui/UI.h>

#include <ui/PopupMgr.h>

//#include <snd/SoundThread.h>

#include <filedevice/seadPath.h>
#include <framework/glfw/seadGameFrameworkBaseGlfw.h>
#include <framework/seadProcessMeter.h>
#include <gfx/gl/seadTextureGL.h>

#include <Utilll.h>

#if defined(SEAD_PLATFORM_WINDOWS)
#include <basis/win/seadWindows.h>
#include <shellapi.h>
#elif defined(SEAD_PLATFORM_LINUX) || defined(SEAD_PLATFORM_MACOSX)
#include <cstdlib>
#include <sys/stat.h>
#endif

#include <cstdio>

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

sead::FixedSafeString<512> sDroppedFilePath;
sead::FixedSafeString<512> sRecentFileClick;

ItemList sFileWindows;

// Windows
void DrawProjectUI();
void DrawInfoUI();
void DrawSubInfoUI();
void DrawFileUI(ImGuiID dockspaceId);
void DrawPropertiesUI();

void DrawAllSoundsUI();
void DrawStreamSoundsUI();
void DrawWaveSoundsUI();
void DrawSequenceSoundsUI();
void DrawAllSoundSetsUI();
void DrawWaveSoundSetsUI();
void DrawSequenceSoundSetsUI();
void DrawWaveFilesUI();
void DrawSequenceFilesUI();
void DrawBankFilesUI();

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

static bool sWantsNew = false;
static bool sWantsOpen = false;
static bool sWantsClose = false;
static bool sWantsExit = false;
static bool sWantsAbout = false;

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
                    NewFile();
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
    _mkdir(GetConfigDir().c_str());
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
                    NewFile();
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
        ImGui::OpenPopup("###Save");
        sWantsNew = false;
        sFileAction = &NewFile;
    }
    else if (sWantsOpen)
    {
        ImGui::OpenPopup("###Save");
        sWantsOpen = false;
        sFileAction = &OpenFile;
    }
    else if (sWantsClose)
    {
        ImGui::OpenPopup("###Save");
        sWantsClose = false;
        sFileAction = &CloseFile;
    }
    else if (sWantsExit)
    {
        ImGui::OpenPopup("###Save");
        sWantsExit = false;
        sFileAction = &Exit;
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
                icon = (ImTextureID)tex->getID();
            }
        }

        DrawTuneBloomSplash(icon, ImVec2(130, 130));
    }
}

void DrawProjectUI()
{
    if (ImGui::Begin(ICON_LC_FOLDER " Project###ProjectWindow"))
    {
        bool selected = false;

        auto resetSelectedItemAndSubWindow = []()
        {
            sSelectedItem = nullptr;
            sSubSelectedItem = nullptr;
            sSelectedItemIsSubWindow = false;
        };

        auto resetSelectedItemAndSubWindowIfNotType = [&](Item::ItemType type)
        {
            if (sSelectedItem && sSelectedItem->getItemType() != type)
            {
                resetSelectedItemAndSubWindow();
            }
        };

        if (ImGui::Selectable(ICON_LC_INFO " Project Info", sSelectedUIType == UIType::ProjectInfo))
        {
            sSelectedUIType = UIType::ProjectInfo;
            selected = true;

            resetSelectedItemAndSubWindow();
        }

        ImGui::Separator();

        if (ImGui::Selectable(ICON_LC_LIST_MUSIC " All Sounds", sSelectedUIType == UIType::AllSounds))
        {
            sSelectedUIType = UIType::AllSounds;
            selected = true;

            resetSelectedItemAndSubWindowIfNotType(Item::ItemType::Sound);
        }

        if (ImGui::Selectable(ICON_LC_DISC_3 " Stream Sounds", sSelectedUIType == UIType::StreamSounds, ImGuiSelectableFlags_SpanAvailWidth))
        {
            sSelectedUIType = UIType::StreamSounds;
            selected = true;

            resetSelectedItemAndSubWindowIfNotType(Item::ItemType::Sound);
        }

        if (ImGui::Selectable(ICON_LC_AUDIO_LINES " Wave Sounds", sSelectedUIType == UIType::WaveSounds))
        {
            sSelectedUIType = UIType::WaveSounds;
            selected = true;

            resetSelectedItemAndSubWindowIfNotType(Item::ItemType::Sound);
        }

        if (ImGui::Selectable(ICON_LC_MUSIC_3 " Sequence Sounds", sSelectedUIType == UIType::SequenceSounds))
        {
            sSelectedUIType = UIType::SequenceSounds;
            selected = true;

            resetSelectedItemAndSubWindowIfNotType(Item::ItemType::Sound);
        }

        ImGui::Separator();

        if (ImGui::Selectable(ICON_LC_MUSIC_4 " All Sound Sets", sSelectedUIType == UIType::AllSoundSets))
        {
            sSelectedUIType = UIType::AllSoundSets;
            selected = true;

            resetSelectedItemAndSubWindowIfNotType(Item::ItemType::SoundSet);
        }

        if (ImGui::Selectable(ICON_LC_AUDIO_LINES " Wave Sound Sets", sSelectedUIType == UIType::WaveSoundSets))
        {
            sSelectedUIType = UIType::WaveSoundSets;
            selected = true;

            resetSelectedItemAndSubWindowIfNotType(Item::ItemType::SoundSet);
        }

        if (ImGui::Selectable(ICON_LC_MUSIC_2 " Sequence Sound Sets", sSelectedUIType == UIType::SequenceSoundSets))
        {
            sSelectedUIType = UIType::SequenceSoundSets;
            selected = true;

            resetSelectedItemAndSubWindowIfNotType(Item::ItemType::SoundSet);
        }

        ImGui::Separator();

        if (ImGui::Selectable(ICON_LC_PIANO " Banks", sSelectedUIType == UIType::Banks))
        {
            sSelectedUIType = UIType::Banks;
            selected = true;

            resetSelectedItemAndSubWindowIfNotType(Item::ItemType::Bank);
        }

        if (ImGui::Selectable(ICON_LC_FILE_MUSIC " Wave Archives", sSelectedUIType == UIType::WaveArchives))
        {
            sSelectedUIType = UIType::WaveArchives;
            selected = true;

            resetSelectedItemAndSubWindowIfNotType(Item::ItemType::WaveArchive);
        }

        if (ImGui::Selectable(ICON_LC_FOLDERS " Groups", sSelectedUIType == UIType::Groups))
        {
            sSelectedUIType = UIType::Groups;
            selected = true;

            resetSelectedItemAndSubWindowIfNotType(Item::ItemType::Group);
        }

        if (ImGui::Selectable(ICON_LC_VOLUME_2 " Players", sSelectedUIType == UIType::Players))
        {
            sSelectedUIType = UIType::Players;
            selected = true;

            resetSelectedItemAndSubWindowIfNotType(Item::ItemType::Player);
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

static sead::FixedSafeString<256> sFilter;
static bool sFilterActive = false;
static bool sFilterCaseSensitive = false;

void CloseFilter()
{
    sFilter.clear();
    sFilterActive = false;
}

bool ItemMatchesFilter(const Item* item)
{
    if (sFilter.isEmpty())
    {
        return true;
    }

    std::string name = item->getFormattedName().cstr();
    std::string filter = sFilter.cstr();

    if (!sFilterCaseSensitive)
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
    return !sFilter.isEmpty() ? &ItemMatchesFilter : nullptr;
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

        bool setFocus = false;
        if (notProjUI && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) && ImGui::IsKeyDown(ImGuiKey_ModCtrl) && ImGui::IsKeyDown(ImGuiKey_F))
        {
            sFilterActive = true;
            setFocus = true;
        }

        if (notProjUI && sFilterActive)
        {
            if (ImGui::BeginChild("##Filter", ImVec2(0, 0), ImGuiChildFlags_AlwaysAutoResize | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Border))
            {
                if (setFocus)
                {
                    ImGui::SetKeyboardFocusHere();
                }

                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("   ").x * 2.0f - ImGui::GetStyle().ItemSpacing.x * 2.0f);
                ImGui::InputTextWithHint("##Search", "Search...", sFilter.getBuffer(), sFilter.getBufferSize(), ImGuiInputTextFlags_CharsNoBlank);

                ImGui::SameLine();

                bool popColor = false;
                if (!sFilterCaseSensitive)
                {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.2f, 0.5f));
                    popColor = true;
                }

                if (ImGui::Button(ICON_LC_CASE_SENSITIVE "##CaseSensitive"))
                {
                    sFilterCaseSensitive = !sFilterCaseSensitive;
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
                    }
                }

                break;

            case Item::ItemType::Bank:
                icon = ICON_LC_PIANO " ";
                break;

            case Item::ItemType::WaveArchive:
                icon = ICON_LC_FILE_MUSIC " ";
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
        if (sSelectedItem)
        {
            switch (sSelectedItem->getItemType())
            {
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
                    }
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
        }

        switch (sSelectedItem->getItemType())
        {
            case Item::ItemType::WaveFile:
            case Item::ItemType::SequenceFile:
            case Item::ItemType::BankFile:
            case Item::ItemType::Bank:
                DrawReferencesUI(sSelectedItem);
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
        }
    }

    {
        CenteredTextX("Version");

        u32 version = sBfsar.getVersion();
        if (DrawVersionUI(&version, sBfsar.getFormat() == ArchiveFormat::BCSAR ? 4 : 3))
        {
            sBfsar.setVersion(version);
        }
    }

    //ImGui::SeparatorText("");
    {
        bool stringTable = sBfsar.isIncludeStringTable();
        if (ImGui::Checkbox("Include String Table", &stringTable))
            sBfsar.setIncludeStringTable(stringTable);
    }

    ImGui::SeparatorText("Sound Archive Player");

    {
        SoundArchivePlayerInfo& playerInfo = sBfsar.getSoundArchivePlayerInfo();

        if (ImGui::InputScalar("Sequence Sound Max", ImGuiDataType_U16, &playerInfo.sequenceSoundMax, &stepU16))
        {
            playerInfo.sequenceSoundMax = sead::MathCalcCommon<u16>::clamp2(0, playerInfo.sequenceSoundMax, 999);
        }
        if (ImGui::InputScalar("Sequence Track Max", ImGuiDataType_U16, &playerInfo.sequenceTrackMax, &stepU16))
        {
            playerInfo.sequenceTrackMax = sead::MathCalcCommon<u16>::clamp2(0, playerInfo.sequenceTrackMax, 999);
        }
        if (ImGui::InputScalar("Stream Sound Max", ImGuiDataType_U16, &playerInfo.streamSoundMax, &stepU16))
        {
            playerInfo.streamSoundMax = sead::MathCalcCommon<u16>::clamp2(0, playerInfo.streamSoundMax, 999);
        }
        if (ImGui::InputScalar("Stream Track Max", ImGuiDataType_U16, &playerInfo.streamTrackMax, &stepU16))
        {
            playerInfo.streamTrackMax = sead::MathCalcCommon<u16>::clamp2(0, playerInfo.streamTrackMax, 999);
        }
        if (ImGui::InputScalar("Stream Channel Max", ImGuiDataType_U16, &playerInfo.streamChannelMax, &stepU16))
        {
            playerInfo.streamChannelMax = sead::MathCalcCommon<u16>::clamp2(0, playerInfo.streamChannelMax, 32);
        }
        if (ImGui::InputScalar("Wave Sound Max", ImGuiDataType_U16, &playerInfo.waveSoundMax, &stepU16))
        {
            playerInfo.waveSoundMax = sead::MathCalcCommon<u16>::clamp2(0, playerInfo.waveSoundMax, 999);
        }
        if (ImGui::InputScalar("Wave Track Max", ImGuiDataType_U16, &playerInfo.waveTrackMax, &stepU16))
        {
            playerInfo.waveTrackMax = sead::MathCalcCommon<u16>::clamp2(0, playerInfo.waveTrackMax, 999);
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
            }

            if (!enable)
                ImGui::EndDisabled();

            if (!enable)
            {
                ImGui::SameLine();
                HelpMarker("Need version to be >= 0.2.2.0");
            }
        }

        ImGui::InputScalar("Options", ImGuiDataType_U32, &playerInfo.options, &stepU32);
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
    }

    return icon;
}

void DrawAllSoundsUI()
{
    DrawAllItemsUI("Sound", sBfsar.getSoundList(),
        &CreateSoundFunc, &SoundNamePrefixFunc, nullptr, GetItemFilterCallback()
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
    DrawAllItemsUI("Stream Sound", sBfsar.getSoundList(), nullptr, &SoundNamePrefixFunc2, nullptr,
        [](const Item* item)
        {
            const Sound* sound = static_cast<const Sound*>(item);
            return sound->getSoundType() == Sound::SoundType::Strm && ItemMatchesFilter(sound);
        }
    );
}

void DrawWaveSoundsUI()
{
    DrawAllItemsUI("Wave Sound", sBfsar.getSoundList(), nullptr, &SoundNamePrefixFunc2, nullptr,
        [](const Item* item)
        {
            const Sound* sound = static_cast<const Sound*>(item);
            return sound->getSoundType() == Sound::SoundType::Wave && ItemMatchesFilter(sound);
        }
    );
}

void DrawSequenceSoundsUI()
{
    DrawAllItemsUI("Sequence Sound", sBfsar.getSoundList(), nullptr, &SoundNamePrefixFunc2, nullptr,
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

void DrawAllSoundSetsUI()
{
    DrawAllItemsUI("Sound Set", sBfsar.getSoundSetList(),
        &CreateSoundSetFunc, &SoundSetNamePrefixFunc, nullptr, GetItemFilterCallback()
    );
}

void DrawWaveSoundSetsUI()
{
    DrawAllItemsUI("Wave Sound Set", sBfsar.getSoundSetList(), nullptr, nullptr, nullptr,
        [](const Item* item)
        {
            const SoundSet* soundSet = static_cast<const SoundSet*>(item);
            return soundSet->getSoundSetType() == SoundSet::SoundSetType::Wave && ItemMatchesFilter(soundSet);
        }
    );
}

void DrawSequenceSoundSetsUI()
{
    DrawAllItemsUI("Sequence Sound Set", sBfsar.getSoundSetList(), nullptr, nullptr, nullptr,
        [](const Item* item)
        {
            const SoundSet* soundSet = static_cast<const SoundSet*>(item);
            return soundSet->getSoundSetType() == SoundSet::SoundSetType::Seq && ItemMatchesFilter(soundSet);
        }
    );
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

    if (clear)
    {
        sRiffWaveInfo.clear();
        sFileName.clear();
        sAskForPath = true;
        sEncoding = WaveFile::Encoding::DspAdpcm;
    }

    if (sAskForPath)
    {
        const u32 filterCount = 1;
        FileFilter filters[filterCount] = {
            { "Wave (*.wav)", "*.wav" }
        };

        sAskForPath = false;

        if (OpenFileDialog(&sRiffWaveInfo.path, nullptr, filterCount, filters))
        {
            sEncoding = WaveFile::Encoding::DspAdpcm;
            sead::Path::getFileName(&sFileName, sRiffWaveInfo.path);

            bool success = WaveFile::readRiffWavInfo(&sRiffWaveInfo);
            if (!success)
            {
                return nullptr;
            }
        }
        else
        {
            return nullptr;
        }
    }

    ImGui::Text("Import '%s'", sFileName.cstr());

    DrawWaveImportInfo(&sEncoding, &sRiffWaveInfo);

    auto doCreate = []() -> Item*
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

    return doCreate;
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

void WaveFileContextMenuFunc(Item* item)
{
    WaveFile* wave = nullptr;
    if (item)
    {
        wave = static_cast<WaveFile*>(item);
    }

    ImGui::Separator();

    bool disableMenu = wave == nullptr;
    if (disableMenu)
    {
        ImGui::BeginDisabled();
    }

    if (ImGui::MenuItem("Replace"))
    {
        sImportWaveFile = nullptr;
        sRiffWaveInfo.clear();
        sWavFileName.clear();

        const u32 filterCount = 1;
        FileFilter filters[filterCount] = {
            { "Wave (*.wav)", "*.wav" }
        };

        if (OpenFileDialog(&sRiffWaveInfo.path, nullptr, filterCount, filters))
        {
            bool success = WaveFile::readRiffWavInfo(&sRiffWaveInfo);
            if (success)
            {
                sEncoding = WaveFile::Encoding::DspAdpcm;
                sImportWaveFile = wave;
                sead::Path::getFileName(&sWavFileName, sRiffWaveInfo.path);
            }
        }
    }

    if (ImGui::MenuItem("Export"))
    {
        sead::FixedSafeString<512> path;

        const u32 filterCount = 1;
        FileFilter filters[filterCount] = {
            { "Wave (*.wav)", "*.wav" }
        };

        if (SaveFileDialog(&path, nullptr, filterCount, filters, "wav"))
        {
            wave->writeWavFile(path);
        }
    }

    if (disableMenu)
    {
        ImGui::EndDisabled();
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
    }

    ImGui::SameLine();

    return nullptr;
}

void DrawSequenceFilesUI()
{
    DrawAllItemsUI("Sequence File", sBfsar.getSequenceFileList(),
        &CreateSequenceFileFunc, &SequenceFileNamePrefixFunc, nullptr, GetItemFilterCallback(), true
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
        &CreateBankFileFunc, &BankFileNamePrefixFunc, nullptr, GetItemFilterCallback(), true
    );
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

    ImGui::Text(text);
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
