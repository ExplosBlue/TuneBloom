/*
The MIT License (MIT)
by https://github.com/frowrik
Piano keyboard for ImGui v1.1
example:
static int PrevNoteActive = -1;
ImGui_PianoKeyboard("PianoTest", ImVec2(1024, 100), &PrevNoteActive, 21, 108, TestPianoBoardFunct, nullptr);
bool TestPianoBoardFunct(void* UserData, int Msg, int Key, float Vel) {
		if (Key >= 128) return false; // midi max keys
		if (Msg == NoteGetStatus) return KeyPresed[Key];
		if (Msg == NoteOn) { KeyPresed[Key] = true; Send_Midi_NoteOn(Key, Vel*127); }
		if (Msg == NoteOff) { KeyPresed[Key] = false; Send_Midi_NoteOff(Key, Vel*127);}
		return false;
}
*/

#include <ui/bank/PianoKeyboard.h>

#include <ui/bank/KeyboardPalette.h>
#include <ui/Messages.h>
#include <ui/app/Shortcuts.h>
#include <ui/app/UI.h>
#include <ui/settings/UiColors.h>

#include <math/seadMathCalcCommon.h>

#include <algorithm>

static constexpr s32 cKeyNum              = 128;
static constexpr s32 cOctaveKeyNum        = 12;
static constexpr s32 cDefaultOctaveBase   = 60; // C4
static constexpr s32 cMaxOctaveBase       = 108;
static constexpr s32 cComputerKeyRange    = 32;
static constexpr f32 cComputerKeyVelocity = 0.8f;

static constexpr f32 cBlackKeyWidth       = 0.85f;
static constexpr f32 cBlackKeyHeight      = 0.62f;
static constexpr f32 cBlackKeyFaceMinPx   = 5.0f;
static constexpr f32 cKeyShadowHeight     = 0.1f;
static constexpr f32 cRootStripePx        = 4.0f;
static constexpr f32 cRulerPadEm          = 0.15f;
static constexpr f32 cRulerRangeBarPx     = 2.0f;

static constexpr uicolors::Themed<f32> cRulerRangeAlpha = { 0.16f, 0.10f };

static constexpr bool cIsBlackKey[cOctaveKeyNum] = { false, true, false, true, false, false, true, false, true, false, true, false };
static const char* const cKeyNames[cOctaveKeyNum] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

static const struct { ImGuiKey key; s32 offset; } cComputerKeys[] = {
    // Lower row
    { ImGuiKey_Z, 0 },  { ImGuiKey_S, 1 },  { ImGuiKey_X, 2 },  { ImGuiKey_D, 3 },
    { ImGuiKey_C, 4 },  { ImGuiKey_V, 5 },  { ImGuiKey_G, 6 },  { ImGuiKey_B, 7 },
    { ImGuiKey_H, 8 },  { ImGuiKey_N, 9 },  { ImGuiKey_J, 10 }, { ImGuiKey_M, 11 },
    { ImGuiKey_Comma, 12 }, { ImGuiKey_L, 13 }, { ImGuiKey_Period, 14 }, { ImGuiKey_Semicolon, 15 },
    { ImGuiKey_Slash, 16 },
    // Upper row
    { ImGuiKey_Q, 12 }, { ImGuiKey_2, 13 }, { ImGuiKey_W, 14 }, { ImGuiKey_3, 15 },
    { ImGuiKey_E, 16 }, { ImGuiKey_R, 17 }, { ImGuiKey_5, 18 }, { ImGuiKey_T, 19 },
    { ImGuiKey_6, 20 }, { ImGuiKey_Y, 21 }, { ImGuiKey_7, 22 }, { ImGuiKey_U, 23 },
    { ImGuiKey_I, 24 }, { ImGuiKey_9, 25 }, { ImGuiKey_O, 26 }, { ImGuiKey_0, 27 },
    { ImGuiKey_P, 28 }, { ImGuiKey_LeftBracket, 29 }, { ImGuiKey_Equal, 30 }, { ImGuiKey_RightBracket, 31 },
};

static s32 sOctaveBase = cDefaultOctaveBase;
static bool sComputerKeyDown[cKeyNum] = { false };

namespace {

struct KeyLayout
{
    ImRect keys;
    f32 noteWidth;
    f32 blackHeight;
    s32 beginNote;
    s32 endNote;

    bool contains(s32 key) const
    {
        return key >= beginNote && key <= endNote;
    }

    bool isBlack(s32 key) const
    {
        return contains(key) && cIsBlackKey[key % cOctaveKeyNum];
    }

    f32 cellX(s32 key) const
    {
        return keys.Min.x + ImFloor((key - beginNote) * noteWidth + 0.5f);
    }

    ImRect blackRect(s32 key) const
    {
        const f32 cellMin = cellX(key);
        const f32 cellWidth = cellX(key + 1) - cellMin;
        const f32 width = std::max(1.0f, ImFloor(cellWidth * cBlackKeyWidth + 0.5f));
        const f32 x = cellMin + ImFloor((cellWidth - width) * 0.5f + 0.5f);

        return ImRect(x, keys.Min.y, x + width, keys.Min.y + blackHeight);
    }

    f32 blackCenterX(s32 key) const
    {
        const ImRect rect = blackRect(key);
        return ImFloor((rect.Min.x + rect.Max.x) * 0.5f);
    }

    ImRect whiteRect(s32 key) const
    {
        const f32 x0 = isBlack(key - 1) ? blackCenterX(key - 1) : cellX(key);
        const f32 x1 = isBlack(key + 1) ? blackCenterX(key + 1) : cellX(key + 1);

        return ImRect(x0, keys.Min.y, x1, keys.Max.y);
    }

    s32 keyAtX(f32 x) const
    {
        const s32 key = beginNote + static_cast<s32>(ImFloor((x - keys.Min.x) / noteWidth));
        return sead::MathCalcCommon<s32>::clamp2(beginNote, key, endNote);
    }

    s32 keyAt(const ImVec2& pos) const
    {
        if (!keys.Contains(pos))
            return -1;

        const s32 cell = keyAtX(pos.x);

        if (pos.y < keys.Min.y + blackHeight)
        {
            for (s32 key = cell - 1; key <= cell + 1; key++)
            {
                if (!isBlack(key))
                    continue;

                const ImRect rect = blackRect(key);
                if (pos.x >= rect.Min.x && pos.x < rect.Max.x)
                    return key;
            }
        }

        for (s32 key = cell - 1; key <= cell + 1; key++)
        {
            if (!contains(key) || isBlack(key))
                continue;

            const ImRect rect = whiteRect(key);
            if (pos.x >= rect.Min.x && pos.x < rect.Max.x)
                return key;
        }

        return -1;
    }
};

struct KeyState
{
    bool active;
    bool hovered;
    bool root;
};

}

static sead::FixedSafeString<16> FormatKeyLabel(s32 key, bool withNumber)
{
    const char* name = cKeyNames[key % cOctaveKeyNum];
    const s32 octave = key / cOctaveKeyNum - 1;

    if (withNumber)
        return sead::FormatFixedSafeString<16>("%s%d (%d)", name, octave, key);

    return sead::FormatFixedSafeString<16>("%s%d", name, octave);
}

static ImU32 GetWhiteKeyColor(const KeyState& state)
{
    const KeyboardPalette& palette = GetKeyboardPalette();

    if (state.root)
        return state.active ? palette.whiteKeyRootActive : palette.whiteKeyRoot;

    if (state.active)
        return palette.whiteKeyActive;

    return state.hovered ? palette.whiteKeyHover : palette.whiteKey;
}

static ImU32 GetBlackKeyColor(const KeyState& state)
{
    const KeyboardPalette& palette = GetKeyboardPalette();

    if (state.root)
        return state.active ? palette.blackKeyRootActive : palette.blackKeyRoot;

    if (state.active)
        return palette.blackKeyActive;

    return state.hovered ? palette.blackKeyHover : palette.blackKey;
}

static void ReleaseComputerKeys(ImGuiPianoKeyboardProc callback, void* userData)
{
    for (s32 note = 0; note < cKeyNum; note++)
    {
        if (!sComputerKeyDown[note])
            continue;

        sComputerKeyDown[note] = false;
        callback(userData, NoteOff, note, 0.0f);
    }
}

static void SetOctaveBase(s32 base, ImGuiPianoKeyboardProc callback, void* userData)
{
    base = sead::MathCalcCommon<s32>::clamp2(0, base, cMaxOctaveBase);
    if (base == sOctaveBase)
        return;

    ReleaseComputerKeys(callback, userData);
    sOctaveBase = base;
}

static void UpdateComputerKeyboard(ImGuiPianoKeyboardProc callback, void* userData)
{
    if (ImGui::GetIO().WantTextInput)
    {
        ReleaseComputerKeys(callback, userData);
        return;
    }

    bool keyDown[cKeyNum] = { false };

    for (s32 i = 0; i < IM_ARRAYSIZE(cComputerKeys); i++)
    {
        const s32 note = sOctaveBase + cComputerKeys[i].offset;
        if (note < 0 || note >= cKeyNum)
            continue;

        if (!shortcuts::Claimed(cComputerKeys[i].key) && ImGui::IsKeyDown(cComputerKeys[i].key))
            keyDown[note] = true;
    }

    for (s32 note = 0; note < cKeyNum; note++)
    {
        if (keyDown[note] == sComputerKeyDown[note])
            continue;

        sComputerKeyDown[note] = keyDown[note];
        callback(userData, keyDown[note] ? NoteOn : NoteOff, note, keyDown[note] ? cComputerKeyVelocity : 0.0f);
    }

    s32 base = sOctaveBase;

    if (shortcuts::Pressed(shortcuts::Action::OctaveDown))
        base -= cOctaveKeyNum;

    if (shortcuts::Pressed(shortcuts::Action::OctaveUp))
        base += cOctaveKeyNum;

    if (shortcuts::Pressed(shortcuts::Action::OctaveReset))
        base = cDefaultOctaveBase;

    SetOctaveBase(base, callback, userData);
}

static void DrawRuler(ImDrawList* draw, const ImRect& ruler, const KeyLayout& layout, s32 firstKey, s32 lastKey)
{
    const f32 pad = ImFloor(ImGui::GetFontSize() * cRulerPadEm);
    const f32 octaveWidth = layout.noteWidth * cOctaveKeyNum;
    const bool withNumber = octaveWidth >= ImGui::CalcTextSize("C-1 (120)").x + pad * 4.0f;
    const s32 rangeEnd = std::min(sOctaveBase + cComputerKeyRange, layout.endNote + 1);

    draw->AddRectFilled(ruler.Min, ruler.Max, ImGui::GetColorU32(ImGuiCol_FrameBg));

    // Computer keyboard range
    if (rangeEnd > sOctaveBase)
    {
        const ImVec2 rangeMin(layout.cellX(sOctaveBase), ruler.Min.y);
        const ImVec2 rangeMax(layout.cellX(rangeEnd), ruler.Max.y);

        draw->AddRectFilled(rangeMin, rangeMax, ImGui::GetColorU32(ImGuiCol_CheckMark, uicolors::Pick(cRulerRangeAlpha)));
        draw->AddRectFilled(ImVec2(rangeMin.x, rangeMax.y - cRulerRangeBarPx), rangeMax, ImGui::GetColorU32(ImGuiCol_CheckMark));
    }

    // Octave labels
    for (s32 key = (firstKey / cOctaveKeyNum) * cOctaveKeyNum; key <= lastKey; key += cOctaveKeyNum)
    {
        if (!layout.contains(key))
            continue;

        const f32 x = layout.cellX(key);
        const bool inRange = key >= sOctaveBase && key < rangeEnd;
        sead::FixedSafeString<16> label = FormatKeyLabel(key, withNumber);

        if (x + pad * 2.0f + ImGui::CalcTextSize(label.cstr()).x > ruler.Max.x)
            label = FormatKeyLabel(key, false);

        draw->AddRectFilled(ImVec2(x, ruler.Min.y), ImVec2(x + 1.0f, ruler.Max.y), ImGui::GetColorU32(ImGuiCol_Border));
        draw->AddText(ImVec2(x + pad * 2.0f, ruler.Min.y + pad), ImGui::GetColorU32(inRange ? ImGuiCol_Text : ImGuiCol_TextDisabled), label.cstr());
    }
}

static void DrawKeys(ImDrawList* draw, const KeyLayout& layout, s32 firstKey, s32 lastKey, s32 hoveredKey, s32 rootKey, ImGuiPianoKeyboardProc callback, void* userData)
{
    const ImRect& keys = layout.keys;
    const f32 whiteWidth = layout.noteWidth * cOctaveKeyNum / 7.0f;
    const f32 separatorAlpha = sead::MathCalcCommon<f32>::clamp2(0.3f, (whiteWidth - 4.0f) / 6.0f, 1.0f);
    const KeyboardPalette& palette = GetKeyboardPalette();
    const ImU32 separatorColor = uicolors::WithAlpha(palette.separator, uicolors::UnitToByte(separatorAlpha));

    auto getState = [&](s32 key) -> KeyState
    {
        return { callback(userData, NoteGetStatus, key, 0.0f), key == hoveredKey, key == rootKey };
    };

    draw->AddRectFilled(keys.Min, keys.Max, palette.whiteKey);

    // Draw white keys
    for (s32 key = firstKey; key <= lastKey; key++)
    {
        if (layout.isBlack(key))
            continue;

        const ImRect rect = layout.whiteRect(key);
        const KeyState state = getState(key);
        const ImU32 color = GetWhiteKeyColor(state);

        if (color != palette.whiteKey)
            draw->AddRectFilled(rect.Min, rect.Max, color);

        if (state.root && state.active)
            draw->AddRectFilled(ImVec2(rect.Min.x, rect.Max.y - cRootStripePx), rect.Max, palette.rootStripe);

        if (key > layout.beginNote)
            draw->AddRectFilled(rect.Min, ImVec2(rect.Min.x + 1.0f, rect.Max.y), separatorColor);
    }

    const f32 shadowHeight = ImFloor(keys.GetHeight() * cKeyShadowHeight);
    draw->AddRectFilledMultiColor(keys.Min, ImVec2(keys.Max.x, keys.Min.y + shadowHeight), palette.keyShadow, palette.keyShadow, uicolors::cTransparentU32, uicolors::cTransparentU32);

    // Draw black keys
    for (s32 key = firstKey; key <= lastKey; key++)
    {
        if (!layout.isBlack(key))
            continue;

        const ImRect rect = layout.blackRect(key);
        const ImU32 color = GetBlackKeyColor(getState(key));

        if (rect.GetWidth() < cBlackKeyFaceMinPx)
        {
            draw->AddRectFilled(rect.Min, rect.Max, color);
            continue;
        }

        const f32 lip = std::max(2.0f, ImFloor(rect.GetHeight() * 0.08f));
        draw->AddRectFilled(rect.Min, rect.Max, palette.blackKeyEdge);
        draw->AddRectFilled(ImVec2(rect.Min.x + 1.0f, rect.Min.y), ImVec2(rect.Max.x - 1.0f, rect.Max.y - lip), color);
    }

    draw->AddRectFilled(ImVec2(keys.Min.x, keys.Max.y - 1.0f), keys.Max, separatorColor);
}

f32 GetPianoRulerHeight()
{
    return ImGui::GetFontSize() + ImFloor(ImGui::GetFontSize() * cRulerPadEm) * 2.0f + cRulerRangeBarPx;
}

void ImGui_PianoKeyboard(const char* idName, ImVec2 size, s32* prevNoteActive, s32 beginNote, s32 endNote, ImGuiPianoKeyboardProc callback, void* userData, s32 rootKey)
{
    if (!idName || !callback || beginNote < 0 || endNote <= beginNote || endNote >= cKeyNum)
        return;

    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return;

    UpdateComputerKeyboard(callback, userData);

    const ImGuiID id = window->GetID(idName);
    const ImVec2 pos = window->DC.CursorPos;
    const ImRect bb(pos, ImVec2(pos.x + size.x, pos.y + size.y));

    ImGui::ItemSize(size, 0.0f);
    if (!ImGui::ItemAdd(bb, id))
        return;

    bool hovered = false;
    bool held = false;
    ImGui::ButtonBehavior(bb, id, &hovered, &held, 0);

    const ImRect ruler(bb.Min, ImVec2(bb.Max.x, bb.Min.y + GetPianoRulerHeight()));

    KeyLayout layout;
    layout.keys        = ImRect(ImVec2(bb.Min.x, ruler.Max.y), bb.Max);
    layout.noteWidth   = size.x / static_cast<f32>(endNote - beginNote + 1);
    layout.blackHeight = ImFloor(layout.keys.GetHeight() * cBlackKeyHeight);
    layout.beginNote   = beginNote;
    layout.endNote     = endNote;

    ImDrawList* draw = window->DrawList;
    const s32 firstKey = std::max(beginNote, layout.keyAtX(draw->GetClipRectMin().x) - 1);
    const s32 lastKey = std::min(endNote, layout.keyAtX(draw->GetClipRectMax().x) + 1);

    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const s32 mouseKey = hovered ? layout.keyAt(mouse) : -1;

    // Ruler input
    if (hovered && ruler.Contains(mouse))
    {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            SetOctaveBase((layout.keyAtX(mouse.x) / cOctaveKeyNum) * cOctaveKeyNum, callback, userData);

        if (!held && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        {
            const s32 rangeLast = std::min(sOctaveBase + cComputerKeyRange - 1, cKeyNum - 1);
            ImGui::SetTooltip(messages::keyboard::cComputerRangeTooltipFormat, FormatKeyLabel(sOctaveBase, false).cstr(), FormatKeyLabel(rangeLast, false).cstr());
        }
    }
    else if (mouseKey >= 0 && !held && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
    {
        ImGui::SetTooltip("%s", FormatKeyLabel(mouseKey, true).cstr());
    }

    if (mouseKey >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        callback(userData, NoteContext, mouseKey, 0.0f);

    DrawRuler(draw, ruler, layout, firstKey, lastKey);
    DrawKeys(draw, layout, firstKey, lastKey, mouseKey, rootKey, callback, userData);

    // Mouse input
    const s32 mouseNote = held ? mouseKey : -1;
    if (*prevNoteActive == mouseNote)
        return;

    if (*prevNoteActive >= 0)
        callback(userData, NoteOff, *prevNoteActive, 0.0f);

    *prevNoteActive = mouseNote;

    if (mouseNote >= 0)
    {
        const f32 keyHeight = layout.isBlack(mouseNote) ? layout.blackHeight : layout.keys.GetHeight();
        const f32 velocity = sead::MathCalcCommon<f32>::clamp2(0.0f, (mouse.y - layout.keys.Min.y) / keyHeight, 1.0f);

        callback(userData, NoteOn, mouseNote, velocity);
    }
}
