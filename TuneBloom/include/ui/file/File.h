#pragma once

#include <aal/AudioResource.h>
#include <bfsar/archive/Bfsar.h>
#include <ui/file/Document.h>

#include <string>

bool NewFile(ArchiveFormat format, ArchivePlatform platform);
bool OpenFile();
void SetStartupFile(const char* path);
bool IsOpenPending();
void UpdatePendingOpen();
void RunStartupAction();
bool SaveFile();
bool SaveFileAs();
void MarkArchiveInSync();
bool CloseFile();
bool Exit();

bool IsDocumentOpen();
bool IsAudioResourceOpen();
const sead::SafeString& GetDocumentPath();
const sead::SafeString& GetDocumentLoadedPath();
const char* GetDocumentExtension();
void RenumberDocumentList(Item::List& list);
std::string GetDocumentName();
bool OpenExternalPath(const char* path);
bool IsValidDocumentName(const sead::SafeString& name);
bool IsUniqueDocumentName(const sead::SafeString& name, const Item* ignoreItem);

bool CheckBlockCorrupt(const char* fileName, const char* blockName, const void* block);
bool CheckBlockCorruptError(const char* fileName, const char* blockName, const void* block);
