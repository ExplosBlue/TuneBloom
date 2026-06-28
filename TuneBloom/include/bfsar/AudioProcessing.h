#pragma once

#include <cstdint>
#include <vector>

namespace AudioProcessing {

using u32 = uint32_t;

std::vector<float> resample(const std::vector<float>& data, u32 fromRate, u32 toRate);

struct NormalizeResult { std::vector<std::vector<float>> channels; float gain; float peakDb; };
NormalizeResult normalizeChannels(const std::vector<std::vector<float>>& channels, bool enabled, float targetDb);

enum class ChannelMode { Stereo, Left, Right, MonoMix };
std::vector<std::vector<float>> selectChannels(const std::vector<std::vector<float>>& channels, ChannelMode mode);
std::vector<std::vector<float>> selectChannelByIndex(const std::vector<std::vector<float>>& channels, u32 index);

struct SpeedResult { std::vector<std::vector<float>> channels; u32 newSampleCount; };
SpeedResult applySpeed(const std::vector<std::vector<float>>& channels, float speedMultiplier);

}