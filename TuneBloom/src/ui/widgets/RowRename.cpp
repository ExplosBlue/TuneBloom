#include <ui/widgets/RowRename.h>

#include <ui/app/Shortcuts.h>

static constexpr s32 cRenameFocusFrames = 4;

static void* sRenameRow = nullptr;
static ImGuiID sRenameTable = 0;
static ImGuiID sRenameField = 0;
static sead::FixedSafeString<256> sRenameText;
static s32 sRenameFrame = 0;
static s32 sRenameFocusFrame = 0;
static bool sRenameFocus = false;
static bool sRenameActive = false;

void BeginRowRename(void* row, ImGuiID table, const char* text, ImGuiID field)
{
    sRenameRow = row;
    sRenameTable = table;
    sRenameField = field;
    sRenameText = text;
    sRenameFrame = ImGui::GetFrameCount();
    sRenameFocus = true;
    sRenameActive = false;
}

void* GetRenamedRow(ImGuiID table, ImGuiID field)
{
    return sRenameTable == table && sRenameField == field ? sRenameRow : nullptr;
}

void EndRowRenameOf(const void* row)
{
    if (sRenameRow == row)
        sRenameRow = nullptr;
}

void EndStaleRowRename()
{
    if (sRenameRow && ImGui::GetFrameCount() - sRenameFrame > 1)
        sRenameRow = nullptr;
}

bool DrawRowRenameField(const char* hint, sead::FixedSafeString<256>* outText, ImGuiInputTextCallback charFilter)
{
    const s32 frame = ImGui::GetFrameCount();

    sRenameFrame = frame;

    if (sRenameFocus && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        ImGui::SetKeyboardFocusHere();
        sRenameFocus = false;
        sRenameFocusFrame = frame;
    }

    const bool cancel = shortcuts::Pressed(shortcuts::Action::CancelEdit);
    const ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll | (charFilter ? ImGuiInputTextFlags_CallbackCharFilter : 0);

    ImGui::InputTextWithHint("##Rename", hint, sRenameText.getBuffer(), sRenameText.getBufferSize(), flags, charFilter);

    if (ImGui::IsItemActive())
    {
        sRenameActive = true;
        return false;
    }

    if (!sRenameActive)
    {
        if (!sRenameFocus && frame - sRenameFocusFrame > cRenameFocusFrames)
            sRenameRow = nullptr;

        return false;
    }

    sRenameRow = nullptr;

    if (cancel)
        return false;

    *outText = sRenameText;

    return true;
}
