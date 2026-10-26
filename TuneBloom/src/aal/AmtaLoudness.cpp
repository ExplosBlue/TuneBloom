#include <aal/AmtaLoudness.h>

#include <bfsar/audio/AudioProcessing.h>

#include <algorithm>
#include <cmath>

namespace aal
{

static constexpr f64 cPcmScale              = 32768.0;
static constexpr f64 cLoudnessOffset        = -0.691;
static constexpr f64 cAbsoluteGate          = -70.0;
static constexpr f64 cRelativeGate          = -10.0;
static constexpr f64 cRangeGate             = -20.0;
static constexpr f64 cFloorLoudness         = -96.0;
static constexpr f64 cEnvelopeTolerance     = 1.0;
static constexpr f64 cRangeLowPercentile    = 0.10;
static constexpr f64 cRangeHighPercentile   = 0.95;
static constexpr u32 cBlocksPerSecond       = 10;
static constexpr u32 cRmsWindowBlocks       = 3;
static constexpr u32 cMomentaryBlocks       = 4;
static constexpr u32 cShortTermBlocks       = 30;
static constexpr u32 cShortTermHopBlocks    = 10;
static constexpr u32 cRangeTailBlocks       = 3;

static constexpr f64 cShelfFrequency        = 1681.974450955533;
static constexpr f64 cShelfGainDb           = 3.999843853973347;
static constexpr f64 cShelfQ                = 0.7071752369554196;
static constexpr f64 cShelfGainExponent     = 0.4996667741545416;
static constexpr f64 cHighPassFrequency     = 38.13547087602444;
static constexpr f64 cHighPassQ             = 0.5003270373238773;
static constexpr f64 cPi                    = 3.14159265358979323846;

namespace
{

struct Biquad
{
    f64 b0 = 0.0;
    f64 b1 = 0.0;
    f64 b2 = 0.0;
    f64 a1 = 0.0;
    f64 a2 = 0.0;
};

struct BiquadState
{
    f64 x1 = 0.0;
    f64 x2 = 0.0;
    f64 y1 = 0.0;
    f64 y2 = 0.0;
};

struct KWeighting
{
    Biquad shelf;
    Biquad highPass;
};

struct ChannelFilter
{
    BiquadState shelf;
    BiquadState highPass;
};

struct WindowSizes
{
    u32 block = 0;
    u32 rms = 0;
    u32 momentary = 0;
};

struct PlaybackSignal
{
    const std::vector<std::vector<s16>>* channels = nullptr;
    u32 length = 0;
    u32 used = 0;
    bool isLoop = false;
    u32 loopStartFrame = 0;

    s16 at(u32 channel, u64 index) const
    {
        const std::vector<s16>& samples = (*channels)[channel];

        if (index < used)
            return samples[index];

        if (!isLoop)
            return 0;

        return samples[loopStartFrame + (index - length) % (length - loopStartFrame)];
    }
};

struct FilteredSums
{
    std::vector<std::vector<f64>> channelPrefix;
    std::vector<f64> combinedPrefix;
};

}

static std::vector<std::vector<s16>> ResampleForAnalysis(const std::vector<std::vector<s16>>& channels, u32 sampleRate)
{
    std::vector<std::vector<s16>> out;

    for (const std::vector<s16>& channel : channels)
    {
        std::vector<float> samples(channel.size());

        for (u32 i = 0; i < channel.size(); i++)
            samples[i] = AudioProcessing::fromPcm16(channel[i]);

        const std::vector<float> resampled = AudioProcessing::resample(samples, sampleRate, cAmtaAnalysisRate);
        std::vector<s16>& converted = out.emplace_back(resampled.size());

        for (u32 i = 0; i < resampled.size(); i++)
            converted[i] = AudioProcessing::restorePcm16(resampled[i]);
    }

    return out;
}

static u32 ScaleFrame(u32 frame, u32 sampleRate)
{
    return static_cast<u32>(static_cast<u64>(frame) * cAmtaAnalysisRate / sampleRate);
}

static KWeighting MakeKWeighting(u32 sampleRate)
{
    KWeighting weighting;

    const f64 shelfK = std::tan(cPi * cShelfFrequency / sampleRate);
    const f64 vh = std::pow(10.0, cShelfGainDb / 20.0);
    const f64 vb = std::pow(vh, cShelfGainExponent);
    const f64 shelfA0 = 1.0 + shelfK / cShelfQ + shelfK * shelfK;

    weighting.shelf.b0 = (vh + vb * shelfK / cShelfQ + shelfK * shelfK) / shelfA0;
    weighting.shelf.b1 = 2.0 * (shelfK * shelfK - vh) / shelfA0;
    weighting.shelf.b2 = (vh - vb * shelfK / cShelfQ + shelfK * shelfK) / shelfA0;
    weighting.shelf.a1 = 2.0 * (shelfK * shelfK - 1.0) / shelfA0;
    weighting.shelf.a2 = (1.0 - shelfK / cShelfQ + shelfK * shelfK) / shelfA0;

    const f64 highPassK = std::tan(cPi * cHighPassFrequency / sampleRate);
    const f64 highPassA0 = 1.0 + highPassK / cHighPassQ + highPassK * highPassK;

    weighting.highPass.b0 = 1.0;
    weighting.highPass.b1 = -2.0;
    weighting.highPass.b2 = 1.0;
    weighting.highPass.a1 = 2.0 * (highPassK * highPassK - 1.0) / highPassA0;
    weighting.highPass.a2 = (1.0 - highPassK / cHighPassQ + highPassK * highPassK) / highPassA0;

    return weighting;
}

static f64 RunBiquad(const Biquad& biquad, BiquadState* state, f64 value)
{
    const f64 out = biquad.b0 * value + biquad.b1 * state->x1 + biquad.b2 * state->x2 - biquad.a1 * state->y1 - biquad.a2 * state->y2;

    state->x2 = state->x1;
    state->x1 = value;
    state->y2 = state->y1;
    state->y1 = out;

    return out;
}

static f64 CalcLoudness(f64 power)
{
    return power > 0.0 ? cLoudnessOffset + 10.0 * std::log10(power) : -INFINITY;
}

static WindowSizes MakeWindowSizes(u32 sampleRate)
{
    WindowSizes sizes;
    sizes.block = std::max<u32>(1, sampleRate / cBlocksPerSecond);
    sizes.rms = sizes.block * cRmsWindowBlocks;
    sizes.momentary = sizes.block * cMomentaryBlocks;

    return sizes;
}

static FilteredSums FilterSignal(const PlaybackSignal& signal, u32 sampleRate, u64 sampleCount, u64 firstMark, u32 markStep)
{
    const KWeighting weighting = MakeKWeighting(sampleRate);
    const u32 channelCount = static_cast<u32>(signal.channels->size());

    std::vector<ChannelFilter> filters(channelCount);
    std::vector<f64> channelSums(channelCount, 0.0);
    f64 combinedSum = 0.0;

    FilteredSums sums;
    sums.channelPrefix.resize(channelCount);

    u64 nextMark = firstMark;

    for (u64 n = 0; n <= sampleCount; n++)
    {
        if (n == nextMark)
        {
            for (u32 c = 0; c < channelCount; c++)
                sums.channelPrefix[c].push_back(channelSums[c]);

            sums.combinedPrefix.push_back(combinedSum);
            nextMark += markStep;
        }

        if (n == sampleCount)
            break;

        f64 frameSum = 0.0;

        for (u32 c = 0; c < channelCount; c++)
        {
            const f64 input = signal.at(c, n) / cPcmScale;
            const f64 weighted = RunBiquad(weighting.highPass, &filters[c].highPass, RunBiquad(weighting.shelf, &filters[c].shelf, input));
            const f64 square = weighted * weighted;

            channelSums[c] += square;
            frameSum += square;
        }

        combinedSum += frameSum;
    }

    return sums;
}

static f64 PrefixAt(const std::vector<f64>& prefix, s64 mark)
{
    return mark < 0 || prefix.empty() ? 0.0 : prefix[std::min<u64>(static_cast<u64>(mark), prefix.size() - 1)];
}

static f64 CalcPeak(const std::vector<std::vector<s16>>& channels, u32 used)
{
    s32 peak = 0;

    for (const std::vector<s16>& channel : channels)
    {
        for (u32 i = 0; i < used; i++)
            peak = std::max<s32>(peak, std::abs(static_cast<s32>(channel[i])));
    }

    return peak / cPcmScale;
}

static f64 CalcGatedLoudness(const std::vector<f64>& powers)
{
    f64 passedSum = 0.0;
    u32 passedCount = 0;

    for (f64 power : powers)
    {
        if (power > 0.0 && CalcLoudness(power) > cAbsoluteGate)
        {
            passedSum += power;
            passedCount++;
        }
    }

    if (passedCount == 0)
        return cFloorLoudness;

    const f64 threshold = CalcLoudness(passedSum / passedCount) + cRelativeGate;
    f64 keptSum = 0.0;
    u32 keptCount = 0;

    for (f64 power : powers)
    {
        if (power > 0.0 && CalcLoudness(power) > cAbsoluteGate && CalcLoudness(power) > threshold)
        {
            keptSum += power;
            keptCount++;
        }
    }

    return CalcLoudness(keptSum / keptCount);
}

static f64 CalcLineDeviation(const std::vector<std::pair<u32, f64>>& points, u32 from, u32 to, u32 index)
{
    const f64 fromX = points[from].first;
    const f64 fromY = points[from].second;
    const f64 toY = points[to].second;
    const f64 slope = (toY - fromY) * (points[index].first - fromX) / (points[to].first - fromX);

    return std::abs(points[index].second - (fromY + slope));
}

static bool DoesSegmentFit(const std::vector<std::pair<u32, f64>>& points, u32 from, u32 to)
{
    for (u32 i = from + 1; i < to; i++)
    {
        if (CalcLineDeviation(points, from, to, i) > cEnvelopeTolerance)
            return false;
    }

    return true;
}

static std::vector<u32> SimplifyEnvelope(const std::vector<std::pair<u32, f64>>& points)
{
    const u32 last = static_cast<u32>(points.size()) - 1;
    std::vector<u32> kept = { 0 };
    u32 anchor = 0;

    while (anchor < last)
    {
        if (std::abs(points[last].second - points[anchor].second) <= cEnvelopeTolerance && DoesSegmentFit(points, anchor, last))
            break;

        u32 end = anchor + 1;

        while (end < last && DoesSegmentFit(points, anchor, end + 1))
            end++;

        kept.push_back(end);
        anchor = end;
    }

    return kept;
}

static std::vector<AmtaEnvelopePoint> BuildEnvelope(const std::vector<f64>& powers, const WindowSizes& sizes, u32 length, bool isLoop)
{
    std::vector<std::pair<u32, f64>> points;
    std::vector<f64> values = powers;

    for (u32 j = 0; j < powers.size(); j++)
        points.emplace_back(j * sizes.block, std::max(CalcLoudness(powers[j]), cFloorLoudness));

    if (!isLoop && points.back().second > cFloorLoudness)
    {
        points.emplace_back(length, cFloorLoudness);
        values.push_back(0.0);
    }

    std::vector<AmtaEnvelopePoint> envelope;

    for (u32 index : SimplifyEnvelope(points))
    {
        AmtaEnvelopePoint point;
        point.position = points[index].first;
        point.power = points[index].second <= cFloorLoudness ? 0.0f : static_cast<f32>(std::pow(10.0, (static_cast<f32>(CalcLoudness(values[index])) - cLoudnessOffset) / 10.0));

        envelope.push_back(point);
    }

    return envelope;
}

static f64 CalcLoudnessRange(const FilteredSums& sums, const WindowSizes& sizes, u32 blockCount)
{
    const u64 limit = static_cast<u64>(blockCount + cRangeTailBlocks) * sizes.block;
    const u64 window = static_cast<u64>(cShortTermBlocks) * sizes.block;
    const u64 hop = static_cast<u64>(cShortTermHopBlocks) * sizes.block;

    std::vector<f64> passed;

    for (u64 end = window; end <= limit; end += hop)
    {
        const f64 power = (PrefixAt(sums.combinedPrefix, end / sizes.block) - PrefixAt(sums.combinedPrefix, (end - window) / sizes.block)) / window;

        if (power > 0.0 && CalcLoudness(power) > cAbsoluteGate)
            passed.push_back(power);
    }

    if (limit < window + hop || passed.size() < 2)
        return 0.0;

    f64 passedSum = 0.0;

    for (f64 power : passed)
        passedSum += power;

    const f64 threshold = CalcLoudness(passedSum / passed.size()) + cRangeGate;
    std::vector<f64> values;

    for (f64 power : passed)
    {
        if (CalcLoudness(power) > threshold)
            values.push_back(CalcLoudness(power));
    }

    if (values.size() < 2)
        return 0.0;

    std::sort(values.begin(), values.end());

    const f64 lastIndex = static_cast<f64>(values.size() - 1);

    return values[static_cast<u64>(std::nearbyint(cRangeHighPercentile * lastIndex))] - values[static_cast<u64>(std::nearbyint(cRangeLowPercentile * lastIndex))];
}

AmtaLoudness MeasureAmta5Loudness(const std::vector<std::vector<s16>>& channels, u32 sampleRate, bool isLoop, u32 loopStartFrame, bool dropLastSample)
{
    AmtaLoudness loudness;

    if (channels.empty() || channels.front().empty() || sampleRate == 0)
        return loudness;

    if (sampleRate != cAmtaAnalysisRate)
        return MeasureAmta5Loudness(ResampleForAnalysis(channels, sampleRate), cAmtaAnalysisRate, isLoop, ScaleFrame(loopStartFrame, sampleRate), dropLastSample);

    const WindowSizes sizes = MakeWindowSizes(sampleRate);
    const u32 length = static_cast<u32>(channels.front().size());
    const u32 channelCount = static_cast<u32>(channels.size());
    const u32 blockCount = (length + sizes.block - 1) / sizes.block;
    const u32 momentaryCount = isLoop ? std::max(blockCount, cMomentaryBlocks) : blockCount;
    const u32 rmsWindowCount = (length + sizes.rms - 1) / sizes.rms;
    const u64 total = std::max({ static_cast<u64>(momentaryCount) * sizes.block, static_cast<u64>(rmsWindowCount) * sizes.rms,
                                 static_cast<u64>(blockCount) * sizes.block + sizes.momentary });

    PlaybackSignal signal;
    signal.channels = &channels;
    signal.length = length;
    signal.used = dropLastSample && !isLoop ? length - 1 : length;
    signal.isLoop = isLoop && loopStartFrame < length;
    signal.loopStartFrame = signal.isLoop ? loopStartFrame : 0;

    u64 bestMeanSquare = 0;

    for (u32 c = 0; c < channelCount; c++)
    {
        for (u32 w = 0; w < rmsWindowCount; w++)
        {
            u64 sum = 0;

            for (u64 n = static_cast<u64>(w) * sizes.rms; n < static_cast<u64>(w + 1) * sizes.rms; n++)
            {
                const s64 sample = signal.at(c, n);
                sum += static_cast<u64>(sample * sample);
            }

            bestMeanSquare = std::max(bestMeanSquare, sum / sizes.rms);
        }
    }

    const FilteredSums sums = FilterSignal(signal, sampleRate, total, 0, sizes.block);

    const auto windowPower = [&](const std::vector<f64>& prefix, s64 startBlock)
    {
        return (PrefixAt(prefix, startBlock + cMomentaryBlocks) - PrefixAt(prefix, startBlock)) / sizes.momentary;
    };

    const auto summedPower = [&](s64 startBlock)
    {
        f64 power = 0.0;

        for (const std::vector<f64>& prefix : sums.channelPrefix)
            power += windowPower(prefix, startBlock);

        return power;
    };

    f64 momentaryMax = 0.0;

    for (u32 j = 1; j <= momentaryCount; j++)
        momentaryMax = std::max(momentaryMax, summedPower(static_cast<s64>(j) - cMomentaryBlocks));

    const std::vector<f64>& lastChannelPrefix = sums.channelPrefix.back();
    std::vector<f64> forward;
    std::vector<f64> envelopePower;

    for (u32 j = 0; j < blockCount; j++)
    {
        forward.push_back(summedPower(j));
        envelopePower.push_back(windowPower(lastChannelPrefix, j) / channelCount);
    }

    loudness.peak = static_cast<f32>(CalcPeak(channels, signal.used));
    loudness.rms300 = static_cast<f32>(std::sqrt(static_cast<f64>(bestMeanSquare)) / cPcmScale);
    loudness.momentaryMax = static_cast<f32>(CalcLoudness(momentaryMax));
    loudness.integrated = static_cast<f32>(CalcGatedLoudness(forward));
    loudness.loudnessRange = static_cast<f32>(CalcLoudnessRange(sums, sizes, blockCount));
    loudness.envelope = BuildEnvelope(envelopePower, sizes, length, signal.isLoop);

    return loudness;
}

AmtaLoudness MeasureAmta4Loudness(const std::vector<std::vector<s16>>& channels, u32 sampleRate, bool hasIntegerMeanSquare)
{
    AmtaLoudness loudness;

    if (channels.empty() || channels.front().empty() || sampleRate == 0)
        return loudness;

    if (sampleRate != cAmtaAnalysisRate)
        return MeasureAmta4Loudness(ResampleForAnalysis(channels, sampleRate), cAmtaAnalysisRate, hasIntegerMeanSquare);

    const WindowSizes sizes = MakeWindowSizes(sampleRate);
    const u32 length = static_cast<u32>(channels.front().size());
    const u32 rmsWindowCount = (length - 1) / sizes.rms;
    const u32 momentaryCount = (length - 1) / sizes.block;

    u64 bestSum = 0;

    for (const std::vector<s16>& channel : channels)
    {
        for (u32 w = 0; w < rmsWindowCount; w++)
        {
            u64 sum = 0;

            for (u64 n = 1 + static_cast<u64>(w) * sizes.rms; n < 1 + static_cast<u64>(w + 1) * sizes.rms; n++)
                sum += static_cast<u64>(static_cast<s64>(channel[n]) * channel[n]);

            bestSum = std::max(bestSum, sum);
        }
    }

    const f64 meanSquare = hasIntegerMeanSquare ? static_cast<f64>(bestSum / sizes.rms) : static_cast<f64>(bestSum) / sizes.rms;

    PlaybackSignal signal;
    signal.channels = &channels;
    signal.length = length;
    signal.used = length;

    const FilteredSums sums = FilterSignal(signal, sampleRate, length, 1, sizes.block);
    f64 momentaryMax = 0.0;

    for (u32 j = 1; j <= momentaryCount; j++)
    {
        const f64 power = (PrefixAt(sums.combinedPrefix, j) - PrefixAt(sums.combinedPrefix, static_cast<s64>(j) - cMomentaryBlocks)) / sizes.momentary;
        momentaryMax = std::max(momentaryMax, power);
    }

    loudness.peak = static_cast<f32>(CalcPeak(channels, length));
    loudness.rms300 = static_cast<f32>(std::sqrt(meanSquare) / cPcmScale);
    loudness.momentaryMax = momentaryCount != 0 ? static_cast<f32>(CalcLoudness(momentaryMax)) : 0.0f;

    return loudness;
}

}
