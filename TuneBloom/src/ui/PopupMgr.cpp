#include <ui/PopupMgr.h>

#include <ui/UI.h>

SEAD_SINGLETON_DISPOSER_IMPL(PopupMgr);

PopupMgr::PopupMgr()
    : mPopups()
    , mPopupOpen(false)
{
}

void PopupMgr::addPopup(const PopupInfo& info)
{
    mPopups.pushBack(info);
}

void PopupMgr::update()
{
    if (mPopups.size() == 0)
    {
        return;
    }

    const PopupInfo& info = mPopups.front();

    static const char* sPopupName = "###PopupMgr";

    if (!mPopupOpen)
    {
        ImGui::OpenPopup(sPopupName);
        mPopupOpen = true;
    }

    {
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

        if (ImGui::BeginPopupModal(sead::FormatFixedSafeString<32>(ICON_LC_ALERT_TRIANGLE " Warning%s", sPopupName).cstr(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImVec2 buttonSize((ImGui::GetWindowContentRegionMax().x - ImGui::GetStyle().WindowPadding.x * 2.0f) / 2.0f, 0.0f);

            Item* item = info.item;
            if (item)
            {
                ImGui::Text("Item '%s' is invalid:", item->getFormattedName().cstr());
                ImGui::Separator();
            }
            else
            {
                buttonSize = ImVec2(ImGui::GetWindowContentRegionMax().x - ImGui::GetStyle().WindowPadding.x, 0);
            }

            ImGui::Text(info.text.cstr());
            ImGui::Separator();

            if (ImGui::Button("OK", buttonSize))
            {
                PopupInfo dummy;
                mPopups.popFront(&dummy);

                mPopupOpen = false;

                ImGui::CloseCurrentPopup();
            }

            if (item)
            {
                ImGui::SameLine();

                if (ImGui::Button(ICON_LC_EXTERNAL_LINK " Go To", buttonSize))
                {
                    PopupInfo dummy;
                    mPopups.popFront(&dummy);

                    mPopupOpen = false;

                    SelectItem(item);

                    ImGui::CloseCurrentPopup();
                }
            }

            ImGui::EndPopup();
        }
    }
}
