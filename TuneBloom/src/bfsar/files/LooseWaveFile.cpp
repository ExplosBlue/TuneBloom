#include <bfsar/files/LooseWaveFile.h>

#include <core/TestHooks.h>

#include <aal/AudioAsset.h>
#include <bfsar/io/PackFile.h>

#include <ui/Messages.h>

#include <filedevice/seadPath.h>

bool LooseWaveFile::IsWaveFile(const void* data, u32 size)
{
    return size >= sizeof(nw::ut::BinaryFileHeader) && FindInnerFileFormat(data, InnerFileKind::Wave) != nullptr;
}

bool LooseWaveFile::open(const void* data, u32 size, const sead::SafeString& path, sead::BufferedSafeString* outError)
{
    WaveFile* wave = IsWaveFile(data, size) ? ReadFwavFile(data, size) : nullptr;

    setPath(path);

    if (!wave)
    {
        outError->format(messages::wavefile::cFileUnreadableFormat, mFileName.cstr());
        return false;
    }

    adoptWave(wave);

    return true;
}

void LooseWaveFile::create(const InnerFileTarget& target, const sead::SafeString& fileName)
{
    WaveFile* wave = new WaveFile();
    wave->setStandaloneTarget(target);
    wave->setVersion(target.version);

    mFilePath.clear();
    mFileName = fileName;

    adoptWave(wave);
}

void LooseWaveFile::adoptWave(WaveFile* wave)
{
    mWave = wave;
    mWave->setEnableName(true);
    mWave->getName().copy(mFileName);
    mWave->setId(0);
    mWaveList.pushBack(mWave);

    mSavedSignature = mWave->computeStateSignature();
}

void LooseWaveFile::setPath(const sead::SafeString& path)
{
    mFilePath = path;
    sead::Path::getFileName(&mFileName, path);
}

bool LooseWaveFile::isModified() const
{
    return mWave->computeStateSignature() != mSavedSignature;
}

bool LooseWaveFile::writeTo(const sead::SafeString& path) const
{
    return mWave->writeFile(path);
}

SaveResult LooseWaveFile::save(sead::BufferedSafeString* outError)
{
    return saveAs(mFilePath, outError);
}

SaveResult LooseWaveFile::saveAs(const sead::SafeString& path, sead::BufferedSafeString* outError)
{
    const u64 signature = mWave->computeStateSignature();
    const bool isUnchanged = signature == mSavedSignature && !testhooks::IsStreamRewriteForced(path.cstr());
    const bool isCopied = isUnchanged && packfile::CopyFileOver(mFilePath.cstr(), path.cstr());

    if (!isCopied && !writeTo(path))
    {
        sead::FixedSafeString<256> fileName;
        sead::Path::getFileName(&fileName, path);

        outError->format(messages::wavefile::cWriteFailedFormat, fileName.cstr());
        return SaveResult::Failed;
    }

    setPath(path);
    mWave->getName().copy(mFileName);
    mSavedSignature = mWave->computeStateSignature();

    return SaveResult::Saved;
}
