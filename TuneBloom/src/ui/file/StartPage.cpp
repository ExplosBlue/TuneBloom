#define IMGUI_DEFINE_MATH_OPERATORS
#include <ui/file/StartPage.h>

#include <ui/app/About.h>
#include <ui/app/Shortcuts.h>
#include <ui/app/ShortcutsDialog.h>
#include <ui/file/File.h>
#include <ui/file/FileFlow.h>
#include <ui/file/RecentFiles.h>
#include <ui/file/SystemShell.h>
#include <ui/items/ItemColumns.h>
#include <ui/project/ProjectViews.h>
#include <ui/widgets/Knob.h>
#include <ui/widgets/Properties.h>
#include <ui/Messages.h>

#include <app/Utilll.h>

#include <libs/IconsLucide.h>
#include <imgui/imgui_internal.h>

#include <string>

struct StartRow
{
    const char* icon;
    const char* label;
    const char* (*shortcut)();
    void (*run)();
};

static constexpr f32 cStartPadEm        = 0.5f;
static constexpr f32 cStartSideWidthEm  = 15.0f;
static constexpr f32 cStartStackWidthEm = 40.0f;
static constexpr f32 cStartBoxRowEm     = 30.0f;
static constexpr f32 cStartLogoEm       = 3.0f;
static constexpr f32 cStartGapEm        = 1.0f;

static const char* GetNewArchiveShortcut()
{
    return shortcuts::Label(shortcuts::Action::NewProject);
}

static const char* GetOpenArchiveShortcut()
{
    return shortcuts::Label(shortcuts::Action::OpenProject);
}

static void OpenArchive()
{
    OpenFile();
}

static void OpenRepository()
{
    OpenURL(cRepositoryUrl);
}

static void OpenIssues()
{
    OpenURL(cIssuesUrl);
}

static void OpenDiscord()
{
    OpenURL(cDiscordUrl);
}

static const StartRow cFileRows[] = {
    { ICON_LC_FILE,        messages::start::cNewArchive,  GetNewArchiveShortcut,  RequestNewFileFormat },
    { ICON_LC_FOLDER_OPEN, messages::start::cOpenArchive, GetOpenArchiveShortcut, OpenArchive },
};

static const StartRow cHelpRows[] = {
    { ICON_LC_KEYBOARD,       messages::mainmenu::cShortcuts,   nullptr, OpenShortcutsDialog },
    { ICON_LC_FOLDER_GIT_2,   messages::mainmenu::cRepository,  nullptr, OpenRepository },
    { ICON_LC_BUG,            messages::mainmenu::cReportIssue, nullptr, OpenIssues },
    { ICON_LC_MESSAGE_CIRCLE, messages::mainmenu::cDiscord,     nullptr, OpenDiscord },
};

static void DrawStartHead()
{
    const f32 em = ImGui::GetFontSize();
    const f32 logoSize = ImFloor(em * cStartLogoEm);
    const f32 textHeight = em * 2.0f + ImGui::GetStyle().ItemSpacing.y;

    DrawAppIcon(logoSize);
    ImGui::SameLine();

    ImGui::BeginGroup();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImFloor((logoSize - textHeight) * 0.5f));
    ImGui::TextUnformatted(util::cAppName.cstr());
    ImGui::TextColored(GetItemValueColor(), GetAppVersionFormat(), util::cAppVersion.cstr());
    ImGui::EndGroup();
}

static void DrawStartCard(const char* id, const char* title, const StartRow* rows, s32 rowNum, f32 width)
{
    ImGui::BeginGroup();
    DrawSectionTitle(title, width);

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, CalcNavRowGap()));

    if (BeginNavBox(id, CalcNavBoxPadding(true), true, width))
    {
        for (s32 i = 0; i < rowNum; i++)
        {
            ImGui::PushID(i);

            if (DrawNavRow(rows[i].icon, rows[i].label, false, rows[i].shortcut ? rows[i].shortcut() : nullptr))
                rows[i].run();

            ImGui::PopID();
        }

        EndNavBox();
    }

    ImGui::PopStyleVar();
    ImGui::EndGroup();
}

static void DrawStartGap(f32 gap)
{
    ImGui::Dummy(ImVec2(0.0f, gap - ImGui::GetStyle().ItemSpacing.y));
}

static void DrawStartSide(f32 width, bool stacked)
{
    const f32 gap = ImFloor(ImGui::GetFontSize() * cStartGapEm);

    DrawStartHead();
    DrawStartGap(gap);

    if (!stacked || width < ImGui::GetFontSize() * cStartBoxRowEm)
    {
        DrawStartCard("##FileRows", messages::start::cStartTitle, cFileRows, IM_ARRAYSIZE(cFileRows), width);
        DrawStartGap(gap);
        DrawStartCard("##HelpRows", messages::start::cHelpTitle, cHelpRows, IM_ARRAYSIZE(cHelpRows), width);
        return;
    }

    const f32 boxWidth = ImFloor((width - gap) * 0.5f);

    DrawStartCard("##FileRows", messages::start::cStartTitle, cFileRows, IM_ARRAYSIZE(cFileRows), boxWidth);
    ImGui::SameLine(0.0f, gap);
    DrawStartCard("##HelpRows", messages::start::cHelpTitle, cHelpRows, IM_ARRAYSIZE(cHelpRows), width - boxWidth - gap);
}

static bool BeginStartChild(const char* id, const ImVec2& size)
{
    const f32 border = sead::Mathf::max(ImGui::GetStyle().ChildBorderSize, 1.0f);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(border, border));
    const bool open = ImGui::BeginChild(id, size, ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();

    return open;
}

static bool DrawStartRecent(const ImVec2& size, std::string* outPath)
{
    bool open = false;

    if (BeginStartChild("##StartRecent", size))
    {
        const f32 width = ImGui::GetContentRegionAvail().x;

        if (HasPinnedFiles())
        {
            DrawSectionTitle(messages::recent::cPinnedTitle, width);
            open |= DrawPinnedFilesTable(outPath);
            DrawStartGap(ImFloor(ImGui::GetFontSize() * cStartGapEm));
        }

        DrawSectionTitle(messages::recent::cRecentTitle, width);
        open |= DrawRecentFilesTable(outPath);
    }
    ImGui::EndChild();

    return open;
}

void DrawStartPage()
{
    const f32 em = ImGui::GetFontSize();
    const f32 pad = ImFloor(em * cStartPadEm);
    const f32 gap = ImFloor(em * cStartGapEm);
    const ImVec2 origin = ImGui::GetCursorPos() + ImVec2(pad, pad);
    const ImVec2 area = ImGui::GetContentRegionAvail() - ImVec2(pad * 2.0f, pad * 2.0f);
    const bool stacked = area.x < em * cStartStackWidthEm;
    const f32 sideWidth = stacked ? area.x : ImFloor(em * cStartSideWidthEm);

    std::string path;
    bool open = false;

    ImGui::SetCursorPos(origin);

    if (stacked)
    {
        ImGui::BeginGroup();
        DrawStartSide(sideWidth, true);
        ImGui::EndGroup();

        const f32 top = ImGui::GetItemRectMax().y - ImGui::GetWindowPos().y + ImGui::GetScrollY() + gap;

        ImGui::SetCursorPos(ImVec2(origin.x, top));
        open = DrawStartRecent(ImVec2(area.x, sead::Mathf::max(origin.y + area.y - top, em * 4.0f)), &path);
    }
    else
    {
        if (BeginStartChild("##StartSide", ImVec2(sideWidth, area.y)))
            DrawStartSide(ImGui::GetContentRegionAvail().x, false);
        ImGui::EndChild();

        ImGui::SetCursorPos(ImVec2(origin.x + sideWidth + gap, origin.y));
        open = DrawStartRecent(ImVec2(area.x - sideWidth - gap, area.y), &path);
    }

    if (open)
        OpenFile(path.c_str());
}
