#pragma once

#include <basis/seadTypes.h>
#include <imgui/imgui.h>

namespace shortcuts
{

enum class Action : s32
{
    NewProject = 0,
    OpenProject = 1,
    SaveProject = 2,
    SaveProjectAs = 3,
    CloseFileWindow = 4,

    SelectAll = 5,
    SelectPrevious = 6,
    SelectNext = 7,
    SelectPageUp = 8,
    SelectPageDown = 9,
    SelectFirst = 10,
    SelectLast = 11,
    ScrollToSelection = 12,
    DeleteSelection = 13,
    RenameSelection = 14,

    FocusFilter = 15,
    CloseFilter = 16,

    ZoomIn = 17,
    ZoomOut = 18,

    OctaveDown = 19,
    OctaveUp = 20,
    OctaveReset = 21,
};

enum class Modifier : s32
{
    ToggleSelection = 0,
    ExtendSelection = 1,
    ZoomHorizontal = 2,
    ZoomVertical = 3,
    PanHorizontal = 4,
};

ImGuiKeyChord Chord(Action action);
const char* Label(Action action);
bool Pressed(Action action);
bool Held(Modifier modifier);
bool Claimed(ImGuiKey key);

}
