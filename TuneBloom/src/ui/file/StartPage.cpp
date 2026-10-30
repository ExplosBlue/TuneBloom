#include <ui/file/StartPage.h>

#include <ui/file/FileFlow.h>
#include <ui/file/RecentFiles.h>
#include <ui/widgets/Properties.h>
#include <ui/Messages.h>

#include <imgui/imgui_internal.h>

#include <cstdio>
#include <string>

static bool BeginStartChild()
{
    const f32 border = ImMax(ImGui::GetStyle().ChildBorderSize, 1.0f);
    const ImVec2 size = ImGui::GetContentRegionAvail() + ImVec2(0.0f, border * 2.0f);

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - border);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(border, border));
    const bool open = ImGui::BeginChild("##StartRecent", size, ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();

    return open;
}

void DrawStartPage()
{
    const s32 fileNum = static_cast<s32>(GetRecentFiles().size());

    std::string path;
    bool open = false;

    if (BeginStartChild())
    {
        char count[16];
        snprintf(count, sizeof(count), "%d", fileNum);

        DrawSectionTitle(messages::common::cRecentFiles, ImGui::GetContentRegionAvail().x, fileNum > 0 ? count : nullptr);
        open = DrawRecentFilesTable(&path);
    }
    ImGui::EndChild();

    if (open)
        RequestOpenFile(path.c_str());
}
