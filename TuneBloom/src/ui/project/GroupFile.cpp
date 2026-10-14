#include <ui/project/ItemViews.h>

#include <ui/widgets/ContextMenu.h>
#include <ui/file/File.h>
#include <ui/file/FileDialogs.h>
#include <ui/file/GroupFileLocate.h>
#include <ui/items/ItemColumns.h>
#include <ui/Messages.h>
#include <ui/app/Notifications.h>
#include <ui/widgets/Properties.h>
#include <ui/file/SystemShell.h>
#include <ui/settings/UiColors.h>
#include <ui/widgets/Widgets.h>

#include <libs/IconsLucide.h>

#include <algorithm>
#include <filesystem>
#include <string>

static constexpr size_t cPathTextMin = 8;

static Group* sExportGroup = nullptr;

static void DrawShowFileButton(const std::string& path)
{
    const f32 size = ImGui::GetFrameHeight();

    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - size));

    if (ImGui::Button(ICON_LC_FOLDER_OPEN "###ShowGroupFile", ImVec2(size, size)))
        ShowInFileManager(path);

    ImGui::SetItemTooltip("%s", messages::recent::cShowInFileManager);
}

void DrawGroupFileRow(const Group* group)
{
    PropertyRow(messages::group::cGroupFileLabel);

    if (group->getExternalFileState() == Group::ExternalFileState::Missing)
    {
        ImGui::TextColored(uicolors::Warning(), "%s", messages::group::cGroupFileNotLoaded);
        return;
    }

    const bool isOnDisk = group->getExternalFileState() == Group::ExternalFileState::Loaded;
    const std::string path = sBfsar.getGroupFileSavePath(group);

    const f32 buttonWidth = isOnDisk ? ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x : 0.0f;
    const f32 textWidth = ImGui::GetContentRegionAvail().x - buttonWidth;
    const size_t textMax = std::max(cPathTextMin, static_cast<size_t>(std::max(0.0f, textWidth) / ImGui::CalcTextSize("0").x));
    const std::string text = CutPathStart(path, textMax);

    ImGui::PushStyleColor(ImGuiCol_Text, GetItemValueColor());
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopStyleColor();

    if (text != path)
        ImGui::SetItemTooltip("%s", path.c_str());

    if (isOnDisk)
        DrawShowFileButton(path);

    const u32 extraFileCount = sBfsar.getGroupExtraFileCount(group);

    if (extraFileCount == 0)
        return;

    PropertyRow(messages::group::cExtraFilesLabel);
    DetailTooltip(messages::group::cExtraFilesDetail);

    ImGui::TextColored(GetItemValueColor(), "%u", extraFileCount);
    DetailTooltip(messages::group::cExtraFilesDetail);
}

void DrawGroupContextMenu(Item* item)
{
    if (!item || item->getItemType() != Item::ItemType::Group)
        return;

    Group* group = static_cast<Group*>(item);

    if (group->getOutputType() != Group::OutputType::External)
        return;

    ContextMenuSeparator();

    if (group->getExternalFileState() == Group::ExternalFileState::Missing)
    {
        if (ContextMenuItem(ICON_LC_FILE_SEARCH, messages::menu::cLocateGroupFiles))
            OpenGroupFileLocate();
    }
    else if (ContextMenuItem(ICON_LC_FILE_OUTPUT, messages::menu::cExportGroupFile))
    {
        sExportGroup = group;
    }
}

static void ExportGroupFile(Group* group)
{
    const char* extension = GetInnerFileExtension(sBfsar.getFormat(), InnerFileKind::Group);
    const sead::FormatFixedSafeString<64> filterName(messages::archive::cGroupFileFilterFormat, extension);
    const std::string fileName = std::filesystem::path(sBfsar.getGroupFileSavePath(group)).filename().string();
    sead::FixedSafeString<512> path;

    if (!SaveFileDialogWithExtension(&path, filterName.cstr(), extension, fileName.c_str()) || !sBfsar.exportGroupFile(group, path))
        return;

    const std::string exportedName = std::filesystem::path(path.cstr()).filename().string();

    notify::Add(notify::Kind::Success, messages::group::cGroupFileExportedTitle, exportedName.c_str());
}

void NotifyDeletedGroupFiles()
{
    for (const std::string& path : sBfsar.takeDeletedGroupFiles())
    {
        const std::string fileName = std::filesystem::path(path).filename().string();

        notify::Add(notify::Kind::Info, messages::group::cGroupFileDeletedTitle, fileName.c_str());
    }
}

void HandleGroupFileRequests()
{
    Group* exportGroup = sExportGroup;

    sExportGroup = nullptr;

    if (exportGroup)
        ExportGroupFile(exportGroup);
}
