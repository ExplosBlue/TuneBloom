#pragma once

#include <basis/seadTypes.h>
#include <ui/app/Shortcuts.h>

#include <imgui/imgui.h>

#include <optional>

enum class MenuRole;
struct MenuTree;

void DrawMenuBar();
void RecordMenuBar(MenuTree* tree);
void RequestMenuActivation(u32 id);
void SetNextMenuRole(MenuRole role);
bool IsSlimMenuBar();

ImDrawList* BeginMenuHighlight();
void EndMenuHighlight(ImDrawList* drawList, bool open, f32 hoverAlpha = 1.0f, const ImVec2* rangeY = nullptr);
bool MenuBarItem(const char* label, std::optional<shortcuts::Action> shortcut = std::nullopt, bool selected = false, bool enabled = true);
bool MenuBarIconItem(const char* icon, const char* label, std::optional<shortcuts::Action> shortcut = std::nullopt, bool enabled = true, const char* disabledReason = nullptr);
void MenuSeparator();
void BeginMenuDisabled(bool disabled);
void EndMenuDisabled();

bool VisitMenuFileItem(const char* path);

bool BeginSubMenu(const char* label);
bool BeginIconSubMenu(const char* icon, const char* label);
void EndSubMenu();

void SetMenuBarTooltip(const char* tooltip);

f32 CalcMenuBarIconButtonWidth(const char* icon);
bool MenuBarIconButton(const char* id, const char* icon, f32 right, f32 top, f32 bottom, bool isOpen = false, const ImVec4* iconColor = nullptr, ImVec2* iconPos = nullptr);
