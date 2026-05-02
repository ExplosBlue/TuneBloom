#pragma once

#include <imgui/imgui.h>

namespace ImGui {

bool BeginComboCustom(const char* label, const char* preview_value);
bool InputTextExCustom(const char* label, const char* hint, char* buf, int buf_size, const ImVec2& size_arg, ImGuiInputTextFlags flags = 0, ImGuiInputTextCallback callback = nullptr, void* callback_user_data = nullptr);

bool InputTextCombo(const char* label, char* buf, int buf_size, const char* const items[], int items_count);

bool InputScalarCustom(const char* label, ImGuiDataType data_type, void* p_data, const void* p_step, const void* p_step_fast, const char* format, ImGuiInputTextFlags flags, bool* p_edited_with_button);

} // namespace ImGui
