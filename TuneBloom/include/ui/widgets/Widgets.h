#pragma once

#include <basis/seadTypes.h>
#include <prim/seadSafeString.h>

#include <imgui/imgui.h>

#include <bfsar/model/ArchiveFormat.h>

#include <string>

void GetFileStem(sead::BufferedSafeString* out, const sead::SafeString& path);
void NormalizePathSlashes(sead::BufferedSafeString* path);

bool DrawIconButton(const char* id, const char* icon, const ImVec2& itemSize, f32 frameSize, f32 iconSize, bool lit, ImGuiButtonFlags flags = ImGuiButtonFlags_MouseButtonLeft);
f32 GetRowButtonSize();
bool DrawRowPlayButton(u32 id, const char* playbackError, bool playing);
bool DrawRowOpenButton(u32 id, const char* tooltip, const char* disabledReason = nullptr);
bool DrawTableRowSelectable(const char* id, bool selected, bool* outHovered);

s32 GetItemWheelSteps();
bool ComboScroll(const char *label, int *current_item, const char *const items[], int items_count, int popup_max_height_in_items = -1);
bool ComboScroll(const char* label, s32* current, const char* const items[], const char* const details[], s32 itemNum, const char* detail = nullptr);

sead::FixedSafeString<24> FormatPanLabel(f32 pan);
sead::FixedSafeString<24> FormatPanLabel(u8 pan, u8 min, u8 center, u8 max);

bool DrawVersionUI(u32 *versionPtr, ArchiveFormat format, bool showLabel = true);
bool DrawInnerVersionUI(u32 *versionPtr, u32 versionByteNum = 3);

void CenteredText(const char* text, const ImVec2& sizeArg = ImGui::GetWindowSize());
void CenteredTextWrapped(const char* text, f32 width);

inline void CenteredTextX(const char* text, f32 sizeX = ImGui::GetWindowSize().x)
{
    CenteredText(text, ImVec2(sizeX, -1.0f));
}

template <typename T>
bool ResetItemOnRightClick(T* value, const T& reset)
{
    if (*value == reset || !ImGui::IsItemClicked(ImGuiMouseButton_Right))
        return false;

    *value = reset;
    return true;
}

std::string CutPathStart(const std::string& path, size_t maxLength);

void HelpMarker(const char* desc);
bool IsItemHoveredAllowDisabled();
bool IsItemHoveredForDetail();
void SetDisabledTooltip(const char* reason);
void DetailTooltip(const char* detail);
void ListItemDetailTooltip(const char* detail);
