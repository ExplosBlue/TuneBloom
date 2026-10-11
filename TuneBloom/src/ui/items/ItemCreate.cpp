#include <ui/items/ItemList.h>

#include <ui/widgets/Dialogs.h>
#include <ui/file/File.h>
#include <ui/Messages.h>
#include <ui/file/UnsavedChanges.h>

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <cctype>

static constexpr f32 cCreateDialogWidthEm = 24.0f;

static sead::FixedSafeString<256> sName;
static bool sEnableName = true;
static InstanciateItemCallback sInstanciateItemCallback = nullptr;
static ItemPropertiesCallback sItemPropertiesCallback = nullptr;

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

    if (!sBfsar.validName(sName))
    {
        ImGui::OpenPopup("###InvalidName");
        return nullptr;
    }

    if (!sBfsar.validateName(sName))
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

    ImGui::Dummy(ImVec2(ImFloor(em * cCreateDialogWidthEm), 0.0f));

    DialogRow(messages::item::cNameFieldLabel);

    if (!sEnableName)
        ImGui::BeginDisabled();

    ImGui::InputText("##Name", sName.getBuffer(), sName.getBufferSize());

    if (!sEnableName)
        ImGui::EndDisabled();

    WarningPopup("###InvalidName", messages::item::cNameInvalidDetail);
    WarningPopup("###EmptyName", messages::item::cNameEmpty);
    DupeNamePopup();

    if (sItemPropertiesCallback)
        sItemPropertiesCallback(clear, nullptr, nullptr);

    return CreateNamedItem;
}
