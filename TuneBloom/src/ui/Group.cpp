#include <ui/UI.h>

// Groups

InstanciateItemCallback CreateGroupFunc(bool clear)
{
    return CreateItemFunc(clear, []() -> Item* { return new Group(); }, nullptr);
}

void DrawGroupsUI()
{
    DrawAllItemsUI("Group", sBfsar.getGroupList(), &CreateGroupFunc);
}

void DrawGroupPropertiesUI()
{
    Group* group = static_cast<Group*>(sSelectedItem);

    {
        static const char* sOutputTypes[] = {
            "Embed",
            "Link",
            // "External"
        };

        u32 outputType = (u32)group->getOutputType();
        if (ImGui::Combo("Output Type", (s32*)&outputType, sOutputTypes, IM_ARRAYSIZE(sOutputTypes)))
        {
            group->setOutputType(static_cast<Group::OutputType>(outputType));
        }
    }

    // TODO: Move to a separate window
    if (ImGui::BeginChild("ChildItems", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border))
    {
        DrawAllItemsUI("Item", group->getItemInfoList());
    }
    ImGui::EndChild();
}
