#include <bfsar/SeqCommand.h>
#include <bfsar/Sound.h>

#include <ui/PopupMgr.h>
#include <ui/UI.h>

#include <heap/seadFrameHeap.h>
#include <heap/seadHeapMgr.h>

#include <snd/snd_SequenceSoundFileReader.h>
#include <snd/ut/res/ut_ResTypes.h>

#include <unordered_map>

extern MmlCommandBase* nw__snd__internal__driver__MmlParser__Parse(const u8*& trackData, nw::snd::internal::SequenceSoundFileReader& reader);

bool ParseSequenceFile(std::vector<std::string>* outLines, std::unordered_map<u32, u32>* offsetToLine, const void* seqFile)
{
    SEAD_ASSERT(outLines);
    SEAD_ASSERT(offsetToLine);

    if (!seqFile)
        return false;

    nw::snd::internal::SequenceSoundFileReader reader(seqFile);
    if (!reader.IsAvailable())
    {
        return false;
    }

    reader.createLabelCache();

    const void* seqData = reader.GetSequenceData();
    const nw::ut::BinaryBlockHeader* seqDataBlockHeader = &reader.mHeader->GetDataBlock()->header;
    const u8* seqDataEnd = (u8*)seqDataBlockHeader + seqDataBlockHeader->size;

    const u8* trackPtr = (const u8*)seqData;

    // Set the correct endianness for sequence parameter values:
    // For 'C' format (CSEQ), parameter endianness is OPPOSITE of the file header BOM.
    // For 'F' format (FSEQ), parameter endianness MATCHES the file header BOM.
    if (sead::MemUtil::compare(seqFile, "CSEQ", 4) == 0)
        MmlParser::sSeqParamEndian = sFileEndian == sead::Endian::eLittle ? sead::Endian::eBig : sead::Endian::eLittle;
    else
        MmlParser::sSeqParamEndian = sFileEndian;

    std::map<u32, MmlCommandBase*> commands;
    while (true)
    {
        if (trackPtr >= seqDataEnd)
            break;

        if (*trackPtr == 0xFF)
        {
            const u8* remainPtr = trackPtr + 1;
            u32 remainSize = seqDataEnd - remainPtr;
            bool allZero = true;

            for (u32 i = 0; i < remainSize; i++)
            {
                if (remainPtr[i] != 0)
                {
                    allZero = false;
                    break;
                }
            }

            if (allZero)
                break;
        }

        u32 offset = reinterpret_cast<uintptr_t>(trackPtr) - reinterpret_cast<uintptr_t>(seqData);

        MmlCommandBase* cmd = nullptr;
        {
            cmd = nw__snd__internal__driver__MmlParser__Parse(trackPtr, reader);
        }

        commands[offset] = cmd;
    }

    const auto& labelCache = reader.mLabelCache;

    u32 endOffset = reinterpret_cast<uintptr_t>(trackPtr) - reinterpret_cast<uintptr_t>(seqData);
    for (const auto& it : labelCache)
    {
        if (commands.find(it.first) == commands.end() && it.first != endOffset)
        {
            sead::FormatFixedSafeString<256> msg("Invalid sequence data: label 0x%X not visited (endOfs=0x%X, cmdCount=%zu, labelCount=%zu)",
                it.first, endOffset, commands.size(), labelCache.size());
            PopupMgr::instance()->pushCurrentItemError(msg);
            return false;
        }
    }

    for (const auto& it : commands)
    {
        u32 offset = it.first;
        MmlCommandBase* cmdInst = it.second;

        const auto& labelListIt = labelCache.find(offset);
        if (labelListIt != labelCache.end())
        {
            if (outLines->size() != 0)
                outLines->emplace_back("");

            for (const auto& label : labelListIt->second)
            {
                outLines->emplace_back(label + ':');
            }
        }

        std::string cmd = "(null)";
        if (cmdInst)
            cmd = cmdInst->toString();

        u32 line = outLines->size() + 1;

        (*offsetToLine)[offset] = line;
        //SEAD_PRINT("offset: %u, line: %u, cmd: %s\n", offset, line, cmd.c_str());

        outLines->emplace_back("    " + cmd);
    }

    const auto& labelListIt = labelCache.find(endOffset);
    if (labelListIt != labelCache.end() && commands.find(endOffset) == commands.end())
    {
        if (outLines->size() != 0)
            outLines->emplace_back("");

        for (const auto& label : labelListIt->second)
        {
            outLines->emplace_back(label + ':');
        }
    }

    //outLines->emplace_back("");

    for (const auto& it : commands)
    {
        delete it.second;
    }

    return true;
}
