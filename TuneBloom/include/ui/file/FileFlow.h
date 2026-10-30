#pragma once

#include <ui/file/OpenQueue.h>

class Document;

void RequestOpenFile();
void RequestOpenFile(const char* path, openqueue::Origin origin = openqueue::Origin::App);
void RequestPreviewFile(const char* path);
void RequestSaveFile();
void RequestSaveDocument(Document* document);
void RequestSaveDocumentAs(Document* document);
void RequestSaveAll();
bool IsSaveQueueBusy();
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
