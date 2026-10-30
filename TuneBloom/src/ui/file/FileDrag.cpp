#include <ui/file/FileDrag.h>

#include <ui/file/FileFlow.h>
#include <ui/file/OpenQueue.h>
#include <ui/Messages.h>

#include <bfsar/model/ArchiveFormat.h>

#include <imgui/imgui_internal.h>

#include <cstring>
#include <filesystem>

static constexpr const char* cFileDragType = "FILES";
static constexpr f32 cDragNameMaxEm = 14.0f;

struct FileDragState
{
    FileDrag drag;
    std::vector<std::string> openPaths;
    u32 serial = 0;
    u32 droppedSerial = 0;
    s32 actionFrame = -1;
    const char* frameAction = nullptr;
    const char* shownAction = nullptr;
};

static FileDragState& GetState()
{
    static FileDragState* state = new FileDragState();
    return *state;
}

static bool IsOpenablePath(const std::string& path)
{
    std::error_code error;

    return !IsWavPath(path) && !std::filesystem::is_directory(path, error);
}

void StartFileDrag(FileDrag drag)
{
    if (drag.paths.empty())
        return;

    FileDragState& state = GetState();

    state.drag = std::move(drag);
    state.drag.serial = ++state.serial;
    state.actionFrame = -1;
    state.frameAction = nullptr;
    state.shownAction = nullptr;
    state.openPaths.clear();

    if (state.drag.origin != FileDragOrigin::Tab)
    {
        for (const std::string& path : state.drag.paths)
        {
            if (IsOpenablePath(path))
                state.openPaths.push_back(path);
        }
    }

    ImGui::SetDragDropPayload(cFileDragType, &state.serial, sizeof(state.serial), ImGuiCond_Once);
}

void DrawFileDragTooltip()
{
    const FileDrag* drag = GetFileDrag();

    if (!drag)
        return;

    DrawDragLabel(drag->icon, drag->iconColor, drag->name.c_str(), GetState().shownAction);
}

void DrawDragLabel(const char* icon, ImU32 iconColor, const char* name, const char* action)
{
    if (icon)
    {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(iconColor), "%s", icon);
        ImGui::SameLine();
    }

    const f32 nameX = ImGui::GetCursorPosX();
    const f32 maxWidth = ImFloor(ImGui::GetFontSize() * cDragNameMaxEm);

    if (ImGui::CalcTextSize(name).x <= maxWidth)
    {
        ImGui::TextUnformatted(name);
    }
    else
    {
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const ImVec2 max(pos.x + maxWidth, pos.y + ImGui::GetTextLineHeight());

        ImGui::Dummy(ImVec2(maxWidth, ImGui::GetTextLineHeight()));
        ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), pos, max, max.x, max.x, name, nullptr, nullptr);
    }

    if (!action)
        return;

    ImGui::SetCursorPosX(nameX);
    ImGui::TextDisabled("%s", action);
}

const FileDrag* GetFileDrag()
{
    const ImGuiPayload* payload = ImGui::GetDragDropPayload();
    FileDragState& state = GetState();

    if (!payload || !payload->IsDataType(cFileDragType) || *static_cast<const u32*>(payload->Data) != state.serial)
        return nullptr;

    return &state.drag;
}

const FileDrag* AcceptFileDrop(const char* action, ImGuiDragDropFlags flags, bool* outIsPreview)
{
    if (outIsPreview)
        *outIsPreview = false;

    const FileDrag* drag = GetFileDrag();

    if (!drag)
        return nullptr;

    const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(cFileDragType, flags | ImGuiDragDropFlags_AcceptBeforeDelivery);

    if (!payload)
        return nullptr;

    FileDragState& state = GetState();

    state.actionFrame = ImGui::GetFrameCount();
    state.frameAction = action;

    if (outIsPreview)
        *outIsPreview = payload->Preview;

    if (!payload->Delivery || state.droppedSerial == state.serial)
        return nullptr;

    state.droppedSerial = state.serial;

    return drag;
}

bool IsWavPath(const std::string& path)
{
    return HasFileExtension(path.c_str(), path.size(), cWavExtension);
}

const std::string* FindDraggedWav(const FileDrag& drag)
{
    for (const std::string& path : drag.paths)
    {
        if (IsWavPath(path))
            return &path;
    }

    return nullptr;
}

static void OpenDroppedFiles(const FileDragState& state)
{
    const openqueue::Origin origin = state.drag.origin == FileDragOrigin::Browser ? openqueue::Origin::Browser : openqueue::Origin::App;

    for (const std::string& path : state.openPaths)
        RequestOpenFile(path.c_str(), origin);
}

void UpdateFileDropElsewhere()
{
    if (!GetFileDrag())
        return;

    FileDragState& state = GetState();

    const bool hasTarget = state.actionFrame == ImGui::GetFrameCount();
    const bool isOverApp = ImGui::GetCurrentContext()->HoveredWindow != nullptr;
    const bool canOpen = !state.openPaths.empty() && isOverApp;

    state.shownAction = hasTarget ? state.frameAction : canOpen ? messages::common::cOpen : nullptr;

    if (hasTarget || !canOpen || !ImGui::IsMouseReleased(ImGuiMouseButton_Left) || state.droppedSerial == state.serial)
        return;

    state.droppedSerial = state.serial;
    OpenDroppedFiles(state);
}
