#pragma once

#include <imgui/imgui.h>

class TextEditor;

inline constexpr ImVec4 cSeqErrorColor(1.0f, 0.35f, 0.35f, 1.0f);

void DrawSeqEditor(TextEditor& editor);
