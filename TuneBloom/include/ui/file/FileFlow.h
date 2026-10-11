#pragma once

void RequestNewFile();
void RequestNewFileFormat();
void RequestOpenFile();
void RequestOpenFile(const char* path);
void RequestSaveFile();
void RequestCloseFile();
bool TryExit();

void CancelPendingExportsAndImports();

void HandleFileShortcuts();
void DrawFileFlow();
