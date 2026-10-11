#include <ui/backup/Backup.h>

#include <ui/backup/BackupInternal.h>
#include <ui/settings/Config.h>
#include <ui/file/File.h>
#include <ui/Messages.h>
#include <ui/app/Notifications.h>
#include <ui/app/PopupMgr.h>
#include <core/TestHooks.h>
#include <ui/file/UnsavedChanges.h>
#include <ui/widgets/Widgets.h>

#include <chrono>

namespace backup
{

using Clock = std::chrono::steady_clock;

static constexpr size_t cNoticePathMax = 60;

static constexpr s32 cRetrySeconds     = 20;
static constexpr s32 cIdleMilliseconds = 1000;

static bool sTimerRunning = false;
static Clock::time_point sTimerStart;
static Clock::time_point sLastInput;
static Clock::time_point sRetryAt;

static bool sHasLastBackup = false;
static u64 sLastBackupGeneration = 0;
static bool sHasFailure = false;
static u64 sFailedGeneration = 0;
static Bfsar::BackupResult sFailure = Bfsar::BackupResult::Ok;
static Bfsar::BackupResult sNoticeShown = Bfsar::BackupResult::Ok;

static bool sScanned = false;

bool IsActive()
{
    return !testhooks::IsEnabled() || testhooks::IsBackupOn();
}

static Clock::duration GetInterval()
{
    if (testhooks::IsEnabled())
    {
        const s32 seconds = testhooks::GetBackupSeconds();

        if (seconds > 0)
            return std::chrono::seconds(seconds);
    }

    return std::chrono::minutes(sAutoBackupIntervalMinutes);
}

bool GetFailure(Bfsar::BackupResult* result)
{
    *result = sFailure;

    return sHasFailure;
}

void MarkEditsBackedUp()
{
    sHasLastBackup = true;
    sLastBackupGeneration = GetEditGeneration();
}

static bool IsInputActive()
{
    if (ImGui::IsAnyMouseDown())
        return true;

    for (s32 key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END; key++)
    {
        if (ImGui::IsKeyDown(static_cast<ImGuiKey>(key)))
            return true;
    }

    return false;
}

static void ShowFailureNotice(Bfsar::BackupResult result)
{
    if (sNoticeShown == result)
        return;

    sNoticeShown = result;

    if (result == Bfsar::BackupResult::Invalid)
    {
        notify::Add(notify::Kind::Warning, messages::backup::cNoticePausedTitle, messages::backup::cNoticePausedText);
        return;
    }

    const std::string folder = CutPathStart(GetRootDir().string(), cNoticePathMax);

    sead::FormatFixedSafeString<1024> msg(messages::backup::cNoticeFailedTextFormat, folder.c_str());
    notify::Add(notify::Kind::Error, messages::backup::cNoticeFailedTitle, msg.cstr());
}

void Update()
{
    if (!sScanned)
    {
        sScanned = true;

        if (IsActive())
            ScanRecoveries();
    }

    if (!sAutoBackupEnabled || !IsActive() || !sBfsar.isOpen() || !gUnsavedChanges)
    {
        sTimerRunning = false;
        return;
    }

    const Clock::time_point now = Clock::now();

    if (!sTimerRunning)
    {
        sTimerRunning = true;
        sTimerStart = now;
        sLastInput = now;
    }

    if (IsInputActive())
        sLastInput = now;

    if (now - sTimerStart < GetInterval())
        return;

    const u64 generation = GetEditGeneration();

    if (sHasLastBackup && generation == sLastBackupGeneration)
    {
        sTimerStart = now;
        return;
    }

    if (sHasFailure)
    {
        if (now < sRetryAt)
            return;

        if (sFailure == Bfsar::BackupResult::Invalid && generation == sFailedGeneration)
            return;
    }

    if (ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup) || now - sLastInput < std::chrono::milliseconds(cIdleMilliseconds))
        return;

    Bfsar::BackupResult result;

    if (!WriteSnapshot(Kind::Auto, &result))
    {
        sHasFailure = true;
        sFailure = result;
        sFailedGeneration = generation;
        sRetryAt = now + std::chrono::seconds(cRetrySeconds);

        ShowFailureNotice(result);
        return;
    }

    sHasFailure = false;
    sNoticeShown = Bfsar::BackupResult::Ok;
    sHasLastBackup = true;
    sLastBackupGeneration = generation;
    sTimerStart = now;
}

void OnClean()
{
    sTimerRunning = false;
    sHasFailure = false;
    sHasLastBackup = false;
    sNoticeShown = Bfsar::BackupResult::Ok;

    if (!sBfsar.isOpen())
        ForgetArchiveKeys();

    if (IsActive())
        RemoveMarker();
}

bool CreateBackup()
{
    if (!IsActive() || !sBfsar.isOpen())
        return false;

    Bfsar::BackupResult result;

    if (!WriteSnapshot(Kind::Manual, &result))
    {
        if (result == Bfsar::BackupResult::Invalid)
        {
            PopupMgr::instance()->addPopup({messages::backup::cManualInvalidDetail, nullptr});
            return false;
        }

        const std::string folder = CutPathStart(GetRootDir().string(), cNoticePathMax);

        sead::FormatFixedSafeString<1024> msg(messages::backup::cManualFailedFormat, folder.c_str());
        PopupMgr::instance()->addPopup({msg, nullptr});

        return false;
    }

    MarkEditsBackedUp();
    sTimerStart = Clock::now();

    return true;
}

}
