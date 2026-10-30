#include <ui/file/Document.h>

#include <ui/file/AudioResourceDocument.h>
#include <ui/file/BarsListDocument.h>
#include <ui/file/DocumentMgr.h>
#include <ui/file/DocumentState.h>
#include <ui/file/FileDialogs.h>
#include <ui/file/LooseStreamDocument.h>
#include <ui/file/LooseWaveDocument.h>
#include <messages/Messages.h>
#include <bfsar/model/MessageLog.h>
#include <ui/file/SoundArchiveDocument.h>
#include <ui/file/WavDocument.h>
#include <ui/player/PlayerFrame.h>

#include <aal/AudioResource.h>
#include <aal/Bars.h>
#include <aal/BarsList.h>
#include <aal/Bwav.h>
#include <core/ByteView.h>
#include <core/Sarc.h>
#include <bfsar/archive/Bea.h>
#include <bfsar/archive/Cmpbin.h>
#include <bfsar/audio/NxOpus.h>
#include <bfsar/io/PackFile.h>
#include <core/Debug.h>

#include <filedevice/seadFileDeviceMgr.h>

#include <cstring>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

static constexpr u32 cBarsHeaderFileSize  = 0x04;
static constexpr u32 cBarsHeaderByteOrder = 0x08;

static constexpr u32 cRiffFormatOffset = 0x08;
static constexpr u32 cRiffHeaderSize   = 0x0C;

static constexpr u32 cBfsarLegacyVersionMin = 0x00010000;
static constexpr u32 cBfsarLegacyVersionEnd = 0x00030000;
static constexpr u32 cBfsarV3Version        = 0x03000000;

static u32 sLastDocumentSerial = 0;

u32 NewDocumentSerial()
{
    return ++sLastDocumentSerial;
}

static bool HasByteOrderMark(const u8* mark)
{
    return (mark[0] == 0xFF && mark[1] == 0xFE) || (mark[0] == 0xFE && mark[1] == 0xFF);
}

static bool CheckSoundArchiveFile(const u8* file, u32 size)
{
    const bool hasHeader = size >= sizeof(nw::snd::internal::SoundArchiveFile::FileHeader);
    const bool isFsar = hasHeader && sead::MemUtil::compare(file, "FSAR", 4) == 0;
    const bool isCsar = hasHeader && sead::MemUtil::compare(file, "CSAR", 4) == 0;
    const u8* byteOrderMark = file + offsetof(nw::ut::BinaryFileHeader, byteOrder);

    if ((!isFsar && !isCsar) || !HasByteOrderMark(byteOrderMark))
    {
        MessageLog::instance()->addPopup({ messages::archive::cNotArchive, nullptr });
        return false;
    }

    if (!isFsar)
        return true;

    const bool isBigEndian = byteOrderMark[0] == 0xFE;
    const u32 version = ByteView(file, size, isBigEndian).readU32(offsetof(nw::ut::BinaryFileHeader, version));

    const bool inLegacyRange = version >= cBfsarLegacyVersionMin && version < cBfsarLegacyVersionEnd;

    if (inLegacyRange || version == cBfsarV3Version)
        return true;

    MessageLog::instance()->addPopup({ sead::FormatFixedSafeString<64>(messages::archive::cBfsarVersionUnsupportedFormat, version), nullptr });
    return false;
}

static bool IsRiffWave(const u8* file, u32 size)
{
    return size >= cRiffHeaderSize && sead::MemUtil::compare(file, "RIFF", 4) == 0 && sead::MemUtil::compare(file + cRiffFormatOffset, "WAVE", 4) == 0;
}

static void TakeBytes(std::vector<u8>&& bytes, Document::File* outFile)
{
    outFile->data = new u8[bytes.size()];
    outFile->size = static_cast<u32>(bytes.size());
    std::memcpy(outFile->data, bytes.data(), bytes.size());
}

static const char* LoadFromBea(Document::File* outFile)
{
    std::vector<u8> container(outFile->data, outFile->data + outFile->size);
    std::vector<u8> archive;

    Document::Free(outFile);

    switch (bea::ExtractSoundArchive(std::move(container), &archive, &outFile->container))
    {
        case bea::ExtractResult::Ok:
            break;

        case bea::ExtractResult::NoSoundArchive:
            return messages::archive::cBeaNoSoundArchive;

        case bea::ExtractResult::Damaged:
            return messages::archive::cBeaDamaged;
    }

    TakeBytes(std::move(archive), outFile);

    return nullptr;
}

static bool LoadFileBytes(const sead::SafeString& path, Document::File* outFile)
{
    std::vector<u8> bytes;

    if (packfile::IsPackPath(path.cstr()))
    {
        if (!packfile::ReadFile(path.cstr(), &bytes) || bytes.empty())
            return false;

        TakeBytes(std::move(bytes), outFile);
        return true;
    }

    sead::FileDevice* device = sead::FileDeviceMgr::instance()->findDevice("native");
    SEAD_ASSERT(device);

    sead::FileDevice::LoadArg arg;
    arg.path = path;

    outFile->data = device->tryLoad(arg);
    outFile->size = static_cast<u32>(arg.read_size);

    return outFile->data != nullptr;
}

const char* Document::Load(const sead::SafeString& path, File* outFile)
{
    LOG_STR(path.cstr());

    *outFile = File();

    if (!LoadFileBytes(path, outFile))
        return messages::archive::cOpenFailed;

    if (cmpbin::IsZstdFrame(outFile->data, outFile->size) || AudioResource::IsResourceFile(outFile->data, outFile->size))
    {
        outFile->kind = Kind::AudioResource;
        return nullptr;
    }

    if (LooseStreamFile::IsStreamFile(outFile->data, outFile->size))
    {
        outFile->kind = Kind::StreamFile;
        return nullptr;
    }

    if (LooseWaveFile::IsWaveFile(outFile->data, outFile->size))
    {
        outFile->kind = Kind::WaveFile;
        return nullptr;
    }

    if (aal::IsBarsList(outFile->data, outFile->size))
    {
        outFile->kind = Kind::BarsList;
        return nullptr;
    }

    if (IsRiffWave(outFile->data, outFile->size))
    {
        outFile->kind = Kind::WavFile;
        return nullptr;
    }

    if (bea::IsBea(outFile->data, outFile->size))
        return LoadFromBea(outFile);

    if (!cmpbin::IsCompressed(outFile->data, outFile->size))
        return nullptr;

    outFile->wasCmpbin = true;
    outFile->prefersZstd = cmpbin::DetectCodec(outFile->data, outFile->size) == cmpbin::Codec::Zstd;

    std::vector<u8> decompressed;
    const bool decoded = cmpbin::Decompress(outFile->data, outFile->size, decompressed);

    Free(outFile);

    if (!decoded)
        return messages::archive::cCmpbinDecompressFailed;

    TakeBytes(std::move(decompressed), outFile);

    return nullptr;
}

void Document::Free(File* file)
{
    delete[] file->data;

    file->data = nullptr;
    file->size = 0;
}

bool Document::Check(const File& file, const char** outError)
{
    *outError = nullptr;

    switch (file.kind)
    {
        case Kind::SoundArchive:
            return CheckSoundArchiveFile(file.data, file.size);

        case Kind::AudioResource:
            *outError = AudioResource::GetResourceDataError(file.data, file.size);
            return *outError == nullptr;

        case Kind::StreamFile:
        case Kind::WaveFile:
            return true;

        case Kind::WavFile:
            *outError = IsPlayerFrameShown() ? nullptr : messages::archive::cNotArchive;
            return *outError == nullptr;

        case Kind::BarsList:
        {
            aal::BarsList list;
            *outError = aal::ReadBarsList(file.data, file.size, &list) ? nullptr : messages::barslist::cNotBarsList;

            return *outError == nullptr;
        }
    }

    return false;
}

bool Document::CheckBeforeOpen(const File& file, const char* failMessage)
{
    const char* error = nullptr;

    if (!Check(file, &error))
    {
        if (error)
            MessageLog::instance()->addPopup({ failMessage ? failMessage : error, nullptr });

        return false;
    }

    sead::Endian::Types endian = sead::Endian::eBig;

    if (PeekEndian(file, &endian))
        PrepareParseEndian(endian);

    return true;
}

bool Document::PeekEndian(const File& file, sead::Endian::Types* outEndian)
{
    if (file.kind != Kind::SoundArchive || file.size < sizeof(nw::ut::BinaryFileHeader))
        return false;

    const u8* byteOrderMark = file.data + offsetof(nw::ut::BinaryFileHeader, byteOrder);

    if (!HasByteOrderMark(byteOrderMark))
        return false;

    *outEndian = sead::Endian::markToEndian(*reinterpret_cast<const u16*>(byteOrderMark));
    return true;
}

bool Document::IsCompleteFile(const u8* header, u64 fileSize)
{
    if (cmpbin::IsZstdFrame(header, fileSize) || bea::IsBea(header, fileSize) || aal::IsBwav(header, fileSize) || nxopus::HasInfoChunk(header, fileSize) || aal::IsBarsList(header, fileSize))
        return true;

    if (aal::IsBars(header, fileSize))
        return ByteView(header, fileSize, ByteView::IsBigEndianMark(header, fileSize, cBarsHeaderByteOrder)).readU32(cBarsHeaderFileSize) <= fileSize;

    if (sarc::IsSarc(header, fileSize))
        return sarc::GetDeclaredFileSize(header, fileSize) == fileSize;

    if (fileSize < sizeof(nw::ut::BinaryFileHeader))
        return false;

    const u8* mark = header + offsetof(nw::ut::BinaryFileHeader, byteOrder);
    const bool isLittle = mark[0] == 0xFF && mark[1] == 0xFE;

    return ByteView(header, sizeof(nw::ut::BinaryFileHeader), !isLittle).readU32(offsetof(nw::ut::BinaryFileHeader, fileSize)) == fileSize;
}

Document* Document::Open(File* file, const sead::SafeString& path, const sead::SafeString* metadataSource)
{
    switch (file->kind)
    {
        case Kind::AudioResource:
            return AudioResourceDocument::Open(file, path);

        case Kind::StreamFile:
            return LooseStreamDocument::Open(file, path);

        case Kind::WaveFile:
            return LooseWaveDocument::Open(file, path);

        case Kind::BarsList:
            return BarsListDocument::Open(file, path);

        case Kind::WavFile:
            return WavDocument::Open(file, path);

        case Kind::SoundArchive:
            break;
    }

    return SoundArchiveDocument::Open(file, path, metadataSource);
}

Document* Document::Create(ArchiveFormat format, ArchivePlatform platform)
{
    return SoundArchiveDocument::Create(format, platform);
}

Document::ParseContext Document::CurrentParseContext()
{
    ParseContext context;
    context.endian = sFileEndian;
    context.usesV3SoundInfo = nw::snd::internal::SoundArchiveFile::UseV3SoundInfoLayout();

    return context;
}

void Document::ApplyParseContext(const ParseContext& context)
{
    sFileEndian = context.endian;
    nw::snd::internal::SoundArchiveFile::UseV3SoundInfoLayout() = context.usesV3SoundInfo;
}

Document::Document()
    : mState(new DocumentState())
{
}

Document::~Document()
{
    delete mState;
}

DocumentState& Document::state()
{
    return *mState;
}

const DocumentState& Document::state() const
{
    return *mState;
}

const char* Document::getNameRuleDetail() const
{
    return messages::item::cNameInvalidDetail;
}

std::string Document::archiveName() const
{
    if (!filePath().isEmpty())
        return std::filesystem::path(filePath().cstr()).filename().string();

    if (!mState->backup.pinnedDir.empty() && !loadedPath().isEmpty())
        return std::filesystem::path(loadedPath().cstr()).filename().string();

    return std::string(messages::backup::cUntitled) + "." + extension();
}

bool Document::showSaveAsDialog(sead::BufferedSafeString* outPath, const char* formatName, const char* fileExtension, const char* compressedExtension) const
{
    const sead::FormatFixedSafeString<32> pattern("*.%s", fileExtension);
    const sead::FormatFixedSafeString<96> filterName("%s (%s)", formatName, pattern.cstr());

    sead::FixedSafeString<32> compressedPattern;
    sead::FixedSafeString<96> compressedName;

    std::vector<FileFilter> filters;
    filters.emplace_back(filterName.cstr(), pattern.cstr());

    if (compressedExtension)
    {
        compressedPattern.format("*.%s", compressedExtension);
        compressedName.format(messages::common::cCompressedFilterFormat, formatName, fileExtension, compressedExtension);
        filters.emplace_back(compressedName.cstr(), compressedPattern.cstr());
    }

    const fs::path documentPath(filePath().isEmpty() ? displayName() : std::string(filePath().cstr()));
    const std::string defaultPath = (fs::path(packfile::GetDiskPath(documentPath.string())).parent_path() / documentPath.filename()).string();

    if (!SaveFileDialog(outPath, nullptr, static_cast<u32>(filters.size()), filters.data(), fileExtension, defaultPath.c_str()))
        return false;

    if (compressedExtension)
        AddInnerExtension(outPath, fileExtension, compressedExtension);

    return true;
}

void Document::closeWindows()
{
    mState->fileWindows.clear();
    mState->shownFileWindow = nullptr;
}

void Document::AddInnerExtension(sead::BufferedSafeString* path, const char* innerExtension, const char* outerExtension)
{
    const size_t length = std::strlen(path->cstr());
    const size_t outerLength = std::strlen(outerExtension) + 1;
    std::error_code error;

    if (!HasFileExtension(path->cstr(), length, outerExtension) || HasFileExtension(path->cstr(), length - outerLength, innerExtension) ||
        std::filesystem::exists(path->cstr(), error))
        return;

    const std::string named = std::string(path->cstr(), length - outerLength) + "." + innerExtension + "." + outerExtension;

    if (named.size() < static_cast<size_t>(path->getBufferSize()) && !std::filesystem::exists(named, error))
        path->copy(named.c_str());
}

bool IsSoundArchiveOpen()
{
    return FindActiveSoundArchive() != nullptr;
}

Bfsar* FindActiveSoundArchive()
{
    Document* document = ActiveDocument();

    if (!document || document->kind() != Document::Kind::SoundArchive)
        return nullptr;

    return &static_cast<SoundArchiveDocument*>(document)->archive();
}

Bfsar& ActiveSoundArchive()
{
    Bfsar* archive = FindActiveSoundArchive();
    SEAD_ASSERT(archive);

    return *archive;
}

AudioResource* FindActiveAudioResource()
{
    Document* document = ActiveDocument();

    if (!document || document->kind() != Document::Kind::AudioResource)
        return nullptr;

    return &static_cast<AudioResourceDocument*>(document)->resource();
}

AudioResource& ActiveAudioResource()
{
    AudioResource* resource = FindActiveAudioResource();
    SEAD_ASSERT(resource);

    return *resource;
}
