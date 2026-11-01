#pragma once

#include <bfsar/model/StreamSource.h>
#include <ui/wave/WaveImportPanel.h>

u32 GetOpusBitrateChoice(const Sound::StreamSoundInfo& stream);
u32 GetOpusOriginalChoice(const Sound::StreamSoundInfo& stream);
bool SetOpusStreamBitrate(const StreamSource& source, u32 bitrate);
void DrawOpusBitrateRow(const StreamSource& source);

StreamSource FindOpusStreamOfWave(const WaveFile* wave);
WaveImportEncoding MakeOpusStreamImportEncoding(s32* codec, u32* bitrate, u32 originalBitrate);
WaveImportEncoding MakeFollowTrackEncoding(const WaveFile& leadWave, s32 leadTrackNo, const Sound::StreamSoundInfo* opusStream, WaveFile::Encoding* encoding, s32* opusCodec, u32* opusBitrate, sead::BufferedSafeString* followsText);
