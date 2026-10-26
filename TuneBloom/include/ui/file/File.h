#pragma once

#include <aal/AudioResource.h>
#include <bfsar/archive/Bfsar.h>

#include <string>

extern Bfsar sBfsar;
extern AudioResource sAudioResource;

extern sead::FixedSafeString<512> sDroppedFilePath;

bool NewFile();
bool NewFile(ArchiveFormat format);
bool NewFile(ArchiveFormat format, ArchivePlatform platform);
bool OpenFile();
bool OpenFile(const char* path);
void SetNextOpenPath(const char* path);
void ClearNextOpenPath();
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
bool IsDocumentPath(const char* path);
bool IsValidDocumentName(const sead::SafeString& name);
bool IsUniqueDocumentName(const sead::SafeString& name, const Item* ignoreItem);

bool ValidBFSARHeader(const void* file);
bool ValidBFSARHeader(const void* file, u32 size);
bool CheckBlockCorrupt(const char* fileName, const char* blockName, const void* block);
bool CheckBlockCorruptError(const char* fileName, const char* blockName, const void* block);
