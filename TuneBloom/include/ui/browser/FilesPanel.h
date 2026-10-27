#pragma once

enum class BrowserFileKind;
struct ImVec4;

const char* GetFileKindIcon(BrowserFileKind kind);
ImVec4 GetFileKindColor(BrowserFileKind kind);

void LoadFilesPanelConfig();
void RequestFilesPanelRoot(const char* path);
void RequestFilesPanelFolderDialog();
bool TakeFilesPanelRequests();
void DrawFilesPanel();
