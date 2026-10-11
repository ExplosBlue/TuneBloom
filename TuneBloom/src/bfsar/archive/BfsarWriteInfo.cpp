#include <bfsar/archive/Bfsar.h>

#include <bfsar/io/FlagParameters.h>
#include <bfsar/archive/BfsarSaveContext.h>
#include <bfsar/files/BfstmFile.h>

#include <snd/snd_WaveSoundFile.h>

void Bfsar::writeSequenceSoundInfo_(BfsarSaveContext& ctx, const Sound* sound)
{
    ctx.writer.closeReference("DetailSoundInfo", nw::snd::internal::ElementType_SoundArchiveFile_SequenceSoundInfo);

    const Sound::SequenceSoundInfo& seqInfo = sound->getSequenceSoundInfo();

    ctx.writer.openReference("BankTable");
    ctx.stream.writeU32(seqInfo.getAllocateTrackFlags());

    std::unordered_map<u32, u32> flags;
    if (seqInfo.isEnableStartOffset())
    {
        flags[nw::snd::internal::SEQ_SOUND_INFO_START_OFFSET] = seqInfo.getStartOffset();
    }

    if (seqInfo.isEnablePriority())
    {
        u32 flag = seqInfo.getChannelPriority() | (seqInfo.getIsReleasePriorityFix() << 8);
        flags[nw::snd::internal::SEQ_SOUND_INFO_PRIORITY] = flag;
    }

    FlagParameters params(flags);
    params.write(ctx.writer);

    ctx.writer.closeReference("BankTable", nw::snd::internal::ElementType_Table_EmbeddingTable);

    u32 numBanks = nw::snd::SoundArchive::SEQ_BANK_MAX;
    for (s32 i = numBanks - 1; i >= 0; i--)
    {
        if (seqInfo.getBankId(i) == nw::snd::SoundArchive::INVALID_ID)
            numBanks--;
        else
            break;
    }

    ctx.stream.writeU32(numBanks);

    for (u32 i = 0; i < numBanks; i++)
    {
        ctx.stream.writeU32(nw::snd::internal::Util::GetMaskedItemId(seqInfo.getBankId(i), nw::snd::internal::ItemType_Bank));
    }
}

void Bfsar::writeStreamTrackInfoTable_(BfsarSaveContext& ctx, const Sound::StreamSoundInfo& strmInfo)
{
    ctx.writer.pushOffsetBase();
    {
        ctx.writer.openReferenceTable("TrackInfoTable", strmInfo.getTrackList().size());

        u32 channelIdxStart = 0;
        for (u32 i = 0; i < strmInfo.getTrackList().size(); i++)
        {
            const Item* trackItem = strmInfo.getTrackList().nth(i)->val();
            const Sound::StreamSoundInfo::Track& track = *static_cast<const Sound::StreamSoundInfo::Track*>(trackItem);

            ctx.writer.addReferenceTableReference("TrackInfoTable", nw::snd::internal::ElementType_SoundArchiveFile_StreamSoundTrackInfo);

            ctx.writer.pushOffsetBase();
            {
                ctx.stream.writeU8(track.getVolume());
                ctx.stream.writeU8(track.getPan());
                ctx.stream.writeU8(track.getSPan());
                ctx.stream.writeU8(track.getFlags());

                ctx.writer.openReference("GlobalChannelIndexTable");

                if (isStreamSendAvailable())
                {
                    ctx.writer.openReference("TrackSendValue");
                }

                if (isFilterSupportedVersion())
                {
                    ctx.stream.writeU8(track.getLpfFreq());
                    ctx.stream.writeU8(track.getBiquadType());
                    ctx.stream.writeU8(track.getBiquadValue());
                    ctx.stream.writeU8(0); // Padding
                }

                ctx.writer.closeReference("GlobalChannelIndexTable", nw::snd::internal::ElementType_Table_EmbeddingTable);
                ctx.stream.writeU32(track.getChannelCount(strmInfo.getStreamType()));

                if (strmInfo.getStreamType() == Sound::StreamSoundInfo::StreamType::NwStreamBinary && track.getWaveFileRef().isAttached())
                {
                    for (u32 j = 0; j < track.getChannelCount(); j++)
                    {
                        ctx.stream.writeU8(channelIdxStart + j);
                    }
                }
                else
                {
                    for (u32 j = 0; j < track.getChannelCount(strmInfo.getStreamType()); j++)
                    {
                        ctx.stream.writeU8(*track.getChannels_().nth(j));
                    }
                }

                ctx.writer.align(0x4);

                if (isStreamSendAvailable())
                {
                    ctx.writer.closeReference("TrackSendValue", nw::snd::internal::ElementType_SoundArchiveFile_SendInfo);

                    ctx.stream.writeU8(track.getMainSend());

                    for (u32 j = 0; j < nw::snd::AUX_BUS_NUM; j++)
                    {
                        ctx.stream.writeU8(track.getFxSend(j));
                    }

                    ctx.writer.align(0x4);
                    ctx.stream.writeU32(0); //! Unknown
                }
            }
            ctx.writer.popOffsetBase();

            channelIdxStart += track.getChannelCount();
        }

        ctx.writer.closeReferenceTable("TrackInfoTable");
    }
    ctx.writer.popOffsetBase();
}

struct StreamExtensionLoop
{
    bool isLoop;
    u32 startFrame;
    u32 endFrame;
};

static const WaveFile* GetOpusMainWave(const Sound::StreamSoundInfo& strmInfo)
{
    if (strmInfo.getStreamType() != Sound::StreamSoundInfo::StreamType::Opus || strmInfo.getTrackList().isEmpty())
        return nullptr;

    const Sound::StreamSoundInfo::Track& mainTrack = *static_cast<const Sound::StreamSoundInfo::Track*>(strmInfo.getTrackList().front()->val());

    return static_cast<const WaveFile*>(mainTrack.getWaveFileRef().getItem());
}

static StreamExtensionLoop GetStreamExtensionLoop(const Sound::StreamSoundInfo& strmInfo)
{
    const WaveFile* mainWave = GetOpusMainWave(strmInfo);

    if (!mainWave)
        return {strmInfo.getIsLoop(), strmInfo.getLoopStartFrame(), strmInfo.getLoopEndFrame()};

    if (mainWave->getIsLoop())
        return {true, mainWave->getLoopStartFrame(false), mainWave->getLoopEndFrame(false)};

    return {false, 0, mainWave->getSampleCount()};
}

bool Bfsar::isStreamSoundExtensionWritten_(const Sound::StreamSoundInfo& strmInfo) const
{
    if (strmInfo.getStreamType() == Sound::StreamSoundInfo::StreamType::NwStreamBinary && BfstmFile::IsTrackInfoAvailable(getVersionForBfstm(), mFormat))
        return false;

    return isStreamTrackInfoAvailable() && isStreamSendAvailable() && hasStreamSoundExtensionRef() && strmInfo.isEnableStreamSoundExtension();
}

void Bfsar::syncOpusStreamLoops_()
{
    for (Item* item : mSoundList)
    {
        Sound* sound = static_cast<Sound*>(item);

        if (sound->getSoundType() != Sound::SoundType::Strm)
            continue;

        Sound::StreamSoundInfo& strmInfo = sound->getStreamSoundInfo();

        if (!GetOpusMainWave(strmInfo) || !isStreamSoundExtensionWritten_(strmInfo))
            continue;

        const StreamExtensionLoop loop = GetStreamExtensionLoop(strmInfo);

        strmInfo.setIsLoop(loop.isLoop);
        strmInfo.setLoopStartFrame(loop.startFrame);
        strmInfo.setLoopEndFrame(loop.endFrame);
    }
}

void Bfsar::writeStreamSoundExtension_(BfsarSaveContext& ctx, const Sound::StreamSoundInfo& strmInfo)
{
    ctx.writer.closeReference("StreamSoundExtension", nw::snd::internal::ElementType_SoundArchiveFile_StreamSoundExtensionInfo);

    const StreamExtensionLoop loop = GetStreamExtensionLoop(strmInfo);

    u32 streamTypeInfo = strmInfo.getStreamType() | (loop.isLoop << 8) | (strmInfo.getStreamTypeInfoUpper() << 16);

    ctx.stream.writeU32(streamTypeInfo);
    ctx.stream.writeU32(loop.startFrame);
    ctx.stream.writeU32(loop.endFrame);
}

void Bfsar::writeStreamSoundInfo_(BfsarSaveContext& ctx, const Sound* sound)
{
    ctx.writer.closeReference("DetailSoundInfo", nw::snd::internal::ElementType_SoundArchiveFile_StreamSoundInfo);

    const Sound::StreamSoundInfo& strmInfo = sound->getStreamSoundInfo();

    ctx.stream.writeU16(strmInfo.getAllocateTrackFlags());
    ctx.stream.writeU16(strmInfo.getAllocateChannelCount());

    if (strmInfo.getStreamType() == Sound::StreamSoundInfo::StreamType::NwStreamBinary && BfstmFile::IsTrackInfoAvailable(getVersionForBfstm(), mFormat))
    {
        ctx.stream.writeU32(0);
    }
    else if (isStreamTrackInfoAvailable())
    {
        ctx.writer.openReference("TrackInfoTableRef");

        if (isStreamSendAvailable())
        {
            ctx.stream.writeF32(strmInfo.getPitch());

            ctx.writer.openReference("SendValue");

            if (hasStreamSoundExtensionRef())
                ctx.writer.openReference("StreamSoundExtension");

            if (hasStreamPrefetchFileId())
                ctx.stream.writeU32(ctx.getPrefetchFileId(sound));
        }
        else
        {
            ctx.stream.writeU32(0); //! Unknown
        }

        if (strmInfo.getTrackList().size() > 0)
        {
            ctx.writer.closeReference("TrackInfoTableRef", nw::snd::internal::ElementType_Table_ReferenceTable);
            writeStreamTrackInfoTable_(ctx, strmInfo);
        }
        else
        {
            ctx.writer.closeNullReference("TrackInfoTableRef");
        }

        if (isStreamSendAvailable())
        {
            ctx.writer.closeReference("SendValue", nw::snd::internal::ElementType_SoundArchiveFile_SendInfo);

            ctx.stream.writeU8(strmInfo.getMainSend());

            for (u32 j = 0; j < nw::snd::AUX_BUS_NUM; j++)
            {
                ctx.stream.writeU8(strmInfo.getFxSend(j));
            }

            ctx.writer.align(0x4);
            ctx.stream.writeU32(0); //! Unknown

            if (hasStreamSoundExtensionRef())
            {
                if (strmInfo.isEnableStreamSoundExtension())
                {
                    writeStreamSoundExtension_(ctx, strmInfo);
                }
                else
                {
                    ctx.writer.closeNullReference("StreamSoundExtension");
                }
            }
        }
    }
    else
    {
        ctx.stream.writeU32(0); //! Unknown if this exists
    }
}

void Bfsar::writeWaveSoundInfo_(BfsarSaveContext& ctx, const Sound* sound)
{
    ctx.writer.closeReference("DetailSoundInfo", nw::snd::internal::ElementType_SoundArchiveFile_WaveSoundInfo);

    const Sound::WaveSoundInfo& waveInfo = sound->getWaveSoundInfo();

    const auto& it = ctx.wsdIndexes.find(sound);
    SEAD_ASSERT(it != ctx.wsdIndexes.end());

    ctx.stream.writeU32(it->second);

    SEAD_ASSERT(waveInfo.getAllocateTrackCount() == 1);
    ctx.stream.writeU32(waveInfo.getAllocateTrackCount());

    std::unordered_map<u32, u32> flags;
    if (waveInfo.isEnablePriority())
    {
        u32 flag = waveInfo.getChannelPriority() | (waveInfo.getIsReleasePriorityFix() << 8);
        flags[nw::snd::internal::WAVE_SOUND_INFO_PRIORITY] = flag;
    }

    u32 sendDataOffset = 0;
    if (waveInfo.isEnableSend() && mFormat == ArchiveFormat::BCSAR && !isVersionOrLater(2, 1, 0))
    {
        sendDataOffset = 4 + 4 + 4;
        if (waveInfo.isEnablePriority())
            sendDataOffset += 4;
        sendDataOffset += 4;

        flags[nw::snd::internal::WAVE_SOUND_INFO_SEND] = sendDataOffset;
    }

    FlagParameters params(flags);
    params.write(ctx.writer);

    if (waveInfo.isEnableSend() && mFormat == ArchiveFormat::BCSAR && !isVersionOrLater(2, 1, 0))
    {
        ctx.stream.writeU32(0x027F);
        ctx.stream.writeU32(0);
    }
}

void Bfsar::writeSoundInfo_(BfsarSaveContext& ctx, const Sound* sound)
{
    ctx.writer.pushOffsetBase();
    {
        u32 pos = ctx.writer.getPosition();

        ctx.stream.writeU32(ctx.itemFileIds[sound].id);

        ctx.stream.writeU32(nw::snd::internal::Util::GetMaskedItemId(sound->getPlayerId(), nw::snd::internal::ItemType_Player));
        ctx.stream.writeU8(sound->getVolume());

        if (mFormat == ArchiveFormat::BCSAR)
            ctx.stream.writeU8(0);
        else
            ctx.stream.writeU8(sound->getRemoteFilter());

        ctx.stream.writeU16(0); // Padding1

        if (mFormat == ArchiveFormat::BFSAR && getDecodedMajor() >= 3)
        {
            const std::vector<u8> &nameField = sound->getV3NameField();

            for (u32 k = 0; k < 0x20; k++)
                ctx.stream.writeU8(k < nameField.size() ? nameField[k] : 0);
        }

        ctx.writer.openReference("DetailSoundInfo");

        {
            u32 sound3DInfoParamOffset = ctx.writer.getPosition() + 4;

            std::unordered_map<u32, u32> flags;
            if (sound->isEnableName())
            {
                flags[nw::snd::internal::SOUND_INFO_STRING_ID] = ctx.getStringId(sound->getName());

                sound3DInfoParamOffset += 4;
            }

            if (sound->isEnablePanParam())
            {
                u32 flag = (u32)sound->getPanMode() | ((u32)sound->getPanCurve() << 8);
                flags[nw::snd::internal::SOUND_INFO_PAN_PARAM] = flag;

                sound3DInfoParamOffset += 4;
            }

            if (sound->isEnablePlayerParam())
            {
                u32 flag = sound->getPlayerPriority() | (sound->getActorPlayerId() << 8);
                flags[nw::snd::internal::SOUND_INFO_PLAYER_PARAM] = flag;

                sound3DInfoParamOffset += 4;
            }

            if (sound->isEnableSound3DInfo())
            {
                flags[nw::snd::internal::SOUND_INFO_OFFSET_TO_3D_PARAM] = 0;
            }

            if (sound->isEnableIsFrontBypass() && ((mFormat == ArchiveFormat::BCSAR && !isVersionOrLater(2, 1, 0)) || sound->getSoundType() != Sound::SoundType::Strm))
            {
                flags[nw::snd::internal::SOUND_INFO_OFFSET_TO_CTR_PARAM] = sound->getIsFrontBypass();
            }

            if (sound->isEnableUserParam(0))
            {
                flags[nw::snd::internal::SOUND_INFO_USER_PARAM] = sound->getUserParam(0);
            }

            if (sound->isEnableUserParam(1))
            {
                flags[nw::snd::internal::SOUND_INFO_USER_PARAM1] = sound->getUserParam(1);
            }

            if (sound->isEnableUserParam(2))
            {
                flags[nw::snd::internal::SOUND_INFO_USER_PARAM2] = sound->getUserParam(2);
            }

            if (sound->isEnableUserParam(3))
            {
                flags[nw::snd::internal::SOUND_INFO_USER_PARAM3] = sound->getUserParam(3);
            }

            // option that tool doesnt model
            for (const std::pair<u32, u32> &opt : sound->getExtraSoundInfoOptions())
            {
                if (flags.find(opt.first) != flags.end())
                    continue;

                flags[opt.first] = opt.second;

                if (opt.first < nw::snd::internal::SOUND_INFO_OFFSET_TO_3D_PARAM)
                    sound3DInfoParamOffset += 4;
            }

            FlagParameters params(flags);
            params.write(ctx.writer);

            if (sound->isEnableSound3DInfo())
            {
                u32 sound3DInfoPos = ctx.writer.getPosition();
                ctx.writer.seek(sound3DInfoParamOffset);
                ctx.stream.writeU32(sound3DInfoPos - pos);
                ctx.writer.seek(sound3DInfoPos);

                const Sound::Sound3DInfo& sound3DInfo = sound->getSound3DInfo();

                ctx.stream.writeU32(sound3DInfo.getFlags());
                ctx.stream.writeF32(sound3DInfo.getDecayRatio());
                ctx.stream.writeU8(sound3DInfo.getDecayCurve());
                ctx.stream.writeU8(sound3DInfo.getDopplerFactor());
                ctx.stream.writeU16(0); // Padding
                ctx.stream.writeU32(0); // Option Parameter
            }
        }

        if ((mFormat == ArchiveFormat::BCSAR && !isVersionOrLater(2, 1, 0)) || sound->getSoundType() == Sound::SoundType::Wave || sound->getSoundType() == Sound::SoundType::Seq)
        {
            ctx.stream.writeU32(sound->getIsFrontBypass()); // BRUH
        }

        ctx.writer.pushOffsetBase();
        {
            switch (sound->getSoundType())
            {
                case Sound::SoundType::Seq:
                    writeSequenceSoundInfo_(ctx, sound);
                    break;

                case Sound::SoundType::Strm:
                    writeStreamSoundInfo_(ctx, sound);
                    break;

                case Sound::SoundType::Wave:
                    writeWaveSoundInfo_(ctx, sound);
                    break;

                default:
                    SEAD_ASSERT_MSG(false, "invalid SoundType");
                    break;
            }
        }
        ctx.writer.popOffsetBase();
    }
    ctx.writer.popOffsetBase();
}

void Bfsar::writeSoundInfoSection_(BfsarSaveContext& ctx)
{
    ctx.writer.closeReference("SoundInfoSection", nw::snd::internal::ElementType_SoundArchiveFile_SoundInfoSection);
    ctx.writer.pushOffsetBase();
    {
        ctx.writer.openReferenceTable("SoundInfoSectionTable", mSoundList.size());

        for (const Item* item : mSoundList)
        {
            SEAD_ASSERT(item->getItemType() == Item::ItemType::Sound);
            const Sound* sound = static_cast<const Sound*>(item);

            ctx.writer.addReferenceTableReference("SoundInfoSectionTable", nw::snd::internal::ElementType_SoundArchiveFile_SoundInfo);

            writeSoundInfo_(ctx, sound);
        }

        ctx.writer.closeReferenceTable("SoundInfoSectionTable");
    }
    ctx.writer.popOffsetBase();
}

void Bfsar::writeSoundSetInfoSection_(BfsarSaveContext& ctx)
{
    ctx.writer.closeReference("SoundGroupInfoSection", nw::snd::internal::ElementType_SoundArchiveFile_SoundGroupInfoSection);
    ctx.writer.pushOffsetBase();
    {
        ctx.writer.openReferenceTable("SoundGroupInfoSectionTable", mSoundSetList.size());

        for (const Item* item : mSoundSetList)
        {
            SEAD_ASSERT(item->getItemType() == Item::ItemType::SoundSet);
            const SoundSet* soundSet = static_cast<const SoundSet*>(item);

            ctx.writer.addReferenceTableReference("SoundGroupInfoSectionTable", nw::snd::internal::ElementType_SoundArchiveFile_SoundGroupInfo);

            ctx.writer.pushOffsetBase();
            {
                if (soundSet->getIsEmpty())
                {
                    ctx.stream.writeU32(nw::snd::SoundArchive::INVALID_ID);
                    ctx.stream.writeU32(nw::snd::SoundArchive::INVALID_ID);
                }
                else
                {
                    ctx.stream.writeU32(nw::snd::internal::Util::GetMaskedItemId(soundSet->getStartId(), nw::snd::internal::ItemType_Sound));
                    ctx.stream.writeU32(nw::snd::internal::Util::GetMaskedItemId(soundSet->getEndId(), nw::snd::internal::ItemType_Sound));
                }

                ctx.writer.openReference("FileIdTable");
                ctx.writer.openReference("DetailSoundGroup");

                std::unordered_map<u32, u32> flags;
                if (soundSet->isEnableName())
                {
                    flags[nw::snd::internal::SOUND_GROUP_INFO_STRING_ID] = ctx.getStringId(soundSet->getName());
                }

                FlagParameters params(flags);
                params.write(ctx.writer);

                ctx.writer.closeReference("FileIdTable", nw::snd::internal::ElementType_Table_EmbeddingTable);

                if (soundSet->getSoundSetType() == SoundSet::SoundSetType::Wave)
                {
                    ctx.stream.writeU32(1); // FileIdTable count
                    ctx.stream.writeU32(ctx.itemFileIds[soundSet].id); // FileIdTable[0]

                    ctx.writer.closeReference("DetailSoundGroup", nw::snd::internal::ElementType_SoundArchiveFile_WaveSoundGroupInfo);

                    ctx.writer.pushOffsetBase();
                    {
                        ctx.writer.openReference("WaveArchiveIdTable");

                        ctx.stream.writeU32(0); // Option Parameter

                        ctx.writer.closeReference("WaveArchiveIdTable", nw::snd::internal::ElementType_Table_EmbeddingTable);

                        if (soundSet->getWaveArchiveType() != WaveArchiveType::AutomaticShared)
                        {
                            const auto& warcs = ctx.waveSoundSetsWarcs[soundSet];

                            ctx.stream.writeU32(warcs.size()); // WaveArchiveIdTable count

                            for (const WaveArchive* warc : warcs)
                            {
                                ctx.stream.writeU32(nw::snd::internal::Util::GetMaskedItemId(warc->getId(), nw::snd::internal::ItemType_WaveArchive)); // WaveArchiveIdTable entries
                            }
                        }
                        else
                        {
                            ctx.stream.writeU32(0); // WaveArchiveIdTable count
                        }
                    }
                    ctx.writer.popOffsetBase();
                }
                else
                {
                    VectorSet<u32> filesIds;

                    for (const Item::ListNode* itemNode = getItem(soundSet->getStartId(), getSoundList()); itemNode && itemNode->val()->getId() <= soundSet->getEndId(); itemNode = getSoundList().next(itemNode))
                    {
                        SEAD_ASSERT(itemNode->val()->getItemType() == Item::ItemType::Sound);
                        const Sound* sound = static_cast<const Sound*>(itemNode->val());

                        if (sound->getSoundType() != Sound::SoundType::Seq)
                        {
                            continue;
                        }

                        filesIds.insert(ctx.itemFileIds[sound].id);
                    }

                    ctx.stream.writeU32(filesIds.size()); // FileIdTable count

                    for (u32 fileId : filesIds)
                    {
                        ctx.stream.writeU32(fileId); // FileIdTable entries
                    }

                    ctx.writer.closeNullReference("DetailSoundGroup");
                }
            }
            ctx.writer.popOffsetBase();
        }

        ctx.writer.closeReferenceTable("SoundGroupInfoSectionTable");
    }
    ctx.writer.popOffsetBase();
}

void Bfsar::writeBankInfoSection_(BfsarSaveContext& ctx)
{
    ctx.writer.closeReference("BankInfoSection", nw::snd::internal::ElementType_SoundArchiveFile_BankInfoSection);
    ctx.writer.pushOffsetBase();
    {
        ctx.writer.openReferenceTable("BankInfoSectionTable", mBankList.size());

        for (const Item* item : mBankList)
        {
            SEAD_ASSERT(item->getItemType() == Item::ItemType::Bank);
            const Bank* bank = static_cast<const Bank*>(item);

            ctx.writer.addReferenceTableReference("BankInfoSectionTable", nw::snd::internal::ElementType_SoundArchiveFile_BankInfo);

            ctx.writer.pushOffsetBase();
            {
                ctx.stream.writeU32(ctx.itemFileIds[bank].id);

                ctx.writer.openReference("WaveArchiveIdTable");

                std::unordered_map<u32, u32> flags;
                if (bank->isEnableName())
                {
                    flags[nw::snd::internal::BANK_INFO_STRING_ID] = ctx.getStringId(bank->getName());
                }

                FlagParameters params(flags);
                params.write(ctx.writer);

                ctx.writer.closeReference("WaveArchiveIdTable", nw::snd::internal::ElementType_Table_EmbeddingTable);

                if (bank->getWaveArchiveType() != WaveArchiveType::AutomaticShared)
                {
                    const auto& warcs = ctx.banksWarcs[bank];

                    ctx.stream.writeU32(warcs.size()); // WaveArchiveIdTable count

                    for (const WaveArchive* warc : warcs)
                    {
                        ctx.stream.writeU32(nw::snd::internal::Util::GetMaskedItemId(warc->getId(), nw::snd::internal::ItemType_WaveArchive)); // WaveArchiveIdTable entries
                    }
                }
                else
                {
                    ctx.stream.writeU32(0); // WaveArchiveIdTable count
                }
            }
            ctx.writer.popOffsetBase();
        }

        ctx.writer.closeReferenceTable("BankInfoSectionTable");
    }
    ctx.writer.popOffsetBase();
}

void Bfsar::writeWaveArchiveInfoSection_(BfsarSaveContext& ctx)
{
    ctx.writer.closeReference("WaveArchiveInfoSection", nw::snd::internal::ElementType_SoundArchiveFile_WaveArchiveInfoSection);
    ctx.writer.pushOffsetBase();
    {
        ctx.writer.openReferenceTable("WaveArchiveInfoSectionTable", mWaveArchiveList.size());

        for (const Item* item : mWaveArchiveList)
        {
            SEAD_ASSERT(item->getItemType() == Item::ItemType::WaveArchive);
            const WaveArchive* warc = static_cast<const WaveArchive*>(item);

            ctx.writer.addReferenceTableReference("WaveArchiveInfoSectionTable", nw::snd::internal::ElementType_SoundArchiveFile_WaveArchiveInfo);

            ctx.stream.writeU32(ctx.itemFileIds[warc].id);
            ctx.stream.writeU8(warc->getIsLoadIndividual());
            ctx.stream.writeU8(0); // Padding1
            ctx.stream.writeU16(0); // Padding2

            std::unordered_map<u32, u32> flags;
            if (warc->isEnableName())
            {
                flags[nw::snd::internal::WAVE_ARCHIVE_INFO_STRING_ID] = ctx.getStringId(warc->getName());
            }

            if (warc->getIsLoadIndividual())
            {
                u32 waveCount = ctx.warcWaveFiles[warc].size();

                flags[nw::snd::internal::WAVE_ARCHIVE_INFO_WAVE_COUNT] = waveCount;
            }

            FlagParameters params(flags);
            params.write(ctx.writer);
        }

        ctx.writer.closeReferenceTable("WaveArchiveInfoSectionTable");
    }
    ctx.writer.popOffsetBase();
}

void Bfsar::writeGroupInfoSection_(BfsarSaveContext& ctx)
{
    ctx.writer.closeReference("GroupInfoSection", nw::snd::internal::ElementType_SoundArchiveFile_GroupInfoSection);
    ctx.writer.pushOffsetBase();
    {
        ctx.writer.openReferenceTable("GroupInfoSectionTable", mGroupList.size());

        for (const Item* item : mGroupList)
        {
            SEAD_ASSERT(item->getItemType() == Item::ItemType::Group);
            const Group* group = static_cast<const Group*>(item);

            ctx.writer.addReferenceTableReference("GroupInfoSectionTable", nw::snd::internal::ElementType_SoundArchiveFile_GroupInfo);

            if (group->getOutputType() == Group::OutputType::External && !(group->hasGroupInfoFileId() && ctx.itemFileIds.contains(group)))
                ctx.stream.writeU32(nw::snd::SoundArchive::INVALID_ID);
            else
                ctx.stream.writeU32(ctx.itemFileIds[group].id);

            std::unordered_map<u32, u32> flags;
            if (group->isEnableName())
            {
                flags[nw::snd::internal::GROUP_INFO_STRING_ID] = ctx.getStringId(group->getName());
            }

            FlagParameters params(flags);
            params.write(ctx.writer);
        }

        ctx.writer.closeReferenceTable("GroupInfoSectionTable");
    }
    ctx.writer.popOffsetBase();
}

void Bfsar::writePlayerInfoSection_(BfsarSaveContext& ctx)
{
    ctx.writer.closeReference("PlayerInfoSection", nw::snd::internal::ElementType_SoundArchiveFile_PlayerInfoSection);
    ctx.writer.pushOffsetBase();
    {
        ctx.writer.openReferenceTable("PlayerInfoSectionTable", mPlayerList.size());

        for (const Item* item : mPlayerList)
        {
            SEAD_ASSERT(item->getItemType() == Item::ItemType::Player);
            const Player* player = static_cast<const Player*>(item);

            ctx.writer.addReferenceTableReference("PlayerInfoSectionTable", nw::snd::internal::ElementType_SoundArchiveFile_PlayerInfo);

            ctx.stream.writeU32(player->getPlayableSoundMax());

            std::unordered_map<u32, u32> flags;
            if (player->isEnableName())
            {
                flags[nw::snd::internal::PLAYER_INFO_STRING_ID] = ctx.getStringId(player->getName());
            }

            if (player->isEnablePlayerHeapSize())
            {
                flags[nw::snd::internal::PLAYER_INFO_HEAP_SIZE] = player->getPlayerHeapSize();
            }

            FlagParameters params(flags);
            params.write(ctx.writer);
        }

        ctx.writer.closeReferenceTable("PlayerInfoSectionTable");
    }
    ctx.writer.popOffsetBase();
}

static std::vector<u32> GetPrefetchFileGroupIds(const BfsarSaveContext& ctx, const File& file)
{
    std::vector<u32> groupIds;

    const auto sound = ctx.filesItems.find(file.id);
    SEAD_ASSERT(sound != ctx.filesItems.end());

    const auto groups = ctx.itemAttachedGroups.find(sound->second.front());

    if (groups == ctx.itemAttachedGroups.end())
        return groupIds;

    for (const Group* group : groups->second)
        groupIds.push_back(nw::snd::internal::Util::GetMaskedItemId(group->getId(), nw::snd::internal::ItemType_Group));

    return groupIds;
}

static std::vector<u32> GetFileGroupIds(const BfsarSaveContext& ctx, const File& file)
{
    std::vector<u32> groupIds;

    const auto groups = ctx.fileGroups.find(file.id);

    if (groups == ctx.fileGroups.end())
        return groupIds;

    for (const Group* group : groups->second)
        groupIds.push_back(nw::snd::internal::Util::GetMaskedItemId(group->getId(), nw::snd::internal::ItemType_Group));

    return groupIds;
}

static bool HasSameGroupIds(const std::vector<u32>& groupIdsA, const std::vector<u32>& groupIdsB)
{
    return std::unordered_set<u32>(groupIdsA.begin(), groupIdsA.end()) == std::unordered_set<u32>(groupIdsB.begin(), groupIdsB.end());
}

void Bfsar::writeFileInfoSection_(BfsarSaveContext& ctx)
{
    ctx.writer.closeReference("FileInfoSection", nw::snd::internal::ElementType_SoundArchiveFile_FileInfoSection);

    ctx.writer.pushOffsetBase();
    {
        ctx.writer.openReferenceTable("FileInfoSectionTable", ctx.files.size());

        for (const File& file : ctx.files)
        {
            ctx.writer.addReferenceTableReference("FileInfoSectionTable", nw::snd::internal::ElementType_SoundArchiveFile_FileInfo);

            ctx.writer.pushOffsetBase();
            {
                ctx.writer.openReference("DetailFileInfo");

                ctx.stream.writeU32(0); // Option Parameter

                ctx.writer.pushOffsetBase();
                {
                    if (file.external)
                    {
                        ctx.writer.closeReference("DetailFileInfo", nw::snd::internal::ElementType_SoundArchiveFile_ExternalFileInfo);

                        ctx.stream.writeString(file.externalPath.c_str(), file.externalPath.size() + 1);
                        ctx.writer.align(4);
                    }
                    else if (file.innerFile)
                    {
                        if (getOrigIncludeInBfsar_(ctx, file))
                        {
                            const sead::FormatFixedSafeString<32> fileRefName("File%u", file.id);

                            ctx.writer.closeReference("DetailFileInfo", nw::snd::internal::ElementType_SoundArchiveFile_InternalFileInfo);
                            ctx.writer.openSizedReference(fileRefName);

                            if (ctx.nullDataFileIds.count(file.id))
                                ctx.writer.closeNullSizedReference(fileRefName);

                            {
                                static const std::vector<u32> sEmptyGroups;

                                sead::FormatFixedSafeString<32> gname("FileGroups%u", file.id);
                                ctx.writer.openReference(gname);
                                ctx.writer.closeReference(gname, nw::snd::internal::ElementType_Category_Tables);

                                const std::vector<u32> prefetchGroupIds = file.isPrefetch ? GetPrefetchFileGroupIds(ctx, file) : sEmptyGroups;
                                const std::vector<u32> currentGroupIds = GetFileGroupIds(ctx, file);
                                const std::vector<u32> *pIds = &sEmptyGroups;

                                if (file.isPrefetch)
                                {
                                    pIds = &prefetchGroupIds;
                                }
                                else if (file.origId < mFileAttachedGroups.size())
                                {
                                    const bool keepsTable = mFormat == ArchiveFormat::BCSAR || HasSameGroupIds(mFileAttachedGroups[file.origId], currentGroupIds);

                                    pIds = keepsTable ? &mFileAttachedGroups[file.origId] : &currentGroupIds;
                                }
                                else if (file.origId == nw::snd::SoundArchive::INVALID_ID && file.id < mFileAttachedGroups.size())
                                {
                                    pIds = &mFileAttachedGroups[file.id];
                                }

                                const std::vector<u32> &ids = *pIds;

                                ctx.stream.writeU32(static_cast<u32>(ids.size()));

                                for (u32 id : ids)
                                    ctx.stream.writeU32(id);
                            }
                        }
                        else
                        {
                            ctx.writer.closeNullReference("DetailFileInfo");
                        }
                    }
                    else
                    {
                        ctx.writer.closeNullReference("DetailFileInfo");
                    }
                }
                ctx.writer.popOffsetBase();
            }
            ctx.writer.popOffsetBase();
        }

        ctx.writer.closeReferenceTable("FileInfoSectionTable");
    }
    ctx.writer.popOffsetBase();
}

void Bfsar::writeSoundArchivePlayerInfo_(BfsarSaveContext& ctx)
{
    ctx.writer.closeReference("SoundArchivePlayerInfo", nw::snd::internal::ElementType_SoundArchiveFile_SoundArchivePlayerInfo);
    ctx.writer.pushOffsetBase();
    {
        ctx.stream.writeU16(mSoundArchivePlayerInfo.sequenceSoundMax);
        ctx.stream.writeU16(mSoundArchivePlayerInfo.sequenceTrackMax);
        ctx.stream.writeU16(mSoundArchivePlayerInfo.streamSoundMax);
        ctx.stream.writeU16(mSoundArchivePlayerInfo.streamTrackMax);
        ctx.stream.writeU16(mSoundArchivePlayerInfo.streamChannelMax);
        ctx.stream.writeU16(mSoundArchivePlayerInfo.waveSoundMax);
        ctx.stream.writeU16(mSoundArchivePlayerInfo.waveTrackMax);
        if (mFormat == ArchiveFormat::BCSAR)
            ctx.stream.writeU16(0); // Padding
        else
        {
            ctx.stream.writeU8(isStreamPrefetchAvailable() ? mSoundArchivePlayerInfo.streamBufferTimes : 0);
            ctx.stream.writeU8(0); // Padding
        }
        ctx.stream.writeU32(mSoundArchivePlayerInfo.options);
    }
    ctx.writer.popOffsetBase();
}

void Bfsar::writeInfoBlock_(BfsarSaveContext& ctx)
{
    ctx.writer.openBlock(nw::snd::internal::ElementType_SoundArchiveFile_InfoBlock, "INFO");

    ctx.writer.openReference("SoundInfoSection");
    ctx.writer.openReference("SoundGroupInfoSection");
    ctx.writer.openReference("BankInfoSection");
    ctx.writer.openReference("WaveArchiveInfoSection");
    ctx.writer.openReference("GroupInfoSection");
    ctx.writer.openReference("PlayerInfoSection");
    ctx.writer.openReference("FileInfoSection");
    ctx.writer.openReference("SoundArchivePlayerInfo");

    writeSoundInfoSection_(ctx);
    writeSoundSetInfoSection_(ctx);
    writeBankInfoSection_(ctx);
    writeWaveArchiveInfoSection_(ctx);
    writeGroupInfoSection_(ctx);
    writePlayerInfoSection_(ctx);
    writeFileInfoSection_(ctx);
    writeSoundArchivePlayerInfo_(ctx);

    ctx.writer.align(0x20);
    ctx.writer.closeBlock();
}
