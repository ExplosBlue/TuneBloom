#include <ui/file/FileWindow.h>

#include <ui/file/DocumentState.h>
#include <ui/Messages.h>
#include <ui/app/PopupMgr.h>
#include <ui/app/Shortcuts.h>

#include <bfsar/files/BankFile.h>
#include <bfsar/files/SequenceFile.h>

#include <libs/IconsLucide.h>
#include <imgui/imgui_internal.h>

static bool sFileWindowFocused = false;

static ItemList& GetFileWindows()
{
    return ActiveDocumentState().fileWindows;
}

void CloseAllFileWindows()
{
    GetFileWindows().clear();
}

void DrawFileWindows(ImGuiID dockspaceId)
{
    ItemList& fileWindows = GetFileWindows();

    for (auto it = fileWindows.robustBegin(); it != fileWindows.robustEnd(); ++it)
    {
        FileWindow* window = static_cast<FileWindow*>(it->val());
        SEAD_ASSERT(window);

        if (!window->isOpen() || !window->getFileRef().isAttached())
        {
            auto* neighbourNode = fileWindows.prev(window);
            if (!neighbourNode)
                neighbourNode = fileWindows.next(window);

            const Item* neighbourFile = neighbourNode ? static_cast<FileWindow*>(neighbourNode->val())->getFileRef().getItem() : nullptr;
            if (neighbourFile)
                ImGui::SetWindowFocus(sead::FormatFixedSafeString<512>(ICON_LC_FILE " %s###%p", neighbourFile->getFormattedName().cstr(), neighbourFile).cstr());

            delete window;
        }
    }

    bool anyFocused = false;

    for (Item* item : fileWindows)
    {
        FileWindow* window = static_cast<FileWindow*>(item);

        Item* file = window->getFileRef().getItem();

        ImGui::SetNextWindowDockID(dockspaceId, ImGuiCond_Appearing);

        bool dirty = false;
        ImGuiWindowFlags flags = ImGuiWindowFlags_None;
        if (file->getItemType() == Item::ItemType::SequenceFile)
        {
            SequenceFile* seq = static_cast<SequenceFile*>(file);

            dirty = seq->isDirty();
            flags |= dirty ? ImGuiWindowFlags_UnsavedDocument : 0;
        }

        bool visible = ImGui::Begin(sead::FormatFixedSafeString<512>(ICON_LC_FILE " %s###%p", file->getFormattedName().cstr(), file).cstr(), window->getOpenPtr(), flags);

        const bool focused = visible && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        anyFocused = anyFocused || focused;

        if (focused && shortcuts::Pressed(shortcuts::Action::CloseFileWindow) && !dirty)
            *window->getOpenPtr() = false;

        if (!window->isOpen() && dirty)
        {
            *window->getOpenPtr() = true;
            PopupMgr::instance()->addPopup({ messages::sequence::cCompileFirst });
        }

        if (visible)
        {
            switch (file->getItemType())
            {
                case Item::ItemType::BankFile:
                {
                    BankFile* bank = static_cast<BankFile*>(file);
                    bank->drawFileUI();
                    break;
                }

                case Item::ItemType::SequenceFile:
                {
                    SequenceFile* seq = static_cast<SequenceFile*>(file);
                    seq->drawFileUI();
                    break;
                }

                default:
                    break;
            }
        }
        ImGui::End();
    }

    sFileWindowFocused = anyFocused;
}

bool IsFileWindowFocused()
{
    return sFileWindowFocused;
}

FileWindow* OpenFileWindow(Item* item)
{
    sead::FormatFixedSafeString<512> windowName("###%p", item);

    ImGuiWindow* imguiWindow = ImGui::FindWindowByName(windowName.cstr());
    if (!imguiWindow || !imguiWindow->WasActive)
    {
        FileWindow* window = new FileWindow(item);

        GetFileWindows().pushBack(window);

        return window;
    }

    ImGui::SetWindowFocus(windowName.cstr());

    return nullptr;
}
