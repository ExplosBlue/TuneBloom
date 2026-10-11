#include <bfsar/io/FileIo.h>

#include <filedevice/seadFileDeviceMgr.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#if defined(SEAD_PLATFORM_WINDOWS)
#include <windows.h>
#endif

namespace fileio
{

static constexpr const char* cTempSuffix = ".tmp";

bool ReadWholeFile(const char* path, std::vector<u8>* out)
{
    out->clear();

    FILE* file = fopen(path, "rb");

    if (!file)
        return false;

    fseek(file, 0, SEEK_END);
    const long size = ftell(file);
    fseek(file, 0, SEEK_SET);

    bool read = size > 0;

    if (read)
    {
        out->resize(static_cast<size_t>(size));
        read = fread(out->data(), 1, out->size(), file) == out->size();
    }

    fclose(file);

    if (!read)
        out->clear();

    return read;
}

bool ReadFileStart(const char* path, u64 sizeMax, std::vector<u8>* out)
{
    out->clear();

    FILE* file = fopen(path, "rb");

    if (!file)
        return false;

    out->resize(static_cast<size_t>(sizeMax));
    out->resize(fread(out->data(), 1, out->size(), file));

    fclose(file);

    return !out->empty();
}

WriteResult WriteWholeFile(const char* path, const void* data, size_t size)
{
    FILE* file = fopen(path, "wb");

    if (!file)
        return WriteResult::OpenFailed;

    const bool written = size == 0 || fwrite(data, 1, size, file) == size;
    const bool closed = fclose(file) == 0;

    return written && closed ? WriteResult::Ok : WriteResult::WriteFailed;
}

bool WriteWholeFileAtomic(const char* path, const void* data, size_t size)
{
    const std::string tempPath = std::string(path) + cTempSuffix;
    const WriteResult staged = WriteWholeFile(tempPath.c_str(), data, size);

    if (staged == WriteResult::OpenFailed)
        return WriteWholeFile(path, data, size) == WriteResult::Ok;

    if (staged == WriteResult::Ok && RenameOver(tempPath.c_str(), path))
        return true;

    RemoveFile(tempPath.c_str());

    return false;
}

static std::filesystem::path ResolveLink(const char* path)
{
    const std::filesystem::path link(path);
    std::error_code ec;

    if (!std::filesystem::is_symlink(link, ec))
        return link;

    const std::filesystem::path target = std::filesystem::weakly_canonical(link, ec);

    return ec ? link : target;
}

#if !defined(SEAD_PLATFORM_WINDOWS)
static void CopyPermissions(const std::filesystem::path& from, const std::filesystem::path& to)
{
    std::error_code ec;
    const std::filesystem::file_status status = std::filesystem::status(from, ec);

    if (!ec && std::filesystem::exists(status))
        std::filesystem::permissions(to, status.permissions(), ec);
}
#endif

static bool CopyAcrossDevices(const std::filesystem::path& temp, const std::filesystem::path& dest)
{
    std::error_code ec;
    std::filesystem::copy_file(temp, dest, std::filesystem::copy_options::overwrite_existing, ec);

    if (ec)
        return false;

    std::filesystem::remove(temp, ec);

    return true;
}

bool RenameOver(const char* tempPath, const char* destPath)
{
    const std::filesystem::path temp(tempPath);
    const std::filesystem::path dest = ResolveLink(destPath);

#if defined(SEAD_PLATFORM_WINDOWS)
    if (ReplaceFileW(dest.c_str(), temp.c_str(), nullptr, REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr))
        return true;
#else
    CopyPermissions(dest, temp);
#endif

    std::error_code ec;
    std::filesystem::rename(temp, dest, ec);

    if (!ec)
        return true;

    if (ec == std::errc::cross_device_link)
        return CopyAcrossDevices(temp, dest);

    std::filesystem::permissions(dest, std::filesystem::perms::owner_write, std::filesystem::perm_options::add, ec);

    if (ec)
        return false;

    std::filesystem::rename(temp, dest, ec);

    return !ec;
}

bool CopyFileOver(const char* srcPath, const char* destPath)
{
    const std::string tempPath = std::string(destPath) + cTempSuffix;
    std::error_code ec;

    RemoveFile(tempPath.c_str());
    std::filesystem::copy_file(srcPath, tempPath, ec);

    if (!ec && RenameOver(tempPath.c_str(), destPath))
        return true;

    RemoveFile(tempPath.c_str());

    return false;
}

void RemoveFile(const char* path)
{
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

const char* GetTempFolder()
{
    for (const char* name : { "TEMP", "TMP", "TMPDIR" })
    {
        if (const char* folder = std::getenv(name))
            return folder;
    }

    return ".";
}

bool HasSameBytes(const char* pathA, const char* pathB)
{
    std::error_code ec;

    if (std::filesystem::file_size(pathA, ec) != std::filesystem::file_size(pathB, ec) || ec)
        return false;

    std::vector<u8> bytesA;
    std::vector<u8> bytesB;

    return ReadWholeFile(pathA, &bytesA) && ReadWholeFile(pathB, &bytesB) && bytesA == bytesB;
}

bool OpenForWrite(sead::FileHandle* handle, const sead::SafeString& path)
{
    sead::FileDevice* device = nullptr;

    if (sead::FileDeviceMgr::instance())
        device = sead::FileDeviceMgr::instance()->findDevice("native");

    if (!device || path.calcLength() > cPathLengthMax)
        return false;

    device->tryOpen(handle, path, sead::FileDevice::FileOpenFlag::eWriteOnly, 0);

    if (!handle->getDevice())
        device->tryOpen(handle, path, sead::FileDevice::FileOpenFlag::eCreate, 0);

    return handle->getDevice() != nullptr;
}

StagedFile::StagedFile()
    : mInPlace(false)
{
}

StagedFile::~StagedFile()
{
    if (mHandle.getDevice())
        mHandle.tryClose();

    if (!mInPlace && !mTempPath.isEmpty())
        RemoveFile(mTempPath.cstr());
}

bool StagedFile::open(const sead::SafeString& path)
{
    if (path.calcLength() > cPathLengthMax)
        return false;

    mPath = path;
    mTempPath.format("%s%s", path.cstr(), cTempSuffix);

    if (OpenForWrite(&mHandle, mTempPath))
        return true;

    mTempPath.clear();
    mInPlace = true;

    return OpenForWrite(&mHandle, mPath);
}

bool StagedFile::commit()
{
    if (!mHandle.getDevice() || !mHandle.tryClose())
        return false;

    if (mInPlace)
        return true;

    const bool replaced = RenameOver(mTempPath.cstr(), mPath.cstr());

    if (!replaced)
        RemoveFile(mTempPath.cstr());

    mTempPath.clear();

    return replaced;
}

}
