#include <bfsar/model/StateHash.h>

#include <bfsar/archive/Bfsar.h>

#include <ui/app/UI.h>

void Item::captureBaseline()
{
    mBaselineSignature = computeStateSignature();
    mHasBaseline = true;

    mCachedSignature = mBaselineSignature;
    mCachedSignatureGeneration = GetEditGeneration();
}

bool Item::isModifiedSinceBaseline() const
{
    if (!isStateTracked())
        return false;

    if (!mHasBaseline)
        return true;

    const u64 generation = GetEditGeneration();

    if (mCachedSignatureGeneration != generation)
    {
        mCachedSignature = computeStateSignature();
        mCachedSignatureGeneration = generation;
    }

    return mCachedSignature != mBaselineSignature;
}

u64 SoundSet::computeStateSignature() const
{
    StateHash hash;

    hash.mixName(*this);

    hash.mixValue(mSoundSetType);
    hash.mixValue(mIsEmpty);

    if (!mIsEmpty)
    {
        hash.mixItem(sBfsar.getItem(mStartId, sBfsar.getSoundList()));
        hash.mixItem(sBfsar.getItem(mEndId, sBfsar.getSoundList()));
        hash.mixValue(mEndId - mStartId);
    }

    hash.mixValue(mWaveArchiveType);
    hash.mixRef(mWaveArchiveRef);

    return hash.value();
}

u64 Bank::computeStateSignature() const
{
    StateHash hash;

    hash.mixName(*this);

    hash.mixValue(mWaveArchiveType);
    hash.mixRef(mWaveArchiveRef);
    hash.mixRef(mFileRef);

    return hash.value();
}

u64 WaveArchive::computeStateSignature() const
{
    StateHash hash;

    hash.mixName(*this);

    hash.mixValue(mIsLoadIndividual);

    return hash.value();
}

u64 Group::computeStateSignature() const
{
    StateHash hash;

    hash.mixName(*this);

    hash.mixValue(mOutputType);

    for (const Item* item : mItemInfoList)
    {
        const ItemInfo* info = static_cast<const ItemInfo*>(item);

        hash.mixValue(info->getIsDisabled());
        hash.mixValue(info->getItemRefType());
        hash.mixRef(info->getItemRef());
        hash.mixValue(info->getLoadItem_());
    }

    return hash.value();
}

u64 Player::computeStateSignature() const
{
    StateHash hash;

    hash.mixName(*this);

    hash.mixValue(mPlayableSoundMax);
    hash.mixValue(mEnablePlayerHeapSize);
    hash.mixValue(mPlayerHeapSize);

    return hash.value();
}

u64 WaveFile::computeStateSignature() const
{
    StateHash hash;

    hash.mixName(*this);

    hash.mixValue(mEncoding);
    hash.mixValue(mSampleRate);
    hash.mixValue(mSampleCount);
    hash.mixValue(mChannels.size());

    hash.mixValue(mIsLoop);
    hash.mixValue(mLoopStartFrame);
    hash.mixValue(mLoopEndFrame);

    hash.mixBytes(mMd5Hash.data(), mMd5Hash.size());

    return hash.value();
}

u64 SequenceFile::computeStateSignature() const
{
    StateHash hash;

    hash.mixName(*this);

    hash.mixValue(mIsDirty);
    hash.mixValue(mSeqBytesSize);

    if (mSeqBytes)
        hash.mixBytes(mSeqBytes, mSeqBytesSize);

    return hash.value();
}

u64 BankFile::computeStateSignature() const
{
    StateHash hash;

    hash.mixName(*this);

    for (const Item* instrumentItem : mInstrumentList)
    {
        const Instrument* instrument = static_cast<const Instrument*>(instrumentItem);

        hash.mixName(*instrument);
        hash.mixValue(instrument->mProgramNo);

        for (const Item* keyRegionItem : instrument->mKeyRegionList)
        {
            const KeyRegion* keyRegion = static_cast<const KeyRegion*>(keyRegionItem);

            hash.mixValue(keyRegion->mKeyMin);
            hash.mixValue(keyRegion->mKeyMax);

            for (const Item* velocityRegionItem : keyRegion->mVelocityRegionList)
            {
                const VelocityRegion* region = static_cast<const VelocityRegion*>(velocityRegionItem);

                hash.mixValue(region->mVelocityMin);
                hash.mixValue(region->mVelocityMax);

                hash.mixRef(region->mWaveFileRef);
                hash.mixValue(region->mRootKey);
                hash.mixValue(region->mVolume);
                hash.mixValue(region->mPan);
                hash.mixValue(region->mPitch);
                hash.mixValue(region->mIsIgnoreNoteOff);
                hash.mixValue(region->mKeyGroup);
                hash.mixValue(region->mInterpolationType);

                hash.mixValue(region->mAdshrCurve.attack);
                hash.mixValue(region->mAdshrCurve.decay);
                hash.mixValue(region->mAdshrCurve.sustain);
                hash.mixValue(region->mAdshrCurve.hold);
                hash.mixValue(region->mAdshrCurve.release);
            }

            hash.mixValue(static_cast<u8>(0xFF));
        }

        hash.mixValue(static_cast<u8>(0xFE));
    }

    return hash.value();
}

u64 Bfsar::computeInfoSignature() const
{
    StateHash hash;

    hash.mixValue(mFormat);
    hash.mixValue(mPlatform);
    hash.mixValue(mEndian);
    hash.mixValue(mVersion);
    hash.mixValue(mIncludeStringTable);

    hash.mixValue(mSoundArchivePlayerInfo.sequenceSoundMax);
    hash.mixValue(mSoundArchivePlayerInfo.sequenceTrackMax);
    hash.mixValue(mSoundArchivePlayerInfo.streamSoundMax);
    hash.mixValue(mSoundArchivePlayerInfo.streamTrackMax);
    hash.mixValue(mSoundArchivePlayerInfo.streamChannelMax);
    hash.mixValue(mSoundArchivePlayerInfo.waveSoundMax);
    hash.mixValue(mSoundArchivePlayerInfo.waveTrackMax);
    hash.mixValue(mSoundArchivePlayerInfo.streamBufferTimes);
    hash.mixValue(mSoundArchivePlayerInfo.options);

    return hash.value();
}

void Bfsar::captureItemBaselines()
{
    const Item::List* lists[] = {
        &mSoundList, &mSoundSetList, &mBankList, &mWaveArchiveList, &mGroupList, &mPlayerList,
        &mWaveFileList, &mSequenceFileList, &mBankFileList
    };

    for (const Item::List* list : lists)
    {
        for (Item* item : *list)
            item->captureBaseline();
    }

    mInfoBaselineSignature = computeInfoSignature();
}
