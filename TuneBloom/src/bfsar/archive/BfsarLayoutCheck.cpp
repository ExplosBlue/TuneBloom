#include <bfsar/archive/BfsarLayoutCheck.h>

#include <ui/Messages.h>
#include <ui/app/PopupMgr.h>

#include <snd/snd_BankFile.h>
#include <snd/snd_ElementType.h>
#include <snd/snd_GroupFile.h>
#include <snd/snd_WaveArchiveFile.h>

#include <algorithm>
#include <cstddef>
#include <cstring>

using nw::snd::internal::GroupFile;
using nw::snd::internal::SoundArchiveFile;
using nw::snd::internal::Util;
using nw::snd::internal::WaveArchiveFile;
using nw::snd::internal::WaveSoundFile;

namespace
{

constexpr u32 cByteOrderOffset  = offsetof(nw::ut::BinaryFileHeader, byteOrder);
constexpr u32 cBlockCountOffset = offsetof(nw::ut::BinaryFileHeader, dataBlocks);
constexpr u32 cBlockRefsOffset  = sizeof(nw::ut::BinaryFileHeader);
constexpr u32 cBlockRefSize     = sizeof(Util::ReferenceWithSize);
constexpr u32 cRefOffsetOffset  = offsetof(Util::ReferenceWithSize, offset);
constexpr u32 cRefSizeOffset    = offsetof(Util::ReferenceWithSize, size);
constexpr u32 cBlockHeaderSize  = sizeof(nw::ut::BinaryBlockHeader);
constexpr u32 cBlockSizeOffset  = offsetof(nw::ut::BinaryBlockHeader, size);

constexpr u32 cAbsentBlock      = 0xFFFFFFFF;
constexpr u32 cItemIndexMax     = 0x00FFFFFF;
constexpr u32 cSoundInfoSizeV3  = 0x38;

struct ArchiveLayout
{
    ByteRange file;
    const SoundArchiveFile::InfoBlockBody* info;
    u32 fileBlockOffset;
    ArchiveLayoutVersion version;
};

}

static bool Fail(const sead::SafeString& text)
{
    PopupMgr::instance()->setCorruptInfo(text);
    return false;
}

static bool ReadByteOrder(const u8* header, bool* outBigEndian)
{
    const u8 first = header[cByteOrderOffset];
    const u8 second = header[cByteOrderOffset + 1];

    if (first == 0xFE && second == 0xFF)
        *outBigEndian = true;
    else if (first == 0xFF && second == 0xFE)
        *outBigEndian = false;
    else
        return false;

    return true;
}

static u32 ReadU16(const u8* bytes, bool bigEndian)
{
    return bigEndian ? (bytes[0] << 8 | bytes[1]) : (bytes[1] << 8 | bytes[0]);
}

static u32 ReadU32(const u8* bytes, bool bigEndian)
{
    return bigEndian ? (ReadU16(bytes, true) << 16 | ReadU16(bytes + 2, true)) : (ReadU16(bytes + 2, false) << 16 | ReadU16(bytes, false));
}

static bool ContainsString(const ByteRange& file, const char* text)
{
    if (!file.contains(text, 1))
        return false;

    return std::memchr(text, '\0', file.end - reinterpret_cast<uintptr_t>(text)) != nullptr;
}

bool CheckSoundArchiveHeader(const void* data, u32 size)
{
    const u8* bytes = static_cast<const u8*>(data);
    bool bigEndian = false;

    if (!bytes || size < sizeof(SoundArchiveFile::FileHeader) || !ReadByteOrder(bytes, &bigEndian))
        return Fail(messages::archive::cCorruptHeader);

    struct Block
    {
        u16 typeId;
        const char* tag;
        u32 minSize;
        bool optional;
        bool found;
    };

    Block blocks[SoundArchiveFile::BLOCK_SIZE] = {
        { nw::snd::internal::ElementType_SoundArchiveFile_StringBlock, "STRG", sizeof(SoundArchiveFile::StringBlock), true,  false },
        { nw::snd::internal::ElementType_SoundArchiveFile_InfoBlock,   "INFO", sizeof(SoundArchiveFile::InfoBlock),   false, false },
        { nw::snd::internal::ElementType_SoundArchiveFile_FileBlock,   "FILE", cBlockHeaderSize,                      false, false },
    };

    for (s32 i = 0; i < SoundArchiveFile::BLOCK_SIZE; i++)
    {
        const u8* ref = bytes + cBlockRefsOffset + i * cBlockRefSize;
        const u32 typeId = ReadU16(ref, bigEndian);
        const u32 offset = ReadU32(ref + cRefOffsetOffset, bigEndian);
        const u32 blockSize = ReadU32(ref + cRefSizeOffset, bigEndian);

        Block* block = std::find_if(std::begin(blocks), std::end(blocks), [typeId](const Block& candidate) { return candidate.typeId == typeId; });

        if (block == std::end(blocks) || block->found)
            return Fail(messages::archive::cCorruptHeader);

        block->found = true;

        if (block->optional && (offset == cAbsentBlock || blockSize == cAbsentBlock))
            continue;

        if (offset > size || size - offset < block->minSize)
            return Fail(sead::FormatFixedSafeString<64>(messages::archive::cCorruptBlockFormat, block->tag));
    }

    return true;
}

static bool CheckStreamTrackInfo(const ArchiveLayout& layout, const SoundArchiveFile::StreamTrackInfo* track)
{
    const ByteRange& file = layout.file;

    if (!file.contains(track, offsetof(SoundArchiveFile::StreamTrackInfo, toSendValue)))
        return false;

    if (!file.containsTable(&track->GetGlobalChannelIndexTable(), sizeof(u8)))
        return false;

    if (layout.version.streamSend)
    {
        if (!file.contains(track, offsetof(SoundArchiveFile::StreamTrackInfo, lpfFreq)))
            return false;

        if (track->toSendValue.typeId == nw::snd::internal::ElementType_SoundArchiveFile_SendInfo && !file.containsStruct(&track->GetSendValue()))
            return false;
    }

    return !layout.version.streamFilter || file.containsStruct(track);
}

static bool CheckStreamSoundInfo(const ArchiveLayout& layout, const SoundArchiveFile::StreamSoundInfo* stream)
{
    const ByteRange& file = layout.file;

    if (!file.contains(stream, offsetof(SoundArchiveFile::StreamSoundInfo, pitch)))
        return false;

    if (!layout.version.streamTrackInfo)
        return true;

    if (const SoundArchiveFile::StreamTrackInfoTable* tracks = stream->GetTrackInfoTable())
    {
        if (!file.containsTable(tracks, sizeof(Util::Reference)))
            return false;

        const u32 trackCount = std::min<u32>(tracks->GetTrackCount(), nw::snd::SoundArchive::STRM_TRACK_NUM);

        for (u32 i = 0; i < trackCount; i++)
        {
            const SoundArchiveFile::StreamTrackInfo* track = tracks->GetTrackInfo(i);

            if (track && !CheckStreamTrackInfo(layout, track))
                return false;
        }
    }

    if (!layout.version.streamSend)
        return true;

    if (!file.contains(stream, offsetof(SoundArchiveFile::StreamSoundInfo, toStreamSoundExtension)))
        return false;

    if (stream->toSendValue.typeId != nw::snd::internal::ElementType_SoundArchiveFile_SendInfo)
        return true;

    if (!file.contains(stream, offsetof(SoundArchiveFile::StreamSoundInfo, prefetchFileId)) || !file.containsStruct(&stream->GetSendValue()))
        return false;

    const SoundArchiveFile::StreamSoundExtension* extension = stream->GetStreamSoundExtension();

    return !extension || file.containsStruct(extension);
}

static bool CheckSoundInfo(const ArchiveLayout& layout, const SoundArchiveFile::SoundInfo* sound)
{
    const ByteRange& file = layout.file;
    const u32 baseSize = SoundArchiveFile::UseV3SoundInfoLayout() ? cSoundInfoSizeV3 : sizeof(SoundArchiveFile::SoundInfo);

    if (!file.contains(sound, baseSize) || !file.containsOptions(&sound->GetOptionParameter()))
        return false;

    const Util::Reference& detail = sound->GetDetailReference();
    const void* detailInfo = sead::PtrUtil::addOffset(sound, detail.offset);

    switch (detail.typeId)
    {
        case nw::snd::internal::ElementType_SoundArchiveFile_StreamSoundInfo:
            if (!CheckStreamSoundInfo(layout, static_cast<const SoundArchiveFile::StreamSoundInfo*>(detailInfo)))
                return false;
            break;

        case nw::snd::internal::ElementType_SoundArchiveFile_WaveSoundInfo:
        {
            const SoundArchiveFile::WaveSoundInfo* wave = static_cast<const SoundArchiveFile::WaveSoundInfo*>(detailInfo);

            if (!file.containsStruct(wave) || !file.containsOptions(&wave->optionParameter))
                return false;
            break;
        }

        case nw::snd::internal::ElementType_SoundArchiveFile_SequenceSoundInfo:
        {
            const SoundArchiveFile::SequenceSoundInfo* sequence = static_cast<const SoundArchiveFile::SequenceSoundInfo*>(detailInfo);

            if (!file.containsStruct(sequence) || !file.containsOptions(&sequence->optionParameter))
                return false;

            if (!file.containsTable(&sequence->GetBankIdTable(), sizeof(nw::ut::ResU32)))
                return false;
            break;
        }

        default:
            break;
    }

    const SoundArchiveFile::Sound3DInfo* sound3DInfo = sound->GetSound3DInfo();

    return !sound3DInfo || file.contains(sound3DInfo, offsetof(SoundArchiveFile::Sound3DInfo, optionParameter));
}

static bool CheckSoundGroupInfo(const ArchiveLayout& layout, const SoundArchiveFile::SoundGroupInfo* soundGroup)
{
    const ByteRange& file = layout.file;

    if (!file.containsStruct(soundGroup) || !file.containsOptions(&soundGroup->optionParameter))
        return false;

    const SoundArchiveFile::WaveSoundGroupInfo* waveSoundGroup = soundGroup->GetWaveSoundGroupInfo();

    if (!waveSoundGroup)
        return true;

    return file.containsStruct(waveSoundGroup) &&
           file.containsTable(waveSoundGroup->GetWaveArchiveItemIdTable(), sizeof(nw::ut::ResU32)) &&
           file.containsTable(soundGroup->GetFileIdTable(), sizeof(nw::ut::ResU32));
}

static bool CheckBankInfo(const ArchiveLayout& layout, const SoundArchiveFile::BankInfo* bank)
{
    const ByteRange& file = layout.file;

    return file.containsStruct(bank) && file.containsOptions(&bank->optionParameter) &&
           file.containsTable(bank->GetWaveArchiveItemIdTable(), sizeof(nw::ut::ResU32));
}

template <typename Info>
static bool CheckInfoWithOptions(const ArchiveLayout& layout, const Info* info)
{
    return layout.file.containsStruct(info) && layout.file.containsOptions(&info->optionParameter);
}

static bool CheckInternalFileInfo(const ArchiveLayout& layout, const SoundArchiveFile::InternalFileInfo* internalFile)
{
    const ByteRange& file = layout.file;

    if (!file.containsStruct(internalFile))
        return false;

    const Util::Reference& toGroupTable = internalFile->toAttachedGroupIdTable;

    if (toGroupTable.typeId == nw::snd::internal::ElementType_Category_Tables && toGroupTable.IsValidOffset() &&
        !file.containsTable(internalFile->GetAttachedGroupTable(), sizeof(nw::ut::ResU32)))
        return false;

    const u32 offsetFromFileBlock = internalFile->GetOffsetFromFileBlockHead();

    if (offsetFromFileBlock == SoundArchiveFile::InternalFileInfo::INVALID_OFFSET)
        return true;

    const u64 offset = static_cast<u64>(layout.fileBlockOffset) + offsetFromFileBlock;

    return offset <= file.size() && internalFile->GetFileSize() <= file.size() - offset;
}

static bool CheckFileInfo(const ArchiveLayout& layout, const SoundArchiveFile::FileInfo* fileInfo)
{
    if (!layout.file.containsStruct(fileInfo))
        return false;

    const void* location = sead::PtrUtil::addOffset(fileInfo, fileInfo->toFileLocation.offset);

    switch (fileInfo->toFileLocation.typeId)
    {
        case nw::snd::internal::ElementType_SoundArchiveFile_InternalFileInfo:
            return CheckInternalFileInfo(layout, static_cast<const SoundArchiveFile::InternalFileInfo*>(location));

        case nw::snd::internal::ElementType_SoundArchiveFile_ExternalFileInfo:
            return ContainsString(layout.file, static_cast<const char*>(location));

        case 0:
            return true;

        default:
            return false;
    }
}

template <typename Info, typename CheckItem>
static bool CheckInfoTable(const ArchiveLayout& layout, const Util::Reference& toTable, CheckItem checkItem)
{
    const void* tableAddress = sead::PtrUtil::addOffset(layout.info, toTable.offset);

    if (!layout.file.containsTable(tableAddress, sizeof(Util::Reference)))
        return false;

    const Util::ReferenceTable* table = static_cast<const Util::ReferenceTable*>(tableAddress);

    if (table->count > cItemIndexMax)
        return false;

    const s64 tableSize = sizeof(nw::ut::ResU32) + static_cast<s64>(table->count) * sizeof(Util::Reference);

    for (u32 i = 0; i < table->count; i++)
    {
        if (table->item[i].offset < tableSize || !checkItem(layout, static_cast<const Info*>(table->GetReferedItem(i))))
            return false;
    }

    return true;
}

static bool CheckStringTable(const ByteRange& file, const SoundArchiveFile::StringBlockBody* strings)
{
    if (!strings)
        return true;

    return file.containsStruct(strings) && file.containsTable(strings->GetStringTable(), sizeof(Util::ReferenceWithSize));
}

bool CheckSoundArchiveLayout(const nw::snd::MemorySoundArchive& archive, u32 size, const ArchiveLayoutVersion& version)
{
    const SoundArchiveFile::InfoBlockBody* info = archive.mInfoBlockBody;
    const ArchiveLayout layout = { ByteRange(archive.mData, size), info, static_cast<u32>(archive.mHeader.GetFileBlockOffset()), version };

    if (!layout.file.containsStruct(info))
        return Fail(sead::FormatFixedSafeString<64>(messages::archive::cCorruptBlockFormat, "INFO"));

    if (!CheckStringTable(layout.file, archive.mStringBlockBody))
        return Fail(messages::archive::cInvalidStringTable);

    if (!CheckInfoTable<SoundArchiveFile::SoundInfo>(layout, info->toSoundInfoReferenceTable, CheckSoundInfo))
        return Fail(messages::archive::cInvalidSoundInfo);

    if (!CheckInfoTable<SoundArchiveFile::SoundGroupInfo>(layout, info->toSoundGroupInfoReferenceTable, CheckSoundGroupInfo))
        return Fail(messages::archive::cInvalidSoundGroupInfo);

    if (!CheckInfoTable<SoundArchiveFile::BankInfo>(layout, info->toBankInfoReferenceTable, CheckBankInfo))
        return Fail(messages::archive::cInvalidBankInfo);

    if (!CheckInfoTable<SoundArchiveFile::WaveArchiveInfo>(layout, info->toWaveArchiveInfoReferenceTable, CheckInfoWithOptions<SoundArchiveFile::WaveArchiveInfo>))
        return Fail(messages::archive::cInvalidWaveArchiveInfo);

    if (!CheckInfoTable<SoundArchiveFile::GroupInfo>(layout, info->toGroupInfoReferenceTable, CheckInfoWithOptions<SoundArchiveFile::GroupInfo>))
        return Fail(messages::archive::cInvalidGroupInfo);

    if (!CheckInfoTable<SoundArchiveFile::PlayerInfo>(layout, info->toPlayerInfoReferenceTable, CheckInfoWithOptions<SoundArchiveFile::PlayerInfo>))
        return Fail(messages::archive::cInvalidPlayerInfo);

    if (!CheckInfoTable<SoundArchiveFile::FileInfo>(layout, info->toFileInfoReferenceTable, CheckFileInfo))
        return Fail(messages::archive::cInvalidFileInfo);

    if (!layout.file.containsStruct(info->GetSoundArchivePlayerInfo()))
        return Fail(messages::archive::cInvalidPlayerSettings);

    return true;
}

const char* FindArchiveString(const nw::snd::MemorySoundArchive& archive, const ByteRange& file, u32 stringId)
{
    const SoundArchiveFile::StringBlockBody* strings = archive.mStringBlockBody;

    if (!strings)
        return nullptr;

    const SoundArchiveFile::StringTable* table = strings->GetStringTable();

    if (stringId >= table->GetCount())
        return nullptr;

    const char* text = table->GetString(stringId);

    return ContainsString(file, text) ? text : nullptr;
}

bool CheckInnerFileBlocks(const ByteRange& file)
{
    const u8* bytes = static_cast<const u8*>(file.data());
    const u64 size = file.size();
    bool bigEndian = false;

    if (!bytes || size < cBlockRefsOffset || !ReadByteOrder(bytes, &bigEndian))
        return false;

    const u64 blockCount = ReadU16(bytes + cBlockCountOffset, bigEndian);

    if (cBlockRefsOffset + blockCount * cBlockRefSize > size)
        return false;

    for (u64 i = 0; i < blockCount; i++)
    {
        const u32 offset = ReadU32(bytes + cBlockRefsOffset + i * cBlockRefSize + cRefOffsetOffset, bigEndian);

        if (offset == 0)
            continue;

        if (offset > size || size - offset < cBlockHeaderSize)
            return false;

        const u32 blockSize = ReadU32(bytes + offset + cBlockSizeOffset, bigEndian);

        if (blockSize < cBlockHeaderSize || blockSize > file.sizeToBufferEnd() - offset)
            return false;
    }

    return true;
}

static bool CheckArchiveOrderBlocks(const ByteRange& file)
{
    bool bigEndian = false;

    if (!CheckInnerFileBlocks(file) || !ReadByteOrder(static_cast<const u8*>(file.data()), &bigEndian))
        return false;

    return bigEndian == (sFileEndian == sead::Endian::eBig);
}

bool CheckGroupFileLayout(const ByteRange& file)
{
    if (!CheckArchiveOrderBlocks(file))
        return false;

    const GroupFile::FileHeader* header = static_cast<const GroupFile::FileHeader*>(file.data());
    const GroupFile::InfoBlock* infoBlock = header->GetInfoBlock();
    const GroupFile::FileBlock* fileBlock = header->GetFileBlock();

    if (!infoBlock || !fileBlock)
        return false;

    const GroupFile::InfoBlockBody& info = infoBlock->body;

    if (!file.containsTable(&info.referenceTableOfGroupItemInfo, sizeof(Util::Reference)))
        return false;

    for (u32 i = 0; i < info.GetGroupItemInfoCount(); i++)
    {
        const GroupFile::GroupItemInfo* item = info.GetGroupItemInfo(i);

        if (!file.containsStruct(item))
            return false;

        const void* location = item->GetFileLocation(&fileBlock->body);

        if (location && !file.contains(location, item->embeddedItemInfo.size))
            return false;
    }

    const GroupFile::InfoExBlock* infoExBlock = header->GetInfoExBlock();

    if (!infoExBlock)
        return true;

    const GroupFile::InfoExBlockBody& infoEx = infoExBlock->body;

    if (!file.containsTable(&infoEx.referenceTableOfGroupItemInfoEx, sizeof(Util::Reference)))
        return false;

    for (u32 i = 0; i < infoEx.GetGroupItemInfoExCount(); i++)
    {
        if (!file.containsStruct(infoEx.GetGroupItemInfoEx(i)))
            return false;
    }

    return true;
}

bool CheckWaveArchiveLayout(const ByteRange& file)
{
    if (!CheckArchiveOrderBlocks(file))
        return false;

    const WaveArchiveFile::FileHeader* header = static_cast<const WaveArchiveFile::FileHeader*>(file.data());

    if (!file.containsStruct(header))
        return false;

    bool hasInfoBlock = false;
    bool hasFileBlock = false;

    for (const Util::ReferenceWithSize& toBlock : header->toBlocks)
    {
        hasInfoBlock |= toBlock.typeId == nw::snd::internal::ElementType_WaveArchiveFile_InfoBlock;
        hasFileBlock |= toBlock.typeId == nw::snd::internal::ElementType_WaveArchiveFile_FileBlock;
    }

    if (!hasInfoBlock || !hasFileBlock)
        return false;

    const WaveArchiveFile::InfoBlock* infoBlock = header->GetInfoBlock();
    const WaveArchiveFile::FileBlock* fileBlock = header->GetFileBlock();

    if (!file.contains(infoBlock, cBlockHeaderSize) || !file.contains(fileBlock, cBlockHeaderSize))
        return false;

    const WaveArchiveFile::InfoBlockBody& info = infoBlock->body;

    if (!file.containsTable(&info.table, sizeof(Util::ReferenceWithSize)))
        return false;

    for (u32 i = 0; i < info.GetWaveFileCount(); i++)
    {
        const void* waveFile = sead::PtrUtil::addOffset(&fileBlock->body, info.GetOffsetFromFileBlockBody(i));

        if (!file.contains(waveFile, info.GetSize(i)))
            return false;
    }

    return true;
}

static bool CheckWaveSoundOptions(const ByteRange& file, const void* owner, const Util::BitFlag& options, u32 sendBit, u32 envelopeBit)
{
    if (!file.containsOptions(&options))
        return false;

    u32 offset = 0;

    if (options.GetValue(&offset, sendBit))
    {
        const nw::snd::internal::SendValueWsd* send = static_cast<const nw::snd::internal::SendValueWsd*>(sead::PtrUtil::addOffset(owner, offset));
        const u64 headSize = offsetof(nw::snd::internal::SendValueWsd, fxSend.item);

        if (!file.contains(send, headSize) || !file.contains(send, headSize + send->fxSend.count))
            return false;
    }

    if (options.GetValue(&offset, envelopeBit))
    {
        const Util::Reference* toCurve = static_cast<const Util::Reference*>(sead::PtrUtil::addOffset(owner, offset));

        if (!file.containsStruct(toCurve) || !file.contains(sead::PtrUtil::addOffset(toCurve, toCurve->offset), sizeof(nw::snd::AdshrCurve)))
            return false;
    }

    return true;
}

bool CheckWaveSoundEntry(const ByteRange& file, const WaveSoundFile::InfoBlockBody& info, u32 index)
{
    const void* dataAddress = info.GetWaveSoundDataReferenceTable().GetReferedItem(index, nw::snd::internal::ElementType_WaveSoundFile_WaveSoundMetaData);
    const WaveSoundFile::WaveSoundData* data = static_cast<const WaveSoundFile::WaveSoundData*>(dataAddress);

    if (!data || !file.containsStruct(data))
        return false;

    if (!data->toWaveSoundInfo.IsValidTypeId(nw::snd::internal::ElementType_WaveSoundFile_WaveSoundInfo))
        return false;

    const WaveSoundFile::WaveSoundInfo& soundInfo = data->GetWaveSoundInfo();

    if (!CheckWaveSoundOptions(file, &soundInfo, soundInfo.optionParameter, nw::snd::internal::WAVE_SOUND_INFO_SEND, nw::snd::internal::WAVE_SOUND_INFO_ENVELOPE))
        return false;

    const Util::ReferenceTable& noteTable = data->GetNoteInfoReferenceTable();

    if (!file.containsTable(&noteTable, sizeof(Util::Reference)))
        return false;

    const WaveSoundFile::NoteInfo* note = static_cast<const WaveSoundFile::NoteInfo*>(noteTable.GetReferedItem(0, nw::snd::internal::ElementType_WaveSoundFile_NoteInfo));

    if (!note || !file.containsStruct(note))
        return false;

    return CheckWaveSoundOptions(file, note, note->optionParameter, nw::snd::internal::NOTE_INFO_SEND, nw::snd::internal::NOTE_INFO_ENVELOPE);
}

const Util::WaveIdTable* FindBankWaveIdTable(const ByteRange& file)
{
    if (!CheckArchiveOrderBlocks(file))
        return nullptr;

    const nw::snd::internal::BankFile::FileHeader* header = static_cast<const nw::snd::internal::BankFile::FileHeader*>(file.data());
    const nw::snd::internal::BankFile::InfoBlock* infoBlock = header->GetInfoBlock();

    if (!infoBlock || !file.containsStruct(infoBlock))
        return nullptr;

    const nw::snd::internal::BankFile::InfoBlockBody& info = infoBlock->body;
    const Util::WaveIdTable* waveIdTable = &info.GetWaveIdTable();

    if (!file.containsTable(waveIdTable, sizeof(Util::WaveId)) || !file.containsTable(&info.GetInstrumentReferenceTable(), sizeof(Util::Reference)))
        return nullptr;

    return waveIdTable;
}

const Util::WaveIdTable* FindWaveSoundWaveIdTable(const ByteRange& file)
{
    if (!CheckArchiveOrderBlocks(file))
        return nullptr;

    const WaveSoundFile::FileHeader* header = static_cast<const WaveSoundFile::FileHeader*>(file.data());
    const WaveSoundFile::InfoBlock* infoBlock = header->GetInfoBlock();

    if (!infoBlock || !file.containsStruct(infoBlock))
        return nullptr;

    const WaveSoundFile::InfoBlockBody& info = infoBlock->body;
    const Util::WaveIdTable* waveIdTable = &info.GetWaveIdTable();

    if (!file.containsTable(waveIdTable, sizeof(Util::WaveId)) || !file.containsTable(&info.GetWaveSoundDataReferenceTable(), sizeof(Util::Reference)))
        return nullptr;

    return waveIdTable;
}
