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

void ImGui_PianoKeyboard(const char* IDName, ImVec2 Size, s32* PrevNoteActive, s32 BeginOctaveNote, s32 EndOctaveNote, ImGuiPianoKeyboardProc Callback, void* UserData, ImGuiPianoStyles* Style)
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
    s32 CountNotesAllign7 = (EndOctaveNote / 12 - BeginOctaveNote / 12) * 7 + NoteLightNumber[EndOctaveNote % 12] - (NoteLightNumber[BeginOctaveNote % 12] - 1);

    f32 NoteHeight    = Size.y;
    f32 NoteWidth     = Size.x / (f32)CountNotesAllign7;

    f32 NoteHeight2   = NoteHeight * Style->NoteDarkHeight;
    f32 NoteWidth2    = NoteWidth * Style->NoteDarkWidth;

    // minimal size draw
    if (NoteHeight < 5.0 || NoteWidth < 3.0)
    {
        return;
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

    f32 OffsetX = bb.Min.x;
    f32 OffsetY = bb.Min.y;
    f32 OffsetY2 = OffsetY + NoteHeight;
    for (s32 RealNum = BeginOctaveNote; RealNum <= EndOctaveNote; RealNum++)
    {
        s32 Octave = RealNum / 12;
        s32 i      = RealNum % 12;

        if (NoteIsDark[i] > 0)
        {
            continue;
        }

        ImRect NoteRect( 
            round(OffsetX), 
            OffsetY, 
            round(OffsetX + NoteWidth), 
            OffsetY2 
        );

        if (held && NoteRect.Contains(MousePos))
        {
            NoteMouseColision = RealNum;
            NoteMouseVel      = (MousePos.y - NoteRect.Min.y) / NoteHeight;
        }

        bool isActive = Callback(UserData, NoteGetStatus, RealNum, 0.0f);

        draw_list->AddRectFilled(NoteRect.Min, NoteRect.Max, Style->Colors[isActive ? 2 : 0], 0.0f);

        draw_list->AddRect(NoteRect.Min, NoteRect.Max, Style->Colors[4], 0.0f);

        OffsetX += NoteWidth;
    }

    // draw dark notes
    OffsetX = bb.Min.x;
    OffsetY = bb.Min.y;
    OffsetY2 = OffsetY + NoteHeight2;
    for (s32 RealNum = BeginOctaveNote; RealNum <= EndOctaveNote; RealNum++)
    {
        s32 Octave = RealNum / 12;
        s32 i      = RealNum % 12;

        if (NoteIsDark[i] == 0)
        {
            OffsetX += NoteWidth;
            continue;
        }

        f32 OffsetDark = NoteDarkOffset[i] * NoteWidth2;
        ImRect NoteRect(
            round(OffsetX + OffsetDark), 
            OffsetY, 
            round(OffsetX + NoteWidth2 + OffsetDark),
            OffsetY2
        );

        if (held && NoteRect.Contains(MousePos))
        {
            NoteMouseColision = RealNum;
            NoteMouseVel      = (MousePos.y - NoteRect.Min.y) / NoteHeight2;
        }

        bool isActive = Callback(UserData, NoteGetStatus, RealNum, 0.0f);

        draw_list->AddRectFilled(NoteRect.Min, NoteRect.Max, Style->Colors[isActive ? 3 : 1], 0.0f);

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
