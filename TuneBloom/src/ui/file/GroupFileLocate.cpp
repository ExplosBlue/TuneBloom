#include <ui/file/GroupFileLocate.h>

#include <ui/backup/Backup.h>
#include <ui/widgets/Dialogs.h>
#include <ui/file/File.h>
#include <ui/file/FileDialogs.h>
#include <ui/file/FileFlow.h>
#include <ui/items/ItemColumns.h>
#include <ui/Messages.h>
#include <ui/widgets/Properties.h>
#include <ui/settings/Theme.h>
#include <ui/widgets/Widgets.h>

#include <bfsar/model/Group.h>

#include <libs/IconsLucide.h>
#include <imgui/imgui_internal.h>

#include <algorithm>
#include <string>
#include <vector>

static constexpr f32 cDialogWidthEm = 38.0f;
static constexpr f32 cStateWidthEm  = 1.3f;
static constexpr f32 cGroupWeight   = 1.0f;
static constexpr f32 cFileWeight    = 1.3f;
static constexpr s32 cListRowMin    = 3;
static constexpr s32 cListRowMax    = 10;

static constexpr const char* cPopupId = "###LocateGroupFiles";

namespace
{

struct LocateRow
{
    std::string groupName;
    std::string label;
    std::string path;
    Bfsar::GroupFileCheck check = Bfsar::GroupFileCheck::Ok;
};

}

static std::vector<LocateRow> sRows;
static bool sWantsOpen = false;
static bool sIsOpen = false;
static bool sIsReloading = false;
static s32 sReloadFrame = 0;
static bool sRemember = true;

static bool IsReady(const LocateRow& row)
{
    return !row.path.empty() && row.check == Bfsar::GroupFileCheck::Ok;
}

static void SetRowFile(LocateRow* row, const sead::SafeString& path)
{
    row->path = path.cstr();
    row->check = sBfsar.checkGroupFile(path);
}

void OpenGroupFileLocate()
{
    sRows.clear();

    for (Item* item : sBfsar.getGroupList())
    {
        const Group* group = static_cast<const Group*>(item);

        if (group->getExternalFileState() == Group::ExternalFileState::Missing)
            sRows.push_back({ group->getName().cstr(), group->getFormattedName().cstr() });
    }

    sWantsOpen = !sRows.empty();
    sIsReloading = false;
}

bool IsGroupFileLocatePending()
{
    return sWantsOpen || sIsOpen || sIsReloading;
}

static void BrowseRowFile(LocateRow* row)
{
    const char* extension = GetInnerFileExtension(sBfsar.getFormat(), InnerFileKind::Group);

    const sead::FormatFixedSafeString<64> filterName(messages::archive::cGroupFileFilterFormat, extension);
    const sead::FormatFixedSafeString<32> filterPattern("*.%s", extension);
    const sead::FormatFixedSafeString<512> title(messages::group::cLocateBrowseTitleFormat, row->label.c_str());

    FileFilter filter(filterName.cstr(), filterPattern.cstr());
    sead::FixedSafeString<512> path;

    if (OpenFileDialog(&path, title.cstr(), 1, &filter))
        SetRowFile(row, path);
}

static void SearchFolder()
{
    sead::FixedSafeString<512> folder;

    if (!SelectFolderDialog(&folder, messages::group::cLocateSearchFolderTitle))
        return;

    for (LocateRow& row : sRows)
    {
        if (IsReady(row))
            continue;

        const std::string path = sBfsar.findGroupFileInFolder(sead::SafeString(row.groupName.c_str()), folder);

        if (!path.empty())
            SetRowFile(&row, sead::SafeString(path.c_str()));
    }
}

static void LoadFiles()
{
    for (const LocateRow& row : sRows)
    {
        if (IsReady(row))
            sBfsar.setGroupFilePath(sead::SafeString(row.groupName.c_str()), sead::SafeString(row.path.c_str()), sRemember);
    }

    const sead::FixedSafeString<512> archivePath(sBfsar.getFilePath());

    RequestOpenFile(archivePath.cstr());

    sIsReloading = true;
    sReloadFrame = ImGui::GetFrameCount();
}

static void DrawHeader()
{
    const u32 rowNum = static_cast<u32>(sRows.size());
    sead::FixedSafeString<128> facts(messages::group::cLocateHeadOne);

    if (rowNum != 1)
        facts.format(messages::group::cLocateHeadManyFormat, rowNum);

    DrawPageHeader(ICON_LC_FOLDERS, backup::GetArchiveName().c_str(), facts.cstr());
}

static const char* GetCheckText(Bfsar::GroupFileCheck check)
{
    switch (check)
    {
        case Bfsar::GroupFileCheck::Unreadable:   return messages::group::cLocateUnreadable;
        case Bfsar::GroupFileCheck::NotGroupFile: return messages::reader::cNotGroupFile;
        case Bfsar::GroupFileCheck::OtherArchive: return messages::group::cLocateOtherArchive;
        default:                                  return "";
    }
}

static void DrawRowState(const LocateRow& row)
{
    ImGui::AlignTextToFramePadding();

    if (IsReady(row))
        ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_CheckMark), "%s", ICON_LC_CIRCLE_CHECK);
    else if (row.path.empty())
        ImGui::TextDisabled("%s", ICON_LC_CIRCLE_DASHED);
    else
        ImGui::TextColored(cWarningTextColor, "%s", ICON_LC_TRIANGLE_ALERT);
}

static void DrawRowFile(const LocateRow& row)
{
    const ImVec2 size(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight());

    if (row.path.empty())
        DrawDialogListText(messages::group::cLocateNotSelected, ImGui::GetColorU32(ImGuiCol_TextDisabled));
    else if (IsReady(row))
        DrawDialogListText(row.path.c_str(), ImGui::GetColorU32(GetItemValueColor()), false, true);
    else
        DrawDialogListText(GetCheckText(row.check), ImGui::GetColorU32(cWarningTextColor));

    ImGui::Dummy(size);

    if (!row.path.empty())
        ImGui::SetItemTooltip("%s", row.path.c_str());
}

static void DrawRow(LocateRow* row)
{
    ImGui::TableNextRow();

    ImGui::TableSetColumnIndex(0);
    DrawRowState(*row);

    ImGui::TableSetColumnIndex(1);
    DrawDialogListText(row->label.c_str(), ImGui::GetColorU32(ImGuiCol_Text));

    ImGui::TableSetColumnIndex(2);
    DrawRowFile(*row);

    ImGui::TableSetColumnIndex(3);

    if (ImGui::Button(messages::group::cLocateBrowse))
        BrowseRowFile(row);
}

static void DrawList()
{
    bool searchPressed = false;

    if (!BeginGroupBox(messages::common::cGroupFiles, messages::group::cLocateSearchFolder, &searchPressed))
        return;

    DetailTooltip(messages::group::cLocateSearchFolderDetail);

    if (searchPressed)
        SearchFolder();

    const char* const titles[] = { "##State", messages::common::cGroup, messages::common::cFile, "##Browse" };
    const ImGuiStyle& style = ImGui::GetStyle();
    const f32 browseWidth = ImGui::CalcTextSize(messages::group::cLocateBrowse).x + style.FramePadding.x * 2.0f;
    const s32 rowNum = ImClamp(static_cast<s32>(sRows.size()), cListRowMin, cListRowMax);

    if (BeginDialogList("##GroupFiles", IM_ARRAYSIZE(titles), rowNum))
    {
        ImGui::TableSetupColumn(titles[0], ImGuiTableColumnFlags_WidthFixed, ImFloor(ImGui::GetFontSize() * cStateWidthEm));
        ImGui::TableSetupColumn(titles[1], ImGuiTableColumnFlags_WidthStretch, cGroupWeight);
        ImGui::TableSetupColumn(titles[2], ImGuiTableColumnFlags_WidthStretch, cFileWeight);
        ImGui::TableSetupColumn(titles[3], ImGuiTableColumnFlags_WidthFixed, browseWidth);

        DrawDialogListTitles(titles, IM_ARRAYSIZE(titles), false);

        for (LocateRow& row : sRows)
        {
            ImGui::PushID(&row);
            DrawRow(&row);
            ImGui::PopID();
        }

        EndDialogList();
    }

    EndGroupBox();
}

void DrawGroupFileLocateDialog()
{
    if (sIsReloading && ImGui::GetFrameCount() > sReloadFrame + 1 && !IsOpenPending())
        sIsReloading = false;

    if (sWantsOpen && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
    {
        ImGui::OpenPopup(cPopupId);
        sWantsOpen = false;
    }

    CenterNextDialog();
    ImGui::SetNextWindowContentSize(ImVec2(ImFloor(ImGui::GetFontSize() * cDialogWidthEm), 0.0f));

    sIsOpen = BeginDialog(sead::FormatFixedSafeString<96>(ICON_LC_FILE_SEARCH " %s%s", messages::menu::cLocateGroupFiles, cPopupId).cstr(), nullptr, ImGuiWindowFlags_AlwaysAutoResize);

    if (!sIsOpen)
        return;

    DrawHeader();
    DrawList();

    const bool canLoad = std::any_of(sRows.begin(), sRows.end(), IsReady);

    ImGui::Checkbox(messages::group::cLocateRemember, &sRemember);
    DetailTooltip(messages::group::cLocateRememberDetail);
    ImGui::SameLine();

    BeginDialogFooter({messages::group::cLocateLoad, messages::group::cLocateSkip}, false);

    ImGui::BeginDisabled(!canLoad);
    const bool load = DialogButton(messages::group::cLocateLoad);
    ImGui::EndDisabled();

    if (!canLoad)
        SetDisabledTooltip(messages::group::cLocateLoadDisabled);

    const bool skip = DialogButton(messages::group::cLocateSkip);
    DetailTooltip(messages::group::cLocateSkipDetail);

    if (load)
        LoadFiles();

    if (load || skip)
    {
        sRows.clear();
        sIsOpen = false;
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}
