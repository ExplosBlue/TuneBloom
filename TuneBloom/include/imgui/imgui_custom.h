#pragma once

#include <imgui/imgui.h>

struct ImGuiWindow;

namespace ImGui {

bool BeginComboCustom(const char* label, const char* preview_value);
bool InputTextExCustom(const char* label, const char* hint, char* buf, int buf_size, const ImVec2& size_arg, ImGuiInputTextFlags flags = 0, ImGuiInputTextCallback callback = nullptr, void* callback_user_data = nullptr);

bool InputTextCombo(const char* label, char* buf, int buf_size, const char* const items[], int items_count);
bool TextMatchesQuery(const char* text, const char* query, bool case_sensitive = false);
bool BeginSearchPopup(const char* str_id, const char* hint, const ImVec2& anchor_min, const ImVec2& anchor_max, int items_count);
bool SearchPopupItem(const char* label, bool selected);
void EndSearchPopup();

void SetSmoothWheelScroll(bool enabled);
void SetWheelScrollSpeed(float speed);

void SetWindowShadowAlpha(float alpha);
void RenderWindowShadow(ImGuiWindow* window);

void AddRectFilledGradient(ImDrawList* draw_list, const ImVec2& p_min, const ImVec2& p_max, ImU32 col, float amount, float rounding = 0.0f, ImDrawFlags flags = 0);

void SetRaisedGradient(float amount);
void AddRectFilledRaised(ImDrawList* draw_list, const ImVec2& p_min, const ImVec2& p_max, ImU32 col, float rounding = 0.0f, ImDrawFlags flags = 0);

void SetSurfaceGradient(float amount);
void AddRectFilledSurface(ImDrawList* draw_list, const ImVec2& p_min, const ImVec2& p_max, ImU32 col, float rounding = 0.0f, ImDrawFlags flags = 0);

void SetHoverAnimation(bool enabled);
float GetHoverRatio(ImGuiID id, bool hovered);
ImU32 GetHoverColorU32(ImGuiID id, bool hovered, bool active, ImGuiCol idle_col, ImGuiCol hovered_col, ImGuiCol active_col, float alpha_mul = 1.0f);
ImU32 GetHoverColorU32(ImGuiID id, bool hovered, ImU32 idle_col, ImU32 hovered_col);

} // namespace ImGui
