#include <ui/items/ItemList.h>

#include <ui/widgets/Dialogs.h>
#include <ui/file/File.h>
#include <messages/Messages.h>
#include <ui/file/UnsavedChanges.h>

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <cctype>

static sead::FixedSafeString<256> sName;
static bool sEnableName = true;
static InstanciateItemCallback sInstanciateItemCallback = nullptr;
static ItemPropertiesCallback sItemPropertiesCallback = nullptr;
static void (*sNextHead)() = nullptr;
static void (*sNextNameTail)() = nullptr;
static f32 sNextNameTailWidth = 0.0f;

void SetNextCreateItemHead(void (*draw)())
{
    sNextHead = draw;
}

void SetNextCreateItemNameTail(void (*draw)(), f32 width)
{
    sNextNameTail = draw;
    sNextNameTailWidth = width;
}

const sead::SafeString& GetCreateItemName()
{
    return sName;
}

void SuggestCreateItemName(const sead::SafeString& name)
{
    if (!sName.isEmpty())
        return;

    sName.copy(name, sead::Mathi::min(name.calcLength(), sName.getBufferSize() - 2));

    for (s32 i = 0; i < sName.calcLength(); i++)
    {
        char& c = sName.getBuffer()[i];

        if (!::isalnum(static_cast<u8>(c)))
            c = '_';
    }

    if (!::isdigit(static_cast<u8>(sName.cstr()[0])))
        return;

    const sead::FixedSafeString<256> digitName(sName);
    sName.format("_%s", digitName.cstr());
}

static Item* CreateNamedItem()
{
    SEAD_ASSERT(sInstanciateItemCallback);

    if (sName.isEmpty())
    {
        ImGui::OpenPopup("###EmptyName");
        return nullptr;
    }

    if (!IsValidDocumentName(sName))
    {
        ImGui::OpenPopup("###InvalidName");
        return nullptr;
    }

    if (!IsUniqueDocumentName(sName, nullptr))
    {
        ImGui::OpenPopup("###Dupe");
        return nullptr;
    }

    if (sItemPropertiesCallback)
    {
        bool validate = true;
        sItemPropertiesCallback(false, nullptr, &validate);

        if (!validate)
            return nullptr;
    }

    Item* item = sInstanciateItemCallback();

    if (!item)
        return nullptr;

    item->setEnableName(sEnableName);
    if (sEnableName)
    {
        item->getName() = sName;
        SetUnsavedChanges(true);
    }

    if (sItemPropertiesCallback)
        sItemPropertiesCallback(false, item, nullptr);

    return item;
}

InstanciateItemCallback CreateItemFunc(bool clear, InstanciateItemCallback instanciateItemCallback, ItemPropertiesCallback itemPropertiesCallback)
{
    SEAD_ASSERT(instanciateItemCallback);

    sInstanciateItemCallback = instanciateItemCallback;
    sItemPropertiesCallback = itemPropertiesCallback;

    if (clear)
    {
        sEnableName = true;
        sName.clear();
    }

    const f32 em = ImGui::GetFontSize();

    ImGuiWindow* window = ImGui::GetCurrentWindow();
    window->DC.CursorMaxPos.x = ImMax(window->DC.CursorMaxPos.x, window->DC.CursorPos.x + ImFloor(em * cDialogFormWidthEm));

    if (sNextHead)
        sNextHead();

    DialogRow(messages::common::cName);

    if (sNextNameTail)
        ImGui::SetNextItemWidth(-(sNextNameTailWidth + ImGui::GetStyle().ItemSpacing.x * 2.0f));

    if (!sEnableName)
        ImGui::BeginDisabled();

    ImGui::InputText("##Name", sName.getBuffer(), sName.getBufferSize());

    if (!sEnableName)
        ImGui::EndDisabled();

    if (sNextNameTail)
    {
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x * 2.0f);
        sNextNameTail();
    }

    sNextHead = nullptr;
    sNextNameTail = nullptr;

    WarningPopup("###InvalidName", GetDocumentNameRuleDetail());
    WarningPopup("###EmptyName", messages::item::cNameEmpty);
    DupeNamePopup();

    if (sItemPropertiesCallback)
        sItemPropertiesCallback(clear, nullptr, nullptr);

    return CreateNamedItem;
}
