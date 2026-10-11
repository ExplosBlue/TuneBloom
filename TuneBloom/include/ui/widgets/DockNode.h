#pragma once

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <basis/seadTypes.h>

inline void SetDockNodeHeight(ImGuiDockNode* node, f32 height)
{
    if (ImAbs(node->Size.y - height) < 1.0f)
        return;

    node->Size.y = height;
    node->WantLockSizeOnce = true;
}
