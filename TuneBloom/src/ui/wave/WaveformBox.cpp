#include <ui/wave/WaveformBox.h>

#include <messages/Messages.h>
#include <ui/wave/WaveFormatUI.h>
#include <ui/widgets/Knob.h>
#include <ui/widgets/Widgets.h>

#include <libs/IconsLucide.h>
#include <imgui/imgui_internal.h>

#include <math/seadMathCalcCommon.h>

static constexpr f32 cTitleGapEm      = 0.3f;
static constexpr f32 cToolGroupGapEm  = 0.6f;
static constexpr f32 cFieldMinEm      = 3.5f;
static constexpr f32 cFieldMaxEm      = 7.0f;
static constexpr f32 cFieldGroupGapEm = 1.5f;
static constexpr f32 cFieldDragRange  = 2000.0f;

static bool DrawLoopSwitch(bool isLoop, f32 size, const char* disabledReason)
{
    ImGui::BeginDisabled(disabledReason != nullptr);

    const bool pressed = ToggleButton(ICON_LC_REPEAT "###LoopSwitch", isLoop, ImVec2(size, size));

    ImGui::EndDisabled();
    ImGui::SetItemTooltip("%s", disabledReason ? disabledReason : (isLoop ? messages::common::cLoopOn : messages::common::cLoopOff));

    return pressed && !disabledReason;
}

bool BeginWaveformHeader(f32 playToolsWidth, bool isLoop, const char* loopDisabledReason)
{
    const f32 buttonSize = ImGui::GetFrameHeight();
    const f32 groupGap = ImFloor(ImGui::GetFontSize() * cToolGroupGapEm);
    const f32 toolsWidth = buttonSize + groupGap + playToolsWidth;
    const ImVec2 headerPos = ImGui::GetCursorScreenPos();
    const f32 toolsX = headerPos.x + ImGui::GetContentRegionAvail().x - toolsWidth;

    RegionHeader(messages::wavefile::cWaveform, toolsWidth + ImGui::GetStyle().ItemSpacing.x);

    ImGui::SameLine();
    ImGui::SetCursorScreenPos(ImVec2(toolsX, headerPos.y));

    const bool loopToggled = DrawLoopSwitch(isLoop, buttonSize, loopDisabledReason);

    ImGui::SameLine(0.0f, groupGap);

    return loopToggled;
}

void EndWaveformHeader()
{
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImFloor(ImGui::GetFontSize() * cTitleGapEm));
}

static bool DrawLoopField(const char* label, const char* id, f32 labelWidth, f32 fieldWidth, u32* frame, u32 min, u32 max, u32 sampleRate, const char* fixedDetail, bool* outCommit)
{
    ImGui::BeginDisabled(fixedDetail != nullptr);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(0.0f, labelWidth - ImGui::CalcTextSize(label).x + ImGui::GetStyle().ItemSpacing.x);
    ImGui::SetNextItemWidth(fieldWidth);

    const bool changed = DragField(id, ImGuiDataType_U32, frame, sead::Mathf::max(1.0f, static_cast<f32>(max) / cFieldDragRange), &min, &max);

    ImGui::EndDisabled();
    *outCommit = false;

    if (fixedDetail)
    {
        ImGui::SetItemTooltip("%s", fixedDetail);
        return false;
    }

    u32 minutes = 0;
    f64 seconds = 0.0;
    SplitSampleTime(*frame, sampleRate, &minutes, &seconds);
    KnobTooltip(messages::wavefile::cTimeFormat, minutes, seconds);

    *outCommit = IsItemEditCommitted(changed);

    return changed;
}

LoopFieldsResult DrawLoopFields(u32* loopStartFrame, u32* loopEndFrame, u32 sampleCount, u32 sampleRate, bool enabled, const char* endFixedDetail)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const f32 em = ImGui::GetFontSize();

    const f32 labelWidth = sead::Mathf::max(ImGui::CalcTextSize(messages::common::cStart).x, ImGui::CalcTextSize(messages::common::cEnd).x);
    const f32 unitWidth = style.ItemSpacing.x + ImGui::CalcTextSize(messages::common::cSamples).x;
    const f32 groupGap = ImFloor(em * cFieldGroupGapEm);
    const f32 avail = ImGui::GetContentRegionAvail().x;
    const f32 fieldMin = ImFloor(em * cFieldMinEm);
    const f32 oneLineWidth = (avail - (labelWidth + style.ItemSpacing.x) * 2.0f - groupGap - unitWidth) * 0.5f;
    const bool oneLine = oneLineWidth >= fieldMin;
    const f32 fieldWidth = oneLine ? sead::Mathf::min(ImFloor(oneLineWidth), ImFloor(em * cFieldMaxEm)) : sead::Mathf::max(fieldMin, avail - labelWidth - style.ItemSpacing.x - unitWidth);
    const f32 startWidth = ImGui::CalcTextSize(messages::common::cStart).x + style.ItemSpacing.x + fieldWidth;
    const f32 endWidth = ImGui::CalcTextSize(messages::common::cEnd).x + style.ItemSpacing.x + fieldWidth + unitWidth;
    const f32 spreadGap = sead::Mathf::max(groupGap, ImFloor(avail - startWidth - endWidth));

    LoopFieldsResult result;

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + style.ItemSpacing.y);
    ImGui::BeginDisabled(!enabled);

    result.changed |= DrawLoopField(messages::common::cStart, "##LoopStart", oneLine ? ImGui::CalcTextSize(messages::common::cStart).x : labelWidth, fieldWidth,
                                    loopStartFrame, 0, sampleCount > 0 ? sampleCount - 1 : 0, sampleRate, nullptr, &result.startCommit);

    if (oneLine)
        ImGui::SameLine(0.0f, spreadGap);

    result.changed |= DrawLoopField(messages::common::cEnd, "##LoopEnd", oneLine ? ImGui::CalcTextSize(messages::common::cEnd).x : labelWidth, fieldWidth,
                                    loopEndFrame, 1, sead::Mathu::max(sampleCount, 1u), sampleRate, endFixedDetail, &result.endCommit);

    ImGui::SameLine();
    ImGui::TextDisabled("%s", messages::common::cSamples);

    ImGui::EndDisabled();

    return result;
}
