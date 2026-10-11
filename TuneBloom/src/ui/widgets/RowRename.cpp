#include <ui/widgets/RowRename.h>

static constexpr s32 cRenameFocusFrames = 4;

static void* sRenameRow = nullptr;
static ImGuiID sRenameTable = 0;
static sead::FixedSafeString<256> sRenameText;
static s32 sRenameFrame = 0;
static s32 sRenameFocusFrame = 0;
static bool sRenameFocus = false;
static bool sRenameActive = false;

void BeginRowRename(void* row, ImGuiID table, const char* text)
{
    sRenameRow = row;
    sRenameTable = table;
    sRenameText = text;
    sRenameFrame = ImGui::GetFrameCount();
    sRenameFocus = true;
    sRenameActive = false;
}

void* GetRenamedRow(ImGuiID table)
{
    return sRenameTable == table ? sRenameRow : nullptr;
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

bool DrawRowRenameField(const char* hint, sead::FixedSafeString<256>* outText)
{
    const s32 frame = ImGui::GetFrameCount();

    sRenameFrame = frame;

    if (sRenameFocus && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        ImGui::SetKeyboardFocusHere();
        sRenameFocus = false;
        sRenameFocusFrame = frame;
    }

    const bool cancel = ImGui::IsKeyPressed(ImGuiKey_Escape);

    ImGui::InputTextWithHint("##Rename", hint, sRenameText.getBuffer(), sRenameText.getBufferSize(), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);

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
