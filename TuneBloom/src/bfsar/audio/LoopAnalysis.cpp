#include <bfsar/audio/LoopAnalysis.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace LoopAnalysis
{

constexpr u32 cWindowPeriods  = 8;
constexpr u32 cZeroSearchMax  = 2000;
constexpr u32 cAdpcmFrameSize = 14;

u32 estimatePeriod(const std::vector<float>& mono, u32 sampleRate)
{
    if (mono.empty() || sampleRate == 0)
        return 0;

    const u32 n = static_cast<u32>(mono.size());
    u32 a = static_cast<u32>(n * 0.4);
    const u32 b = static_cast<u32>(n * 0.6);
    if (b <= a)
        return 0;
    u32 winLen = b - a;

    const u32 minP = static_cast<u32>(sampleRate / 1200);
    u32 maxP = static_cast<u32>(sampleRate / 40);

    const u32 maxWinLen = maxP * cWindowPeriods;
    if (maxWinLen > 0 && winLen > maxWinLen)
    {
        a += (winLen - maxWinLen) / 2;
        winLen = maxWinLen;
    }

    const u32 halfWin = winLen / 2;

    if (maxP > halfWin)
        maxP = halfWin;

    if (minP > maxP)
        return 0;

    u32 best = 0;
    double bestVal = -1.0;

    const float* window = mono.data() + a;

    for (u32 p = minP; p <= maxP; p++)
    {
        double s = 0.0, e1 = 0.0, e2 = 0.0;

        for (u32 i = 0; i + p < winLen; i++)
        {
            const double wi  = static_cast<double>(window[i]);
            const double wip = static_cast<double>(window[i + p]);
            s  += wi * wip;
            e1 += wi * wi;
            e2 += wip * wip;
        }
        const double nc = s / (std::sqrt(e1 * e2) + 1e-9);

        if (nc > bestVal)
        {
            bestVal = nc;
            best = p;
        }
    }

    return best;
}

static u32 nearestZeroUp(const std::vector<float>& mono, u32 i)
{
    if (mono.empty())
        return i;

    const u32 n = static_cast<u32>(mono.size());

    if (n < 3)
        return 0;

    i = std::min(n - 2, std::max<u32>(1, i));

    for (u32 d = 0; d < cZeroSearchMax; d++)
    {
        const u32 a = i + d;
        if (a < n - 1 && mono[a] <= 0.0f && mono[a + 1] > 0.0f)
            return a;

        if (d <= i)
        {
            const u32 b = i - d;
            if (b > 0 && mono[b] <= 0.0f && mono[b + 1] > 0.0f)
                return b;
        }
    }
    return i;
}

static u32 refineEnd(const std::vector<float>& mono, u32 loopStart, u32 loopEnd, u32 period)
{
    const u32 n = static_cast<u32>(mono.size());
    const u32 W = (period == 0) ? 256u : std::min(period, 512u);

    if (n == 0 || loopStart + W >= n)
        return loopEnd;

    u32 best = loopEnd;
    double bestErr = std::numeric_limits<double>::infinity();

    const u32 lowerFromPeriod = (loopEnd > period) ? (loopEnd - period) : 0;
    const u32 lo = std::max(loopStart + period, lowerFromPeriod);
    const u32 upperBound = (n > W + 1) ? (n - W - 1) : 0;
    const u32 hi = std::min(upperBound, loopEnd + period);

    if (lo > hi)
        return loopEnd;

    for (u32 cand = lo; cand <= hi; cand++)
    {
        double err = 0.0;
        for (u32 i = 0; i < W; i++)
        {
            const double d = static_cast<double>(mono[loopStart + i]) - static_cast<double>(mono[cand + i]);
            err += d * d;
        }

        if (err < bestErr)
        {
            bestErr = err;
            best = cand;
        }
    }
    return best;
}

SuggestResult suggestLoop(const std::vector<float>& mono, u32 sampleRate)
{
    SuggestResult result{ false, 0, 0 };

    if (mono.empty())
        return result;

    const u32 n = static_cast<u32>(mono.size());
    const u32 period = estimatePeriod(mono, sampleRate);

    const u32 startGuess = static_cast<u32>(n * 0.45);
    u32 ls = nearestZeroUp(mono, startGuess);

    const u32 periodForDiv = std::max<u32>(period, 1);
    const u32 periodsFor02s = static_cast<u32>(std::lround(0.2 * sampleRate / static_cast<double>(periodForDiv)));
    const u32 multiplier = std::max<u32>(4, periodsFor02s);
    const u32 targetLen = std::max(period * multiplier, period * 4);

    u32 le = ls + targetLen;

    if (n >= 2 && le >= n - 2)
        le = n - 2;

    if (period > 0 && le > ls)
    {
        const u32 rawLen = le - ls;
        const u32 periodsRounded = static_cast<u32>(std::lround(static_cast<double>(rawLen) / period));
        const u32 alignedLen = std::max(period, periodsRounded * period);
        le = ls + alignedLen;
    }

    le = refineEnd(mono, ls, le, period);

    result.ok = true;
    result.loopStart = ls;
    result.loopEnd = std::min(le, n > 0 ? n - 1 : 0);
    return result;
}

FrameSnapResult snapFramePeriod(u32 loopStart, u32 loopEnd, u32 period, u32 bufferSampleCount)
{
    u32 newStart = static_cast<u32>(std::lround(static_cast<double>(loopStart) / cAdpcmFrameSize)) * cAdpcmFrameSize;

    const u32 curLen = std::max(period > 0 ? period : 1u, (loopEnd > newStart) ? (loopEnd - newStart) : 1u);

    u32 bestLen;
    if (period > 0)
    {
        const u32 baseFrames = static_cast<u32>(std::lround(static_cast<double>(curLen) / cAdpcmFrameSize));
        const u32 fLo = (baseFrames > 8) ? (baseFrames - 8) : 1;
        const u32 fHi = baseFrames + 8;

        double bestErr = std::numeric_limits<double>::infinity();
        bestLen = fLo * cAdpcmFrameSize;
        for (u32 f = fLo; f <= fHi; f++)
        {
            const u32 len = f * cAdpcmFrameSize;
            const double periodsOff = std::abs(static_cast<double>(len) / period -
                                                std::round(static_cast<double>(len) / period));

            if (periodsOff < bestErr)
            {
                bestErr = periodsOff;
                bestLen = len;
            }
        }
    }
    else
    {
        bestLen = static_cast<u32>(std::lround(static_cast<double>(curLen) / cAdpcmFrameSize)) * cAdpcmFrameSize;
    }

    u32 newEnd = newStart + bestLen;

    if (bufferSampleCount > 0 && newEnd > bufferSampleCount - 1)
        newEnd = ((bufferSampleCount - 1) / cAdpcmFrameSize) * cAdpcmFrameSize;

    return { newStart, newEnd };
}

}
