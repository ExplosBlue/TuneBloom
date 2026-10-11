#include <ui/items/ItemList.h>

#include <ui/widgets/Dialogs.h>
#include <ui/file/File.h>
#include <ui/items/ItemListInternal.h>
#include <ui/Messages.h>
#include <ui/widgets/Properties.h>
#include <ui/app/Shortcuts.h>

#include <libs/IconsLucide.h>

bool Item::validateName(sead::BufferedSafeString& error) const
{
    if (!sBfsar.validName(getName()))
    {
        error = messages::item::cNameInvalidDetail;
        return false;
    }

    if (!sBfsar.validateName(*this))
    {
        error = messages::item::cNameDuplicated;
        return false;
    }

    return true;
}

bool Item::validateWaveArchive(WaveArchiveType type, const ItemReference& ref, sead::BufferedSafeString& error)
{
    switch (type)
    {
        case WaveArchiveType::AutomaticShared:
        case WaveArchiveType::AutomaticIndividual:
            return true;

        case WaveArchiveType::Explicit:
            if (ref.isAttached())
                return true;

            break;

        default:
            break;
    }

    error = messages::validation::cInvalidWaveArchive;
    return false;
}

void WarningPopup(const char* name, const char* content)
{
    SEAD_ASSERT(name);

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (BeginDialog(sead::FormatFixedSafeString<32>(ICON_LC_ALERT_TRIANGLE " %s%s", messages::common::cWarning, name).cstr(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("%s", content);

        BeginDialogFooter({messages::dialog::cOk});

        if (DialogButton(messages::dialog::cOk))
        {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void ApplyRenameShortcut()
{
    if (shortcuts::Pressed(shortcuts::Action::RenameSelection) && !ImGui::IsAnyItemActive())
        ImGui::SetKeyboardFocusHere();
}

void DrawItemNameRow(Item* item)
{
    PropertyRow(messages::common::cName);

    sead::FixedSafeString<256> shown(item->isEnableName() ? item->getName().cstr() : "");
    sead::FixedSafeString<256> name(shown);

    ApplyRenameShortcut();

    if (!ImGui::InputTextWithHint("##Name", messages::innerfile::cNoNameHint, name.getBuffer(), name.getBufferSize(), ImGuiInputTextFlags_EnterReturnsTrue) && !ImGui::IsItemDeactivatedAfterEdit())
        return;

    RenameItem(item, name);
}

void DrawItemPropertiesUI(bool labelLeft)
{
    Item* item = sSelectedItem;

    bool enableName = item->isEnableName();

    if (!enableName)
        ImGui::BeginDisabled();

    if (labelLeft)
        PropertyRow(messages::common::cName);

    sead::FixedSafeString<256> name(item->getName());
    ApplyRenameShortcut();
    if (ImGui::InputText(labelLeft ? "##Name" : messages::common::cName,name.getBuffer(), name.getBufferSize(), ImGuiInputTextFlags_EnterReturnsTrue) || ImGui::IsItemDeactivatedAfterEdit())
        OpenRenamePopup(RenameItem(item, name));

    if (!enableName)
        ImGui::EndDisabled();

    DrawRenamePopups();
}
