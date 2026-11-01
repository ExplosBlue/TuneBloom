#include <ui/wave/WaveformBox.h>

#include <messages/Messages.h>
#include <ui/wave/LoopWaveformEditor.h>
#include <ui/wave/LoopWaveformInternal.h>
#include <ui/wave/WaveFormatUI.h>
#include <ui/wave/WaveMonoJob.h>
#include <ui/widgets/Knob.h>
#include <ui/widgets/Widgets.h>

#include <bfsar/player/SoundPlayer.h>

#include <libs/IconsLucide.h>
#include <imgui/imgui_internal.h>

#include <math/seadMathCalcCommon.h>

static constexpr f32 cTitleGapEm      = 0.3f;
static constexpr f32 cToolGroupGapEm  = 0.6f;
static constexpr f32 cFieldMinEm      = 3.5f;
static constexpr f32 cFieldMaxEm      = 7.0f;
static constexpr f32 cFieldGroupGapEm = 1.5f;
static constexpr f32 cFieldDragRange  = 2000.0f;
static constexpr f32 cSpinnerRadiusEm = 0.3f;
static constexpr f32 cSpinnerGapEm    = 0.4f;

void SeekPlayerToSample(f64 sample)
{
    if (sample < 0.0 || sSoundPlayer.getSampleCount() == 0)
        return;

    sSoundPlayer.seek(sead::Mathf::clamp2(0.0f, static_cast<f32>(sample / static_cast<f64>(sSoundPlayer.getSampleCount())), 1.0f));
}

static bool DrawLoopSwitch(bool isLoop, f32 size, const char* disabledReason)
{
    ImGui::BeginDisabled(disabledReason != nullptr);

    const bool pressed = ToggleButton(ICON_LC_REPEAT "###LoopSwitch", isLoop, ImVec2(size, size));

    ImGui::EndDisabled();
    ImGui::SetItemTooltip("%s", disabledReason ? disabledReason : (isLoop ? messages::common::cLoopOn : messages::common::cLoopOff));

    return pressed && !disabledReason;
}

static void DrawSourceTool(WaveformTool* tool, const char* label, f32 size)
{
    ImGui::BeginDisabled(tool->disabledReason != nullptr);

    tool->pressed = ImGui::Button(label, ImVec2(size, size)) && !tool->disabledReason;

    ImGui::EndDisabled();
    ImGui::SetItemTooltip("%s", tool->disabledReason ? tool->disabledReason : tool->tooltip);
}

static f32 CalcSourceToolsWidth(const WaveformSourceTools& tools, f32 buttonSize, f32 groupGap)
{
    const u32 toolNum = (tools.reimport ? 1 : 0) + (tools.replace ? 1 : 0);

    if (toolNum == 0)
        return 0.0f;

    return buttonSize * toolNum + ImGui::GetStyle().ItemSpacing.x * (toolNum - 1) + groupGap;
}

static void DrawSourceTools(const WaveformSourceTools& tools, f32 buttonSize, f32 groupGap)
{
    if (tools.reimport)
        DrawSourceTool(tools.reimport, ICON_LC_SLIDERS_HORIZONTAL "###ReimportTool", buttonSize);

    if (tools.reimport && tools.replace)
        ImGui::SameLine();

    if (tools.replace)
        DrawSourceTool(tools.replace, ICON_LC_ARROW_DOWN_UP "###ReplaceTool", buttonSize);

    if (tools.reimport || tools.replace)
        ImGui::SameLine(0.0f, groupGap);
}

bool BeginWaveformHeader(f32 playToolsWidth, bool isLoop, const char* loopDisabledReason, const WaveformSourceTools& sourceTools)
{
    const f32 buttonSize = ImGui::GetFrameHeight();
    const f32 groupGap = ImFloor(ImGui::GetFontSize() * cToolGroupGapEm);
    const f32 toolsWidth = CalcSourceToolsWidth(sourceTools, buttonSize, groupGap) + buttonSize + groupGap + playToolsWidth;
    const ImVec2 headerPos = ImGui::GetCursorScreenPos();
    const f32 toolsX = headerPos.x + ImGui::GetContentRegionAvail().x - toolsWidth;

    RegionHeader(messages::wavefile::cWaveform, toolsWidth + ImGui::GetStyle().ItemSpacing.x);

    ImGui::SameLine();
    ImGui::SetCursorScreenPos(ImVec2(toolsX, headerPos.y));

    DrawSourceTools(sourceTools, buttonSize, groupGap);

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

static void DrawLoadingLabel(const ImVec2& min, const ImVec2& max)
{
    const f32 em = ImGui::GetFontSize();
    const f32 spinnerWidth = em + ImFloor(em * cSpinnerGapEm);
    const ImU32 color = ImGui::GetColorU32(ImGuiCol_TextDisabled);

    const ImVec2 textSize = ImGui::CalcTextSize(messages::wavefile::cLoading);
    const ImVec2 pos = ImFloor(min + (max - min - ImVec2(spinnerWidth + textSize.x, textSize.y)) * 0.5f);

    DrawSpinner(ImVec2(pos.x + ImFloor(em * 0.5f), pos.y + ImFloor(textSize.y * 0.5f)), ImFloor(em * cSpinnerRadiusEm), color);
    ImGui::GetWindowDrawList()->AddText(ImVec2(pos.x + spinnerWidth, pos.y), color, messages::wavefile::cLoading);
}

void DrawWaveformPlaceholder(const WaveMonoJob& job, bool tooLong, const char* emptyText, f32 canvasHeight)
{
    if (!job.isLoading())
    {
        ImGui::TextDisabled("%s", tooLong ? messages::wavefile::cTooLong : emptyText);
        return;
    }

    const ImVec2 min = ImGui::GetCursorScreenPos();
    const f32 width = ImGui::GetContentRegionAvail().x;
    const ImVec2 canvasMax(min.x + width, min.y + canvasHeight);

    ImGui::GetWindowDrawList()->AddRectFilled(min, canvasMax, ImGui::Palette().bg);

    if (job.showsProgress())
        DrawLoadingLabel(min, canvasMax);

    ImGui::Dummy(ImVec2(width, ImGui::LoopWaveformEditorHeight(canvasHeight, true)));
}
