#pragma once

#include <basis/seadTypes.h>

#include <imgui/imgui.h>

/*
The MIT License (MIT)
by https://github.com/frowrik
Piano keyboard for ImGui v1.1
*/

enum ImGuiPianoKeyboardMsg
{
    NoteGetStatus,
    NoteOn,
    NoteOff,
    NoteContext,
};

using ImGuiPianoKeyboardProc = bool (*)(void* UserData, s32 Msg, s32 Key, f32 Vel);

f32 GetPianoRulerHeight();
void ImGui_PianoKeyboard(const char* idName, ImVec2 size, s32* prevNoteActive, s32 beginNote, s32 endNote, ImGuiPianoKeyboardProc callback, void* userData, s32 rootKey = -1);
