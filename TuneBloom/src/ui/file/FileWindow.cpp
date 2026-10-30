#include <ui/file/FileWindow.h>

#include <ui/file/DocumentState.h>
#include <messages/Messages.h>
#include <bfsar/model/MessageLog.h>
#include <ui/app/Shortcuts.h>

#include <bfsar/files/BankFile.h>
#include <bfsar/files/SequenceFile.h>

#include <libs/IconsLucide.h>

#include <climits>

static s32 sFileWindowFocusFrame = INT_MIN;
static bool sFocusRequest = false;

static ItemList& GetFileWindows()
{
    return ActiveDocumentState().fileWindows;
}

static FileWindow* FindFileWindow(const Item* file)
{
    for (Item* item : GetFileWindows())
    {
        FileWindow* window = static_cast<FileWindow*>(item);

        if (window->getFileRef().getItem() == file)
            return window;
    }

    return nullptr;
}

void UpdateFileWindows()
{
    DocumentState& state = ActiveDocumentState();
    ItemList& fileWindows = state.fileWindows;

    for (auto it = fileWindows.robustBegin(); it != fileWindows.robustEnd(); ++it)
    {
        FileWindow* window = static_cast<FileWindow*>(it->val());
        SEAD_ASSERT(window);

        if (window->isOpen() && window->getFileRef().isAttached())
            continue;

        if (state.shownFileWindow == window)
        {
            auto* neighbourNode = fileWindows.prev(window);
            if (!neighbourNode)
                neighbourNode = fileWindows.next(window);

            state.shownFileWindow = neighbourNode ? static_cast<FileWindow*>(neighbourNode->val()) : nullptr;
        }

        delete window;
    }
}

FileWindow* GetShownFileWindow()
{
    FileWindow* window = ActiveDocumentState().shownFileWindow;

    if (!window || !window->isOpen() || !window->getFileRef().isAttached())
        return nullptr;

    return window;
}

void ShowFileWindow(FileWindow* window)
{
    ActiveDocumentState().shownFileWindow = window;
    sFocusRequest = window != nullptr;
}

const char* GetFileWindowIcon(const FileWindow& window)
{
    const Item* file = window.getFileRef().getItem();

    if (!file)
        return ICON_LC_FILE;

    switch (file->getItemType())
    {
        case Item::ItemType::BankFile:
            return ICON_LC_KEYBOARD_MUSIC;

        case Item::ItemType::SequenceFile:
            return ICON_LC_FILE_MUSIC;

        default:
            return ICON_LC_FILE;
    }
}

bool IsFileWindowDirty(const FileWindow& window)
{
    const Item* file = window.getFileRef().getItem();

    if (!file || file->getItemType() != Item::ItemType::SequenceFile)
        return false;

    return static_cast<const SequenceFile*>(file)->isDirty();
}

void RequestCloseFileWindow(FileWindow* window)
{
    if (IsFileWindowDirty(*window))
    {
        MessageLog::instance()->addPopup({ messages::sequence::cCompileFirst });
        return;
    }

    *window->getOpenPtr() = false;
}

static void DrawFileUI(Item* file)
{
    switch (file->getItemType())
    {
        case Item::ItemType::BankFile:
            static_cast<BankFile*>(file)->drawFileUI();
            break;

        case Item::ItemType::SequenceFile:
            static_cast<SequenceFile*>(file)->drawFileUI();
            break;

        default:
            break;
    }
}

bool DrawShownFileWindow()
{
    FileWindow* window = GetShownFileWindow();

    if (!window)
        return false;

    Item* file = window->getFileRef().getItem();

    if (ImGui::BeginChild(ImGui::GetID(file), ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding))
    {
        if (sFocusRequest)
        {
            ImGui::SetWindowFocus();
            sFocusRequest = false;
        }

        const bool isFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);

        if (isFocused)
            sFileWindowFocusFrame = ImGui::GetFrameCount();

        if (isFocused && shortcuts::Pressed(shortcuts::Action::CloseFileWindow))
            RequestCloseFileWindow(window);

        DrawFileUI(file);
    }

    ImGui::EndChild();

    return true;
}

bool IsFileWindowFocused()
{
    return sFileWindowFocusFrame >= ImGui::GetFrameCount() - 1;
}

FileWindow* OpenFileWindow(Item* item)
{
    FileWindow* window = FindFileWindow(item);
    const bool isNew = window == nullptr;

    if (isNew)
    {
        window = new FileWindow(item);
        GetFileWindows().pushBack(window);
    }

    *window->getOpenPtr() = true;
    ShowFileWindow(window);

    return isNew ? window : nullptr;
}
