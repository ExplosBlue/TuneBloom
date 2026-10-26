#pragma once

class Document;

void RequestNewFile();
void RequestOpenFile();
void RequestOpenFile(const char* path);
void RequestPreviewFile(const char* path);
void RequestSaveFile();
void RequestSaveAll();
void RequestCloseFile();
void RequestCloseDocument(Document* document);
void RequestReloadDocument(Document* document);
void RequestExitAfterSaveAll();
void RequestExitWithoutSaving();
bool TryExit();

void CancelPendingExportsAndImports();
void ForgetDocumentRequests(const Document* document);

void HandleFileShortcuts();
void DrawFileFlow();
