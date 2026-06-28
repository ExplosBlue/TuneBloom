/*
The MIT License (MIT)
by https://github.com/frowrik
Piano keyboard for ImGui v1.1
example:
static int PrevNoteActive = -1;
ImGui_PianoKeyboard("PianoTest", ImVec2(1024, 100), &PrevNoteActive, 21, 108, TestPianoBoardFunct, nullptr, nullptr);
bool TestPianoBoardFunct(void* UserData, int Msg, int Key, float Vel) {
		if (Key >= 128) return false; // midi max keys
		if (Msg == NoteGetStatus) return KeyPresed[Key];
		if (Msg == NoteOn) { KeyPresed[Key] = true; Send_Midi_NoteOn(Key, Vel*127); }
		if (Msg == NoteOff) { KeyPresed[Key] = false; Send_Midi_NoteOff(Key, Vel*127);}
		return false;
}
*/

#include <ui/UI.h>
#include <math/seadMathCalcCommon.h>

void ImGui_PianoKeyboard(const char* IDName, ImVec2 Size, s32* PrevNoteActive, s32 BeginOctaveNote, s32 EndOctaveNote, ImGuiPianoKeyboardProc Callback, void* UserData, ImGuiPianoStyles* Style, s32 OriginalKey)
{
    // const
    static const s32 NoteIsDark[12] = { 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0 };
    static const s32 NoteLightNumber[12] = { 1, 1, 2, 2, 3, 4, 4, 5, 5, 6, 6, 7 };
    static const f32 NoteDarkOffset[12] = { 0.0f,  -2.0f / 3.0f, 0.0f, -1.0f / 3.0f, 0.0f, 0.0f, -2.0f / 3.0f, 0.0f, -0.5f, 0.0f, -1.0f / 3.0f, 0.0f };

    // fix range dark keys
    if (NoteIsDark[BeginOctaveNote % 12] > 0)
    {
        BeginOctaveNote++;
    }

    if (NoteIsDark[EndOctaveNote % 12] > 0)
    {
        EndOctaveNote--;
    }

    // bad range
    if (!IDName || !Callback || BeginOctaveNote < 0 || EndOctaveNote < 0 || EndOctaveNote <= BeginOctaveNote)
    {
        return;
    }

    // style
    static ImGuiPianoStyles ColorsBase;
    if (!Style)
    {
        Style = &ColorsBase;
    }

    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
    {
        return;
    }

    const ImGuiID id = window->GetID(IDName);

    ImDrawList* draw_list = window->DrawList;

    ImVec2 Pos = window->DC.CursorPos;
    ImVec2 MousePos = ImGui::GetIO().MousePos;

    // sizes
    s32 CountNotes = EndOctaveNote - BeginOctaveNote + 1;

    f32 NoteHeight    = Size.y;
    f32 NoteWidth     = Size.x / (f32)CountNotes;

    f32 NoteHeight2   = NoteHeight * Style->NoteDarkHeight;
    f32 NoteWidth2    = NoteWidth * Style->NoteDarkWidth;

    // minimal size draw
    if (NoteHeight < 5.0 || NoteWidth < 3.0)
    {
        return;
    }

    // PC keyboard input — 2-octave DAW-style layout
    static s32 sOctaveBase = 48; // C3
    static bool sPrevKeyState[128] = { false };

    const struct { ImGuiKey key; s32 offset; } sKeyMappings[] = {
        // Lower octave (white: Z X C V B N M, black: S D G H J)
        { ImGuiKey_Z, 0 },  { ImGuiKey_S, 1 },  { ImGuiKey_X, 2 },  { ImGuiKey_D, 3 },
        { ImGuiKey_C, 4 },  { ImGuiKey_V, 5 },  { ImGuiKey_G, 6 },  { ImGuiKey_B, 7 },
        { ImGuiKey_H, 8 },  { ImGuiKey_N, 9 },  { ImGuiKey_J, 10 }, { ImGuiKey_M, 11 },
        // Upper octave (white: Q W E R T Y U, black: 2 3 5 6 7)
        { ImGuiKey_Q, 12 }, { ImGuiKey_2, 13 }, { ImGuiKey_W, 14 }, { ImGuiKey_3, 15 },
        { ImGuiKey_E, 16 }, { ImGuiKey_R, 17 }, { ImGuiKey_5, 18 }, { ImGuiKey_T, 19 },
        { ImGuiKey_6, 20 }, { ImGuiKey_Y, 21 }, { ImGuiKey_7, 22 }, { ImGuiKey_U, 23 },
    };

    // Note detection (uses current sOctaveBase, don't modify it here)
    if (!ImGui::GetIO().WantTextInput)
    {
        for (s32 i = 0; i < IM_ARRAYSIZE(sKeyMappings); i++)
        {
            bool isDown = ImGui::IsKeyDown(sKeyMappings[i].key);
            s32 note = sOctaveBase + sKeyMappings[i].offset;
            if (note < 0 || note >= 128)
                continue;
            if (isDown != sPrevKeyState[note])
            {
                sPrevKeyState[note] = isDown;
                Callback(UserData, isDown ? NoteOn : NoteOff, note, isDown ? 0.8f : 0.0f);
            }
        }
    }

    // Octave shift — arrows & buttons, with proper cleanup
    {
        s32 newBase = sOctaveBase;

        if (!ImGui::GetIO().WantTextInput)
        {
            if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))
                newBase -= 12;
            if (ImGui::IsKeyPressed(ImGuiKey_RightArrow))
                newBase += 12;
            if (ImGui::IsKeyPressed(ImGuiKey_Backspace))
                newBase = 48;
        }

        ImGui::Text("Octave %d (%s%d)", sOctaveBase / 12 - 1, "C", sOctaveBase / 12 - 1);

        s32 clamped = sead::MathCalcCommon<s32>::clamp2(0, newBase, 108);
        if (clamped != sOctaveBase)
        {
            sOctaveBase = clamped;
            for (s32 i = 0; i < 128; i++)
            {
                if (sPrevKeyState[i])
                {
                    sPrevKeyState[i] = false;
                    Callback(UserData, NoteOff, i, 0.0f);
                }
            }
        }
    }

    // minimal size using mouse
    bool isMouseInput = (NoteHeight >= 10.0 && NoteWidth >= 5.0);

    // item
    const ImRect bb(Pos, ImVec2(Pos.x + Size.x, Pos.y + Size.y));
    ImGui::ItemSize(Size, 0);
    if (!ImGui::ItemAdd(bb, id))
    {
        return;
    }

    // item input
    bool held = false;
    if (isMouseInput)
    {
        ImGui::ButtonBehavior(bb, id, nullptr, &held, 0);
    }

    s32 NoteMouseColision = -1;
    f32 NoteMouseVel = 0.0f;

    auto IsBlackKey = [&](s32 k) -> bool
    {
        return k >= BeginOctaveNote && k <= EndOctaveNote && NoteIsDark[k % 12] > 0;
    };

    for (s32 RealNum = BeginOctaveNote; RealNum <= EndOctaveNote; RealNum++)
    {
        s32 Octave = RealNum / 12;
        s32 i      = RealNum % 12;
        if (NoteIsDark[i] > 0)
            continue;

        f32 cellL = bb.Min.x + (RealNum - BeginOctaveNote) * NoteWidth;
        f32 cellR = cellL + NoteWidth;
        f32 L = IsBlackKey(RealNum - 1) ? cellL - NoteWidth * 0.5f : cellL;
        f32 R = IsBlackKey(RealNum + 1) ? cellR + NoteWidth * 0.5f : cellR;

        ImRect NoteRect(round(L), bb.Min.y, round(R), bb.Min.y + NoteHeight);

        if (held && NoteRect.Contains(MousePos))
        {
            NoteMouseColision = RealNum;
            NoteMouseVel      = (MousePos.y - NoteRect.Min.y) / NoteHeight;
        }

        bool isActive = Callback(UserData, NoteGetStatus, RealNum, 0.0f);
        u32 colIdx = isActive ? (RealNum == OriginalKey ? 7 : 2)
                              : (RealNum == OriginalKey ? 5 : 0);

        draw_list->AddRectFilled(NoteRect.Min, NoteRect.Max, Style->Colors[colIdx], 0.0f);
        draw_list->AddRect(NoteRect.Min, NoteRect.Max, Style->Colors[4], 0.0f);

        if (Octave > 0 && i == 0)
        {
            draw_list->AddText(
                ImVec2(cellL + NoteWidth / 2.0f - 3, bb.Min.y + 50),
                IM_COL32(0, 0, 255, 255),
                sead::FormatFixedSafeString<8>("%d", Octave - 1).cstr()
            );
        }
    }

    // Draw black keys
    for (s32 RealNum = BeginOctaveNote; RealNum <= EndOctaveNote; RealNum++)
    {
        s32 i = RealNum % 12;
        if (NoteIsDark[i] == 0)
            continue;

        f32 cellL = bb.Min.x + (RealNum - BeginOctaveNote) * NoteWidth;
        f32 bx0 = cellL + (NoteWidth - NoteWidth2) * 0.5f;
        ImRect NoteRect(round(bx0), bb.Min.y, round(bx0 + NoteWidth2), bb.Min.y + NoteHeight2);

        if (held && NoteRect.Contains(MousePos))
        {
            NoteMouseColision = RealNum;
            NoteMouseVel      = (MousePos.y - NoteRect.Min.y) / NoteHeight2;
        }

        bool isActive = Callback(UserData, NoteGetStatus, RealNum, 0.0f);
        u32 colIdx = isActive ? (RealNum == OriginalKey ? 8 : 3)
                              : (RealNum == OriginalKey ? 6 : 1);

        draw_list->AddRectFilled(NoteRect.Min, NoteRect.Max, Style->Colors[colIdx], 0.0f);
        draw_list->AddRect(NoteRect.Min, NoteRect.Max, Style->Colors[4], 0.0f);
    }

    // mouse note click
    if (*PrevNoteActive != NoteMouseColision)
    {
        Callback(UserData, NoteOff, *PrevNoteActive, 0.0f);
        *PrevNoteActive = -1;

        if (held && NoteMouseColision >= 0)
        {
            Callback(UserData, NoteOn, NoteMouseColision, NoteMouseVel);
            *PrevNoteActive = NoteMouseColision;
        }
    }
}
