#include <ui/player/PlayerFileFacts.h>

#include <ui/widgets/ContextMenu.h>
#include <ui/file/Document.h>
#include <ui/file/LooseFileInfo.h>
#include <ui/app/MenuBar.h>
#include <messages/Messages.h>
#include <ui/widgets/Properties.h>
#include <ui/widgets/TimeUtil.h>
#include <ui/widgets/Widgets.h>

#include <imgui/imgui_internal.h>
#include <libs/IconsLucide.h>

static constexpr const char* cDetailsPopupId = "###FileDetails";
static constexpr f32 cStatusBarPadEm = 0.35f;

bool DrawFileFactsLine(const Document& document, f32 gap)
{
    FileFacts facts;

    if (!document.getFileFacts(&facts))
        return false;

    sead::FixedSafeString<256> line;
    FormatFileFactsLine(&line, facts);

    ImFont* font = GetSmallFont();
    const ImGuiWindow* window = ImGui::GetCurrentWindow();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const f32 right = pos.x + ImGui::GetContentRegionAvail().x;
    const f32 lineY = ImFloor(pos.y + gap);
    const f32 barHeight = font->FontSize + ImFloor(ImGui::GetFontSize() * cStatusBarPadEm) * 2.0f;
    const f32 textY = ImFloor(lineY + 1.0f + barHeight * 0.5f - CalcCapCenterOffset(font, font->FontSize));

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddLine(ImVec2(pos.x, lineY), ImVec2(right, lineY), ImGui::GetColorU32(ImGuiCol_Separator));

    ImGui::PushClipRect(window->Pos, window->Pos + window->Size, false);
    ImGui::PushFont(font);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));

    f32 factsRight = right;

    if (facts.kindName)
    {
        const f32 kindLeft = right - ImGui::CalcTextSize(facts.kindName).x;

        drawList->AddText(ImVec2(kindLeft, textY), ImGui::GetColorU32(ImGuiCol_Text), facts.kindName);
        factsRight = kindLeft - ImGui::GetFontSize();
    }

    ImGui::RenderTextEllipsis(drawList, ImVec2(pos.x, textY), ImVec2(factsRight, textY + font->FontSize), factsRight, factsRight, line.cstr(), nullptr, nullptr);

    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::PopClipRect();

    ImGui::Dummy(ImVec2(right - pos.x, lineY + 1.0f + barHeight - pos.y));

    return true;
}

static void DrawFactRow(const char* label, const char* value)
{
    PropertyRow(label);
    ImGui::TextUnformatted(value);
}

static void FormatSampleClock(char (&buffer)[16], u32 samples, u32 sampleRate)
{
    timeutil::FormatClock(buffer, sizeof(buffer), timeutil::SamplesToSeconds(samples, sampleRate));
}

static void DrawTimeRows(const FileFacts& facts)
{
    if (facts.sampleRate == 0)
        return;

    char length[16];
    FormatSampleClock(length, facts.sampleCount, facts.sampleRate);
    DrawFactRow(messages::common::cLength, length);

    if (!facts.isLoop)
        return;

    char start[16];
    char end[16];
    FormatSampleClock(start, facts.loopStartFrame, facts.sampleRate);
    FormatSampleClock(end, facts.loopEndFrame, facts.sampleRate);

    DrawFactRow(messages::common::cLoop, sead::FormatFixedSafeString<80>(messages::common::cLoopRangeFormat, start, end).cstr());
}

static void DrawAudioRows(const FileFacts& facts)
{
    DrawTimeRows(facts);

    if (!facts.encoding.isEmpty())
        DrawFactRow(messages::common::cEncoding, facts.encoding.cstr());

    if (facts.bitrate != 0)
        DrawFactRow(messages::common::cBitrate, sead::FormatFixedSafeString<32>(messages::unit::cKilobitsFormat, (facts.bitrate + 500) / 1000).cstr());

    if (facts.sampleRate != 0)
        DrawFactRow(messages::common::cSampleRate, sead::FormatFixedSafeString<32>(messages::unit::cHertzFormat, facts.sampleRate).cstr());

    if (facts.trackNum > 1)
    {
        PropertyRow(messages::common::cTracks);
        ImGui::Text("%u", facts.trackNum);
    }

    PropertyRow(messages::common::cChannels);
    ImGui::Text("%u", facts.channelNum);
}

static void DrawFileDetailsPopup(const FileFacts& facts)
{
    if (!BeginMenuPanel(cDetailsPopupId))
        return;

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, 0.0f));

    DrawAudioRows(facts);
    ImGui::Separator();
    DrawLooseFileRows(facts.format);

    ImGui::PopStyleVar();

    EndMenuPanel();
    SetMenuBarFontSize();
}

f32 DrawFileDetailsButton(const Document& document, f32 right, f32 top, f32 bottom)
{
    FileFacts facts;

    if (!document.getFileFacts(&facts))
        return 0.0f;

    const bool isOpen = ImGui::IsPopupOpen(cDetailsPopupId);

    const bool pressed = MenuBarIconButton("##FileDetails", ICON_LC_INFO, right, top, bottom, isOpen);

    if (!isOpen)
        SetMenuBarTooltip(messages::playerframe::cFileDetails);

    if (pressed)
    {
        ImGui::SetNextWindowPos(ImVec2(right, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
        ImGui::OpenPopup(cDetailsPopupId);
    }

    DrawFileDetailsPopup(facts);

    return CalcMenuBarIconButtonWidth(ICON_LC_INFO);
}
