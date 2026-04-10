#include <ui/PopupMgr.h>

#include <ui/UI.h>

SEAD_SINGLETON_DISPOSER_IMPL(PopupMgr);

PopupMgr::PopupMgr()
    : mPopups()
    , mPopupOpen(false)
    , mCorruptInfo()
    , mCurrentProcessItem(nullptr)
    , mProcessedErrors()
{
}

void PopupMgr::addPopup(const PopupInfo& info)
{
    mPopups.pushBack(info);
}

void PopupMgr::update()
{
    updateErrors_();

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
                const char* name = info.super ? info.super->getFormattedName().cstr() : item->getFormattedName().cstr();
                ImGui::Text("Item '%s' is invalid:", name);
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

                    if (info.super)
                    {
                        if (item->getItemType() == Item::ItemType::BankFileInstrument)
                        {
                            SelectItem(item);
                            OpenFileWindow(info.super);
                        }
                        else
                        {
                            SelectItem(info.super);
                            sSubSelectedItem = item;
                            sSelectedItemIsSubWindow = true;
                        }
                    }
                    else
                    {
                        SelectItem(item);
                    }

                    ImGui::CloseCurrentPopup();
                }
            }

            ImGui::EndPopup();
        }
    }
}

void PopupMgr::pushCurrentItemError(const sead::SafeString& error)
{
    if (!mCurrentProcessItem)
    {
        return;
    }

    const char* str = error.cstr();
    if (error.isEmpty())
    {
        str = "Unknown";
    }

    if (mProcessedErrors.contains(mCurrentProcessItem))
    {
        mProcessedErrors[mCurrentProcessItem].second.push_back(str);
    }
    else
    {
        mProcessedErrors.insert(mCurrentProcessItem, { mCurrentProcessItem, { str } });
    }
}

void PopupMgr::updateErrors_()
{
    if (mProcessedErrors.empty())
    {
        return;
    }

    static const char* sPopupName = "###PopupMgrErrors";

    if (!mPopupOpen)
    {
        ImGui::OpenPopup(sPopupName);
        mPopupOpen = true;
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal(sead::FormatFixedSafeString<64>(ICON_LC_ALERT_TRIANGLE " Errors with file%s", sPopupName).cstr()))
    {
        ImVec2 buttonSize((ImGui::GetWindowContentRegionMax().x - ImGui::GetStyle().WindowPadding.x * 1.0f) / 1.0f, 25.0f);
        f32 sizeSlack = -buttonSize.y - ImGui::GetStyle().WindowPadding.y / 2.0f;

        static size_t sSelected = 0;
        bool scroll = false;
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow))
        {
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
            {
                if (sSelected != 0)
                {
                    sSelected--;
                    scroll = true;
                }
            }
            
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
            {
                if (sSelected < mProcessedErrors.size() - 1)
                {
                    sSelected++;
                    scroll = true;
                }
            }
        }

        ImGui::Text("The following items contains errors that must be fixed before saving");

        // Left
        {
            ImGui::BeginChild("LeftPane", ImVec2(150, sizeSlack), ImGuiChildFlags_Border | ImGuiChildFlags_ResizeX);

            for (size_t i = 0; i < mProcessedErrors.size(); i++)
            {
                if (scroll && sSelected == i)
                {
                    ImGui::SetScrollHereY();
                }

                Item* item = mProcessedErrors.getVector()[i].first;
                if (ImGui::Selectable(item->getFormattedName().cstr(), sSelected == i))
                {
                    sSelected = i;
                }
            }

            ImGui::EndChild();
        }
        ImGui::SameLine();

        // Right
        {
            ImGui::BeginGroup();
            ImGui::BeginChild("TtemView", ImVec2(0, sizeSlack));

            ImGui::Text("Info");
            ImGui::Separator();
            
            const std::vector<std::string>& errors = mProcessedErrors.getVector()[sSelected].second;
            for (size_t i = 0; i < errors.size(); i++)
            {
                ImGui::Text(errors[i].c_str());

                if (i != errors.size() - 1)
                {
                    ImGui::Separator();
                }
            }

            ImGui::EndChild();
            ImGui::EndGroup();
        }

        ImGui::Separator();

        if (ImGui::Button("Oof", buttonSize))
        {
            mCurrentProcessItem = nullptr;
            mProcessedErrors.clear();
            mPopupOpen = false;
            sSelected = 0;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}
