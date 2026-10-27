#include <ui/backup/Backup.h>

#include <ui/backup/BackupInternal.h>
#include <ui/settings/Config.h>
#include <ui/file/DocumentMgr.h>
#include <ui/file/DocumentState.h>
#include <ui/file/File.h>
#include <ui/Messages.h>
#include <ui/app/Notifications.h>
#include <ui/app/PopupMgr.h>
#include <core/TestHooks.h>
#include <ui/file/UnsavedChanges.h>
#include <ui/widgets/Widgets.h>

#include <chrono>
#include <vector>

namespace backup
{

using Clock = DocumentState::Backup::Clock;

static constexpr size_t cNoticePathMax = 60;

static constexpr s32 cRetrySeconds     = 20;
static constexpr s32 cIdleMilliseconds = 1000;

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

static DocumentState::Backup& GetState()
{
    return ActiveDocumentState().backup;
}

bool GetFailure(Bfsar::BackupResult* result)
{
    const DocumentState::Backup& state = GetState();
    *result = state.failure;

    return state.hasFailure;
}

static const char* GetFailureTitle(Bfsar::BackupResult result)
{
    return result == Bfsar::BackupResult::Invalid ? messages::backup::cNoticePausedTitle : messages::backup::cNoticeFailedTitle;
}

const char* GetFailureTitle()
{
    Bfsar::BackupResult failure;

    return GetFailure(&failure) ? GetFailureTitle(failure) : nullptr;
}

void MarkEditsBackedUp()
{
    DocumentState::Backup& state = GetState();
    state.hasLastBackup = true;
    state.lastBackupGeneration = GetEditGeneration();
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

static void ShowFailureNotice(DocumentState::Backup& state, Bfsar::BackupResult result)
{
    if (state.noticeShown == result)
        return;

    state.noticeShown = result;

    const char* title = GetFailureTitle(result);

    if (result == Bfsar::BackupResult::Invalid)
    {
        notify::Add(notify::Kind::Warning, title, messages::backup::cNoticePausedText);
        return;
    }

    const std::string folder = CutPathStart(GetRootDir().string(), cNoticePathMax);

    sead::FormatFixedSafeString<1024> msg(messages::backup::cNoticeFailedTextFormat, folder.c_str());
    notify::Add(notify::Kind::Error, title, msg.cstr());
}

static void UpdateDocument(Document& document)
{
    DocumentState::Backup& state = document.state().backup;

    if (!sAutoBackupEnabled || !IsActive() || !document.isLoaded() || !document.state().unsaved)
    {
        state.timerRunning = false;
        return;
    }

    const Clock::time_point now = Clock::now();

    if (!state.timerRunning)
    {
        state.timerRunning = true;
        state.timerStart = now;
        state.lastInput = now;
    }

    if (IsInputActive())
        state.lastInput = now;

    if (now - state.timerStart < GetInterval())
        return;

    const u64 generation = document.state().editGeneration;

    if (state.hasLastBackup && generation == state.lastBackupGeneration)
    {
        state.timerStart = now;
        return;
    }

    if (state.hasFailure)
    {
        if (now < state.retryAt)
            return;

        if (state.failure == Bfsar::BackupResult::Invalid && generation == state.failedGeneration)
            return;
    }

    if (ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup) || now - state.lastInput < std::chrono::milliseconds(cIdleMilliseconds))
        return;

    Bfsar::BackupResult result;
    bool written = false;

    {
        DocumentFocus focus(&document);
        written = WriteSnapshot(Kind::Auto, &result);
    }

    if (!written)
    {
        state.hasFailure = true;
        state.failure = result;
        state.failedGeneration = generation;
        state.retryAt = now + std::chrono::seconds(cRetrySeconds);

        ShowFailureNotice(state, result);
        return;
    }

    state.hasFailure = false;
    state.noticeShown = Bfsar::BackupResult::Ok;
    state.hasLastBackup = true;
    state.lastBackupGeneration = generation;
    state.timerStart = now;
}

void Update()
{
    if (!sScanned)
    {
        sScanned = true;

        if (IsActive())
            ScanRecoveries();
    }

    const std::vector<Document*> documents = GetOpenDocuments();

    for (Document* document : documents)
        UpdateDocument(*document);
}

void OnClean()
{
    DocumentState::Backup& state = GetState();
    state.timerRunning = false;
    state.hasFailure = false;
    state.hasLastBackup = false;
    state.noticeShown = Bfsar::BackupResult::Ok;

    if (!IsDocumentOpen())
        ForgetArchiveKeys();

    if (IsActive())
        RemoveMarker();
}

void OnDocumentClosed()
{
    if (IsActive())
        RemoveMarker();
}

bool CreateBackup()
{
    if (!IsActive() || !IsDocumentOpen())
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
    GetState().timerStart = Clock::now();

    return true;
}

}
