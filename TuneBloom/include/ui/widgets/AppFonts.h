#pragma once

#include <basis/seadTypes.h>

struct ImDrawList;
struct ImFont;
struct ImVec2;

void LoadAppFonts();
ImFont* GetCodeFont();

f32 CalcTabularTextWidth(ImFont* font, f32 fontSize, const char* text, const char* textEnd = nullptr);
f32 AddTabularText(ImDrawList* draw, ImFont* font, f32 fontSize, const ImVec2& pos, u32 color, const char* text, const char* textEnd = nullptr);
