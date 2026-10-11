#pragma once

#include <bfsar/archive/Bfsar.h>

extern Bfsar sBfsar;

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

bool ValidBFSARHeader(const void* file);
bool ValidBFSARHeader(const void* file, u32 size);
bool CheckBlockCorrupt(const char* fileName, const char* blockName, const void* block);
bool CheckBlockCorruptError(const char* fileName, const char* blockName, const void* block);
