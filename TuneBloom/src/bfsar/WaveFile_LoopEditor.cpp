#include <bfsar/WaveFile.h>
#include <bfsar/DecodedPcm.h>
#include <cmath>

#include <snd/SoundThread.h>

bool WaveFile::setupPreviewPcm16(const DecodedPcm& pcm, bool isLoop, u32 loopStartFrame, u32 loopEndFrame)
{
    if (!pcm.isValid())
        return false;

    {
        snd::internal::driver::SoundThreadLock lock;
        mChannels.clear();
    }

    mDataEndian = sead::Endian::getHostEndian();
    mEncoding = Encoding::Pcm16;
    mSampleRate = pcm.sampleRate;
    mSampleCount = pcm.sampleCount;
    mIsLoop = isLoop;
    mLoopStartFrame = loopStartFrame;
    mLoopEndFrame = loopEndFrame;
    mIsLoopDirty = false;

    for (const auto& chFloats : pcm.channels)
    {
        Channel* channel = mChannels.birthBack();
        SEAD_ASSERT(channel);

        s16* data = new s16[pcm.sampleCount];
        for (u32 i = 0; i < pcm.sampleCount; i++)
        {
            float v = chFloats[i];
            v = v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v);
            const float scale = v < 0.0f ? 32768.0f : 32767.0f;
            data[i] = static_cast<s16>(std::lround(v * scale));
        }

        channel->mOwnsData = true;
        channel->mData = data;
        channel->mDataSize = pcm.sampleCount * sizeof(s16);
        channel->mDataSizeMin = channel->mDataSize;
    }

    return true;
}
