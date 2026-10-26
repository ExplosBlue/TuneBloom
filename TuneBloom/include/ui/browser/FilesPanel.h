#pragma once

enum class BrowserFileKind;
struct ImVec4;

inline constexpr const char* cFilesWindowId = "###FilesWindow";

const char* GetFileKindIcon(BrowserFileKind kind);
ImVec4 GetFileKindColor(BrowserFileKind kind);

void LoadFilesPanelConfig();
void SetFilesPanelRoot(const char* path);
void RequestFilesPanelRoot(const char* path);
void DrawFilesPanel();
