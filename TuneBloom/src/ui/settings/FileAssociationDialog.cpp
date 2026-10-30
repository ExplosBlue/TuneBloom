#include <ui/settings/FileAssociationDialog.h>

#include <messages/Messages.h>
#include <ui/file/FileAssociation.h>
#include <ui/items/ItemColumns.h>
#include <ui/widgets/Dialogs.h>
#include <ui/widgets/Widgets.h>

#include <libs/IconsLucide.h>
#include <imgui/imgui_internal.h>

#include <prim/seadSafeString.h>

#include <vector>

static constexpr const char* cPopupId        = "###FileAssociation";
static constexpr f32         cDialogWidthEm  = 36.0f;
static constexpr f32         cColumnPadEm    = 1.0f;

static std::vector<fileassoc::TypeState> sTypes;
static bool sChangeFailed = false;

void UpdateFileAssociations()
{
    sTypes.clear();

    for (u32 i = 0; i < fileassoc::GetTypeCount(); i++)
        sTypes.push_back(fileassoc::GetTypeState(i));
}

u32 GetFileAssociationOnCount()
{
    u32 count = 0;

    for (const fileassoc::TypeState& type : sTypes)
        count += type.isOn ? 1 : 0;

    return count;
}

u32 GetFileAssociationTypeCount()
{
    return static_cast<u32>(sTypes.size());
}

void OpenFileAssociationDialog()
{
    UpdateFileAssociations();
    sChangeFailed = false;
    ImGui::OpenPopup(cPopupId);
}

static void SetTypes(bool on, s32 onlyIndex)
{
    bool success = true;

    for (u32 i = 0; i < sTypes.size(); i++)
    {
        const fileassoc::TypeState& type = sTypes[i];

        if ((onlyIndex >= 0 && static_cast<u32>(onlyIndex) != i) || type.isOn == on || type.lock != fileassoc::Lock::None)
            continue;

        success &= fileassoc::SetTypeOn(i, on);
    }

    UpdateFileAssociations();
    sChangeFailed = !success;
}

static const char* GetLockDetail(fileassoc::Lock lock)
{
    switch (lock)
    {
        case fileassoc::Lock::OnlyApp:
            return messages::preferences::cAssociationOnlyApp;

        case fileassoc::Lock::None:
            break;
    }

    return nullptr;
}

static void DrawAllCheck()
{
    const u32 onCount = GetFileAssociationOnCount();
    bool all = onCount == sTypes.size();
    bool canChange = false;

    for (const fileassoc::TypeState& type : sTypes)
        canChange |= type.lock == fileassoc::Lock::None;

    ImGui::TableSetColumnIndex(0);

    const ImVec2 pos = ImGui::GetCursorPos();

    ImGui::TableHeader("##AllHeader");
    ImGui::SetCursorPos(pos);

    ImGui::BeginDisabled(!canChange);
    ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, onCount > 0 && !all);

    const bool pressed = ImGui::Checkbox("##All", &all);

    ImGui::PopItemFlag();
    ImGui::EndDisabled();

    if (pressed)
        SetTypes(all, -1);
}

static void DrawHeaderRow(const char* const* titles, s32 titleNum)
{
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableNextRow(ImGuiTableRowFlags_Headers, ImGui::GetFrameHeight());

    for (s32 i = 1; i < titleNum; i++)
    {
        if (!ImGui::TableSetColumnIndex(i))
            continue;

        ImGui::AlignTextToFramePadding();
        ImGui::TableHeader(titles[i]);
    }
}

static void DrawTypeRow(u32 index)
{
    const fileassoc::TypeState& type = sTypes[index];
    const char* lockDetail = GetLockDetail(type.lock);
    const ImU32 textColor = ImGui::GetColorU32(type.isOn ? ImGuiCol_Text : ImGuiCol_TextDisabled);
    const ImU32 valueColor = ImGui::GetColorU32(type.isOn ? GetItemValueColor() : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));

    bool on = type.isOn;

    ImGui::PushID(static_cast<s32>(index));
    ImGui::BeginDisabled(lockDetail != nullptr);

    const bool pressed = DrawDialogListCheck(&on);

    ImGui::EndDisabled();

    if (lockDetail && IsItemHoveredForDetail())
        ImGui::SetTooltip("%s", lockDetail);

    ImGui::TableSetColumnIndex(1);
    DrawDialogListText(sead::FormatFixedSafeString<16>(".%s", type.extension).cstr(), textColor);

    ImGui::TableSetColumnIndex(2);
    DrawDialogListText(type.typeName.c_str(), valueColor);

    ImGui::TableSetColumnIndex(3);
    DrawDialogListText(type.appName.empty() ? messages::common::cNone : type.appName.c_str(), type.appName.empty() ? ImGui::GetColorU32(ImGuiCol_TextDisabled) : valueColor);

    ImGui::PopID();

    if (pressed)
        SetTypes(on, static_cast<s32>(index));
}

static void DrawTypeList()
{
    const f32 em = ImGui::GetFontSize();
    const char* titles[] = { "##All", messages::common::cExtension, messages::common::cFileType, messages::preferences::cAssociationAppColumn };

    if (!BeginDialogList("##FileTypes", IM_ARRAYSIZE(titles), static_cast<s32>(sTypes.size()), true))
        return;

    ImGui::TableSetupColumn(titles[0], ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
    f32 typeWidth = ImGui::CalcTextSize(titles[2]).x;

    for (const fileassoc::TypeState& type : sTypes)
        typeWidth = ImMax(typeWidth, ImGui::CalcTextSize(type.typeName.c_str()).x);

    ImGui::TableSetupColumn(titles[1], ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize(titles[1]).x + ImFloor(em * cColumnPadEm));
    ImGui::TableSetupColumn(titles[2], ImGuiTableColumnFlags_WidthFixed, typeWidth + ImFloor(em * cColumnPadEm));
    ImGui::TableSetupColumn(titles[3], ImGuiTableColumnFlags_WidthStretch);

    DrawHeaderRow(titles, IM_ARRAYSIZE(titles));
    DrawAllCheck();

    for (u32 i = 0; i < sTypes.size(); i++)
        DrawTypeRow(i);

    EndDialogList();
}

void DrawFileAssociationDialog()
{
    CenterNextDialog();

    if (!BeginDialog(sead::FormatFixedSafeString<96>(ICON_LC_FILE_CHECK " %s%s", messages::preferences::cAssociationLabel, cPopupId).cstr(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::Dummy(ImVec2(ImFloor(ImGui::GetFontSize() * cDialogWidthEm), 0.0f));

    DrawTypeList();

    BeginDialogFooter({messages::common::cClose}, true, sChangeFailed ? messages::preferences::cAssociationFailed : nullptr);

    if (DialogButton(messages::common::cClose))
        ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}
