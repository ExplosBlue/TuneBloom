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

inline void SetDockNodeWidth(ImGuiDockNode* node, f32 width)
{
    if (ImAbs(node->Size.x - width) < 1.0f)
        return;

    node->Size.x = width;
    node->WantLockSizeOnce = true;
}

inline ImGuiDockNode* FindDockColumnNode(ImGuiDockNode* node)
{
    while (node && node->ParentNode && node->ParentNode->SplitAxis != ImGuiAxis_X)
        node = node->ParentNode;

    return node && node->ParentNode ? node : nullptr;
}
