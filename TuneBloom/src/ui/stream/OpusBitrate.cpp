#include <ui/stream/OpusBitrate.h>

#include <ui/app/Workspace.h>
#include <ui/file/Document.h>
#include <ui/file/UnsavedChanges.h>
#include <messages/Messages.h>
#include <ui/wave/WaveFormatUI.h>
#include <ui/widgets/Properties.h>
#include <ui/widgets/Widgets.h>

#include <aal/Bwav.h>
#include <bfsar/archive/Bfsar.h>
#include <bfsar/audio/NxOpus.h>
#include <bfsar/files/OpusStream.h>

#include <imgui/imgui.h>

u32 GetOpusBitrateChoice(const Sound::StreamSoundInfo& stream)
{
    return nxopus::FindNearestChannelBitrate(opusstream::GetEncodeChannelBitrate(stream));
}

u32 GetOpusOriginalChoice(const Sound::StreamSoundInfo& stream)
{
    const u32 original = stream.getOriginalOpusChannelBitrate();

    return nxopus::IsChannelBitratePreset(original) ? original : 0;
}

bool SetOpusStreamBitrate(const StreamSource& source, u32 bitrate)
{
    Bfsar* archive = source.sound ? source.sound->getArchive() : nullptr;

    if (!archive)
    {
        source.stream->setOpusChannelBitrate(bitrate);
        return true;
    }

    if (!source.stream->areTrackWavesAttached() && !opusstream::AttachStreamWaves(*archive, source.sound))
        return false;

    for (Item* item : archive->getSoundList())
    {
        Sound* sound = static_cast<Sound*>(item);

        if (sound->getSoundType() == Sound::SoundType::Strm && sound->getStreamSoundInfo().getPath() == source.stream->getPath())
            sound->getStreamSoundInfo().setOpusChannelBitrate(bitrate);
    }

    return true;
}

void DrawOpusBitrateRow(const StreamSource& source)
{
    const Sound::StreamSoundInfo& stream = *source.stream;
    const bool isKnown = stream.getOriginalOpusChannelBitrate() != 0 || stream.areTrackWavesAttached();
    u32 bitrate = GetOpusBitrateChoice(stream);

    PropertyRow(messages::common::cBitrate);
    DetailTooltip(messages::import::cWaveBitrateTip);

    const bool changed = DrawOpusBitrateCombo("##OpusBitrate", &bitrate, GetOpusOriginalChoice(stream), isKnown);

    if (!isKnown)
        ImGui::SetItemTooltip("%s", messages::stream::cFileNotLoaded);

    if (changed && SetOpusStreamBitrate(source, bitrate))
        SetUnsavedChanges(true);
}

static bool HoldsOpusWave(const Sound::StreamSoundInfo& stream, const WaveFile* wave)
{
    if (stream.getStreamType() != Sound::StreamSoundInfo::StreamType::Opus)
        return false;

    for (const Item* item : stream.getTrackList())
    {
        if (static_cast<const Sound::StreamSoundInfo::Track*>(item)->getWaveFileRef().getItem() == wave)
            return true;
    }

    return false;
}

StreamSource FindOpusStreamOfWave(const WaveFile* wave)
{
    if (!wave)
        return StreamSource();

    const StreamSource active = GetActiveStreamSource();

    if (active.isValid() && HoldsOpusWave(*active.stream, wave))
        return active;

    Bfsar* archive = FindActiveSoundArchive();

    if (!archive)
        return StreamSource();

    for (Item* item : archive->getSoundList())
    {
        Sound* sound = static_cast<Sound*>(item);

        if (sound->getSoundType() == Sound::SoundType::Strm && HoldsOpusWave(sound->getStreamSoundInfo(), wave))
            return archive->getStreamSource(sound);
    }

    return StreamSource();
}

WaveImportEncoding MakeOpusStreamImportEncoding(s32* codec, u32* bitrate, u32 originalBitrate)
{
    static const char* const cOpusOnlyReasons[cBwavCodecCount] = { messages::import::cWaveOpusOnlyTip, messages::import::cWaveOpusOnlyTip, nullptr };

    WaveImportEncoding choice;
    choice.value = codec;
    choice.names = cBwavCodecNames;
    choice.details = cBwavCodecDetails;
    choice.reasons = cOpusOnlyReasons;
    choice.count = static_cast<s32>(cBwavCodecCount);
    choice.acceptsSampleRate = &AcceptsBwavCodecSampleRate;
    choice.sampleRateReason = messages::import::cWaveOpusRateTip;
    choice.bitrate = bitrate;
    choice.bitrateValue = static_cast<s32>(aal::BwavCodec::Opus);
    choice.originalBitrate = originalBitrate;

    return choice;
}
