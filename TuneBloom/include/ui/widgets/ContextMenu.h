#pragma once

#include <basis/seadTypes.h>

inline constexpr f32 cMenuRoundingEm = 0.4f;
inline constexpr f32 cMenuItemGapEm  = 0.35f;

bool BeginContextMenu(const char* id);
bool BeginItemContextMenu(const char* id = nullptr);
bool BeginWindowContextMenu(const char* id = nullptr);
bool BeginTableContextMenu();
void EndContextMenu();

bool ContextMenuItem(const char* icon, const char* label, bool enabled = true, const char* disabledReason = nullptr);
bool ContextMenuDangerItem(const char* icon, const char* label);
bool ContextMenuCheckItem(const char* label, bool checked, bool enabled = true);
void ContextMenuSeparator();

bool BeginContextSubMenu(const char* icon, const char* label);
void EndContextSubMenu();

void DrawTableContextMenu();
