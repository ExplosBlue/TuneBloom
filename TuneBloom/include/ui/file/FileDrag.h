#pragma once

#include <basis/seadTypes.h>
#include <imgui/imgui.h>

#include <string>
#include <vector>

enum class FileDragOrigin
{
    Browser,
    Tab,
    Recent
};

struct FileDrag
{
    std::vector<std::string> paths;
    std::string name;
    const char* icon = nullptr;
    ImU32 iconColor = 0;
    FileDragOrigin origin = FileDragOrigin::Browser;
    u32 serial = 0;
};

void StartFileDrag(FileDrag drag);
void DrawFileDragTooltip();
void DrawDragLabel(const char* icon, ImU32 iconColor, const char* name, const char* action);

const FileDrag* GetFileDrag();
const FileDrag* AcceptFileDrop(const char* action, ImGuiDragDropFlags flags = 0, bool* outIsPreview = nullptr);

bool IsWavPath(const std::string& path);
const std::string* FindDraggedWav(const FileDrag& drag);

void UpdateFileDropElsewhere();

template <typename MakeDrag>
void FileDragSource(MakeDrag makeDrag)
{
    if (!ImGui::BeginDragDropSource())
        return;

    if (!ImGui::GetDragDropPayload())
        StartFileDrag(makeDrag());

    DrawFileDragTooltip();
    ImGui::EndDragDropSource();
}
