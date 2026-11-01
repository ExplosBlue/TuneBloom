#pragma once

#include <basis/seadTypes.h>
#include <prim/seadSafeString.h>

#include <imgui/imgui.h>

#include <bfsar/model/ArchiveFormat.h>

#include <string>

inline constexpr f32 cSmallFontSize = 18.0f;

void NormalizePathSlashes(sead::BufferedSafeString* path);
ImFont* FindFontOfSize(f32 size);
ImFont* GetSmallFont();
f32 CalcCapCenterOffset(const ImFont* font, f32 size);

enum class TransportGlyph
{
    Play,
    Pause,
    Stop,
};

enum class IconButtonTint
{
    Accent,
    Play,
};

void PushBrightButtonColors(const ImVec4& color);
void PopBrightButtonColors();
void DrawSegmentFace(const ImVec2& min, const ImVec2& max, ImU32 color, bool isSelected);
bool DrawIconButton(const char* id, const char* icon, const ImVec2& itemSize, f32 frameSize, f32 iconSize, bool lit, IconButtonTint tint = IconButtonTint::Accent, ImGuiButtonFlags flags = ImGuiButtonFlags_MouseButtonLeft);
bool DrawGlyphButton(const char* id, TransportGlyph glyph, const ImVec2& itemSize, f32 frameSize, bool lit, IconButtonTint tint = IconButtonTint::Accent, ImGuiButtonFlags flags = ImGuiButtonFlags_MouseButtonLeft);
bool TransportButton(TransportGlyph glyph, const char* id, const ImVec2& size = ImVec2(0.0f, 0.0f));
bool PlayToggleButton(TransportGlyph glyph, const char* id, const ImVec2& size, bool playing);
bool ToggleButton(const char* label, bool on, const ImVec2& size = ImVec2(0.0f, 0.0f));
f32 CalcTextButtonWidth(const char* label);
bool TextButton(const char* label);
f32 CalcTextActionWidth(const char* label);
bool DrawTextAction(const char* id, const char* label, f32 height, f32 textOffsetY, bool* outHovered = nullptr);
bool IsItemEditCommitted(bool changed);
f32 GetRowButtonSize();
bool DrawRowPlayButton(u32 id, const char* playbackError, bool playing);
bool DrawRowOpenButton(u32 id, const char* tooltip, const char* disabledReason = nullptr);
bool DrawBareRowSelectable(const char* id, bool selected, const ImVec2& size);
bool DrawTableRowSelectable(const char* id, bool selected, bool* outHovered);
void DrawInsertLine(ImDrawList* drawList, f32 left, f32 right, f32 centerY);
bool DrawRowHoverButton(const char* id, const char* icon, bool visible, const char* tooltip, const ImVec2& hitSize, f32 size);
void DrawSpinner(const ImVec2& center, f32 radius, ImU32 color);
void DrawProgressBar(const ImVec2& min, const ImVec2& max, f32 fraction);

s32 GetItemWheelSteps();
f32 CalcFitComboWidth(const char* const* items, s32 itemNum);
bool ComboScroll(const char *label, int *current_item, const char *const items[], int items_count, int popup_max_height_in_items = -1);
bool ComboScrollSectioned(const char* label, s32* current, const char* const items[], s32 itemNum, s32 sectionStart);
bool ComboScroll(const char* label, s32* current, const char* const items[], const char* const details[], s32 itemNum, const char* detail = nullptr);
bool ComboScroll(const char* label, s32* current, const char* const items[], const char* const details[], const char* const reasons[], s32 itemNum, const char* detail = nullptr);

sead::FixedSafeString<24> FormatPanLabel(f32 pan);
sead::FixedSafeString<24> FormatPanLabel(u8 pan, u8 min, u8 center, u8 max);
sead::FixedSafeString<32> FormatCountText(u64 count);

f32 CalcVersionUIWidth();
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

const char* FindTextTailStart(const char* text, f32 width);
const char* FindWordTailStart(const char* text, f32 width);
const char* FindTextHeadEnd(const char* text, f32 width);
std::string CutPathStart(const std::string& path, size_t maxLength);

void HelpMarker(const char* desc);
bool IsItemHoveredForDetail();
void DetailTooltip(const char* detail);
struct ListOpener
{
    f32 top = 0.0f;
    f32 bottom = 0.0f;
};

ListOpener GetNextListOpener();
void ListItemDetailTooltip(const char* detail, const ListOpener& opener);
