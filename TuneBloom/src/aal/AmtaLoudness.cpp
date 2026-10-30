#include <aal/AmtaLoudness.h>

#include <bfsar/audio/AudioProcessing.h>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace aal
{

static constexpr f64 cPcmScale              = 32768.0;
static constexpr f64 cPcm16Min              = -32768.0;
static constexpr f64 cPcm16Max              = 32767.0;
static constexpr f64 cLoudnessOffset        = -0.691;
static constexpr f64 cAbsoluteGate          = -70.0;
static constexpr f64 cRelativeGate          = -10.0;
static constexpr f64 cRangeGate             = -20.0;
static constexpr f64 cFloorLoudness         = -96.0;
static constexpr f64 cAmta4SilentLoudness   = -100.0;
static constexpr f64 cEnvelopeTolerance     = 1.0;
static constexpr f64 cRangeLowPercentile    = 0.10;
static constexpr f64 cRangeHighPercentile   = 0.95;
static constexpr u32 cBlocksPerSecond       = 10;
static constexpr u32 cRmsWindowBlocks       = 3;
static constexpr u32 cMomentaryBlocks       = 4;
static constexpr u32 cShortTermBlocks       = 30;
static constexpr u32 cShortTermHopBlocks    = 10;
static constexpr u32 cTailBlocks            = 3;

static constexpr s32 cSincHalfTaps          = 32;
static constexpr f64 cSincCutoff            = 0.98;
static constexpr f64 cSincKaiserBeta        = 8.0;
static constexpr u32 cBesselTermsMax        = 64;

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
        const u64 source = index < length || !isLoop ? index : loopStartFrame + (index - length) % (length - loopStartFrame);

        return source < used ? (*channels)[channel][source] : 0;
    }
};

struct AnalysisFrames
{
    u32 length = 0;
    u32 blockCount = 0;
    u32 pointCount = 0;
    u32 sampleCount = 0;
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
            converted[i] = AudioProcessing::toPcm16(resampled[i]);
    }

    return out;
}

static f64 CalcBesselI0(f64 value)
{
    f64 sum = 1.0;
    f64 term = 1.0;

    for (u32 k = 1; k < cBesselTermsMax; k++)
    {
        const f64 factor = value / (2.0 * k);
        term *= factor * factor;
        sum += term;
    }

    return sum;
}

static f64 CalcSincTap(f64 offset)
{
    const f64 ratio = offset / cSincHalfTaps;
    const f64 window = CalcBesselI0(cSincKaiserBeta * std::sqrt(std::max(0.0, 1.0 - ratio * ratio))) / CalcBesselI0(cSincKaiserBeta);
    const f64 phase = cPi * cSincCutoff * offset;

    return cSincCutoff * (phase == 0.0 ? 1.0 : std::sin(phase) / phase) * window;
}

static std::vector<f64> MakeSincTable(u32 phaseCount)
{
    std::vector<f64> table;
    table.reserve(static_cast<size_t>(phaseCount) * cSincHalfTaps * 2);

    for (u32 phase = 0; phase < phaseCount; phase++)
    {
        for (s32 tap = 1 - cSincHalfTaps; tap <= cSincHalfTaps; tap++)
            table.push_back(CalcSincTap(static_cast<f64>(phase) / phaseCount - tap));
    }

    return table;
}

static std::vector<std::vector<s16>> ResampleForAmta5(const std::vector<std::vector<s16>>& channels, u32 sampleRate)
{
    const u64 divisor = std::gcd(sampleRate, cAmtaAnalysisRate);
    const u64 step = sampleRate / divisor;
    const u64 phaseCount = cAmtaAnalysisRate / divisor;
    const std::vector<f64> table = MakeSincTable(static_cast<u32>(phaseCount));
    const s64 inputCount = static_cast<s64>(channels.front().size());
    const u64 outputCount = static_cast<u64>(std::llround(static_cast<f64>(inputCount) * cAmtaAnalysisRate / sampleRate));

    std::vector<std::vector<s16>> out;

    for (const std::vector<s16>& channel : channels)
    {
        std::vector<s16>& converted = out.emplace_back(outputCount);

        for (u64 i = 0; i < outputCount; i++)
        {
            const s64 base = static_cast<s64>(i * step / phaseCount);
            const f64* taps = &table[(i * step % phaseCount) * cSincHalfTaps * 2];
            f64 sum = 0.0;

            for (s32 tap = 1 - cSincHalfTaps; tap <= cSincHalfTaps; tap++, taps++)
            {
                const s64 index = base + tap;

                if (index >= 0 && index < inputCount)
                    sum += channel[index] * *taps;
            }

            converted[i] = static_cast<s16>(std::clamp(std::nearbyint(sum), cPcm16Min, cPcm16Max));
        }
    }

    return out;
}

static u32 ScaleFrame(u32 frame, u32 sampleRate)
{
    return static_cast<u32>(static_cast<u64>(frame) * cAmtaAnalysisRate / sampleRate);
}

static u32 ScaleFrameToSource(u32 frame, u32 sampleRate)
{
    return static_cast<u32>(static_cast<u64>(frame) * sampleRate / cAmtaAnalysisRate);
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

static f64 WeighSample(const KWeighting& weighting, ChannelFilter* filter, f64 input)
{
    return RunBiquad(weighting.highPass, &filter->highPass, RunBiquad(weighting.shelf, &filter->shelf, input));
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
            const f64 weighted = WeighSample(weighting, &filters[c], signal.at(c, n) / cPcmScale);
            const f64 square = weighted * weighted;

            channelSums[c] += square;
            frameSum += square;
        }

        combinedSum += frameSum;
    }

    return sums;
}

static std::vector<f64> FilterBlockSums(const PlaybackSignal& signal, u32 sampleRate, u64 sampleCount, u32 blockSize)
{
    const KWeighting weighting = MakeKWeighting(sampleRate);
    ChannelFilter filter;

    std::vector<f64> blocks;
    f64 sum = 0.0;

    for (u64 n = 0; n < sampleCount; n++)
    {
        const f64 weighted = WeighSample(weighting, &filter, signal.at(0, n) / cPcmScale);
        sum += weighted * weighted;

        if ((n + 1) % blockSize == 0)
        {
            blocks.push_back(sum);
            sum = 0.0;
        }
    }

    return blocks;
}

static std::vector<std::vector<s16>> MakeEnvelopeSource(const std::vector<std::vector<s16>>& channels)
{
    const f64 gain = 1.0 / std::sqrt(static_cast<f64>(channels.size()));
    const std::vector<s16>& last = channels.back();

    std::vector<std::vector<s16>> source(1, std::vector<s16>(last.size()));

    for (size_t i = 0; i < last.size(); i++)
        source[0][i] = static_cast<s16>(std::clamp(std::nearbyint(last[i] * gain), cPcm16Min, cPcm16Max));

    return source;
}

static std::vector<f64> MeasureEnvelopePowers(const PlaybackSignal& signal, u32 sampleRate, u64 sampleCount, const WindowSizes& sizes, u32 pointCount)
{
    const std::vector<std::vector<s16>> source = MakeEnvelopeSource(*signal.channels);

    PlaybackSignal mono = signal;
    mono.channels = &source;

    const std::vector<f64> blocks = FilterBlockSums(mono, sampleRate, sampleCount, sizes.block);
    std::vector<f64> powers;

    for (u32 j = 0; j < pointCount; j++)
    {
        f64 sum = 0.0;

        for (u64 b = j; b < static_cast<u64>(j) + cMomentaryBlocks && b < blocks.size(); b++)
            sum += blocks[b];

        powers.push_back(sum / sizes.momentary);
    }

    return powers;
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
        u32 end = anchor + 1;

        while (end < last && DoesSegmentFit(points, anchor, end + 1))
            end++;

        if (end == last && std::abs(points[last].second - points[anchor].second) <= cEnvelopeTolerance)
            break;

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
        points.emplace_back(points.back().first < length ? length : length + sizes.block, cFloorLoudness);
        values.push_back(0.0);
    }

    std::vector<AmtaEnvelopePoint> envelope;

    for (u32 index : SimplifyEnvelope(points))
    {
        AmtaEnvelopePoint point;
        point.position = std::min(points[index].first, length);
        point.power = points[index].second <= cFloorLoudness ? 0.0f : static_cast<f32>(std::pow(10.0, (static_cast<f32>(CalcLoudness(values[index])) - cLoudnessOffset) / 10.0));

        envelope.push_back(point);
    }

    return envelope;
}

static std::vector<AmtaEnvelopePoint> MakeLoudnessCurve(const std::vector<f64>& powers, const WindowSizes& sizes, u32 length, bool isLoop)
{
    std::vector<AmtaEnvelopePoint> curve;

    for (u32 j = 0; j < powers.size(); j++)
        curve.push_back({ std::min(j * sizes.block, length), static_cast<f32>(powers[j]) });

    if (!isLoop && !powers.empty() && CalcLoudness(powers.back()) > cFloorLoudness)
        curve.push_back({ length, 0.0f });

    return curve;
}

static void ScaleToSource(std::vector<AmtaEnvelopePoint>* points, u32 sampleRate)
{
    for (AmtaEnvelopePoint& point : *points)
        point.position = ScaleFrameToSource(point.position, sampleRate);
}

static f64 CalcLoudnessRange(const FilteredSums& sums, const WindowSizes& sizes, u32 blockCount)
{
    const u64 limit = static_cast<u64>(blockCount + cTailBlocks) * sizes.block;
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

    const f64 count = static_cast<f64>(values.size());
    const u64 highIndex = std::min<u64>(static_cast<u64>(std::nearbyint(cRangeHighPercentile * count)), values.size() - 1);

    return values[highIndex] - values[static_cast<u64>(std::nearbyint(cRangeLowPercentile * count))];
}

static AnalysisFrames MakeAnalysisFrames(u32 length, const WindowSizes& sizes)
{
    AnalysisFrames frames;
    frames.length = length;
    frames.blockCount = (length + sizes.block - 1) / sizes.block;
    frames.pointCount = length / sizes.block + 1;
    frames.sampleCount = (frames.pointCount + cTailBlocks) * sizes.block;

    return frames;
}

static u64 CalcBestWindowSum(const PlaybackSignal& signal, u32 windowSize, u32 windowCount, u64 start)
{
    u64 best = 0;

    for (u32 c = 0; c < signal.channels->size(); c++)
    {
        for (u32 w = 0; w < windowCount; w++)
        {
            u64 sum = 0;

            for (u64 n = start + static_cast<u64>(w) * windowSize; n < start + static_cast<u64>(w + 1) * windowSize; n++)
            {
                const s64 sample = signal.at(c, n);
                sum += static_cast<u64>(sample * sample);
            }

            best = std::max(best, sum);
        }
    }

    return best;
}

static u64 CalcBestMeanSquare(const PlaybackSignal& signal, const WindowSizes& sizes, u32 windowCount)
{
    return CalcBestWindowSum(signal, sizes.rms, windowCount, 0) / sizes.rms;
}

static AmtaLoudness MeasureAnalysisSignal(const PlaybackSignal& signal, u32 sourceRate, u32 sourceLength, f32 quietTailLevel)
{
    const WindowSizes sizes = MakeWindowSizes(cAmtaAnalysisRate);
    const AnalysisFrames frames = MakeAnalysisFrames(signal.length, sizes);
    const FilteredSums sums = FilterSignal(signal, cAmtaAnalysisRate, frames.sampleCount, 0, sizes.block);

    const auto summedPower = [&](s64 startBlock)
    {
        f64 power = 0.0;

        for (const std::vector<f64>& prefix : sums.channelPrefix)
            power += (PrefixAt(prefix, startBlock + cMomentaryBlocks) - PrefixAt(prefix, startBlock)) / sizes.momentary;

        return power;
    };

    f64 momentaryMax = 0.0;

    for (u32 j = 1; j <= frames.blockCount + cTailBlocks; j++)
        momentaryMax = std::max(momentaryMax, summedPower(static_cast<s64>(j) - cMomentaryBlocks));

    std::vector<f64> summed;

    for (u32 j = 0; j < frames.pointCount; j++)
        summed.push_back(summedPower(j));

    std::vector<AmtaEnvelopePoint> curve = MakeLoudnessCurve(summed, sizes, signal.length, signal.isLoop);
    const u64 bestMeanSquare = CalcBestMeanSquare(signal, sizes, (frames.blockCount + cTailBlocks) / cRmsWindowBlocks);

    AmtaLoudness loudness;
    loudness.peak = static_cast<f32>(CalcPeak(*signal.channels, signal.used));
    loudness.rms300 = static_cast<f32>(std::sqrt(static_cast<f64>(bestMeanSquare)) / cPcmScale);
    loudness.momentaryMax = static_cast<f32>(CalcLoudness(momentaryMax));
    loudness.integrated = static_cast<f32>(CalcGatedLoudness(summed));
    loudness.loudnessRange = static_cast<f32>(CalcLoudnessRange(sums, sizes, frames.blockCount));
    loudness.envelope = BuildEnvelope(MeasureEnvelopePowers(signal, cAmtaAnalysisRate, frames.sampleCount, sizes, frames.pointCount), sizes, signal.length, signal.isLoop);

    ScaleToSource(&loudness.envelope, sourceRate);
    ScaleToSource(&curve, sourceRate);

    loudness.quietTailFrames = CalcAmtaQuietTailFrames(loudness.envelope, sourceLength, quietTailLevel);
    loudness.summedQuietTailFrames = CalcAmtaQuietTailFrames(curve, sourceLength, quietTailLevel);

    return loudness;
}

AmtaLoudness MeasureAmta5Loudness(const std::vector<std::vector<s16>>& channels, u32 sampleRate, bool isLoop, u32 loopStartFrame, bool dropLastSample, f32 quietTailLevel)
{
    if (channels.empty() || channels.front().empty() || sampleRate == 0)
        return {};

    const u32 sourceLength = static_cast<u32>(channels.front().size());
    std::vector<std::vector<s16>> resampled;

    if (sampleRate != cAmtaAnalysisRate)
    {
        std::vector<std::vector<s16>> source = channels;

        for (std::vector<s16>& channel : source)
        {
            if (dropLastSample)
                channel.back() = 0;
        }

        resampled = ResampleForAmta5(source, sampleRate);
    }

    const std::vector<std::vector<s16>>& analysis = resampled.empty() ? channels : resampled;
    const u32 length = static_cast<u32>(analysis.front().size());
    const u32 analysisLoopStart = resampled.empty() ? loopStartFrame : ScaleFrame(loopStartFrame, sampleRate);

    if (length == 0)
        return {};

    PlaybackSignal signal;
    signal.channels = &analysis;
    signal.length = length;
    signal.used = dropLastSample && resampled.empty() ? length - 1 : length;
    signal.isLoop = isLoop && analysisLoopStart < length;
    signal.loopStartFrame = signal.isLoop ? analysisLoopStart : 0;

    return MeasureAnalysisSignal(signal, sampleRate, sourceLength, quietTailLevel);
}

u32 CalcAmtaQuietTailFrames(const std::vector<AmtaEnvelopePoint>& envelope, u32 length, f32 level)
{
    const auto loudnessOf = [](const AmtaEnvelopePoint& point) { return std::max(CalcLoudness(point.power), cFloorLoudness); };

    for (size_t i = envelope.size(); i >= 2; i--)
    {
        const AmtaEnvelopePoint& from = envelope[i - 2];
        const AmtaEnvelopePoint& to = envelope[i - 1];
        const f64 fromLoudness = loudnessOf(from);
        const f64 toLoudness = loudnessOf(to);

        if (fromLoudness <= level || toLoudness > level)
            continue;

        const f64 position = from.position + (level - fromLoudness) / (toLoudness - fromLoudness) * (static_cast<f64>(to.position) - from.position);

        return static_cast<u32>(std::max(0.0, std::round(length - position)));
    }

    return 0;
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

    PlaybackSignal signal;
    signal.channels = &channels;
    signal.length = length;
    signal.used = length;

    const u64 bestSum = CalcBestWindowSum(signal, sizes.rms, rmsWindowCount, 1);
    const f64 meanSquare = hasIntegerMeanSquare ? static_cast<f64>(bestSum / sizes.rms) : static_cast<f64>(bestSum) / sizes.rms;

    const FilteredSums sums = FilterSignal(signal, sampleRate, length, 1, sizes.block);
    f64 momentaryMax = 0.0;

    for (u32 j = 1; j <= momentaryCount; j++)
    {
        const f64 power = (PrefixAt(sums.combinedPrefix, j) - PrefixAt(sums.combinedPrefix, static_cast<s64>(j) - cMomentaryBlocks)) / sizes.momentary;
        momentaryMax = std::max(momentaryMax, power);
    }

    loudness.peak = static_cast<f32>(CalcPeak(channels, length));
    loudness.rms300 = static_cast<f32>(std::sqrt(meanSquare) / cPcmScale);

    if (momentaryCount != 0)
        loudness.momentaryMax = static_cast<f32>(momentaryMax > 0.0 ? CalcLoudness(momentaryMax) : cAmta4SilentLoudness);

    return loudness;
}

}
