#pragma once

#include <aal/AudioResource.h>
#include <bfsar/archive/Bfsar.h>
#include <ui/file/Document.h>

#include <string>
#include <vector>

void AddNewDocument(Document* document);
bool NewFile(ArchiveFormat format, ArchivePlatform platform);
bool OpenFile();
bool ChooseDocumentFiles(std::vector<std::string>* paths, const char* title);
void SetStartupFile(const char* path);
bool IsOpenPending();
void UpdatePendingOpen();
void RunStartupAction();
bool SaveFile();
bool SaveFileAs(Document* document);
void MarkArchiveInSync();
bool Exit();

bool IsDocumentOpen();
const sead::SafeString& GetDocumentPath();
const char* GetDocumentExtension();
void RenumberDocumentList(Item::List& list);
std::string GetDocumentName();
bool OpenExternalPath(const char* path);
bool PinExternalPath(const char* path);
bool IsValidDocumentName(const sead::SafeString& name);
const char* GetDocumentNameRuleDetail();
bool IsUniqueDocumentName(const sead::SafeString& name, const Item* ignoreItem);

bool CheckBlockCorrupt(const char* fileName, const char* blockName, const void* block);
bool CheckBlockCorruptError(const char* fileName, const char* blockName, const void* block);
