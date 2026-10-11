#pragma once

#include <filedevice/seadFileDevice.h>
#include <prim/seadSafeString.h>

#include <vector>

namespace fileio
{

inline constexpr s32 cPathLengthMax = 500;

enum class WriteResult
{
    Ok,
    OpenFailed,
    WriteFailed
};

bool ReadWholeFile(const char* path, std::vector<u8>* out);
bool ReadFileStart(const char* path, u64 sizeMax, std::vector<u8>* out);
bool HasSameBytes(const char* pathA, const char* pathB);
WriteResult WriteWholeFile(const char* path, const void* data, size_t size);
bool WriteWholeFileAtomic(const char* path, const void* data, size_t size);
bool RenameOver(const char* tempPath, const char* destPath);
bool CopyFileOver(const char* srcPath, const char* destPath);
void RemoveFile(const char* path);
const char* GetTempFolder();

bool OpenForWrite(sead::FileHandle* handle, const sead::SafeString& path);

class StagedFile
{
public:
    StagedFile();
    ~StagedFile();

    bool open(const sead::SafeString& path);
    bool commit();

    sead::FileHandle* getHandle()
    {
        return &mHandle;
    }

private:
    sead::FileHandle mHandle;
    sead::FixedSafeString<cPathLengthMax + 12> mPath;
    sead::FixedSafeString<cPathLengthMax + 12> mTempPath;
    bool mInPlace;
};

}
