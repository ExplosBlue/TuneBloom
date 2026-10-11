#include <bfsar/archive/Bfsar.h>

#include <core/Debug.h>

#include <bfsar/archive/BfsarInternal.h>
#include <bfsar/archive/BfsarOpenContext.h>
#include <bfsar/files/BfstpFile.h>
#include <bfsar/files/BfwsdFile.h>
#include <bfsar/files/OpusStream.h>

#include <ui/file/LoadProgress.h>
#include <ui/Messages.h>
#include <ui/app/PopupMgr.h>

#include <filedevice/seadFileDeviceMgr.h>
#include <filedevice/seadPath.h>

#include <snd/snd_StreamSoundFileReader.h>
#include <snd/snd_WaveSoundFileReader.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

using nw::snd::internal::SoundArchiveFile;
using nw::snd::internal::Util;
using nw::snd::internal::WaveSoundFile;

namespace
{

constexpr u32 cPathBufferSize = 512;
constexpr u32 cNameBufferSize = 256;

enum class StreamFileState
{
    NotLoaded,
    Opus,
    Incomplete,
    Complete
};

}

ByteRange LoadFileBytes(const sead::SafeString& path)
{
    sead::FileDevice* device = sead::FileDeviceMgr::instance() ? sead::FileDeviceMgr::instance()->findDevice("native") : nullptr;

    if (!device || path.isEmpty())
        return ByteRange();

    sead::FileDevice::LoadArg arg;
    arg.path = path;
    arg.enable_alloc_assert = false;

    const u8* data = device->tryLoad(arg);

    return data ? ByteRange(data, arg.read_size) : ByteRange();
}

void FreeFileBytes(const ByteRange& file)
{
    delete[] static_cast<const u8*>(file.data());
}

static void FormatCut(sead::BufferedSafeString* out, const char* format, ...)
{
    char text[cNameBufferSize];

    std::va_list args;
    va_start(args, format);
    std::vsnprintf(text, std::min<size_t>(sizeof(text), out->getBufferSize()), format, args);
    va_end(args);

    out->cutOffCopy(text);
}

template <typename IsModeled>
static void CollectUnmodeledOptionBits(const Util::BitFlag& flags, IsModeled isModeled, std::vector<std::pair<u32, u32>>& out)
{
    const u32 bits = flags.bitFlag;

    for (u32 bit = 0; bit <= static_cast<u32>(Util::BitFlag::BIT_NUMBER_MAX); bit++)
    {
        if (!(bits & (1u << bit)) || isModeled(bit))
            continue;

        u32 value = 0;
        flags.GetValue(&value, bit);
        out.push_back({bit, value});
    }
}

bool Bfsar::open_(const nw::snd::MemorySoundArchive& soundArchive, u32 bfsarSize, sead::Heap* heap, const sead::SafeString& groupFileSource)
{
    LOG_FUNC();

    BfsarOpenContext ctx(soundArchive, bfsarSize, heap, groupFileSource);

    if (!CheckSoundArchiveHeader(soundArchive.mData, bfsarSize))
        return false;

    readArchiveVersion_(ctx);

    const ArchiveLayoutVersion layoutVersion = { isStreamTrackInfoAvailable(), isStreamSendAvailable(), isFilterSupportedVersion() };

    if (!CheckSoundArchiveLayout(soundArchive, bfsarSize, layoutVersion))
        return false;

    readPlayerSettings_(ctx);
    setLoadStageCounts_(ctx);

    if (!readGroups_(ctx) || !CheckSoundArchiveLayout(soundArchive, bfsarSize, layoutVersion))
        return false;

    if (!readPlayers_(ctx))
        return false;

    readSequenceFiles_(ctx);
    hashWaveArchiveFiles_(ctx);

    if (!readWaveArchiveFiles_(ctx))
        return false;

    if (!readWaveArchives_(ctx))
        return false;

    readFileGroupTables_(ctx);

    if (!patchBankFileWaveIds_(ctx))
        return false;

    if (!patchWaveSoundFileWaveIds_(ctx) || !CheckSoundArchiveLayout(soundArchive, bfsarSize, layoutVersion))
        return false;

    readBankFiles_(ctx);

    if (!readBanks_(ctx))
        return false;

    if (!readSounds_(ctx))
        return false;

    guessBankWaveFileNames_();

    if (!readSoundSets_(ctx))
        return false;

    if (!readGroupItems_(ctx))
        return false;

    logLoadedItems_();
    recordOriginalFileIds_(ctx);
    assignGroupFileSlots_(ctx);

    return true;
}

void Bfsar::readArchiveVersion_(const BfsarOpenContext& ctx)
{
    LOG_U32("fileSize", ctx.soundArchive.mHeader.fileSize);
    LOG_HEX("magic", &ctx.soundArchive.mHeader.signature, 4);
    LOG_U32("version", ctx.soundArchive.mHeader.version);
    LOG_U32("blockCount", ctx.soundArchive.mHeader.dataBlocks);

    mEndian = nw::ut::GetFileEndian(ctx.soundArchive.mHeader);

    if (mFormat == ArchiveFormat::BFSAR)
        mPlatform = mEndian == sead::Endian::eLittle ? ArchivePlatform::NX : ArchivePlatform::CAFE;

    mVersion = ctx.soundArchive.mHeader.version;

    SoundArchiveFile::UseV3SoundInfoLayout() = mFormat == ArchiveFormat::BFSAR && getDecodedMajor() >= 3;

    mIncludeStringTable = ctx.soundArchive.mStringBlockBody != nullptr;
}

void Bfsar::readPlayerSettings_(const BfsarOpenContext& ctx)
{
    const SoundArchiveFile::SoundArchivePlayerInfo* playerInfo = ctx.soundArchive.GetSoundArchivePlayerInfo();

    mSoundArchivePlayerInfo.sequenceSoundMax = playerInfo->sequenceSoundMax;
    mSoundArchivePlayerInfo.sequenceTrackMax = playerInfo->sequenceTrackMax;
    mSoundArchivePlayerInfo.streamSoundMax = playerInfo->streamSoundMax;
    mSoundArchivePlayerInfo.streamTrackMax = playerInfo->streamTrackMax;
    mSoundArchivePlayerInfo.streamChannelMax = playerInfo->streamChannelMax;
    mSoundArchivePlayerInfo.waveSoundMax = playerInfo->waveSoundMax;
    mSoundArchivePlayerInfo.waveTrackMax = playerInfo->waveTrackMax;
    mSoundArchivePlayerInfo.streamBufferTimes = isStreamPrefetchAvailable() ? playerInfo->streamBufferTimes : 1;
    mSoundArchivePlayerInfo.options = playerInfo->options;
}

void Bfsar::setLoadStageCounts_(const BfsarOpenContext& ctx)
{
    loadprogress::SetStageCount(loadprogress::Stage::Groups, ctx.soundArchive.GetGroupCount());
    loadprogress::SetStageCount(loadprogress::Stage::SequenceFiles, ctx.soundArchive.detail_GetFileCount());
    loadprogress::SetStageCount(loadprogress::Stage::WaveFiles, ctx.soundArchive.GetWaveArchiveCount());
    loadprogress::SetStageCount(loadprogress::Stage::BankFiles, ctx.soundArchive.detail_GetFileCount());

    u32 streamSoundCount = 0;

    for (u32 i = 0; i < ctx.soundArchive.GetSoundCount(); i++)
    {
        const SoundArchiveFile::SoundInfo* soundInfo = ctx.soundArchive.GetSoundInfo(ctx.soundArchive.GetSoundIdFromIndex(i));

        if (soundInfo && soundInfo->GetSoundType() == nw::snd::SoundArchive::SOUND_TYPE_STRM)
            streamSoundCount++;
    }

    loadprogress::SetStageCount(loadprogress::Stage::Sounds, streamSoundCount);
    loadprogress::SetStageCount(loadprogress::Stage::SoundSets, ctx.soundArchive.GetSoundGroupCount());
    loadprogress::SetStageCount(loadprogress::Stage::GroupFiles, ctx.soundArchive.GetGroupCount());
}

void Bfsar::readItemName_(const BfsarOpenContext& ctx, Item* item, u32 stringId, const char* defaultNameFormat)
{
    if (!mIncludeStringTable || stringId == nw::snd::internal::DEFAULT_STRING_ID)
    {
        item->mName.format(defaultNameFormat, item->mId);
        return;
    }

    const char* name = FindArchiveString(ctx.soundArchive, ctx.archiveBytes, stringId);

    if (!name)
    {
        item->mName.format(defaultNameFormat, item->mId);
        PopupMgr::instance()->pushCurrentItemError(messages::archive::cNameNotInStringTable);
        return;
    }

    const size_t length = std::strlen(name);
    const s32 lengthMax = item->mName.getBufferSize() - 1;

    item->mName.cutOffCopy(name, static_cast<s32>(std::min<size_t>(length, lengthMax)));

    if (length > static_cast<size_t>(lengthMax))
        PopupMgr::instance()->pushCurrentItemWarning(sead::FormatFixedSafeString<64>(messages::archive::cNameCutFormat, lengthMax));
}

WaveArchiveType Bfsar::readWaveArchiveRef_(const Util::Table<nw::ut::ResU32>& warcIdTable, ItemReference* waveArchiveRef)
{
    if (warcIdTable.count == 0)
        return WaveArchiveType::AutomaticShared;

    waveArchiveRef->attach(getItem(warcIdTable.item[0], getWaveArchiveList()));

    return waveArchiveRef->isAttached() ? WaveArchiveType::Explicit : WaveArchiveType::AutomaticIndividual;
}

bool Bfsar::readPlayers_(BfsarOpenContext& ctx)
{
    for (u32 i = 0; i < ctx.soundArchive.GetPlayerCount(); i++)
    {
        const SoundArchiveFile::PlayerInfo* playerInfo = ctx.soundArchive.GetPlayerInfo(ctx.soundArchive.GetPlayerIdFromIndex(i));

        if (!playerInfo)
        {
            PopupMgr::instance()->setCorruptInfo(messages::archive::cInvalidPlayerInfo);
            return false;
        }

        Player* player = new(ctx.heap) Player();
        player->mId = i;
        PopupMgr::instance()->setCurrentProcessItem(player);

        player->mEnableName = playerInfo->optionParameter.GetTrueCount(nw::snd::internal::PLAYER_INFO_STRING_ID) != 0;
        readItemName_(ctx, player, playerInfo->GetStringId(), "PLAYER_%u");

        player->mPlayableSoundMax = playerInfo->playableSoundMax;

        player->mEnablePlayerHeapSize = playerInfo->optionParameter.GetTrueCount(nw::snd::internal::PLAYER_INFO_HEAP_SIZE) != 0;

        if (player->mEnablePlayerHeapSize)
            player->mPlayerHeapSize = playerInfo->GetPlayerHeapSize();

        mPlayerList.pushBack(player);
    }

    LOG_FMT("Players: %d, Groups: %u", mPlayerList.size(), ctx.soundArchive.GetGroupCount());

    return true;
}

void Bfsar::attachBankFile_(BfsarOpenContext& ctx, Bank* bank, u32 fileIdx)
{
    const auto it = ctx.bankFileIdxMap.find(fileIdx);
    BankFile* bankFile = static_cast<BankFile*>(getItem(it != ctx.bankFileIdxMap.end() ? it->second : Item::cInvalidId, getBankFileList()));

    bank->mFileRef.attach(bankFile);

    if (bank->mFileRef.isAttached())
    {
        if (bankFile->mName == "Bank")
            FormatCut(&bankFile->mName, "GUESS_%s", bank->mName.cstr());

        return;
    }

    if (fileIdx >= ctx.soundArchive.detail_GetFileCount() || FindArchiveFile(ctx, fileIdx).size() != 0)
        PopupMgr::instance()->pushCurrentItemError(messages::bank::cFileLoadFailed);
}

bool Bfsar::readBanks_(BfsarOpenContext& ctx)
{
    for (u32 i = 0; i < ctx.soundArchive.GetBankCount(); i++)
    {
        const SoundArchiveFile::BankInfo* bankInfo = ctx.soundArchive.GetBankInfo(ctx.soundArchive.GetBankIdFromIndex(i));

        if (!bankInfo)
        {
            PopupMgr::instance()->setCorruptInfo(messages::archive::cInvalidBankInfo);
            return false;
        }

        Bank* bank = new(ctx.heap) Bank();
        bank->mId = i;
        PopupMgr::instance()->setCurrentProcessItem(bank);

        bank->mEnableName = bankInfo->optionParameter.GetTrueCount(nw::snd::internal::BANK_INFO_STRING_ID) != 0;
        readItemName_(ctx, bank, bankInfo->GetStringId(), "BANK_%u");

        bank->mWaveArchiveType = readWaveArchiveRef_(*bankInfo->GetWaveArchiveItemIdTable(), &bank->mWaveArchiveRef);

        attachBankFile_(ctx, bank, Util::GetItemIndex(bankInfo->fileId));

        mBankList.pushBack(bank);
    }

    LOG_FMT("Banks: %d, BankFiles: %d", mBankList.size(), mBankFileList.size());

    return true;
}

void Bfsar::readSequenceSoundInfo_(BfsarOpenContext& ctx, Sound* sound, const SoundArchiveFile::SoundInfo& soundInfo)
{
    const SoundArchiveFile::SequenceSoundInfo& seqSoundInfo = soundInfo.GetSequenceSoundInfo();
    const Util::Table<nw::ut::ResU32>& bankIdTable = seqSoundInfo.GetBankIdTable();
    Sound::SequenceSoundInfo& sequence = sound->mSequenceSoundInfo;

    for (u32 i = 0; i < bankIdTable.count; i++)
    {
        if (i >= nw::snd::SoundArchive::SEQ_BANK_MAX)
        {
            PopupMgr::instance()->pushCurrentItemError(sead::FormatFixedSafeString<64>(messages::archive::cBankReferenceInvalidFormat, i));
            break;
        }

        if (bankIdTable.item[i] == nw::snd::SoundArchive::INVALID_ID)
            continue;

        sequence.mBankRefs[i]->attach(getItem(bankIdTable.item[i], getBankList()));

        if (!sequence.mBankRefs[i]->isAttached())
            PopupMgr::instance()->pushCurrentItemError(sead::FormatFixedSafeString<64>(messages::archive::cBankReferenceInvalidFormat, i));
    }

    const auto it = ctx.seqFileIdxMap.find(Util::GetItemIndex(soundInfo.fileId));
    SequenceFile* seqFile = static_cast<SequenceFile*>(getItem(it != ctx.seqFileIdxMap.end() ? it->second : Item::cInvalidId, getSequenceFileList()));

    sequence.mSequenceFileRef.attach(seqFile);

    if (!sequence.mSequenceFileRef.isAttached())
        PopupMgr::instance()->pushCurrentItemError(messages::archive::cSequenceFileLoadFailed);
    else if (seqFile->mName == "Sequence")
        FormatCut(&seqFile->mName, "GUESS_%s", sound->mName.cstr());

    sequence.mEnableStartOffset = seqSoundInfo.optionParameter.GetTrueCount(nw::snd::internal::SEQ_SOUND_INFO_START_OFFSET) != 0;

    if (sequence.mEnableStartOffset && seqFile)
    {
        const u32 startOffset = seqSoundInfo.GetStartOffset();
        const u32 allocTrackFlags = seqSoundInfo.allocateTrackFlags;
        const std::string label = seqFile->getLabelFromParsedOffset(startOffset, allocTrackFlags);
        const s32 labelMax = sequence.mStartLabel.getBufferSize() - 1;

        if (label.size() <= static_cast<size_t>(labelMax))
            sequence.mStartLabel.copy(label.c_str());
        else
            PopupMgr::instance()->pushCurrentItemError(sead::FormatFixedSafeString<128>(messages::archive::cStartLabelTooLongFormat, labelMax));

        LOG_FMT("Sound %s: startOffset=%u allocTrackFlags=0x%x label='%s'", sound->mName.cstr(), startOffset, allocTrackFlags, label.c_str());
    }

    sequence.mEnablePriority = seqSoundInfo.optionParameter.GetTrueCount(nw::snd::internal::SEQ_SOUND_INFO_PRIORITY) != 0;

    if (sequence.mEnablePriority)
    {
        sequence.mChannelPriority = seqSoundInfo.GetChannelPriority();
        sequence.mIsReleasePriorityFix = seqSoundInfo.IsReleasePriorityFix();
    }
}

void Bfsar::readStreamTrackInfo_(Sound::StreamSoundInfo::Track* track, const SoundArchiveFile::StreamTrackInfo& trackInfo)
{
    track->mVolume = trackInfo.volume;
    track->mPan = trackInfo.pan;
    track->mSPan = trackInfo.span;
    track->mFlags = trackInfo.flags;

    u32 channelCount = trackInfo.GetTrackChannelCount();

    if (channelCount > nw::snd::WAVE_CHANNEL_MAX)
    {
        PopupMgr::instance()->pushCurrentItemError(sead::FormatFixedSafeString<64>(messages::archive::cTrackReadErrorFormat, track->mId));
        channelCount = 0;
    }

    for (u32 i = 0; i < channelCount; i++)
        *track->mChannels.birthBack() = trackInfo.GetGlobalChannelIndex(i);

    if (isStreamSendAvailable() && trackInfo.toSendValue.typeId == nw::snd::internal::ElementType_SoundArchiveFile_SendInfo)
    {
        const SoundArchiveFile::SendValue send = trackInfo.GetSendValue();

        track->mMainSend = send.mainSend;

        for (u32 i = 0; i < nw::snd::AUX_BUS_NUM; i++)
            track->mFxSend[i] = send.fxSend[i];
    }

    if (isFilterSupportedVersion())
    {
        track->mLpfFreq = trackInfo.lpfFreq;
        track->mBiquadType = trackInfo.biquadType;
        track->mBiquadValue = trackInfo.biquadValue;
    }
}

void Bfsar::readStreamTrackInfoTable_(BfsarOpenContext& ctx, Sound* sound, const SoundArchiveFile::StreamSoundInfo& strmSoundInfo)
{
    const SoundArchiveFile::StreamTrackInfoTable* trackInfoTable = strmSoundInfo.GetTrackInfoTable();

    if (!trackInfoTable)
        return;

    for (u32 i = 0; i < nw::snd::SoundArchive::STRM_TRACK_NUM && i < trackInfoTable->GetTrackCount(); i++)
    {
        Sound::StreamSoundInfo::Track* track = new(ctx.heap) Sound::StreamSoundInfo::Track();
        track->mId = i;

        track->mEnableName = true;
        track->mName = "Track";

        const SoundArchiveFile::StreamTrackInfo* trackInfo = trackInfoTable->GetTrackInfo(i);

        if (trackInfo)
            readStreamTrackInfo_(track, *trackInfo);
        else
            PopupMgr::instance()->pushCurrentItemError(sead::FormatFixedSafeString<64>(messages::archive::cTrackReadErrorFormat, i));

        sound->mStreamSoundInfo.mTrackList.pushBack(track);
    }
}

void Bfsar::readStreamFilePath_(const BfsarOpenContext& ctx, Sound* sound, u32 fileId)
{
    const SoundArchiveFile::FileInfo* fileInfo = ctx.soundArchive.detail_GetFileInfo(fileId);
    const SoundArchiveFile::ExternalFileInfo* externalInfo = fileInfo ? fileInfo->GetExternalFileInfo() : nullptr;
    sead::BufferedSafeString& path = sound->mStreamSoundInfo.mPath;

    if (externalInfo && std::strlen(externalInfo->filePath) < static_cast<size_t>(path.getBufferSize()))
        path.copy(externalInfo->filePath);

    if (path.isEmpty())
        PopupMgr::instance()->pushCurrentItemError(messages::stream::cNoPath);
}

bool Bfsar::findStreamFilePath_(const Sound* sound, sead::BufferedSafeString* outPath) const
{
    const char* streamPath = sound->mStreamSoundInfo.mPath.cstr();
    sead::FixedSafeString<cPathBufferSize> archiveDir;

    if (!sead::Path::getDirectoryName(&archiveDir, getFilePath()))
    {
        PopupMgr::instance()->pushCurrentItemError(sead::FormatFixedSafeString<1024>(messages::archive::cPathDirectoryInvalidFormat, getFilePath().cstr()));
        return false;
    }

    if (std::snprintf(nullptr, 0, "%s/%s", archiveDir.cstr(), streamPath) < outPath->getBufferSize())
        outPath->format("%s/%s", archiveDir.cstr(), streamPath);

    return true;
}

static StreamFileState GetStreamFileState(const ByteRange& strmFile)
{
    const u32 size = static_cast<u32>(strmFile.size());

    if (!strmFile.data())
        return StreamFileState::NotLoaded;

    if (opusstream::IsOpusStream(strmFile.data(), size))
        return StreamFileState::Opus;

    if (!BfstmFile::IsComplete(strmFile.data(), size) && IsInnerFileKind(strmFile, InnerFileKind::Stream))
        return StreamFileState::Incomplete;

    return StreamFileState::Complete;
}

bool Bfsar::readStreamFileTracks_(BfsarOpenContext& ctx, Sound* sound, const ByteRange& strmFile)
{
    Sound::StreamSoundInfo& stream = sound->mStreamSoundInfo;
    nw::snd::internal::StreamSoundFileReader reader;

    if (BfstmFile::IsComplete(strmFile.data(), static_cast<u32>(strmFile.size())))
        reader.Initialize(strmFile.data());

    if (!reader.IsAvailable())
    {
        sead::FormatFixedSafeString<1024> msg(messages::stream::cFileUnreadableFormat, stream.mPath.cstr());
        PopupMgr::instance()->pushCurrentItemWarning(msg, messages::stream::cAudioUnreadableDetail);
        return false;
    }

    if (reader.IsTrackInfoAvailable() && !readEmbeddedStreamTracks(reader, stream, ctx.heap))
        stream.mTrackList.clear();

    return true;
}

void Bfsar::readStreamSoundParams_(BfsarOpenContext& ctx, Sound* sound, const SoundArchiveFile::StreamSoundInfo& strmSoundInfo)
{
    Sound::StreamSoundInfo& stream = sound->mStreamSoundInfo;

    if (stream.mTrackList.isEmpty())
        readStreamTrackInfoTable_(ctx, sound, strmSoundInfo);

    if (!isStreamSendAvailable() || strmSoundInfo.toSendValue.typeId != nw::snd::internal::ElementType_SoundArchiveFile_SendInfo)
        return;

    stream.mPitch = strmSoundInfo.pitch;

    const SoundArchiveFile::SendValue send = strmSoundInfo.GetSendValue();

    stream.mMainSend = send.mainSend;

    for (u32 i = 0; i < nw::snd::AUX_BUS_NUM; i++)
        stream.mFxSend[i] = send.fxSend[i];

    const SoundArchiveFile::StreamSoundExtension* extension = strmSoundInfo.GetStreamSoundExtension();

    stream.mEnableStreamSoundExtension = extension != nullptr;

    if (!extension)
        return;

    stream.mStreamType = static_cast<Sound::StreamSoundInfo::StreamType>(extension->GetStreamFileType());
    stream.mIsLoop = extension->IsLoop();
    stream.mStreamTypeInfoUpper = static_cast<u16>((extension->streamTypeInfo >> 16) & 0xFFFF);
    stream.mLoopStartFrame = extension->loopStartFrame;
    stream.mLoopEndFrame = extension->loopEndFrame;

    if (stream.mStreamType == Sound::StreamSoundInfo::StreamType::Adts)
        PopupMgr::instance()->pushCurrentItemWarning(messages::stream::cAdtsUnsupported);
}

void Bfsar::readStreamFileWaves_(BfsarOpenContext& ctx, Sound* sound, const ByteRange& strmFile)
{
    const auto it = ctx.streamSounds.find(sound->mStreamSoundInfo.mPath.cstr());
    const Sound* sourceSound = it != ctx.streamSounds.end() ? it->second : nullptr;

    if (!ReadStreamWaves(sound, strmFile.data(), static_cast<u32>(strmFile.size()), sourceSound) || !StreamWavesCoverLayout(sound->mStreamSoundInfo))
    {
        StreamLoopInfoBatch::Flush();
        detachStreamWaves(sound);
    }

    sound->mStreamFileSignature = BfstmFile::ComputeContentSignature(sound->mStreamSoundInfo, getVersionForBfstm(), mEndian, mFormat);
    sound->mHasStreamFileBaseline = true;
}

static u32 GetStreamSoundInfoSize(const SoundArchiveFile::StreamSoundInfo& strmSoundInfo)
{
    u32 size = 0;

    for (const Util::Reference* subItem : { &strmSoundInfo.toTrackInfoTable, &strmSoundInfo.toSendValue })
    {
        if (subItem->IsValidOffset() && subItem->offset > 0 && (size == 0 || static_cast<u32>(subItem->offset) < size))
            size = static_cast<u32>(subItem->offset);
    }

    return size;
}

void Bfsar::readStreamPrefetch_(BfsarOpenContext& ctx, Sound* sound, const SoundArchiveFile::StreamSoundInfo& strmSoundInfo)
{
    if (!ctx.archiveBytes.containsStruct(&strmSoundInfo) || GetStreamSoundInfoSize(strmSoundInfo) < sizeof(SoundArchiveFile::StreamSoundInfo))
        return;

    const u32 fileId = strmSoundInfo.prefetchFileId;

    if (fileId == nw::snd::SoundArchive::INVALID_ID)
        return;

    const ByteRange file = fileId < ctx.soundArchive.detail_GetFileCount() ? FindArchiveFile(ctx, fileId) : ByteRange();

    if (!file.data() || !BfstpFile::CheckLayout(file.data(), file.size()))
    {
        sound->mStreamSoundInfo.mHasPrefetch = isStreamPrefetchBuildable();
        PopupMgr::instance()->pushCurrentItemWarning(isStreamPrefetchBuildable() ? messages::stream::cPrefetchRemade : messages::stream::cPrefetchUnreadable);
        return;
    }

    const u8* bytes = static_cast<const u8*>(file.data());

    sound->mStreamSoundInfo.mHasPrefetch = true;
    sound->mStreamSoundInfo.mPrefetchSource.assign(bytes, bytes + file.size());
}

void Bfsar::readStreamSoundInfo_(BfsarOpenContext& ctx, Sound* sound, const SoundArchiveFile::SoundInfo& soundInfo)
{
    const SoundArchiveFile::StreamSoundInfo& strmSoundInfo = soundInfo.GetStreamSoundInfo();
    Sound::StreamSoundInfo& stream = sound->mStreamSoundInfo;

    readStreamFilePath_(ctx, sound, soundInfo.fileId);

    stream.mAllocateTrackFlags = strmSoundInfo.allocateTrackFlags;
    stream.mAllocateChannelCount = strmSoundInfo.allocateChannelCount;

    sead::FixedSafeString<cPathBufferSize> filePath;
    const bool hasArchiveDir = !stream.mPath.isEmpty() && sead::FileDeviceMgr::instance() && findStreamFilePath_(sound, &filePath);

    const LoadedFile strmFile(filePath);
    const StreamFileState fileState = GetStreamFileState(strmFile.bytes);
    bool isFileReadable = false;

    if (hasArchiveDir && fileState == StreamFileState::NotLoaded)
    {
        sead::FormatFixedSafeString<1024> msg(messages::stream::cFileMissingFormat, stream.mPath.cstr(), GetInnerFileExtension(mFormat, InnerFileKind::SoundArchive));
        PopupMgr::instance()->pushCurrentItemWarning(msg, messages::stream::cFileMissingDetail);
    }
    else if (fileState == StreamFileState::Incomplete)
    {
        sead::FormatFixedSafeString<1024> msg(messages::stream::cFileIncompleteFormat, stream.mPath.cstr());
        PopupMgr::instance()->pushCurrentItemWarning(msg, messages::stream::cFileMissingDetail);
    }
    else if (fileState == StreamFileState::Complete)
    {
        isFileReadable = readStreamFileTracks_(ctx, sound, strmFile.bytes);
    }

    if (isStreamTrackInfoAvailable())
        readStreamSoundParams_(ctx, sound, strmSoundInfo);

    if (isStreamSendAvailable() && mStreamSoundInfoSize == 0)
        mStreamSoundInfoSize = GetStreamSoundInfoSize(strmSoundInfo);

    if (isStreamSendAvailable())
        readStreamPrefetch_(ctx, sound, strmSoundInfo);

    if (isFileReadable)
        readStreamFileWaves_(ctx, sound, strmFile.bytes);

    if (fileState != StreamFileState::NotLoaded && fileState != StreamFileState::Incomplete && stream.mTrackList.isEmpty())
        PopupMgr::instance()->pushCurrentItemWarning(messages::stream::cNoTracks);

    if (!stream.mPath.isEmpty())
        ctx.streamSounds.insert({ stream.mPath.cstr(), sound });
}

void Bfsar::attachWaveSoundWave_(Sound* sound, const nw::snd::internal::WaveSoundFileReader& reader, u32 index)
{
    nw::snd::internal::WaveSoundNoteInfo noteInfo;

    if (!reader.ReadNoteInfo(&noteInfo, index, 0))
    {
        PopupMgr::instance()->pushCurrentItemError(messages::archive::cWaveFileLoadFailed);
        return;
    }

    if (noteInfo.waveArchiveId != 0)
    {
        PopupMgr::instance()->pushCurrentItemError(messages::archive::cWaveFileLinkFailed);
        return;
    }

    WaveFile* waveFile = static_cast<WaveFile*>(getItem(noteInfo.waveIndex, getWaveFileList()));

    sound->mWaveSoundInfo.mWaveFileRef.attach(waveFile);

    if (!sound->mWaveSoundInfo.mWaveFileRef.isAttached())
        PopupMgr::instance()->pushCurrentItemError(messages::archive::cWaveFileLoadFailed);
    else if (waveFile->mName == "Wave")
        FormatCut(&waveFile->mName, "GUESS_%s", sound->mName.cstr());
}

void Bfsar::readWaveSoundParams_(Sound* sound, const nw::snd::internal::WaveSoundFileReader& reader, u32 index)
{
    const WaveSoundFile::WaveSoundInfo& info = reader.GetWaveSoundInfo(index);
    Sound::WaveSoundInfo& wave = sound->mWaveSoundInfo;

    wave.mEnablePan = info.optionParameter.GetTrueCount(nw::snd::internal::WAVE_SOUND_INFO_PAN) != 0;

    if (wave.mEnablePan)
    {
        wave.mPan = info.GetPan();
        wave.mSurroundPan = info.GetSurroundPan();
    }

    wave.mEnablePitch = info.optionParameter.GetTrueCount(nw::snd::internal::WAVE_SOUND_INFO_PITCH) != 0;

    if (wave.mEnablePitch)
        wave.mPitch = info.GetPitch();

    wave.mEnableSend = info.optionParameter.GetTrueCount(nw::snd::internal::WAVE_SOUND_INFO_SEND) != 0;

    if (wave.mEnableSend)
    {
        info.GetSendValue(&wave.mMainSend, wave.mFxSend, nw::snd::AUX_BUS_NUM);

        u32 sendOffset = 0;
        info.optionParameter.GetValue(&sendOffset, nw::snd::internal::WAVE_SOUND_INFO_SEND);

        const nw::snd::internal::SendValueWsd& sendValue = *static_cast<const nw::snd::internal::SendValueWsd*>(sead::PtrUtil::addOffset(&info, sendOffset));
        wave.mOriginalFxSendCount = sendValue.fxSend.count;
    }

    wave.mEnableEnvelope = info.optionParameter.GetTrueCount(nw::snd::internal::WAVE_SOUND_INFO_ENVELOPE) != 0;

    if (wave.mEnableEnvelope)
    {
        const nw::snd::AdshrCurve& curve = info.GetAdshrCurve();

        wave.mAdshrCurve.attack = curve.attack;
        wave.mAdshrCurve.decay = curve.decay;
        wave.mAdshrCurve.sustain = curve.sustain;
        wave.mAdshrCurve.hold = curve.hold;
        wave.mAdshrCurve.release = curve.release;
    }

    u32 wsdVersion = reader.mHeader->header.version;
    const InnerFileFormatInfo* wsdFormat = FindInnerFileFormat(reader.mHeader->header.signature, InnerFileKind::WaveSoundData);

    if (wsdFormat && wsdFormat->format == ArchiveFormat::BCSAR)
    {
        const u32 major = (wsdVersion >> 24) & 0xFF;
        const u32 minor = (wsdVersion >> 16) & 0xFF;

        wsdVersion = (major << 16) | (minor << 8);
    }

    const bool isFilterModeled = BfwsdFile::isFilterSupportedVersion(wsdVersion, mFormat);

    if (isFilterModeled)
    {
        wave.mEnableFilter = info.optionParameter.GetTrueCount(nw::snd::internal::WAVE_SOUND_INFO_FILTER) != 0;

        if (wave.mEnableFilter)
        {
            wave.mLpfFreq = info.GetLpfFreq();
            wave.mBiquadType = info.GetBiquadType();
            wave.mBiquadValue = info.GetBiquadValue();
        }
    }

    auto isModeledBit = [isFilterModeled](u32 bit)
    {
        switch (bit)
        {
            case nw::snd::internal::WAVE_SOUND_INFO_PAN:
            case nw::snd::internal::WAVE_SOUND_INFO_PITCH:
            case nw::snd::internal::WAVE_SOUND_INFO_SEND:
            case nw::snd::internal::WAVE_SOUND_INFO_ENVELOPE:
                return true;

            case nw::snd::internal::WAVE_SOUND_INFO_FILTER:
                return isFilterModeled;

            default:
                return false;
        }
    };

    CollectUnmodeledOptionBits(info.optionParameter, isModeledBit, wave.mExtraOptions);
}

void Bfsar::readWaveSoundInfo_(BfsarOpenContext& ctx, Sound* sound, const SoundArchiveFile::SoundInfo& soundInfo)
{
    const SoundArchiveFile::WaveSoundInfo& waveSoundInfo = soundInfo.GetWaveSoundInfo();
    const char* wsdFormatName = GetInnerFileDisplayName(mFormat, InnerFileKind::WaveSoundData);
    Sound::WaveSoundInfo& wave = sound->mWaveSoundInfo;

    wave.mAllocateTrackCount = waveSoundInfo.allocateTrackCount;

    wave.mEnablePriority = waveSoundInfo.optionParameter.GetTrueCount(nw::snd::internal::WAVE_SOUND_INFO_PRIORITY) != 0;

    if (wave.mEnablePriority)
    {
        wave.mChannelPriority = waveSoundInfo.GetChannelPriority();
        wave.mIsReleasePriorityFix = waveSoundInfo.GetIsReleasePriorityFix();
    }

    const ByteRange wsdFile = FindArchiveFile(ctx, soundInfo.fileId);

    if (!wsdFile.data())
    {
        if (mFormat != ArchiveFormat::BCSAR)
            PopupMgr::instance()->pushCurrentItemError(sead::FormatFixedSafeString<64>(messages::archive::cWaveSoundDataLoadFailedFormat, wsdFormatName));

        return;
    }

    const nw::snd::internal::WaveSoundFileReader reader(FindWaveSoundWaveIdTable(wsdFile) ? wsdFile.data() : nullptr);

    if (!reader.IsAvailable() || !CheckWaveSoundEntry(wsdFile, *reader.mInfoBlockBody, waveSoundInfo.index))
    {
        PopupMgr::instance()->pushCurrentItemError(sead::FormatFixedSafeString<64>(messages::archive::cWaveSoundDataInvalidFormat, wsdFormatName));
        return;
    }

    attachWaveSoundWave_(sound, reader, waveSoundInfo.index);
    readWaveSoundParams_(sound, reader, waveSoundInfo.index);
}

static bool IsModeledSoundOption(u32 bit)
{
    if (bit == nw::snd::internal::SOUND_INFO_STRING_ID ||
        bit == nw::snd::internal::SOUND_INFO_PAN_PARAM ||
        bit == nw::snd::internal::SOUND_INFO_PLAYER_PARAM ||
        bit == nw::snd::internal::SOUND_INFO_OFFSET_TO_3D_PARAM ||
        bit == nw::snd::internal::SOUND_INFO_OFFSET_TO_CTR_PARAM)
        return true;

    for (s32 userParamBit : nw::snd::internal::USER_PARAM_INDEX)
    {
        if (bit == static_cast<u32>(userParamBit))
            return true;
    }

    return false;
}

void Bfsar::readSoundInfo_(BfsarOpenContext& ctx, Sound* sound, const SoundArchiveFile::SoundInfo& soundInfo)
{
    const Util::BitFlag& options = soundInfo.GetOptionParameter();

    sound->mEnableName = options.GetTrueCount(nw::snd::internal::SOUND_INFO_STRING_ID) != 0;
    readItemName_(ctx, sound, soundInfo.GetStringId(), "SOUND_%u");

    sound->mPlayerRef.attach(getItem(soundInfo.playerId, getPlayerList()));

    if (!sound->mPlayerRef.isAttached())
        PopupMgr::instance()->pushCurrentItemError(messages::archive::cInvalidPlayerReference);

    sound->mVolume = soundInfo.volume;
    sound->mRemoteFilter = soundInfo.remoteFilter;
    sound->mSoundType = static_cast<Sound::SoundType>(soundInfo.GetSoundType());

    if (SoundArchiveFile::UseV3SoundInfoLayout())
    {
        const u8* nameField = reinterpret_cast<const u8*>(&soundInfo) + 0x0C;
        sound->mV3NameField.assign(nameField, nameField + 0x20);
    }

    sound->mEnablePanParam = options.GetTrueCount(nw::snd::internal::SOUND_INFO_PAN_PARAM) != 0;

    if (sound->mEnablePanParam)
    {
        sound->mPanMode = static_cast<snd::PanMode>(soundInfo.GetPanMode());
        sound->mPanCurve = static_cast<snd::PanCurve>(soundInfo.GetPanCurve());
    }

    sound->mEnablePlayerParam = options.GetTrueCount(nw::snd::internal::SOUND_INFO_PLAYER_PARAM) != 0;

    if (sound->mEnablePlayerParam)
    {
        sound->mPlayerPriority = soundInfo.GetPlayerPriority();
        sound->mActorPlayerId = soundInfo.GetActorPlayerId();
    }

    for (s32 i = 0; i < nw::snd::internal::USER_PARAM_COUNT; i++)
    {
        sound->mEnableUserParam[i] = options.GetTrueCount(nw::snd::internal::USER_PARAM_INDEX[i]) != 0;

        if (sound->mEnableUserParam[i])
            soundInfo.ReadUserParam(i, sound->mUserParam[i]);
    }

    sound->mEnableIsFrontBypass = options.GetTrueCount(nw::snd::internal::SOUND_INFO_OFFSET_TO_CTR_PARAM) != 0;

    if (sound->mEnableIsFrontBypass)
        sound->mIsFrontBypass = soundInfo.IsFrontBypass();

    const SoundArchiveFile::Sound3DInfo* sound3DInfo = soundInfo.GetSound3DInfo();

    sound->mEnableSound3DInfo = sound3DInfo != nullptr;

    if (sound3DInfo)
    {
        sound->mSound3DInfo.mFlags = sound3DInfo->flags;
        sound->mSound3DInfo.mDecayRatio = sound3DInfo->decayRatio;
        sound->mSound3DInfo.mDecayCurve = sound3DInfo->decayCurve;
        sound->mSound3DInfo.mDopplerFactor = sound3DInfo->dopplerFactor;
    }

    CollectUnmodeledOptionBits(options, IsModeledSoundOption, sound->mExtraSoundInfoOptions);
}

bool Bfsar::readSounds_(BfsarOpenContext& ctx)
{
    loadprogress::BeginStage(loadprogress::Stage::Sounds);

    ctx.loopInfoBatch.emplace();

    for (u32 i = 0; i < ctx.soundArchive.GetSoundCount(); i++)
    {
        const SoundArchiveFile::SoundInfo* soundInfo = ctx.soundArchive.GetSoundInfo(ctx.soundArchive.GetSoundIdFromIndex(i));

        if (!soundInfo)
        {
            PopupMgr::instance()->setCorruptInfo(messages::archive::cInvalidSoundInfo);
            return false;
        }

        if (soundInfo->GetSoundType() == nw::snd::SoundArchive::SOUND_TYPE_STRM)
            loadprogress::Step();
        else
            loadprogress::Update();

        Sound* sound = new(ctx.heap) Sound();
        sound->mId = i;
        PopupMgr::instance()->setCurrentProcessItem(sound);

        readSoundInfo_(ctx, sound, *soundInfo);

        if (sound->mSoundType == Sound::SoundType::Seq)
            readSequenceSoundInfo_(ctx, sound, *soundInfo);
        else if (sound->mSoundType == Sound::SoundType::Strm)
            readStreamSoundInfo_(ctx, sound, *soundInfo);
        else if (sound->mSoundType == Sound::SoundType::Wave)
            readWaveSoundInfo_(ctx, sound, *soundInfo);

        mSoundList.pushBack(sound);
    }

    StreamLoopInfoBatch::Flush();

    return true;
}

void Bfsar::guessBankWaveFileNames_()
{
    for (Item* bankItem : mBankList)
    {
        Bank* bank = static_cast<Bank*>(bankItem);

        if (!bank->mFileRef.isAttached())
            continue;

        BankFile* bankFile = static_cast<BankFile*>(bank->mFileRef.getItem());

        for (Item* instrItem : bankFile->getInstrumentList())
        {
            BankFile::Instrument& instr = *static_cast<BankFile::Instrument*>(instrItem);

            for (Item* keyRegionItem : instr.getKeyRegionList())
            {
                BankFile::KeyRegion& keyRegion = *static_cast<BankFile::KeyRegion*>(keyRegionItem);

                for (Item* velRegionItem : keyRegion.getVelocityRegionList())
                {
                    BankFile::VelocityRegion& velRegion = *static_cast<BankFile::VelocityRegion*>(velRegionItem);

                    if (!velRegion.getWaveFileRef().isAttached())
                        continue;

                    Item* waveFile = velRegion.getWaveFileRef().getItem();

                    if (waveFile->mName == "Wave")
                        FormatCut(&waveFile->mName, "GUESS_%s_INSTR_%u", bank->mName.cstr(), instr.mId);
                }
            }
        }
    }
}

bool Bfsar::readSoundSets_(BfsarOpenContext& ctx)
{
    loadprogress::BeginStage(loadprogress::Stage::SoundSets);

    for (u32 i = 0; i < ctx.soundArchive.GetSoundGroupCount(); i++)
    {
        loadprogress::Step();

        const SoundArchiveFile::SoundGroupInfo* soundSetInfo = ctx.soundArchive.detail_GetSoundGroupInfo(ctx.soundArchive.GetSoundGroupIdFromIndex(i));

        if (!soundSetInfo)
        {
            PopupMgr::instance()->setCorruptInfo(messages::archive::cInvalidSoundGroupInfo);
            return false;
        }

        const SoundArchiveFile::WaveSoundGroupInfo* waveSoundSetInfo = soundSetInfo->GetWaveSoundGroupInfo();

        SoundSet* soundSet = new(ctx.heap) SoundSet();
        soundSet->mId = i;
        PopupMgr::instance()->setCurrentProcessItem(soundSet);

        soundSet->mEnableName = soundSetInfo->optionParameter.GetTrueCount(nw::snd::internal::SOUND_GROUP_INFO_STRING_ID) != 0;
        readItemName_(ctx, soundSet, soundSetInfo->GetStringId(), waveSoundSetInfo ? "WSDSET_%u" : "SEQSET_%u");

        soundSet->mSoundSetType = waveSoundSetInfo ? SoundSet::SoundSetType::Wave : SoundSet::SoundSetType::Seq;
        soundSet->mWaveArchiveType = WaveArchiveType::AutomaticShared;

        if (waveSoundSetInfo)
            soundSet->mWaveArchiveType = readWaveArchiveRef_(*waveSoundSetInfo->GetWaveArchiveItemIdTable(), &soundSet->mWaveArchiveRef);

        if (soundSetInfo->startId == nw::snd::SoundArchive::INVALID_ID && soundSetInfo->endId == nw::snd::SoundArchive::INVALID_ID)
        {
            soundSet->mIsEmpty = true;
        }
        else
        {
            soundSet->mStartId = Util::GetItemIndex(soundSetInfo->startId);
            soundSet->mEndId = Util::GetItemIndex(soundSetInfo->endId);
        }

        mSoundSetList.pushBack(soundSet);
    }

    return true;
}

static void LogItemList(const char* label, const Item::List& list)
{
    for (const Item* item : list)
        LOG_FMT("  %s[%d]: \"%s\"", label, item->getId(), item->getNameOrNull().cstr());
}

void Bfsar::logLoadedItems_() const
{
    LOG_FMT("=== Archive loaded: %d groups, %d players, %d banks, %d sounds, %d waveArchives, %d waveFiles",
        mGroupList.size(), mPlayerList.size(), mBankList.size(), mSoundList.size(), mWaveArchiveList.size(), mWaveFileList.size());

    LogItemList("Group", mGroupList);
    LogItemList("Player", mPlayerList);
    LogItemList("Bank", mBankList);
    LogItemList("BankFile", mBankFileList);
    LogItemList("Sound", mSoundList);
    LogItemList("SoundSet", mSoundSetList);
    LogItemList("SeqFile", mSequenceFileList);
    LogItemList("WaveArchive", mWaveArchiveList);
    LogItemList("WaveFile", mWaveFileList);
}

void Bfsar::recordOriginalFileIds_(const BfsarOpenContext& ctx)
{
    mItemOrigFileIds.clear();

    const u32 origFileCount = ctx.soundArchive.detail_GetFileCount();

    auto addOrig = [&](const Item::ListNode* node, u32 fileId)
    {
        if (node && fileId < origFileCount)
            mItemOrigFileIds.emplace_back(node->val(), fileId);
    };

    for (const auto& [origFileId, seqIdx] : ctx.seqFileIdxMap)
        addOrig(mSequenceFileList.nth(seqIdx), origFileId);

    for (u32 i = 0; i < ctx.soundArchive.GetSoundGroupCount(); i++)
    {
        const SoundArchiveFile::SoundGroupInfo* soundSetInfo = ctx.soundArchive.detail_GetSoundGroupInfo(ctx.soundArchive.GetSoundGroupIdFromIndex(i));

        if (!soundSetInfo || !soundSetInfo->GetWaveSoundGroupInfo())
            continue;

        const Util::Table<nw::ut::ResU32>* fileIdTable = soundSetInfo->GetFileIdTable();

        if (fileIdTable->count > 0)
            addOrig(getItem(i, mSoundSetList), fileIdTable->item[0]);
    }

    for (u32 i = 0; i < ctx.soundArchive.GetBankCount(); i++)
    {
        const SoundArchiveFile::BankInfo* bankInfo = ctx.soundArchive.GetBankInfo(ctx.soundArchive.GetBankIdFromIndex(i));

        if (bankInfo)
            addOrig(getItem(i, mBankList), bankInfo->fileId);
    }

    for (u32 i = 0; i < ctx.soundArchive.GetWaveArchiveCount(); i++)
    {
        const u32 warcId = ctx.soundArchive.GetWaveArchiveIdFromIndex(i);
        const SoundArchiveFile::WaveArchiveInfo* warcInfo = ctx.soundArchive.GetWaveArchiveInfo(warcId);

        if (warcInfo)
            addOrig(getItem(warcId, getWaveArchiveList()), warcInfo->fileId);
    }

    for (u32 i = 0; i < ctx.soundArchive.GetGroupCount(); i++)
    {
        const SoundArchiveFile::GroupInfo* groupInfo = ctx.soundArchive.GetGroupInfo(ctx.soundArchive.GetGroupIdFromIndex(i));

        if (groupInfo)
            addOrig(getItem(i, mGroupList), groupInfo->fileId);
    }
}
