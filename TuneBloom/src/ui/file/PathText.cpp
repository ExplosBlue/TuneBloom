#include <ui/file/PathText.h>

#include <imgui/imgui.h>

#include <algorithm>

namespace pathtext
{

static constexpr const char* cLeadingCut         = "...";
static constexpr f32         cTooltipFolderMinEm = 22.0f;

static bool IsSeparator(char c)
{
    return c == '/' || c == '\\';
}

const char* FileName(const char* path)
{
    const char* name = path;

    for (const char* c = path; *c != '\0'; c++)
    {
        if (IsSeparator(*c))
            name = c + 1;
    }

    return name;
}

std::string Folder(const std::string& path)
{
    const char* name = FileName(path.c_str());

    if (name == path.c_str())
        return std::string();

    return std::string(path.c_str(), name - 1);
}

std::string FitToWidth(const std::string& path, f32 width)
{
    if (ImGui::CalcTextSize(path.c_str()).x <= width)
        return path;

    std::string fitted;

    for (size_t i = 1; i < path.size(); i++)
    {
        if (!IsSeparator(path[i]))
            continue;

        fitted = cLeadingCut + path.substr(i);

        if (ImGui::CalcTextSize(fitted.c_str()).x <= width)
            return fitted;
    }

    return fitted.empty() ? path : fitted;
}

void DrawTooltipFolder(const std::string& path, f32 contentWidth)
{
    const std::string folder = Folder(path);

    if (folder.empty())
        return;

    const f32 width = std::max(contentWidth, ImGui::GetFontSize() * cTooltipFolderMinEm);

    ImGui::TextDisabled("%s", FitToWidth(folder, width).c_str());
}

}
