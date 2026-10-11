#include <bfsar/seq/SeqDecompile.h>

#include <bfsar/model/ArchiveFormat.h>
#include <bfsar/seq/SeqCommand.h>

#include <snd/snd_SequenceSoundFileReader.h>

#include <ui/Messages.h>
#include <ui/app/PopupMgr.h>

#include <cstring>
#include <map>

using nw::snd::internal::SequenceSoundFile;
using nw::snd::internal::SequenceSoundFileReader;

static constexpr u32 cLabelTextOffset       = sizeof(nw::snd::internal::Util::Reference) + sizeof(nw::ut::ResU32);
static constexpr u32 cAllocTrackCommandSize = 3;

static bool CheckBlockBounds(const nw::ut::BinaryFileHeader& fileHeader, const nw::ut::BinaryBlockHeader& block, const char* blockName)
{
    const uintptr_t offset = reinterpret_cast<uintptr_t>(&block) - reinterpret_cast<uintptr_t>(&fileHeader);
    const u32 fileSize = fileHeader.fileSize;
    const u32 blockSize = block.size;

    if (blockSize >= sizeof(nw::ut::BinaryBlockHeader) && offset <= fileSize && blockSize <= fileSize - offset)
        return true;

    PopupMgr::instance()->pushCurrentItemError(sead::FormatFixedSafeString<128>(messages::sequence::cDataBlockSizeFormat, blockName, blockSize));
    return false;
}

static bool CheckLabelInfo(const SequenceSoundFile::LabelBlockBody& body, u32 bodySize, u32 index, const SeqFileLayout& layout)
{
    const s32 infoOffset = body.labelInfoReferenceTable.item[index].offset;

    if (infoOffset < 0 || static_cast<u32>(infoOffset) >= bodySize || bodySize - infoOffset <= cLabelTextOffset)
    {
        PopupMgr::instance()->pushCurrentItemError(sead::FormatFixedSafeString<128>(messages::sequence::cDataLabelEntryFormat, index));
        return false;
    }

    const SequenceSoundFile::LabelInfo* info = static_cast<const SequenceSoundFile::LabelInfo*>(sead::PtrUtil::addOffset(&body, infoOffset));

    if (!std::memchr(info->label, 0, bodySize - infoOffset - cLabelTextOffset))
    {
        PopupMgr::instance()->pushCurrentItemError(sead::FormatFixedSafeString<128>(messages::sequence::cDataLabelEntryFormat, index));
        return false;
    }

    const s32 seqOffset = info->referToSequenceData.offset;

    bool inData = seqOffset >= 0 && static_cast<u32>(seqOffset) <= layout.seqDataSize;
    if (inData && static_cast<u32>(seqOffset) < layout.seqDataSize && layout.seqData[seqOffset] == MmlCommand::MML_ALLOC_TRACK)
        inData = layout.seqDataSize - seqOffset >= cAllocTrackCommandSize;

    if (!inData)
    {
        PopupMgr::instance()->pushCurrentItemError(sead::FormatFixedSafeString<128>(messages::sequence::cDataLabelOffsetFormat, index, static_cast<u32>(seqOffset)));
        return false;
    }

    return true;
}

bool CheckSequenceFileLayout(SeqFileLayout& layout, const SequenceSoundFileReader& reader)
{
    const nw::ut::BinaryFileHeader& fileHeader = reader.mHeader->header;
    const SequenceSoundFile::DataBlock* dataBlock = reader.mHeader->GetDataBlock();
    const SequenceSoundFile::LabelBlock* labelBlock = reader.mHeader->GetLabelBlock();

    if (!CheckBlockBounds(fileHeader, dataBlock->header, "DATA"))
        return false;

    if (!CheckBlockBounds(fileHeader, labelBlock->header, "LABL"))
        return false;

    layout.seqData = static_cast<const u8*>(dataBlock->body.GetSequenceData());
    layout.seqDataSize = dataBlock->header.size - sizeof(nw::ut::BinaryBlockHeader);

    const u32 bodySize = labelBlock->header.size - sizeof(nw::ut::BinaryBlockHeader);
    const u32 labelNum = bodySize >= sizeof(nw::ut::ResU32) ? static_cast<u32>(labelBlock->body.labelInfoReferenceTable.count) : 0;

    if (bodySize < sizeof(nw::ut::ResU32) || labelNum > (bodySize - sizeof(nw::ut::ResU32)) / sizeof(nw::snd::internal::Util::Reference))
    {
        PopupMgr::instance()->pushCurrentItemError(sead::FormatFixedSafeString<128>(messages::sequence::cDataLabelTableFormat, labelNum));
        return false;
    }

    for (u32 i = 0; i < labelNum; i++)
    {
        if (!CheckLabelInfo(labelBlock->body, bodySize, i, layout))
            return false;
    }

    return true;
}

static bool IsFinalFin(const u8* trackPtr, const u8* seqDataEnd)
{
    if (*trackPtr != MmlCommand::MML_FIN)
        return false;

    for (const u8* ptr = trackPtr + 1; ptr < seqDataEnd; ptr++)
    {
        if (*ptr != 0)
            return false;
    }

    return true;
}

static void AppendLabelLines(std::vector<std::string>& outLines, const std::vector<std::string>& labels)
{
    if (outLines.size() != 0)
        outLines.emplace_back("");

    for (const auto& label : labels)
    {
        outLines.emplace_back(label + ':');
    }
}

static void DeleteCommands(const std::map<u32, MmlCommandBase*>& commands)
{
    for (const auto& it : commands)
    {
        delete it.second;
    }
}

bool DecompileSequenceFile(std::vector<std::string>& outLines, std::unordered_map<u32, u32>& offsetToLine, SequenceSoundFileReader& reader, const SeqFileLayout& layout)
{
    reader.createLabelCache();

    const u8* seqData = layout.seqData;
    const u8* seqDataEnd = seqData + layout.seqDataSize;

    const InnerFileFormatInfo* seqFormat = FindInnerFileFormat(reader.mHeader, InnerFileKind::Sequence);
    if (seqFormat && seqFormat->format == ArchiveFormat::BCSAR)
        MmlParser::sSeqParamEndian = sFileEndian == sead::Endian::eLittle ? sead::Endian::eBig : sead::Endian::eLittle;
    else
        MmlParser::sSeqParamEndian = sFileEndian;

    const u8* trackPtr = seqData;

    std::map<u32, MmlCommandBase*> commands;
    while (trackPtr < seqDataEnd && !IsFinalFin(trackPtr, seqDataEnd))
    {
        u32 offset = static_cast<u32>(trackPtr - seqData);

        commands[offset] = nw__snd__internal__driver__MmlParser__Parse(trackPtr, reader);
    }

    const auto& labelCache = reader.mLabelCache;

    u32 endOffset = static_cast<u32>(trackPtr - seqData);
    for (const auto& it : labelCache)
    {
        if (commands.find(it.first) == commands.end() && it.first != endOffset)
        {
            sead::FormatFixedSafeString<256> msg(messages::sequence::cDataLabelNotVisitedFormat,
                it.first, endOffset, commands.size(), labelCache.size());
            PopupMgr::instance()->pushCurrentItemError(msg);

            DeleteCommands(commands);
            return false;
        }
    }

    for (const auto& it : commands)
    {
        u32 offset = it.first;
        MmlCommandBase* cmdInst = it.second;

        const auto& labelListIt = labelCache.find(offset);
        if (labelListIt != labelCache.end())
            AppendLabelLines(outLines, labelListIt->second);

        std::string cmd = "(null)";
        if (cmdInst)
            cmd = cmdInst->toString();

        offsetToLine[offset] = outLines.size() + 1;

        outLines.emplace_back("    " + cmd);
    }

    const auto& labelListIt = labelCache.find(endOffset);
    if (labelListIt != labelCache.end() && commands.find(endOffset) == commands.end())
        AppendLabelLines(outLines, labelListIt->second);

    DeleteCommands(commands);

    return true;
}
