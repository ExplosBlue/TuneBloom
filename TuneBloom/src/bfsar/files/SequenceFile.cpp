#include <bfsar/files/SequenceFile.h>

#include <bfsar/seq/SeqDecompile.h>
#include <bfsar/io/FileIo.h>

#include <snd/snd_SequenceSoundFileReader.h>
#include <snd/DisposeCallbackMgr.h>
#include <snd/MmlParser.h>

#include <ui/Messages.h>
#include <ui/app/UI.h>

#include <filedevice/seadFileDeviceMgr.h>

#include <libs/md5.h>

#include <core/Debug.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

static u32 sSeqBytesRevisionCounter = 0;

SequenceFile::~SequenceFile()
{
    invalidatePlayer_();

    deleteEditor_();

    if (mSeqText)
    {
        delete mSeqText;
        mSeqText = nullptr;
    }

    freeSeqBytes_();

    for (u32 i = 0; i < 4; i++)
    {
        delete mBankRefs[i];
        mBankRefs[i] = nullptr;
    }
}

void SequenceFile::setSeqBytes_(const u8* bytes, u32 size)
{
    mSeqBytesSize = size;
    mSeqBytes = new u8[mSeqBytesSize];
    mSeqBytesRevision = ++sSeqBytesRevisionCounter;
    sead::MemUtil::copy(mSeqBytes, bytes, mSeqBytesSize);
}

void SequenceFile::freeSeqBytes_()
{
    if (mSeqBytes)
        delete[] mSeqBytes;

    mSeqBytes = nullptr;
    mSeqBytesSize = 0;
}

void SequenceFile::discardSeqBytes_()
{
    if (!mSeqBytes)
        return;

    invalidatePlayer_();
    freeSeqBytes_();
}

const Item* SequenceFile::validate(sead::BufferedSafeString& error) const
{
    if (!isValid())
    {
        error = messages::sequence::cNotCompiled;
        return this;
    }

    if (!getSeqBytes())
    {
        error = messages::sequence::cNoBytesError;
        return this;
    }

    return nullptr;
}

u32 SequenceFile::getLabelOffset(const sead::SafeString& label, bool parsed) const
{
    if (!mIsValid)
    {
        return cInvaldOffset;
    }

    auto it = mLabelsStartInfo.find(label.cstr());
    if (it == mLabelsStartInfo.end())
    {
        return cInvaldOffset;
    }

    return parsed ? it->second.parsedOffset : it->second.offset;
}

u32 SequenceFile::getLabelAllocTracks(const sead::SafeString& label) const
{
    if (!mIsValid)
    {
        return 0;
    }

    auto it = mLabelsStartInfo.find(label.cstr());
    if (it == mLabelsStartInfo.end())
    {
        return 0;
    }

    return it->second.allocTrackFlags;
}

std::string SequenceFile::getLabelFromParsedOffset(u32 offset, u32 allocTrackFlags) const
{
    LOG_FMT("getLabelFromParsedOffset: look for offset=%u allocTrackFlags=0x%x in %s (%zu labels)",
        offset, allocTrackFlags, mName.cstr(), mLabelsStartInfo.size());
    for (const auto& pair : mLabelsStartInfo)
    {
        const std::string& label = pair.first;
        if (label.empty())
        {
            continue;
        }

        const LabelStartInfo& startInfo = pair.second;

        if ((offset == startInfo.parsedOffset || offset == startInfo.offset) && allocTrackFlags == startInfo.allocTrackFlags)
        {
            LOG_FMT("  MATCH: '%s' offset=%u parsedOffset=%u allocTrackFlags=0x%x",
                label.c_str(), startInfo.offset, startInfo.parsedOffset, startInfo.allocTrackFlags);
            return pair.first;
        }
        LOG_FMT("  CHECK '%s': offset=%u parsedOffset=%u allocTrackFlags=0x%x (match_off=%d match_flags=%d)",
            label.c_str(), startInfo.offset, startInfo.parsedOffset, startInfo.allocTrackFlags,
            (offset == startInfo.parsedOffset || offset == startInfo.offset),
            allocTrackFlags == startInfo.allocTrackFlags);
    }

    LOG_FMT("  NO MATCH found");
    return "";
}

void SequenceFile::invalidatePlayer_() const
{
    if (sBfsar.getCliMode())
        return;

    if (sSoundPlayer.getSequencePlayer().isPlayingFile(*this))
    {
        sSoundPlayer.getSequencePlayer().clearPlayingFile();
    }

    if (mSeqBytes)
    {
        snd::internal::driver::SoundThreadLock lock;

        snd::internal::driver::DisposeCallbackMgr::instance()->dispose(mSeqBytes, mSeqBytesSize);
    }
}

static SequenceFile::LabelStartInfo ReadLabelStartInfo(const SeqFileLayout& layout, u32 offset)
{
    u32 parsedOffset = offset;
    u32 allocTrackFlags = 1;

    if (offset < layout.seqDataSize)
        parsedOffset = MmlParser::ParseAllocTrack(layout.seqData, offset, &allocTrackFlags);

    return { offset, parsedOffset, allocTrackFlags };
}

bool SequenceFile::doRead(const void* fileAddr)
{
    LOG_FMT("%s: doRead at %p, magic=%.4s", getNameOrNull().cstr(), fileAddr, (const char*)fileAddr);

    nw::snd::internal::SequenceSoundFileReader reader(fileAddr);
    if (!reader.IsAvailable())
    {
        LOG_FMT("doRead: SequenceSoundFileReader not available");
        return false;
    }

    SeqFileLayout layout;
    if (!CheckSequenceFileLayout(layout, reader))
    {
        LOG_FMT("doRead: CheckSequenceFileLayout failed");
        return false;
    }

    std::vector<std::string> lines;
    std::unordered_map<u32, u32> offsetToLine;
    if (!DecompileSequenceFile(lines, offsetToLine, reader, layout))
    {
        LOG_FMT("doRead: DecompileSequenceFile failed");
        return false;
    }

    LOG_U32("parsedLines", lines.size());

    std::string fseq;
    for (const auto& line : lines)
    {
        fseq += line + '\n';
    }

    mSeqText = new sead::HeapSafeString(nullptr, fseq.c_str());

    LOG_U32("seqBytesSize", layout.seqDataSize);
    setSeqBytes_(layout.seqData, layout.seqDataSize);

    mSeqTextInfo.reset();
    mLabelsStartInfo.clear();
    mLabels.clear();
    mOffsetToLine.clear();
    mStartLabel.clear();

    std::vector<std::pair<u32, const char*>> labelOffsets;

    LOG_U32("labelCount", reader.GetLabelCount());
    for (s32 i = 0; i < reader.GetLabelCount(); i++)
    {
        const char* label = reader.GetLabel(i);

        u32 offset;
        if (!reader.mLabelBlockBody->GetOffset(i, &offset))
        {
            return false;
        }

        labelOffsets.push_back(std::make_pair(offset, label));

        mLabelsStartInfo[label] = ReadLabelStartInfo(layout, offset);
    }

    std::sort(labelOffsets.begin(), labelOffsets.end());
    for (u32 i = 0; i < labelOffsets.size(); i++)
    {
        mLabels.push_back(labelOffsets[i].second);
    }

    mCompiledLabels = mLabels;

    mLabelsStartInfo[""] = ReadLabelStartInfo(layout, 0);

    if (mName.startsWith("SE_DEMO_OP_LOGO_LAND") || mName.startsWith("GUESS"))
    {
        LOG_FMT("CSEQ %s labels:", mName.cstr());
        for (const auto& [lbl, info] : mLabelsStartInfo)
        {
            LOG_FMT("  '%s': offset=%u parsedOffset=%u allocTrackFlags=0x%x",
                lbl.empty() ? "(empty)" : lbl.c_str(), info.offset, info.parsedOffset, info.allocTrackFlags);
        }
    }

    mOffsetToLine = std::move(offsetToLine);

    if (!mLoadError)
    {
        mIsValid = true;
    }

    return true;
}

u32 SequenceFile::doWrite(sead::FileHandle* handle, sead::WriteStream* stream, bool isLast) const
{
    const char* magic = GetInnerFileMagic(mFormat, InnerFileKind::Sequence);
    LOG_FMT("%s: doWrite magic=%.4s seqBytes=%u labels=%zu version=%u", getNameOrNull().cstr(), magic, mSeqBytesSize, mLabels.size(), mVersion);

    if (!isValid())
    {
        LOG_FMT("doWrite: not valid, returning 0");
        return 0;
    }

    FileWriter writer(handle, stream);
    writer.openFile(magic, 2, mVersion);

    //? Data Block
    {
        writer.openBlock(nw::snd::internal::ElementType_SequenceSoundFile_DataBlock, "DATA");

        LOG_FMT("writing %u bytes of sequence data", mSeqBytesSize);
        stream->writeMemBlock(mSeqBytes, mSeqBytesSize);

        writer.align(0x20);
        writer.closeBlock();
    }

    //? Label Block
    {
        writer.openBlock(nw::snd::internal::ElementType_SequenceSoundFile_LabelBlock, "LABL");

        writer.pushOffsetBase();
        {
            writer.openReferenceTable("LabelInfoTable", mLabels.size());

            std::vector<std::string> labels = mLabels;
            std::sort(labels.begin(), labels.end());

            for (u32 i = 0; i < labels.size(); i++)
            {
                writer.addReferenceTableReference("LabelInfoTable", nw::snd::internal::ElementType_SequenceSoundFile_LabelInfo);

                const std::string& label = labels[i];
                u32 labelSize = label.size();

                auto it = mLabelsStartInfo.find(label);
                SEAD_ASSERT(it != mLabelsStartInfo.end());
                u32 offset = it->second.offset;

                writer.pushOffsetBase();
                {
                    stream->writeU16(nw::snd::internal::ElementType_General_ByteStream); // Reference type
                    stream->writeU16(0); // Padding
                    stream->writeU32(offset);

                    stream->writeU32(labelSize);
                    stream->writeString(label.c_str(), labelSize + 1);

                    writer.align(4);
                }
                writer.popOffsetBase();
            }

            writer.closeReferenceTable("LabelInfoTable");
        }
        writer.popOffsetBase();

        writer.align(0x20);
        writer.closeBlock();
    }

    u32 fileSize = writer.getPosition();

    writer.closeFile();

    return fileSize;
}

const std::string& SequenceFile::getMd5Hash()
{
    const bool isStale = mMd5Dirty || mMd5Format != sBfsar.getFormat() ||
                         mMd5Version != sBfsar.getVersionForBfseq() || mMd5Endian != sBfsar.getEndian();

    if (isStale)
        refreshSerializedHash();

    return mMd5Hash;
}

void SequenceFile::setSerializedHash(const std::string& hash)
{
    mMd5Hash = hash;
    mMd5Dirty = false;
    mMd5Format = sBfsar.getFormat();
    mMd5Version = sBfsar.getVersionForBfseq();
    mMd5Endian = sBfsar.getEndian();
}

void SequenceFile::refreshSerializedHash()
{
    sead::FileDevice *device = sead::FileDeviceMgr::instance()->findDevice("native");

    if (!device)
        return;

    char tempPath[700];
    snprintf(tempPath, sizeof(tempPath), "%s/tunebloom_seqhash_%p.bin", fileio::GetTempFolder(), static_cast<const void *>(this));

    sead::FileHandle handle;
    device->tryOpen(&handle, tempPath, sead::FileDevice::FileOpenFlag::eWriteOnly, 0);

    if (!handle.getDevice())
    {
        device->tryOpen(&handle, tempPath, sead::FileDevice::FileOpenFlag::eCreate, 0);

        if (!handle.getDevice())
            return;
    }

    {
        sead::FileDeviceWriteStream stream(&handle, sead::Stream::Modes::eBinary);
        mEndian = sBfsar.getEndian();
        mVersion = sBfsar.getVersionForBfseq();
        setFormat(sBfsar.getFormat());

        write(&handle, &stream, sBfsar.getEndian(), true);
    }

    handle.close();

    sead::FileDevice::LoadArg loadArg;
    loadArg.path = tempPath;
    u8 *buf = device->tryLoad(loadArg);

    if (buf)
    {
        setSerializedHash(md5(buf, loadArg.read_size));
        device->unload(buf);
    }

    ::remove(tempPath);
}
