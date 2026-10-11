#include <bfsar/files/BfwsdFile.h>

#include <core/Debug.h>

#include <bfsar/io/FileWriter.h>
#include <bfsar/io/FlagParameters.h>
#include <bfsar/io/WaveIdTableWriter.h>

#include <ui/app/UI.h>

#include <snd/snd_WaveSoundFile.h>

#include <bit>
#include <unordered_map>
#include <vector>

static constexpr f32 cNoteEventPosition   = 0.0f;
static constexpr f32 cNoteEventLength     = 0.0f;
static constexpr u32 cNoteEventNoteIndex  = 0;
static constexpr u32 cNoteEventReserved   = 0;
static constexpr u32 cNoteInfoOptionParam = 0;

namespace
{

struct WaveSoundWriteContext
{
    WaveSoundWriteContext(sead::FileHandle* handle, sead::WriteStream* stream_, u32 maskedWaveArchiveId, const std::unordered_map<const WaveFile*, u32>* waveArchiveIndexes)
        : writer(handle, stream_)
        , stream(stream_)
        , waveIds(maskedWaveArchiveId, waveArchiveIndexes)
    {
    }

    FileWriter writer;
    sead::WriteStream* stream;
    WaveIdTableWriter waveIds;
    std::vector<const Sound*> waveSounds;
    bool isFilterSupported = false;
};

struct WaveSoundInfoFlags
{
    std::unordered_map<u32, u32> flags;
    u32 sendParamOffset = 0;
    u32 envelopeParamOffset = 0;
};

}

static const WaveFile* GetSoundWaveFile(const Sound& sound)
{
    const Item* waveFileItem = sound.getWaveSoundInfo().getWaveFileRef().getItem();
    SEAD_ASSERT(waveFileItem);
    SEAD_ASSERT(waveFileItem->getItemType() == Item::ItemType::WaveFile);

    return static_cast<const WaveFile*>(waveFileItem);
}

static void CollectWaveSounds(WaveSoundWriteContext& context, const SoundSet& soundSet)
{
    for (const Item::ListNode* itemNode = sBfsar.getItem(soundSet.getStartId(), sBfsar.getSoundList()); itemNode && itemNode->val()->getId() <= soundSet.getEndId(); itemNode = sBfsar.getSoundList().next(itemNode))
    {
        SEAD_ASSERT(itemNode->val()->getItemType() == Item::ItemType::Sound);
        const Sound* sound = static_cast<const Sound*>(itemNode->val());

        if (sound->getSoundType() != Sound::SoundType::Wave)
            continue;

        context.waveSounds.push_back(sound);
        context.waveIds.add(GetSoundWaveFile(*sound));
    }
}

static WaveSoundInfoFlags BuildWaveSoundInfoFlags(const Sound::WaveSoundInfo& waveSoundInfo, u32 firstParamOffset, bool isFilterSupported)
{
    WaveSoundInfoFlags result;
    result.sendParamOffset = firstParamOffset;
    result.envelopeParamOffset = firstParamOffset;

    if (waveSoundInfo.isEnablePan())
    {
        result.flags[nw::snd::internal::WAVE_SOUND_INFO_PAN] = waveSoundInfo.getPan() | (waveSoundInfo.getSurroundPan() << 8);

        result.sendParamOffset += 4;
        result.envelopeParamOffset += 4;
    }

    if (waveSoundInfo.isEnablePitch())
    {
        result.flags[nw::snd::internal::WAVE_SOUND_INFO_PITCH] = std::bit_cast<u32>(waveSoundInfo.getPitch());

        result.sendParamOffset += 4;
        result.envelopeParamOffset += 4;
    }

    if (isFilterSupported && waveSoundInfo.isEnableFilter())
    {
        result.flags[nw::snd::internal::WAVE_SOUND_INFO_FILTER] = waveSoundInfo.getLpfFreq() | (waveSoundInfo.getBiquadType() << 8) | (waveSoundInfo.getBiquadValue() << 16);

        result.sendParamOffset += 4;
        result.envelopeParamOffset += 4;
    }

    if (waveSoundInfo.isEnableSend())
    {
        result.flags[nw::snd::internal::WAVE_SOUND_INFO_SEND] = 0;

        result.envelopeParamOffset += 4;
    }

    if (waveSoundInfo.isEnableEnvelope())
        result.flags[nw::snd::internal::WAVE_SOUND_INFO_ENVELOPE] = 0;

    for (const std::pair<u32, u32>& opt : waveSoundInfo.getExtraOptions())
    {
        if (result.flags.find(opt.first) != result.flags.end())
            continue;

        result.flags[opt.first] = opt.second;

        if (opt.first < nw::snd::internal::WAVE_SOUND_INFO_SEND)
        {
            result.sendParamOffset += 4;
            result.envelopeParamOffset += 4;
        }
        else if (opt.first < nw::snd::internal::WAVE_SOUND_INFO_ENVELOPE)
        {
            result.envelopeParamOffset += 4;
        }
    }

    return result;
}

static void WriteParamOffset(WaveSoundWriteContext& context, u32 paramOffset, u32 infoPos)
{
    u32 paramPos = context.writer.getPosition();
    context.writer.seek(paramOffset);
    context.stream->writeU32(paramPos - infoPos);
    context.writer.seek(paramPos);
}

static void WriteSendParam(WaveSoundWriteContext& context, const Sound::WaveSoundInfo& waveSoundInfo)
{
    sead::WriteStream* stream = context.stream;

    stream->writeU8(waveSoundInfo.getMainSend());

    u8 fxSendCount = waveSoundInfo.getOriginalFxSendCount();
    if (fxSendCount == 0)
        fxSendCount = nw::snd::AUX_BUS_NUM;

    stream->writeU8(fxSendCount);

    for (u32 i = 0; i < fxSendCount; i++)
    {
        stream->writeU8(waveSoundInfo.getFxSend(i));
    }

    u8 padBytes = 4 - (2 + fxSendCount) % 4;
    for (u32 i = 0; i < padBytes; i++)
    {
        stream->writeU8(0);
    }
}

static void WriteEnvelopeParam(WaveSoundWriteContext& context, const Sound::WaveSoundInfo& waveSoundInfo)
{
    FileWriter& writer = context.writer;
    sead::WriteStream* stream = context.stream;

    writer.pushOffsetBase();
    {
        writer.openReference("AdshrCurve");

        writer.closeReference("AdshrCurve", 0);
        const snd::AdshrCurve& adshrCurve = waveSoundInfo.getAdshrCurve();

        stream->writeU8(adshrCurve.attack);
        stream->writeU8(adshrCurve.decay);
        stream->writeU8(adshrCurve.sustain);
        stream->writeU8(adshrCurve.hold);
        stream->writeU8(adshrCurve.release);

        writer.align(4);
    }
    writer.popOffsetBase();
}

static void WriteWaveSoundInfo(WaveSoundWriteContext& context, const Sound::WaveSoundInfo& waveSoundInfo)
{
    FileWriter& writer = context.writer;

    u32 infoPos = writer.getPosition();

    WaveSoundInfoFlags info = BuildWaveSoundInfoFlags(waveSoundInfo, infoPos + 4, context.isFilterSupported);

    FlagParameters params(info.flags);
    params.write(writer);

    if (waveSoundInfo.isEnableSend())
    {
        WriteParamOffset(context, info.sendParamOffset, infoPos);
        WriteSendParam(context, waveSoundInfo);
    }

    if (waveSoundInfo.isEnableEnvelope())
    {
        WriteParamOffset(context, info.envelopeParamOffset, infoPos);
        WriteEnvelopeParam(context, waveSoundInfo);
    }
}

static void WriteTrackInfoTable(WaveSoundWriteContext& context)
{
    FileWriter& writer = context.writer;
    sead::WriteStream* stream = context.stream;

    writer.openReferenceTable("TrackInfoTable", 1);

    writer.addReferenceTableReference("TrackInfoTable", nw::snd::internal::ElementType_WaveSoundFile_TrackInfo);

    writer.pushOffsetBase();
    {
        writer.openReference("NoteEventTableRef");

        writer.closeReference("NoteEventTableRef", 0);
        writer.pushOffsetBase();
        {
            writer.openReferenceTable("NoteEventTable", 1);

            writer.addReferenceTableReference("NoteEventTable", nw::snd::internal::ElementType_WaveSoundFile_NoteEvent);

            stream->writeF32(cNoteEventPosition);
            stream->writeF32(cNoteEventLength);
            stream->writeU32(cNoteEventNoteIndex);
            stream->writeU32(cNoteEventReserved);

            writer.closeReferenceTable("NoteEventTable");
        }
        writer.popOffsetBase();
    }
    writer.popOffsetBase();

    writer.closeReferenceTable("TrackInfoTable");
}

static void WriteNoteInfoTable(WaveSoundWriteContext& context, const Sound& sound)
{
    FileWriter& writer = context.writer;
    sead::WriteStream* stream = context.stream;

    writer.openReferenceTable("NoteInfoTable", 1);

    writer.addReferenceTableReference("NoteInfoTable", nw::snd::internal::ElementType_WaveSoundFile_NoteInfo);

    stream->writeU32(context.waveIds.getTableIndex(GetSoundWaveFile(sound)));
    stream->writeU32(cNoteInfoOptionParam);

    writer.closeReferenceTable("NoteInfoTable");
}

static void WriteWaveSoundData(WaveSoundWriteContext& context, const Sound& sound)
{
    FileWriter& writer = context.writer;

    writer.openReference("WaveSoundInfo");
    writer.openReference("TrackInfoTableRef");
    writer.openReference("NoteInfoTableRef");

    writer.closeReference("WaveSoundInfo", nw::snd::internal::ElementType_WaveSoundFile_WaveSoundInfo);
    writer.pushOffsetBase();
    {
        WriteWaveSoundInfo(context, sound.getWaveSoundInfo());
    }
    writer.popOffsetBase();

    writer.closeReference("TrackInfoTableRef", nw::snd::internal::ElementType_Table_ReferenceTable);
    writer.pushOffsetBase();
    {
        WriteTrackInfoTable(context);
    }
    writer.popOffsetBase();

    writer.closeReference("NoteInfoTableRef", nw::snd::internal::ElementType_Table_ReferenceTable);
    writer.pushOffsetBase();
    {
        WriteNoteInfoTable(context, sound);
    }
    writer.popOffsetBase();
}

static void WriteInfoBlock(WaveSoundWriteContext& context)
{
    FileWriter& writer = context.writer;

    writer.openBlock(nw::snd::internal::ElementType_WaveSoundFile_InfoBlock, "INFO");

    writer.openReference("WaveIdTable");
    writer.openReference("WaveSoundDataTableRef");

    writer.closeReference("WaveIdTable", nw::snd::internal::ElementType_Table_EmbeddingTable);

    context.waveIds.write(context.stream);

    writer.closeReference("WaveSoundDataTableRef", nw::snd::internal::ElementType_Table_ReferenceTable);
    writer.pushOffsetBase();
    {
        writer.openReferenceTable("WaveSoundDataTable", static_cast<u32>(context.waveSounds.size()));

        for (const Sound* sound : context.waveSounds)
        {
            writer.addReferenceTableReference("WaveSoundDataTable", nw::snd::internal::ElementType_WaveSoundFile_WaveSoundMetaData);

            writer.pushOffsetBase();
            {
                WriteWaveSoundData(context, *sound);
            }
            writer.popOffsetBase();
        }

        writer.closeReferenceTable("WaveSoundDataTable");
    }
    writer.popOffsetBase();

    writer.closeBlock();
}

u32 BfwsdFile::doWrite(sead::FileHandle* handle, sead::WriteStream* stream, bool isLast) const
{
    LOG_FUNC();
    LOG_FMT("format=%s, version=0x%04X", GetInnerFileMagic(mFormat, InnerFileKind::WaveSoundData), mVersion);

    SEAD_ASSERT(mSoundSet);
    SEAD_ASSERT(mWaveArchive);

    LOG_U32("soundSetStartId", mSoundSet->getStartId());
    LOG_U32("soundSetEndId", mSoundSet->getEndId());

    u32 maskedWaveArchiveId = nw::snd::internal::Util::GetMaskedItemId(mWaveArchive->getId(), nw::snd::internal::ItemType_WaveArchive);

    WaveSoundWriteContext context(handle, stream, maskedWaveArchiveId, mWaveArchiveWaveFilesIndexes);
    context.isFilterSupported = isFilterSupportedVersion(mVersion, mFormat);
    CollectWaveSounds(context, *mSoundSet);

    LOG_U32("waveIdsCount", context.waveIds.getCount());
    LOG_SIZE("waveSoundCount", context.waveSounds.size());

    context.writer.openFile(GetInnerFileMagic(mFormat, InnerFileKind::WaveSoundData), 1, mVersion);

    WriteInfoBlock(context);

    u32 fileSize = context.writer.getPosition();

    LOG_U32("fileSize", fileSize);

    context.writer.closeFile();

    mSoundSet = nullptr;
    mWaveArchive = nullptr;
    mWaveArchiveWaveFilesIndexes = nullptr;

    return fileSize;
}
