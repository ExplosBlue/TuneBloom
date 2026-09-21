#include <ui/Shortcuts.h>

#include <imgui/imgui_internal.h>

namespace
{

using Action = shortcuts::Action;
using Modifier = shortcuts::Modifier;

struct ActionBinding
{
    Action action;
    ImGuiKeyChord chord = ImGuiKey_None;
    bool repeats = false;
};

struct ModifierBinding
{
    Modifier modifier;
    ImGuiKey key = ImGuiKey_None;
};

constexpr ActionBinding cActionBindings[] = {
    { Action::NewProject,        ImGuiMod_Shortcut | ImGuiMod_Shift | ImGuiKey_N },
    { Action::OpenProject,       ImGuiMod_Shortcut | ImGuiKey_O },
    { Action::SaveProject,       ImGuiMod_Shortcut | ImGuiKey_S },
    { Action::SaveProjectAs,     ImGuiMod_Shortcut | ImGuiMod_Shift | ImGuiKey_S },
    { Action::CloseFileWindow,   ImGuiMod_Shortcut | ImGuiKey_W },

    { Action::SelectAll,         ImGuiMod_Shortcut | ImGuiKey_A },
    { Action::SelectPrevious,    ImGuiKey_UpArrow,   true },
    { Action::SelectNext,        ImGuiKey_DownArrow, true },
    { Action::SelectPageUp,      ImGuiKey_PageUp,    true },
    { Action::SelectPageDown,    ImGuiKey_PageDown,  true },
    { Action::SelectFirst,       ImGuiKey_Home },
    { Action::SelectLast,        ImGuiKey_End },
    { Action::ScrollToSelection, ImGuiKey_G },
    { Action::DeleteSelection,   ImGuiKey_Delete },
    { Action::RenameSelection,   ImGuiKey_F2 },

    { Action::FocusFilter,       ImGuiMod_Shortcut | ImGuiKey_F },
    { Action::CloseFilter,       ImGuiKey_Escape },

    { Action::ZoomIn,            ImGuiMod_Shortcut | ImGuiKey_Equal, true },
    { Action::ZoomOut,           ImGuiMod_Shortcut | ImGuiKey_Minus, true },

    { Action::OctaveDown,        ImGuiKey_LeftArrow,  true },
    { Action::OctaveUp,          ImGuiKey_RightArrow, true },
    { Action::OctaveReset,       ImGuiKey_Backspace },
};

constexpr ModifierBinding cModifierBindings[] = {
    { Modifier::ToggleSelection, ImGuiMod_Shortcut },
    { Modifier::ExtendSelection, ImGuiMod_Shift },
    { Modifier::ZoomHorizontal,  ImGuiMod_Shortcut },
    { Modifier::ZoomVertical,    ImGuiMod_Alt },
    { Modifier::PanHorizontal,   ImGuiMod_Shift },
};

constexpr ImGuiKeyChord cCommandModifiers = ImGuiMod_Ctrl | ImGuiMod_Alt | ImGuiMod_Super;

static_assert(IM_ARRAYSIZE(cActionBindings) == static_cast<s32>(Action::OctaveReset) + 1);
static_assert(IM_ARRAYSIZE(cModifierBindings) == static_cast<s32>(Modifier::PanHorizontal) + 1);

ImGuiKey sClaimedKeys[IM_ARRAYSIZE(cActionBindings)] = {};
s32 sClaimedCount = 0;
s32 sClaimedFrame = -1;

ImGuiKey BaseKey(ImGuiKeyChord chord)
{
    return static_cast<ImGuiKey>(chord & ~ImGuiMod_Mask_);
}

void ReleaseClaimedKeys()
{
    const s32 frame = ImGui::GetFrameCount();
    if (sClaimedFrame == frame)
        return;

    sClaimedFrame = frame;

    s32 stillDown = 0;
    for (s32 i = 0; i < sClaimedCount; i++)
    {
        if (ImGui::IsKeyDown(sClaimedKeys[i]))
            sClaimedKeys[stillDown++] = sClaimedKeys[i];
    }

    sClaimedCount = stillDown;
}

void ClaimKey(ImGuiKey key)
{
    if (key == ImGuiKey_None)
        return;

    for (s32 i = 0; i < sClaimedCount; i++)
    {
        if (sClaimedKeys[i] == key)
            return;
    }

    if (sClaimedCount < IM_ARRAYSIZE(sClaimedKeys))
        sClaimedKeys[sClaimedCount++] = key;
}

const ActionBinding& FindActionBinding(Action action)
{
    const ActionBinding& binding = cActionBindings[static_cast<s32>(action)];
    IM_ASSERT(binding.action == action);
    return binding;
}

const ModifierBinding& FindModifierBinding(Modifier modifier)
{
    const ModifierBinding& binding = cModifierBindings[static_cast<s32>(modifier)];
    IM_ASSERT(binding.modifier == modifier);
    return binding;
}

}

namespace shortcuts
{

ImGuiKeyChord Chord(Action action)
{
    return FindActionBinding(action).chord;
}

const char* Label(Action action)
{
    return ImGui::GetKeyChordName(Chord(action));
}

bool Pressed(Action action)
{
    ReleaseClaimedKeys();

    const ActionBinding& binding = FindActionBinding(action);
    const ImGuiInputFlags flags = binding.repeats ? ImGuiInputFlags_Repeat : ImGuiInputFlags_None;

    if (!ImGui::IsKeyChordPressed(binding.chord, ImGuiKeyOwner_Any, flags))
        return false;

    ClaimKey(BaseKey(binding.chord));
    return true;
}

bool Held(Modifier modifier)
{
    return ImGui::IsKeyDown(FindModifierBinding(modifier).key);
}

bool Claimed(ImGuiKey key)
{
    if (ImGui::GetIO().KeyMods & cCommandModifiers)
        return true;

    ReleaseClaimedKeys();

    for (s32 i = 0; i < sClaimedCount; i++)
    {
        if (sClaimedKeys[i] == key)
            return true;
    }

    return false;
}

}
