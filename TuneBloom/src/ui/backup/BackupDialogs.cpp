#include <ui/backup/BackupInternal.h>

#include <ui/widgets/Dialogs.h>
#include <ui/file/File.h>
#include <ui/items/ItemColumns.h>
#include <ui/Messages.h>
#include <ui/file/SystemShell.h>
#include <ui/settings/Theme.h>
#include <ui/file/UnsavedChanges.h>
#include <ui/widgets/TimeUtil.h>
#include <ui/widgets/Widgets.h>

#include <libs/IconsLucide.h>
#include <imgui/imgui_internal.h>

#include <algorithm>
#include <cstdio>

namespace backup
{

static constexpr size_t cRecoverFolderMax = 48;

static constexpr f32 cRestoreWidthEm   = 36.0f;
static constexpr f32 cRestoreHeightEm  = 24.0f;
static constexpr f32 cRestoreRowPadEm  = 0.3f;
static constexpr f32 cRowHoverAlpha    = 0.5f;
static constexpr f32 cHeadGapEm        = 0.5f;
static constexpr f32 cFooterGapEm      = 0.6f;
static constexpr f32 cRecoverLabelEm   = 5.0f;
static constexpr f32 cRecoverRowGapEm  = 0.3f;

static constexpr ImVec4 cNoticeColor = cWarningTextColor;

static bool sWantsRestoreDialog = false;
static std::vector<Snapshot> sDialogSnapshots;
static s32 sDialogSelected = 0;
static bool sDialogConfirmDiscard = false;

static const char* GetKindLabel(Kind kind)
{
    switch (kind)
    {
    case Kind::Save:
        return messages::backup::cKindSave;

    case Kind::Restore:
        return messages::backup::cKindRestore;

    case Kind::Manual:
        return messages::backup::cKindManual;

    default:
        return messages::backup::cKindAuto;
    }
}

void FormatSize(u64 size, char* buffer, size_t bufferSize)
{
    const f64 kb = std::max(1.0, static_cast<f64>(size) / 1024.0);

    if (kb < 1024.0)
        std::snprintf(buffer, bufferSize, messages::backup::cSizeKbFormat, kb);
    else
        std::snprintf(buffer, bufferSize, messages::backup::cSizeMbFormat, kb / 1024.0);
}

static void DrawRestoreHead()
{
    const f32 em = ImGui::GetFontSize();

    ImGui::Text(messages::backup::cRestoreHeadFormat, GetArchiveName().c_str());
    ImGui::TextColored(GetItemValueColor(), "%s", messages::backup::cRestoreDetail);

    Bfsar::BackupResult failure;

    if (sDialogConfirmDiscard)
    {
        ImGui::TextColored(cNoticeColor, ICON_LC_TRIANGLE_ALERT " %s", messages::backup::cRestoreDiscards);
    }
    else if (GetFailure(&failure))
    {
        const char* notice = failure == Bfsar::BackupResult::Invalid ? messages::backup::cPausedInvalid : messages::backup::cPausedWrite;
        ImGui::TextColored(cNoticeColor, ICON_LC_TRIANGLE_ALERT " %s", notice);
    }

    ImGui::Dummy(ImVec2(0.0f, ImFloor(em * cHeadGapEm) - ImGui::GetStyle().ItemSpacing.y));
}

static bool DrawRestoreRows()
{
    const ImVec4 valueColor = GetItemValueColor();
    const s32 count = static_cast<s32>(sDialogSnapshots.size());
    bool restore = false;

    for (s32 i = 0; i < count; i++)
    {
        const Snapshot& snapshot = sDialogSnapshots[i];

        char date[48];
        timeutil::FormatDate(date, sizeof(date), snapshot.time);

        char size[32];
        FormatSize(snapshot.size, size, sizeof(size));

        ImGui::PushID(i);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();

        if (ImGui::Selectable(date, sDialogSelected == i, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_DontClosePopups | ImGuiSelectableFlags_AllowDoubleClick))
        {
            sDialogSelected = i;

            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                restore = true;
        }

        ImGui::TableNextColumn();
        ImGui::TextColored(valueColor, "%s", GetKindLabel(snapshot.kind));

        ImGui::TableNextColumn();
        ImGui::TextColored(valueColor, "%s", size);

        ImGui::PopID();
    }

    if (count == 0)
    {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s", messages::backup::cRestoreEmpty);
    }

    return restore;
}

static bool DrawRestoreTable()
{
    const f32 em = ImGui::GetFontSize();
    const f32 listHeight = -(CalcDialogFooterHeight() + ImFloor(em * cFooterGapEm));
    const ImVec2 cellPadding(ImGui::GetStyle().CellPadding.x, ImFloor(em * cRestoreRowPadEm));
    bool restore = false;

    const ImVec4 selectedColor = ImGui::GetStyleColorVec4(ImGuiCol_Header);
    ImVec4 hoverColor = selectedColor;
    hoverColor.w *= cRowHoverAlpha;

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, cellPadding);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, hoverColor);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, selectedColor);

    if (ImGui::BeginTable("##Backups", 3, ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerH, ImVec2(0.0f, listHeight)))
    {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn(messages::backup::cColumnDate, ImGuiTableColumnFlags_WidthStretch, 2.0f);
        ImGui::TableSetupColumn(messages::backup::cColumnType, ImGuiTableColumnFlags_WidthStretch, 1.5f);
        ImGui::TableSetupColumn(messages::backup::cColumnSize, ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableHeadersRow();

        restore = DrawRestoreRows();

        ImGui::EndTable();
    }

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();

    ImGui::Dummy(ImVec2(0.0f, ImFloor(em * cFooterGapEm) - ImGui::GetStyle().ItemSpacing.y));

    return restore;
}

static void DrawRestoreTotal()
{
    ImGui::AlignTextToFramePadding();

    if (sDialogSnapshots.empty())
        return;

    u64 totalSize = 0;

    for (const Snapshot& snapshot : sDialogSnapshots)
        totalSize += snapshot.size;

    char total[32];
    FormatSize(totalSize, total, sizeof(total));

    if (sDialogSnapshots.size() == 1)
        ImGui::TextDisabled(messages::backup::cCountOneFormat, total);
    else
        ImGui::TextDisabled(messages::backup::cCountFormat, sDialogSnapshots.size(), total);

    ImGui::SameLine();
}

static void DrawRestoreFooter(bool restore)
{
    const s32 count = static_cast<s32>(sDialogSnapshots.size());
    const bool hasSelection = sDialogSelected >= 0 && sDialogSelected < count;
    const char* restoreLabel = sDialogConfirmDiscard ? messages::backup::cRestoreAnyway : messages::backup::cRestore;

    BeginDialogFooter({restoreLabel, messages::recent::cShowInFileManager, messages::dialog::cCancel}, false);

    ImGui::BeginDisabled(!hasSelection);

    if (DialogButton(restoreLabel))
        restore = true;

    if (DialogButton(messages::recent::cShowInFileManager) && hasSelection)
        ShowInFileManager(sDialogSnapshots[sDialogSelected].dir);

    ImGui::EndDisabled();

    if (DialogButton(messages::dialog::cCancel))
        ImGui::CloseCurrentPopup();

    if (!restore || !hasSelection)
        return;

    const Snapshot snapshot = sDialogSnapshots[sDialogSelected];

    if (sDialogConfirmDiscard || SaveRestorePoint(snapshot))
    {
        ImGui::CloseCurrentPopup();
        RestoreSnapshot(snapshot);
    }
    else
    {
        sDialogConfirmDiscard = true;
    }
}

static void DrawRestoreDialog()
{
    const f32 em = ImGui::GetFontSize();

    if (sWantsRestoreDialog)
    {
        sWantsRestoreDialog = false;
        sDialogSnapshots = ListSnapshots();
        sDialogSelected = 0;
        sDialogConfirmDiscard = false;

        ImGui::OpenPopup("###BackupRestore");
    }

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(ImFloor(em * cRestoreWidthEm), ImFloor(em * cRestoreHeightEm)), ImGuiCond_Appearing);

    sead::FormatFixedSafeString<64> title("%s###BackupRestore", messages::backup::cMenuRestore);

    if (!BeginDialog(title.cstr(), nullptr, ImGuiWindowFlags_NoResize))
        return;

    DrawRestoreHead();

    const bool restore = DrawRestoreTable();

    DrawRestoreTotal();
    DrawRestoreFooter(restore);

    ImGui::EndPopup();
}

static void DrawRecoverRow(const char* label, const char* value)
{
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextColored(GetItemValueColor(), "%s", label);

    ImGui::TableNextColumn();
    ImGui::TextUnformatted(value);
}

static void DrawRecoverRows(const Snapshot& snapshot)
{
    const f32 em = ImGui::GetFontSize();
    const ImVec2 cellPadding(ImGui::GetStyle().CellPadding.x, ImFloor(em * cRecoverRowGapEm));

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, cellPadding);

    if (ImGui::BeginTable("##Recover", 2, ImGuiTableFlags_SizingFixedFit))
    {
        ImGui::TableSetupColumn("##Label", ImGuiTableColumnFlags_WidthFixed, ImFloor(em * cRecoverLabelEm));
        ImGui::TableSetupColumn("##Value", ImGuiTableColumnFlags_WidthFixed);

        char date[48];
        timeutil::FormatDate(date, sizeof(date), snapshot.time);

        char size[32];
        FormatSize(snapshot.size, size, sizeof(size));

        const sead::FormatFixedSafeString<96> backupText(messages::backup::cRecoverBackupFormat, date, size);
        const std::string folder = snapshot.originalPath.empty() ? std::string(messages::backup::cRecoverNoFolder)
                                                                 : fs::path(snapshot.originalPath).parent_path().string();
        const std::string shortFolder = CutPathStart(folder, cRecoverFolderMax);

        DrawRecoverRow(messages::backup::cRecoverArchive, snapshot.archiveName.c_str());
        DrawRecoverRow(messages::backup::cRecoverFolder, shortFolder.c_str());

        if (shortFolder != folder && IsItemHoveredForDetail())
            ImGui::SetTooltip("%s", folder.c_str());

        DrawRecoverRow(messages::backup::cRecoverBackup, backupText.cstr());

        ImGui::EndTable();
    }

    ImGui::PopStyleVar();
}

static void DrawRecoverDialog()
{
    const f32 em = ImGui::GetFontSize();

    const bool hasUnsavedArchive = sBfsar.isOpen() && gUnsavedChanges;

    if (HasRecovery() && !hasUnsavedArchive && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup))
        ImGui::OpenPopup("###BackupRecover");

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    sead::FormatFixedSafeString<64> title("%s###BackupRecover", messages::backup::cRecoverTitle);

    if (!BeginDialog(title.cstr(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    if (!HasRecovery())
    {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    const Recovery recovery = GetNextRecovery();
    const Snapshot& snapshot = recovery.snapshot;

    ImGui::Text(ICON_LC_TRIANGLE_ALERT " %s", messages::backup::cRecoverDetail);

    ImGui::Dummy(ImVec2(0.0f, ImFloor(em * cHeadGapEm) - ImGui::GetStyle().ItemSpacing.y));

    DrawRecoverRows(snapshot);

    BeginDialogFooter({messages::backup::cRecover, messages::backup::cDiscard, messages::backup::cNotNow});

    if (DialogButton(messages::backup::cRecover))
    {
        DropNextRecovery();
        ImGui::CloseCurrentPopup();

        if (RestoreSnapshot(snapshot))
            RemoveRecoveryMarker(recovery);
    }

    if (DialogButton(messages::backup::cDiscard))
    {
        DropNextRecovery();
        ImGui::CloseCurrentPopup();

        DiscardRecovery(recovery);
    }

    if (DialogButton(messages::backup::cNotNow))
    {
        DropNextRecovery();
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void OpenRestoreDialog()
{
    sWantsRestoreDialog = true;
}

void DrawDialogs()
{
    DrawRestoreDialog();
    DrawRecoverDialog();
}

}
