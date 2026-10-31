#include <ui/export/ExportInternal.h>

#include <ui/items/ItemColumns.h>
#include <ui/project/References.h>
#include <ui/widgets/Dialogs.h>
#include <ui/widgets/Properties.h>
#include <ui/widgets/Widgets.h>
#include <messages/Messages.h>

#include <imgui/imgui_internal.h>

static bool sIdInName = false;

bool IsExportIdInName()
{
    return sIdInName;
}

void DrawExportIdCheck(const char* exampleName, const char* extension)
{
    ImGui::Checkbox("##IncludeId", &sIdInName);
    DetailTooltip(sead::FormatFixedSafeString<288>(messages::exporting::cIdInNamesFormat, exampleName, extension).cstr());
}

static void DrawBatchExportFormatBox(const ExportFileType& type, const char* exampleName)
{
    if (!BeginGroupBox(messages::common::cFormat))
        return;

    const f32 labelWidth = ImFloor(ImGui::GetFontSize() * cExportLabelWidthEm);

    GroupBoxRowLabel(messages::common::cFileType, labelWidth);
    ImGui::TextColored(GetItemValueColor(), "%s", type.filterName);

    GroupBoxRowLabel(messages::exporting::cIdInNames, labelWidth);
    DrawExportIdCheck(exampleName, type.extension);

    EndGroupBox();
}

BatchExportStep DrawBatchExportDialog(const ExportFileType& type, const Item* firstItem, s32 count, const char* exampleName)
{
    const sead::FormatFixedSafeString<64> popupName("%s###ExportFiles", messages::exporting::cFilesTitle);

    ImGui::OpenPopup(popupName.cstr());

    CenterNextDialog();
    ImGui::SetNextWindowContentSize(ImVec2(ImFloor(ImGui::GetFontSize() * cExportDialogWidthEm), 0.0f));

    bool popupOpen = true;
    if (!BeginDialog(popupName.cstr(), &popupOpen, ImGuiWindowFlags_AlwaysAutoResize))
        return BatchExportStep::Cancel;

    const sead::FormatFixedSafeString<64> headTitle(messages::item::cCountAllFormat, FormatCountText(count).cstr(), GetItemTypeName(firstItem->getItemType()));
    DrawPageHeader(GetItemIcon(firstItem), headTitle.cstr(), nullptr);

    DrawBatchExportFormatBox(type, exampleName);

    DrawShowExportedFilesCheck();
    ImGui::SameLine();

    BeginDialogFooter({messages::common::cExport, messages::common::cCancel}, false);

    BatchExportStep step = BatchExportStep::Open;

    if (DialogButton(messages::common::cExport))
        step = BatchExportStep::Export;

    ImGui::SetItemDefaultFocus();

    if (DialogButton(messages::common::cCancel))
        step = BatchExportStep::Cancel;

    if (step != BatchExportStep::Open)
        ImGui::CloseCurrentPopup();

    ImGui::EndPopup();

    return step;
}
