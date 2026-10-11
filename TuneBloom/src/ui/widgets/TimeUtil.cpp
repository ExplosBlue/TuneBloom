#include <ui/widgets/TimeUtil.h>

#include <ui/Messages.h>

#include <cmath>
#include <cstdio>
#include <ctime>

namespace timeutil
{

double SamplesToSeconds(u32 samples, u32 sampleRate)
{
    return sampleRate != 0 ? static_cast<double>(samples) / static_cast<double>(sampleRate) : 0.0;
}

void FormatClock(char* buffer, size_t bufferSize, double seconds)
{
    if (seconds < 0.0)
        seconds = 0.0;

    const u32 minutes = static_cast<u32>(seconds) / 60;
    const u32 secs = static_cast<u32>(seconds) % 60;
    const u32 millis = static_cast<u32>(seconds * 1000.0) % 1000;

    snprintf(buffer, bufferSize, "%02u:%02u.%03u", minutes, secs, millis);
}

void FormatTimeLabel(char* buffer, size_t bufferSize, double seconds, double majorStepSeconds)
{
    const int decimals = majorStepSeconds < 0.001 ? 4 : majorStepSeconds < 0.01 ? 3 : 2;

    if (seconds >= 60.0)
    {
        const int minutes = static_cast<int>(seconds / 60.0);
        snprintf(buffer, bufferSize, "%d:%0*.*f", minutes, decimals + 3, decimals, seconds - minutes * 60.0);
    }
    else
    {
        snprintf(buffer, bufferSize, "%.*f", decimals, seconds);
    }
}

static bool sTwelveHourClock = false;

void SetTwelveHourClock(bool twelveHour)
{
    sTwelveHourClock = twelveHour;
}

static void FormatClockTime(char* buffer, size_t bufferSize, const std::tm& day, bool twelveHour)
{
    if (!twelveHour)
    {
        snprintf(buffer, bufferSize, "%02d:%02d", day.tm_hour, day.tm_min);
        return;
    }

    const int hour = day.tm_hour % 12 == 0 ? 12 : day.tm_hour % 12;
    snprintf(buffer, bufferSize, day.tm_hour < 12 ? messages::date::cAmFormat : messages::date::cPmFormat, hour, day.tm_min);
}

static bool GetLocalDay(int64_t time, std::tm* day)
{
    const std::time_t stamp = static_cast<std::time_t>(time);

    const std::tm* local = std::localtime(&stamp);
    if (!local)
        return false;

    *day = *local;
    return true;
}

static int CalcDaysAgo(const std::tm& day, const std::tm& today)
{
    std::tm dayStart = day;
    dayStart.tm_hour = dayStart.tm_min = dayStart.tm_sec = 0;

    std::tm todayStart = today;
    todayStart.tm_hour = todayStart.tm_min = todayStart.tm_sec = 0;

    const double days = std::difftime(std::mktime(&todayStart), std::mktime(&dayStart)) / (60.0 * 60.0 * 24.0);

    return static_cast<int>(std::floor(days + 0.5));
}

static void FormatCount(char* buffer, size_t bufferSize, int count, const char* one, const char* manyFormat)
{
    if (count == 1)
        snprintf(buffer, bufferSize, "%s", one);
    else
        snprintf(buffer, bufferSize, manyFormat, count);
}

void FormatTimeOfDay(char* buffer, size_t bufferSize, int64_t time, bool twelveHour)
{
    std::tm day;
    if (!GetLocalDay(time, &day))
    {
        snprintf(buffer, bufferSize, "%s", messages::date::cUnknown);
        return;
    }

    FormatClockTime(buffer, bufferSize, day, twelveHour);
}

void FormatTimeOfDay(char* buffer, size_t bufferSize, int64_t time)
{
    FormatTimeOfDay(buffer, bufferSize, time, sTwelveHourClock);
}

void FormatDate(char* buffer, size_t bufferSize, int64_t time)
{
    std::tm day;
    std::tm today;

    if (!GetLocalDay(time, &day) || !GetLocalDay(static_cast<int64_t>(std::time(nullptr)), &today))
    {
        snprintf(buffer, bufferSize, "%s", messages::date::cUnknown);
        return;
    }

    char clock[16];
    FormatClockTime(clock, sizeof(clock), day, sTwelveHourClock);

    const int days = CalcDaysAgo(day, today);
    const char* month = messages::date::cMonths[day.tm_mon];

    if (days == 0)
        snprintf(buffer, bufferSize, messages::date::cTodayFormat, clock);
    else if (days == 1)
        snprintf(buffer, bufferSize, messages::date::cYesterdayFormat, clock);
    else if (day.tm_year == today.tm_year)
        snprintf(buffer, bufferSize, messages::date::cThisYearFormat, month, day.tm_mday, clock);
    else
        snprintf(buffer, bufferSize, messages::date::cOtherYearFormat, month, day.tm_mday, day.tm_year + 1900);
}

void FormatFullDate(char* buffer, size_t bufferSize, int64_t time)
{
    std::tm day;
    if (!GetLocalDay(time, &day))
    {
        snprintf(buffer, bufferSize, "%s", messages::date::cUnknown);
        return;
    }

    char clock[16];
    FormatClockTime(clock, sizeof(clock), day, sTwelveHourClock);

    snprintf(buffer, bufferSize, messages::date::cFullFormat, messages::date::cMonths[day.tm_mon], day.tm_mday, day.tm_year + 1900, clock);
}

void FormatRelativeDate(char* buffer, size_t bufferSize, int64_t time)
{
    const int64_t now = static_cast<int64_t>(std::time(nullptr));

    std::tm day;
    std::tm today;

    if (!GetLocalDay(time, &day) || !GetLocalDay(now, &today))
    {
        snprintf(buffer, bufferSize, "%s", messages::date::cUnknown);
        return;
    }

    const int64_t seconds = now - time;
    const int days = CalcDaysAgo(day, today);

    if (seconds < 60)
        snprintf(buffer, bufferSize, "%s", messages::date::cJustNow);
    else if (days <= 0 && seconds < 60 * 60)
        FormatCount(buffer, bufferSize, static_cast<int>(seconds / 60), messages::date::cMinuteAgo, messages::date::cMinutesAgoFormat);
    else if (days <= 0)
        FormatCount(buffer, bufferSize, static_cast<int>(seconds / (60 * 60)), messages::date::cHourAgo, messages::date::cHoursAgoFormat);
    else if (days < 30)
        FormatCount(buffer, bufferSize, days, messages::date::cYesterday, messages::date::cDaysAgoFormat);
    else if (days < 365)
        FormatCount(buffer, bufferSize, days / 30, messages::date::cMonthAgo, messages::date::cMonthsAgoFormat);
    else
        FormatCount(buffer, bufferSize, days / 365, messages::date::cYearAgo, messages::date::cYearsAgoFormat);
}

}
