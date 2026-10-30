#pragma once

#include <aal/AmtaLoudness.h>

#include <basis/seadTypes.h>

#include <string>
#include <vector>

namespace aal
{

inline constexpr u16 cAmtaVersion1 = 0x0100;
inline constexpr u16 cAmtaVersion4 = 0x0400;
inline constexpr u16 cAmtaVersion5 = 0x0500;

enum class AmtaAudioKind
{
    Bfwav,
    Bwav
};

struct AmtaWaveFields
{
    u32 sampleRate = 0;
    u32 storedSampleCount = 0;
    u32 sampleCount = 0;
    u32 channelCount = 0;
    bool isLoop = false;
    u32 loopStartFrame = 0;
    bool isOpus = false;
    bool isStream = false;
};

struct AmtaSourceAudio
{
    u32 sampleRate = 0;
    u32 sampleCount = 0;
};

struct AmtaMarker
{
    std::string name;
    u32 startFrame = 0;
    u32 frameCount = 0;

    bool endsAfter(u32 sampleCount) const
    {
        return static_cast<u64>(startFrame) + frameCount > sampleCount;
    }

    bool operator==(const AmtaMarker&) const = default;
};

struct AmtaExtEntry
{
    std::string name;
    u32 value = 0;
    bool isText = false;
    std::string text;

    bool operator==(const AmtaExtEntry&) const = default;
};

struct AmtaLabels
{
    std::vector<AmtaMarker> markers;
    std::vector<std::string> tags;
    std::vector<AmtaExtEntry> keys;

    bool operator==(const AmtaLabels&) const = default;
};

bool IsAmta(const void* data, u64 size);
u32 MeasureAmta(const void* data, u64 size);
u16 GetAmtaVersion(const std::vector<u8>& meta);
bool IsAmtaBigEndian(const std::vector<u8>& meta);
u32 GetNewAmta5InfoMask(const std::vector<u8>& meta);

std::vector<u8> MakeAmta1(const std::string& name, bool isBigEndian);
std::vector<u8> MakeAmta4(const std::string& name, AmtaAudioKind kind, bool isBigEndian);
bool WriteAmta4StreamLayout(std::vector<u8>* meta, u32 channelCount);
std::vector<u8> MakeAmta5(const std::string& name, u32 channelCount, u32 infoMask, bool isBigEndian);
bool ReadAmtaName(const std::vector<u8>& meta, std::string* outName);
bool ReadAmtaLabels(const std::vector<u8>& meta, AmtaLabels* out);
bool IsAmtaTextKey(const std::string& name);
bool CanAddAmtaKeys(const std::vector<u8>& meta);
bool CanWriteAmtaLabels(const std::vector<u8>& meta);
bool WriteAmtaLabels(std::vector<u8>* meta, const AmtaLabels& labels);
bool RenameAmta(std::vector<u8>* meta, const std::string& name);
bool CanRenameAmta(const std::vector<u8>& meta);
bool UpdateAmtaWaveFields(std::vector<u8>* meta, const AmtaWaveFields& fields);
bool HasAmtaSourceRate(const std::vector<u8>& meta, u32 audioSampleRate);
bool WriteAmtaSourceRate(std::vector<u8>* meta, const AmtaSourceAudio& source);
bool ClearAmtaMidSide(std::vector<u8>* meta);
bool IsAmtaMidSide(const std::vector<u8>& meta);
bool HasAmtaMusicInfo(const std::vector<u8>& meta);
f32 ReadAmtaTempo(const std::vector<u8>& meta);
bool ClearAmtaMusicInfo(std::vector<u8>* meta);
bool ClearAmtaMarkers(std::vector<u8>* meta);
u32 CalcAmtaMarkerLimit(bool keepsSourceRate, const AmtaSourceAudio& source, u32 audioSampleRate, u32 audioSampleCount);
bool ClampAmtaMarkers(std::vector<u8>* meta, u32 sampleCount);
bool IsAmtaLoop(const std::vector<u8>& meta);
bool ReadAmtaLength(const std::vector<u8>& meta, AmtaSourceAudio* out);
bool IsAmtaStream(const std::vector<u8>& meta, AmtaAudioKind* outKind);
bool HasAmtaLoudness(const std::vector<u8>& meta);
bool WriteAmtaLoudness(std::vector<u8>* meta, const AmtaLoudness& loudness);
bool CanWriteAmta5Loudness(const std::vector<u8>& meta);
bool ReadAmtaQuietTail(const std::vector<u8>& meta, u32* outFrames, std::vector<AmtaEnvelopePoint>* outEnvelope);

}
