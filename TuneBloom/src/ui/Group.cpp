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
}

static const char* sItemIdTypes[] = {
    //"None",
    "Sound",
    "Sound Set",
    "Bank",
    "Wave Archive"
};

void Group::ItemInfo::drawUI()
{
    const ImU32 cStepU32 = 1;

    {
        u32 itemRefType = (u32)mItemRefType - 1;
        if (ImGui::Combo("Item Type", (s32*)&itemRefType, sItemIdTypes, IM_ARRAYSIZE(sItemIdTypes)))
        {
            mItemRefType = static_cast<ItemType>(itemRefType + 1);
            mItemRef.detach();
        }
    }

    {
        Item* item = mItemRef.getItem();
        if (ItemSelector("Item", sBfsar.getItemList(mItemRefType), &item, false))
        {
            mItemRef.attach(item);
        }
    }

    {
        u32 loadFlag = mLoadFlag;
        //if (ImGui::InputScalar("Load Flags", ImGuiDataType_U32, &loadFlag, &cStepU32))
        //{
        //    mLoadFlag = loadFlag;
        //}

        // TODO: Use Combo for flags

        CenteredTextX("Load Flags");

        if (ImGui::CheckboxFlagsT<u32>("Sequence", &loadFlag, LoadFlag::LoadSeq))
        {
            mLoadFlag = loadFlag;
        }

        ImGui::SameLine();

        if (ImGui::CheckboxFlagsT<u32>("Wave Sound", &loadFlag, LoadFlag::LoadWsd))
        {
            mLoadFlag = loadFlag;
        }

        ImGui::SameLine();

        if (ImGui::CheckboxFlagsT<u32>("Bank", &loadFlag, LoadFlag::LoadBank))
        {
            mLoadFlag = loadFlag;
        }

        ImGui::SameLine();

        if (ImGui::CheckboxFlagsT<u32>("Wave Archive", &loadFlag, LoadFlag::LoadWarc))
        {
            mLoadFlag = loadFlag;
        }
    }
}
