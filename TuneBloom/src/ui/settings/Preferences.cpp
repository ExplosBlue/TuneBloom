#define IMGUI_DEFINE_MATH_OPERATORS
#include <ui/settings/Preferences.h>

#include <ui/backup/Backup.h>
#include <ui/settings/Config.h>
#include <ui/settings/ConfigFile.h>
#include <ui/widgets/Dialogs.h>
#include <ui/file/FileAssociation.h>
#include <ui/items/ItemColumns.h>
#include <ui/app/MenuBar.h>
#include <ui/Messages.h>
#include <ui/player/OutputMeter.h>
#include <ui/widgets/PanelLayout.h>
#include <ui/app/PopupMgr.h>
#include <ui/project/ProjectViews.h>
#include <ui/widgets/Properties.h>
#include <ui/widgets/TimeUtil.h>
#include <ui/file/RecentFiles.h>
#include <ui/file/SystemShell.h>
#include <ui/settings/Theme.h>
#include <ui/widgets/Widgets.h>
#include <ui/widgets/WindowAnimation.h>
#include <ui/app/Workspace.h>

#include <libs/IconsLucide.h>
#include <imgui/imgui_custom.h>
#include <imgui/imgui_internal.h>

#include <bfsar/files/BankFile.h>
#include <midi/MidiInput.h>

#include <snd/SoundSystem.h>

#include <cstring>
#include <ctime>
#include <string>
#include <vector>

namespace
{

struct PageInfo
{
    PreferencesPage page;
    const char* icon;
    const char* name;
    void (*draw)();
};

struct BackupPreset
{
    const char* label;
    s32 minutes;
};

struct SampleRate
{
    const char* label;
    u32 rate;
};

struct TooltipDelay
{
    const char* label;
    f32 seconds;
};

}

static constexpr f32 cWindowWidthEm  = 40.0f;
static constexpr f32 cWindowHeightEm = 27.0f;
static constexpr f32 cWindowPadEm    = 0.8f;
static constexpr f32 cNavWidthEm     = 10.0f;
static constexpr f32 cNavRowGapEm    = 0.5f;
static constexpr f32 cPagePadXEm     = 1.0f;
static constexpr f32 cPagePadYEm     = 0.8f;
static constexpr f32 cRowGapEm       = 0.4f;
static constexpr f32 cFooterGapEm    = 0.4f;
static constexpr f32 cLabelWidthEm   = 8.5f;
static constexpr f32 cCloseWidthEm   = 6.0f;
static constexpr f32 cRowButtonEm    = 6.5f;
static constexpr f32 cHueHandleEm    = 0.5f;
static constexpr f32 cNavBarWidthEm  = 0.15f;
static constexpr f32 cCardHeightEm   = 4.2f;
static constexpr f32 cCardGapEm      = 0.6f;

static constexpr f32 cMidiNotesHeightEm = 7.0f;
static constexpr f32 cUIScales[]        = { 1.0f, 1.25f, 1.5f };

static constexpr f32 cNavTintAlpha    = 0.25f;
static constexpr f32 cSliderFillAlpha = 0.55f;

static constexpr size_t cFolderTextMax = 22;

static constexpr s32 cBackupCountMax      = 100;
static constexpr s32 cBackupDefaultPreset = 2;
static constexpr s32 cSampleRateDefault   = 5;

static constexpr f32 cAccentSaturation = 0.50f;
static constexpr f32 cAccentValue      = 0.60f;
static constexpr f32 cAccentHueMax     = 359.0f / 360.0f;
static constexpr f32 cBrightnessMin   = 0.3f;
static constexpr f32 cBrightnessMax    = 2.5f;
static constexpr f32 cSaturationMin    = 0.0f;
static constexpr f32 cSaturationMax    = 2.0f;
static constexpr f32 cContrastMin      = 0.5f;
static constexpr f32 cContrastMax      = 1.8f;
static constexpr f32 cGradientMin      = 0.0f;
static constexpr f32 cGradientMax      = 3.0f;

static const ImVec4 cMidiConnectedColor  = ImVec4(0.45f, 0.85f, 0.50f, 1.00f);
static const ImVec4 cMidiNotFoundColor   = ImVec4(0.82f, 0.56f, 0.30f, 1.00f);
static const BackupPreset cBackupPresets[] = {
    { messages::common::cOff,       0  },
    { messages::preferences::cBackupMinute,    1  },
    { messages::preferences::cBackup5Minutes,  5  },
    { messages::preferences::cBackup10Minutes, 10 },
    { messages::preferences::cBackup15Minutes, 15 },
};

static const SampleRate cSampleRates[] = {
    { messages::unit::cSampleRate8000,  8000  },
    { messages::unit::cSampleRate11025, 11025 },
    { messages::unit::cSampleRate16000, 16000 },
    { messages::unit::cSampleRate22050, 22050 },
    { messages::unit::cSampleRate32000, 32000 },
    { messages::unit::cSampleRate44100, 44100 },
    { messages::unit::cSampleRate48000, 48000 },
    { messages::unit::cSampleRate96000, 96000 },
};

static const TooltipDelay cTooltipDelays[] = {
    { messages::preferences::cTooltipDelayShort,  0.2f },
    { messages::preferences::cTooltipDelayNormal, 0.4f },
    { messages::preferences::cTooltipDelayLong,   0.8f },
};

static PreferencesPage sPage = PreferencesPage::File;
static bool sRequestOpen = false;
static bool sThemeChanged = false;
static bool sResetAllRequested = false;

static size_t sBackupCount = 0;
static u64 sBackupSize = 0;
static bool sBackupFolderFound = false;

static fileassoc::State sAssociation = fileassoc::State::None;

static f32 sMidiNotesHeight = 0.0f;

void OpenPreferencesWindow(PreferencesPage page)
{
    sPage = page;
    sRequestOpen = true;
}

static void DrawRowLabel(const char* label)
{
    GroupBoxRowLabel(label, ImFloor(ImGui::GetFontSize() * cLabelWidthEm));
}

static ImDrawList* BeginSliderFill()
{
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->ChannelsSplit(2);
    drawList->ChannelsSetCurrent(1);

    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

    return drawList;
}

static void EndSliderFill(ImDrawList* drawList, f32 fraction)
{
    constexpr f32 cGrabPadding = 2.0f;

    ImGui::PopStyleColor(3);

    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    const f32 travel = max.x - min.x - cGrabPadding * 2.0f - style.GrabMinSize;
    const f32 fillX = ImFloor(min.x + cGrabPadding + travel * sead::Mathf::clamp2(0.0f, fraction, 1.0f) + style.GrabMinSize * 0.5f);
    const ImGuiCol frameColor = ImGui::IsItemActive() ? ImGuiCol_FrameBgActive : ImGui::IsItemHovered() ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg;

    drawList->ChannelsSetCurrent(0);
    drawList->AddRectFilled(min, max, ImGui::GetColorU32(frameColor), style.FrameRounding);
    drawList->AddRectFilled(min, ImVec2(fillX, max.y), ImGui::GetColorU32(ImGuiCol_SliderGrab, cSliderFillAlpha), style.FrameRounding, ImDrawFlags_RoundCornersLeft);
    drawList->ChannelsMerge();
}

static bool DrawThemeCard(const char* label, f32 width, bool dark, bool selected)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const f32 em = ImGui::GetFontSize();
    const ImVec2 size(width, ImFloor(em * cCardHeightEm));
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 max = pos + size;
    const f32 rounding = style.FrameRounding * 2.0f;
    const f32 pad = ImFloor(em * 0.5f);
    const f32 barHeight = ImFloor(em * 0.7f);
    const f32 rowHeight = ImFloor(em * 0.8f);

    const bool pressed = ImGui::InvisibleButton(label, size);
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();

    ImVec4 colors[ImGuiCol_COUNT];
    memcpy(colors, style.Colors, sizeof(colors));
    CalcThemeColors(gAccentColor, dark, colors);

    auto color = [&colors](ImGuiCol index, f32 alpha = 1.0f)
    {
        return ImGui::ColorConvertFloat4ToU32(ImVec4(colors[index].x, colors[index].y, colors[index].z, alpha));
    };

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    // Window and title bar
    ImGui::AddRectFilledGradient(drawList, ImVec2(pos.x, pos.y + barHeight), max, color(ImGuiCol_WindowBg), CalcSurfaceGradient(dark), rounding, ImDrawFlags_RoundCornersBottom);
    drawList->AddRectFilled(pos, ImVec2(max.x, pos.y + barHeight), color(ImGuiCol_TitleBgActive), rounding, ImDrawFlags_RoundCornersTop);

    const f32 rowY = pos.y + barHeight + pad;
    const f32 innerWidth = size.x - pad * 2.0f;
    const f32 rowWidth = ImFloor(innerWidth * 0.32f);
    const f32 buttonWidth = ImFloor(innerWidth * 0.2f);
    const f32 fieldMin = pos.x + pad + rowWidth + pad;
    const f32 fieldMax = max.x - pad - buttonWidth - pad;

    drawList->AddRectFilled(ImVec2(pos.x + pad, rowY), ImVec2(pos.x + pad + rowWidth, rowY + rowHeight), color(ImGuiCol_Header), style.FrameRounding);
    drawList->AddRectFilled(ImVec2(fieldMin, rowY), ImVec2(fieldMax, rowY + rowHeight), color(ImGuiCol_FrameBg), style.FrameRounding);
    drawList->AddRectFilled(ImVec2(fieldMin, rowY), ImVec2(ImFloor(fieldMin + (fieldMax - fieldMin) * 0.55f), rowY + rowHeight), color(ImGuiCol_SliderGrab), style.FrameRounding, ImDrawFlags_RoundCornersLeft);
    ImGui::AddRectFilledGradient(drawList, ImVec2(max.x - pad - buttonWidth, rowY), ImVec2(max.x - pad, rowY + rowHeight), color(ImGuiCol_Button), CalcRaisedGradient(dark), style.FrameRounding);

    const f32 textY = max.y - em - pad * 0.6f;

    drawList->AddText(ImVec2(pos.x + pad, textY), color(ImGuiCol_Text), label);

    if (selected)
        drawList->AddText(ImVec2(max.x - pad - ImGui::CalcTextSize(ICON_LC_CHECK).x, textY), color(ImGuiCol_CheckMark), ICON_LC_CHECK);

    if (selected)
        drawList->AddRect(pos, max, ImGui::GetColorU32(ImGuiCol_CheckMark), rounding, 0, 2.0f);
    else
        drawList->AddRect(pos, max, ImGui::GetColorU32(held ? ImGuiCol_Text : hovered ? ImGuiCol_TextDisabled : ImGuiCol_Border), rounding);

    return pressed;
}

static void DrawDetailTooltip(const char* text)
{
    if (IsItemHoveredForDetail())
        ImGui::SetTooltip("%s", text);
}

static void DrawResetTooltip(bool isDefault, const char* detail = nullptr)
{
    if (!IsItemHoveredForDetail())
        return;

    if (!isDefault && detail)
        ImGui::SetTooltip("%s\n%s", detail, messages::playback::cRightClickReset);
    else if (!isDefault)
        ImGui::SetTooltip("%s", messages::playback::cRightClickReset);
    else if (detail)
        ImGui::SetTooltip("%s", detail);
}

static bool DrawCheckRow(const char* label, const char* id, bool* value, const char* detail = nullptr)
{
    DrawRowLabel(label);

    const bool changed = ImGui::Checkbox(id, value);

    if (detail)
        DrawDetailTooltip(detail);

    return changed;
}

static bool DrawCountSliderRow(const char* label, const char* id, s32* value, s32 min, s32 max, s32 reset, const char* disabledReason = nullptr)
{
    ImGui::BeginDisabled(disabledReason != nullptr);

    DrawRowLabel(label);

    ImDrawList* drawList = BeginSliderFill();
    ImGui::SliderInt(id, value, min, max, "%d", ImGuiSliderFlags_AlwaysClamp);
    EndSliderFill(drawList, static_cast<f32>(*value - min) / (max - min));

    const bool done = ResetItemOnRightClick(value, reset) || ImGui::IsItemDeactivatedAfterEdit();

    DrawResetTooltip(*value == reset);

    ImGui::EndDisabled();

    if (disabledReason)
        SetDisabledTooltip(disabledReason);

    return done;
}

static bool DrawAmountSliderRow(const char* label, const char* id, f32* value, f32 min, f32 max, f32 reset, const char* detail, bool* done)
{
    DrawRowLabel(label);

    ImDrawList* drawList = BeginSliderFill();
    bool changed = ImGui::SliderFloat(id, value, min, max, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    EndSliderFill(drawList, (*value - min) / (max - min));

    if (ResetItemOnRightClick(value, reset))
    {
        changed = true;
        *done = true;
    }

    DrawResetTooltip(*value == reset, detail);

    if (ImGui::IsItemDeactivatedAfterEdit())
        *done = true;

    return changed;
}

static bool DrawValueButtonRow(const char* label, const char* text, bool dim, const char* button, bool enabled, const char* detail = nullptr)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const f32 buttonWidth = ImFloor(ImGui::GetFontSize() * cRowButtonEm);

    DrawRowLabel(label);

    if (dim)
        ImGui::TextDisabled("%s", text);
    else
        ImGui::TextUnformatted(text);

    if (detail)
        DrawDetailTooltip(detail);

    ImGui::SameLine(ImGui::GetContentRegionMax().x - buttonWidth - style.CellPadding.x);

    ImGui::PushID(label);
    ImGui::BeginDisabled(!enabled);

    const bool pressed = ImGui::Button(button, ImVec2(buttonWidth, 0.0f));

    ImGui::EndDisabled();
    ImGui::PopID();

    return pressed;
}

static void UpdateBackupUsage()
{
    backup::GetUsage(&sBackupCount, &sBackupSize);
    sBackupFolderFound = !backup::GetRootFolder().empty();
}

static void DrawBackupFolderRow()
{
    char size[32];
    backup::FormatSize(sBackupSize, size, sizeof(size));

    const sead::FormatFixedSafeString<64> usage(messages::preferences::cBackupUsageFormat, sBackupCount, sBackupCount == 1 ? messages::common::cBackup : messages::common::cBackups, size);
    const bool empty = sBackupCount == 0;

    if (DrawValueButtonRow(messages::preferences::cBackupFolderLabel, empty ? messages::common::cEmpty : usage.cstr(), empty, messages::preferences::cOpenFolder, sBackupFolderFound))
        OpenFolder(backup::GetRootFolder());
}

static void DrawBackupRows()
{
    // Automatic backup
    {
        const char* labels[IM_ARRAYSIZE(cBackupPresets)];
        s32 selected = sAutoBackupEnabled ? cBackupDefaultPreset : 0;

        for (s32 i = 0; i < IM_ARRAYSIZE(cBackupPresets); i++)
        {
            labels[i] = cBackupPresets[i].label;

            if (sAutoBackupEnabled && i > 0 && cBackupPresets[i].minutes == sAutoBackupIntervalMinutes)
                selected = i;
        }

        DrawRowLabel(messages::preferences::cBackupLabel);
        if (ComboScroll("##AutoBackup", &selected, labels, IM_ARRAYSIZE(cBackupPresets)))
        {
            sAutoBackupEnabled = selected > 0;

            if (sAutoBackupEnabled)
                sAutoBackupIntervalMinutes = cBackupPresets[selected].minutes;

            SaveBackupConfig();
        }

        DrawDetailTooltip(messages::preferences::cBackupDetail);
    }

    // Maximum backups
    if (DrawCountSliderRow(messages::preferences::cBackupCountLabel, "##BackupCount", &sAutoBackupMaxBackups, cBackupCountMin, cBackupCountMax, cBackupCountDefault, sAutoBackupEnabled ? nullptr : messages::preferences::cBackupCountDisabled))
        SaveBackupConfig();

    DrawBackupFolderRow();
}

static void DrawSavingRows()
{
    // External archive metadata
    if (DrawCheckRow(messages::preferences::cMetadataLabel, "##Metadata", &sSaveMetadataDefault, messages::preferences::cMetadataDetail))
        SaveArchiveConfig();

    // Files in use
    {
        const char* labels[] = { messages::preferences::cInUseAsk, messages::preferences::cInUseSave };
        s32 selected = sForceSaveWhenInUse ? 1 : 0;

        DrawRowLabel(messages::preferences::cInUseLabel);
        if (ComboScroll("##InUse", &selected, labels, IM_ARRAYSIZE(labels)))
        {
            sForceSaveWhenInUse = selected == 1;
            SaveArchiveConfig();
        }
    }

    // Unused group files
    {
        const char* labels[] = { messages::preferences::cUnusedGroupFilesKeep, messages::preferences::cUnusedGroupFilesDelete };
        s32 selected = sDeleteUnusedGroupFiles ? 1 : 0;

        DrawRowLabel(messages::preferences::cUnusedGroupFilesLabel);
        DetailTooltip(messages::preferences::cUnusedGroupFilesDetail);

        if (ComboScroll("##UnusedGroupFiles", &selected, labels, IM_ARRAYSIZE(labels)))
        {
            sDeleteUnusedGroupFiles = selected == 1;
            SaveArchiveConfig();
        }
    }
}

static void DrawAssociationRow()
{
    const bool associated = sAssociation == fileassoc::State::ThisCopy;
    const char* text = associated ? messages::preferences::cAssociationOn : messages::preferences::cAssociationNone;

    if (!DrawValueButtonRow(messages::preferences::cAssociationLabel, text, !associated, associated ? messages::preferences::cAssociationRemove : messages::preferences::cAssociationAdd, true, messages::preferences::cAssociationDetail))
        return;

    if (!(associated ? fileassoc::Remove() : fileassoc::Register()))
        PopupMgr::instance()->addPopup({messages::preferences::cAssociationFailed});

    sAssociation = fileassoc::GetState();
}

static void DrawOpeningRows()
{
    StartupSettings& settings = GetStartupSettings();

    // Startup
    {
        const char* labels[] = { messages::preferences::cStartupStartPage, messages::preferences::cStartupLastArchive };
        const char* details[] = { messages::preferences::cStartupStartPageDetail, nullptr };
        s32 selected = static_cast<s32>(settings.action);

        DrawRowLabel(messages::preferences::cStartupLabel);
        if (ComboScroll("##Startup", &selected, labels, details, IM_ARRAYSIZE(labels)))
        {
            settings.action = static_cast<StartupAction>(selected);
            SaveStartupConfig();
        }
    }

    // Recent files
    if (DrawCountSliderRow(messages::common::cRecentFiles, "##RecentCount", &settings.recentFileCount, cRecentFileCountMin, cRecentFileCountMax, cRecentFileCountDefault))
    {
        SaveStartupConfig();
        TrimRecentFiles();
    }

    // Ignored warnings
    {
        PopupMgr* popupMgr = PopupMgr::instance();

        const size_t count = popupMgr->getIgnoredWarningArchiveCount();
        const sead::FormatFixedSafeString<64> text(messages::preferences::cIgnoredCountFormat, count, count == 1 ? messages::common::cArchive : messages::preferences::cIgnoredArchivePlural);

        if (DrawValueButtonRow(messages::preferences::cIgnoredLabel, count == 0 ? messages::common::cNone : text.cstr(), count == 0, messages::common::cReset, count != 0, messages::preferences::cIgnoredDetail))
            popupMgr->clearIgnoredWarnings();
    }
}

static void DrawFilePage()
{
    DrawPageHeader(ICON_LC_FILE, messages::common::cFile, nullptr);

    if (BeginGroupBox(messages::common::cBackup))
    {
        DrawBackupRows();
        EndGroupBox();
    }

    if (BeginGroupBox(messages::preferences::cBoxSaving))
    {
        DrawSavingRows();
        EndGroupBox();
    }

    if (BeginGroupBox(messages::preferences::cBoxOpening))
    {
        DrawOpeningRows();
        EndGroupBox();
    }
}

static void DrawOutputDeviceRows()
{
    // Playback device
    {
        const u32 deviceNum = snd::SoundSystem::getPlaybackDeviceCount();

        std::vector<const char*> names(deviceNum);
        for (u32 i = 0; i < deviceNum; i++)
            names[i] = snd::SoundSystem::getPlaybackDeviceName(i);

        s32 selected = static_cast<s32>(snd::SoundSystem::getPlaybackDevice());

        DrawRowLabel(messages::preferences::cDeviceLabel);

        if (deviceNum == 0)
        {
            ImGui::TextDisabled("%s", messages::preferences::cDeviceEmpty);
        }
        else if (ComboScroll("##Device", &selected, names.data(), static_cast<s32>(deviceNum)))
        {
            snd::SoundSystem::setPlaybackDevice(static_cast<u32>(selected));
            GetAudioSettings().playbackDeviceName = selected == 0 ? std::string() : names[selected];
            SaveAudioConfig();
        }
    }

    // Sample rate
    {
        const char* labels[IM_ARRAYSIZE(cSampleRates)];
        s32 selected = cSampleRateDefault;

        for (s32 i = 0; i < IM_ARRAYSIZE(cSampleRates); i++)
        {
            labels[i] = cSampleRates[i].label;

            if (cSampleRates[i].rate == GetAudioSettings().outputSampleRate)
                selected = i;
        }

        DrawRowLabel(messages::common::cSampleRate);
        if (ComboScroll("##SampleRate", &selected, labels, IM_ARRAYSIZE(cSampleRates)))
        {
            GetAudioSettings().outputSampleRate = cSampleRates[selected].rate;
            snd::SoundSystem::setOutputSampleRate(GetAudioSettings().outputSampleRate);
            SaveAudioConfig();
        }
    }
}

static void DrawPlaybackRows()
{
    AudioSettings& settings = GetAudioSettings();

    // Output mode
    {
        const char* labels[] = { messages::common::cStereo, messages::common::cMono };
        s32 selected = settings.monoOutput ? 1 : 0;

        DrawRowLabel(messages::preferences::cOutputModeLabel);
        if (ComboScroll("##OutputMode", &selected, labels, IM_ARRAYSIZE(labels)))
        {
            settings.monoOutput = selected == 1;
            snd::SoundSystem::setOutputMode(settings.monoOutput ? snd::OutputMode::Mono : snd::OutputMode::Stereo);
            SaveAudioConfig();
        }
    }

    // Interpolation
    {
        const char* labels[] = { messages::bank::cInterpolationPolyphase, messages::common::cLinear };
        s32 selected = settings.linearInterpolation ? 1 : 0;

        DrawRowLabel(messages::common::cInterpolation);
        if (ComboScroll("##Interpolation", &selected, labels, IM_ARRAYSIZE(labels)))
        {
            settings.linearInterpolation = selected == 1;
            snd::SoundSystem::setSrcType(settings.linearInterpolation ? snd::SrcType::Type_Linear : snd::SrcType::Type_4Tap);
            SaveAudioConfig();
        }

        DrawDetailTooltip(messages::preferences::cInterpolationDetail);
    }

    // Output meter window
    {
        bool visible = IsOutputMeterWindowVisible();

        if (DrawCheckRow(messages::mainmenu::cOutputMeter, "##OutputMeter", &visible))
            SetOutputMeterWindowVisible(visible);
    }
}

static void FormatOutputInfo(sead::BufferedSafeString* text)
{
    const snd::SoundSystem::OutputInfo info = snd::SoundSystem::getOutputInfo();

    text->clear();

    if (info.channelCount == 0)
        return;

    sead::FixedSafeString<32> channels;

    if (info.channelCount == 1)
        channels = messages::common::cMono;
    else if (info.channelCount == 2)
        channels = messages::common::cStereo;
    else
        channels.format(messages::common::cChannelCountFormat, info.channelCount);

    text->format(messages::preferences::cOutputInfoFormat, channels.cstr(), info.sampleRate, info.bufferMsec);
}

static void DrawAudioPage()
{
    if (DrawPageHeader(ICON_LC_VOLUME_2, messages::preferences::cPageAudio, nullptr, messages::preferences::cRefreshDevices))
        snd::SoundSystem::refreshPlaybackDevices();

    sead::FixedSafeString<64> outputInfo;
    FormatOutputInfo(&outputInfo);

    if (BeginGroupBoxWithStatus(messages::common::cDevice, outputInfo.cstr()))
    {
        DrawOutputDeviceRows();
        EndGroupBox();
    }

    if (BeginGroupBox(messages::common::cPlayback))
    {
        DrawPlaybackRows();
        EndGroupBox();
    }
}

static bool IsMidiDevicePresent(const char* name)
{
    const u32 deviceNum = MidiInput::getDeviceCount();

    for (u32 i = 0; i < deviceNum; i++)
    {
        const char* deviceName = MidiInput::getDeviceName(i);

        if (deviceName && strcmp(deviceName, name) == 0)
            return true;
    }

    return false;
}

static bool DrawMidiDeviceRow(const char* name, bool* on, bool present)
{
    bool changed = false;

    ImGui::TableNextRow();
    ImGui::PushID(name);

    ImGui::TableNextColumn();
    changed = ImGui::Checkbox("##On", on);

    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(present ? ImGuiCol_Text : ImGuiCol_TextDisabled));
    ImGui::TextUnformatted(name);
    ImGui::PopStyleColor();

    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();

    if (!present)
    {
        ImGui::TextColored(cMidiNotFoundColor, "%s", messages::preferences::cMidiNotFound);

        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            ImGui::SetTooltip("%s", messages::preferences::cMidiNotFoundDetail);
    }
    else if (*on)
    {
        ImGui::TextColored(cMidiConnectedColor, "%s", messages::preferences::cMidiConnected);
    }

    ImGui::PopID();

    return changed;
}

static void DrawMidiNotesBox()
{
    MidiSettings& settings = GetMidiSettings();

    if (!BeginGroupBox(messages::common::cNotes))
        return;

    // Velocity
    {
        const char* labels[] = { messages::preferences::cVelocityDevice, messages::preferences::cVelocityFixed };
        s32 selected = settings.fixedVelocity ? 1 : 0;

        DrawRowLabel(messages::preferences::cVelocityLabel);
        if (ComboScroll("##Velocity", &selected, labels, IM_ARRAYSIZE(labels)))
        {
            settings.fixedVelocity = selected == 1;
            SaveMidiConfig();
        }
    }

    // Fixed velocity
    if (DrawCountSliderRow(messages::preferences::cFixedVelocityLabel, "##FixedVelocity", &settings.velocity, cMidiVelocityMin, cMidiVelocityMax, cMidiVelocityDefault, settings.fixedVelocity ? nullptr : messages::preferences::cFixedVelocityDisabled))
        SaveMidiConfig();

    EndGroupBox();
}

static void DrawMidiDeviceTable(f32 height)
{
    const ImGuiStyle& style = ImGui::GetStyle();

    const f32 statusWidth = sead::Mathf::max(ImGui::CalcTextSize(messages::preferences::cMidiConnected).x, ImGui::CalcTextSize(messages::preferences::cMidiNotFound).x);

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(ImFloor(ImGui::GetFontSize() * cSoundPanelPadXEm), ImFloor(ImGui::GetFontSize() * cSoundPanelPadYEm)));

    if (!ImGui::BeginTable("##MidiDevices", 3, ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerH, ImVec2(0.0f, height)))
    {
        ImGui::PopStyleVar();
        return;
    }

    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn(messages::common::cOn, ImGuiTableColumnFlags_WidthFixed, sead::Mathf::max(ImGui::GetFrameHeight(), ImGui::CalcTextSize(messages::common::cOn).x));
    ImGui::TableSetupColumn(messages::common::cDevice, ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn(messages::preferences::cMidiColumnStatus, ImGuiTableColumnFlags_WidthFixed, statusWidth + style.ItemSpacing.x);
    ImGui::TableHeadersRow();

    const u32 deviceNum = MidiInput::getDeviceCount();
    bool any = false;

    for (u32 i = 0; i < deviceNum; i++)
    {
        const char* name = MidiInput::getDeviceName(i);

        if (!name || name[0] == '\0')
            continue;

        any = true;

        bool on = MidiIsDeviceConnected(name);

        if (!DrawMidiDeviceRow(name, &on, true))
            continue;

        if (!on)
        {
            MidiDisconnectDevice(name);
            SetMidiDeviceEnabled(name, false);
        }
        else if (MidiConnectDevice(static_cast<s32>(i)))
        {
            SetMidiDeviceEnabled(name, true);
        }
    }

    const std::vector<std::string>& enabledDevices = GetMidiEnabledDevices();

    for (size_t i = 0; i < enabledDevices.size(); i++)
    {
        const std::string& name = enabledDevices[i];

        if (IsMidiDevicePresent(name.c_str()))
            continue;

        any = true;

        bool on = true;

        if (DrawMidiDeviceRow(name.c_str(), &on, false))
        {
            const std::string removed = name;

            MidiDisconnectDevice(removed.c_str());
            SetMidiDeviceEnabled(removed.c_str(), false);
            break;
        }
    }

    if (!any)
    {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(1);
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", messages::preferences::cMidiEmpty);
    }

    ImGui::EndTable();
    ImGui::PopStyleVar();
}

static void DrawMidiPage()
{
    if (DrawPageHeader(ICON_LC_PIANO, messages::preferences::cPageMidi, nullptr, messages::preferences::cRefreshDevices))
        MidiInput::refreshDevices();

    const f32 notesHeight = sMidiNotesHeight > 0.0f ? sMidiNotesHeight : ImFloor(ImGui::GetFontSize() * cMidiNotesHeightEm);
    const f32 top = ImGui::GetCursorScreenPos().y;
    const f32 height = sead::Mathf::max(ImGui::GetContentRegionAvail().y - 1.0f - notesHeight, ImGui::GetFrameHeightWithSpacing() * 2.0f);

    DrawMidiDeviceTable(height);
    GroupBoxGap();

    DrawMidiNotesBox();

    sMidiNotesHeight = ImGui::GetCursorScreenPos().y - ImGui::GetStyle().ItemSpacing.y - top - height;
}

static ImU32 GetAccentColor(f32 hue)
{
    ImVec4 color(0.0f, 0.0f, 0.0f, 1.0f);
    ImGui::ColorConvertHSVtoRGB(hue, cAccentSaturation, cAccentValue, color.x, color.y, color.z);

    return ImGui::ColorConvertFloat4ToU32(color);
}

static bool IsDefaultAccentColor()
{
    return gAccentColor.x == cDefaultAccentColor.x && gAccentColor.y == cDefaultAccentColor.y && gAccentColor.z == cDefaultAccentColor.z && gAccentColor.w == cDefaultAccentColor.w;
}

static bool DrawHueSlider(const char* id, f32* hue, bool* done)
{
    constexpr s32 cSegmentNum = 6;

    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 size(ImGui::CalcItemWidth(), ImGui::GetFrameHeight());
    const f32 handleWidth = ImFloor(ImGui::GetFontSize() * cHueHandleEm);
    const f32 trackStart = pos.x + handleWidth * 0.5f;
    const f32 trackWidth = size.x - handleWidth;

    ImGui::InvisibleButton(id, size);

    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();

    if (held && trackWidth > 0.0f)
        *hue = sead::Mathf::clamp2(0.0f, (ImGui::GetMousePos().x - trackStart) / trackWidth, 1.0f) * cAccentHueMax;

    if (ImGui::IsItemDeactivated())
        *done = true;

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    for (s32 i = 0; i < cSegmentNum; i++)
    {
        const f32 start = static_cast<f32>(i) / cSegmentNum;
        const f32 end = static_cast<f32>(i + 1) / cSegmentNum;
        const ImU32 startColor = GetAccentColor(start * cAccentHueMax);
        const ImU32 endColor = GetAccentColor(end * cAccentHueMax);

        const ImVec2 min(ImFloor(trackStart + trackWidth * start), pos.y);
        const ImVec2 max(ImFloor(trackStart + trackWidth * end), pos.y + size.y);

        drawList->AddRectFilledMultiColor(min, max, startColor, endColor, endColor, startColor);
    }

    // The ends below the handle
    drawList->AddRectFilled(pos, ImVec2(ImFloor(trackStart) + 1.0f, pos.y + size.y), GetAccentColor(0.0f), style.FrameRounding, ImDrawFlags_RoundCornersLeft);
    drawList->AddRectFilled(ImVec2(ImFloor(trackStart + trackWidth) - 1.0f, pos.y), pos + size, GetAccentColor(cAccentHueMax), style.FrameRounding, ImDrawFlags_RoundCornersRight);

    const f32 handlePos = sead::Mathf::min(*hue / cAccentHueMax, 1.0f);
    const f32 handleX = ImFloor(trackStart + trackWidth * handlePos - handleWidth * 0.5f);
    const ImVec2 handleMin(handleX, pos.y);
    const ImVec2 handleMax(handleX + handleWidth, pos.y + size.y);

    drawList->AddRectFilled(handleMin, handleMax, GetAccentColor(*hue), style.GrabRounding);
    drawList->AddRect(handleMin, handleMax, ImGui::GetColorU32(ImGuiCol_Text, (held || hovered) ? 1.0f : 0.7f), style.GrabRounding, 0, held ? 2.0f : 1.0f);

    return held;
}

static void DrawThemeCards(bool* changed, bool* done)
{
    const f32 em = ImGui::GetFontSize();
    const f32 gap = ImFloor(em * cCardGapEm);
    const f32 width = ImFloor((ImGui::GetContentRegionAvail().x - gap) * 0.5f);

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImFloor(em * cRowGapEm));

    if (DrawThemeCard(messages::mainmenu::cThemeDark, width, true, gThemeIsDark) && !gThemeIsDark)
    {
        gThemeIsDark = true;
        *changed = true;
        *done = true;
    }

    ImGui::SameLine(0.0f, gap);

    if (DrawThemeCard(messages::mainmenu::cThemeLight, width, false, !gThemeIsDark) && gThemeIsDark)
    {
        gThemeIsDark = false;
        *changed = true;
        *done = true;
    }
}

static void DrawColorRows(bool* changed, bool* done)
{
    // Hue
    {
        f32 hue, saturation, value;
        ImGui::ColorConvertRGBtoHSV(gAccentColor.x, gAccentColor.y, gAccentColor.z, hue, saturation, value);

        DrawRowLabel(messages::preferences::cHueLabel);
        if (DrawHueSlider("##Hue", &hue, done))
        {
            ImGui::ColorConvertHSVtoRGB(hue, cAccentSaturation, cAccentValue, gAccentColor.x, gAccentColor.y, gAccentColor.z);
            gAccentColor.w = 1.0f;
            *changed = true;
        }

        const bool isDefault = IsDefaultAccentColor();

        if (!isDefault && ImGui::IsItemClicked(ImGuiMouseButton_Right))
        {
            gAccentColor = cDefaultAccentColor;
            *changed = true;
            *done = true;
        }

        DrawResetTooltip(isDefault);
    }

    const ThemeAdjust defaults;
    ThemeAdjust& adjust = GetThemeAdjust();

    *changed |= DrawAmountSliderRow(messages::preferences::cSaturationLabel, "##Saturation", &adjust.saturation, cSaturationMin, cSaturationMax, defaults.saturation, nullptr, done);
    *changed |= DrawAmountSliderRow(messages::preferences::cBrightnessLabel, "##Brightness", &adjust.brightness, cBrightnessMin, cBrightnessMax, defaults.brightness, nullptr, done);
    *changed |= DrawAmountSliderRow(messages::preferences::cContrastLabel, "##Contrast", &adjust.contrast, cContrastMin, cContrastMax, defaults.contrast, nullptr, done);
    *changed |= DrawAmountSliderRow(messages::preferences::cGradientLabel, "##Gradient", &adjust.gradient, cGradientMin, cGradientMax, defaults.gradient, messages::preferences::cGradientDetail, done);
}

static void DrawAppearancePage()
{
    bool changed = false;
    bool done = false;

    DrawPageHeader(ICON_LC_PALETTE, messages::preferences::cPageAppearance, nullptr);

    if (BeginGroupBox(messages::common::cTheme))
    {
        DrawThemeCards(&changed, &done);
        EndGroupBox();
    }

    bool reset = false;

    if (BeginGroupBox(messages::preferences::cBoxColors, messages::preferences::cAppearanceReset, &reset))
    {
        DrawColorRows(&changed, &done);
        EndGroupBox();
    }

    if (reset)
    {
        gAccentColor = cDefaultAccentColor;
        GetThemeAdjust() = ThemeAdjust();

        changed = true;
        done = true;
    }

    if (changed)
        sThemeChanged = true;

    if (done)
        SaveAccentColor();
}

static bool DrawTooltipDelayRow(f32* seconds)
{
    const char* labels[IM_ARRAYSIZE(cTooltipDelays)];
    s32 selected = 0;

    for (s32 i = 0; i < IM_ARRAYSIZE(cTooltipDelays); i++)
    {
        labels[i] = cTooltipDelays[i].label;

        if (ImFabs(cTooltipDelays[i].seconds - *seconds) < ImFabs(cTooltipDelays[selected].seconds - *seconds))
            selected = i;
    }

    DrawRowLabel(messages::preferences::cTooltipDelayLabel);

    if (!ComboScroll("##TooltipDelay", &selected, labels, IM_ARRAYSIZE(cTooltipDelays)))
        return false;

    *seconds = cTooltipDelays[selected].seconds;
    return true;
}

static bool DrawTimeFormatRow(bool* twelveHour)
{
    const s64 now = static_cast<s64>(std::time(nullptr));
    sead::FixedSafeString<32> texts[2];

    for (s32 i = 0; i < 2; i++)
    {
        char clock[16];
        timeutil::FormatTimeOfDay(clock, sizeof(clock), now, i == 1);
        texts[i].format(i == 1 ? messages::preferences::cTimeFormat12Format : messages::preferences::cTimeFormat24Format, clock);
    }

    const char* labels[] = { texts[0].cstr(), texts[1].cstr() };
    s32 selected = *twelveHour ? 1 : 0;

    DrawRowLabel(messages::preferences::cTimeFormatLabel);

    if (!ComboScroll("##TimeFormat", &selected, labels, IM_ARRAYSIZE(labels)))
        return false;

    *twelveHour = selected == 1;
    return true;
}

static bool DrawUIScaleRow(f32* scale)
{
    sead::FixedSafeString<8> texts[IM_ARRAYSIZE(cUIScales)];
    const char* labels[IM_ARRAYSIZE(cUIScales)];
    s32 selected = 0;

    for (s32 i = 0; i < IM_ARRAYSIZE(cUIScales); i++)
    {
        texts[i].format(messages::common::cPercentFormat, static_cast<s32>(cUIScales[i] * 100.0f + 0.5f));
        labels[i] = texts[i].cstr();

        if (ImFabs(cUIScales[i] - *scale) < ImFabs(cUIScales[selected] - *scale))
            selected = i;
    }

    DrawRowLabel(messages::preferences::cUIScaleLabel);

    const bool changed = ComboScroll("##UIScale", &selected, labels, IM_ARRAYSIZE(cUIScales));

    if (changed)
        *scale = cUIScales[selected];

    return changed;
}

static void DrawInterfacePage()
{
    InterfaceSettings& settings = GetInterfaceSettings();
    bool selected = false;
    bool dragged = false;
    bool released = false;

    DrawPageHeader(ICON_LC_APP_WINDOW, messages::preferences::cPageInterface, nullptr);

    if (BeginGroupBox(messages::preferences::cBoxMotion))
    {
        selected |= DrawCheckRow(messages::preferences::cAnimationsLabel, "##Animations", &settings.animations, messages::preferences::cAnimationsDetail);
        selected |= DrawCheckRow(messages::preferences::cSmoothScrollLabel, "##SmoothScroll", &settings.smoothScrolling);
        dragged |= DrawAmountSliderRow(messages::preferences::cScrollSpeedLabel, "##ScrollSpeed", &settings.scrollSpeed, cScrollSpeedMin, cScrollSpeedMax, cScrollSpeedDefault, nullptr, &released);
        EndGroupBox();
    }

    const bool restart = settings.uiScale != GetUIScale();

    if (restart ? BeginGroupBoxWithStatus(messages::preferences::cBoxDisplay, messages::preferences::cUIScaleRestart) : BeginGroupBox(messages::preferences::cBoxDisplay))
    {
        selected |= DrawUIScaleRow(&settings.uiScale);
        selected |= DrawCheckRow(messages::preferences::cShadowsLabel, "##Shadows", &settings.windowShadows);
        selected |= DrawTooltipDelayRow(&settings.tooltipDelay);
        selected |= DrawTimeFormatRow(&settings.twelveHourClock);
        EndGroupBox();
    }

    if (selected || dragged)
        ApplyInterfaceConfig();

    if (selected || released)
        SaveInterfaceConfig();
}

static void DrawResetAllDialog()
{
    CenterNextDialog();

    if (!BeginDialog(sead::FormatFixedSafeString<64>(ICON_LC_ROTATE_CCW " %s###ResetAllSettings", messages::preferences::cResetAll).cstr(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    BeginDialogMessage(ICON_LC_CIRCLE_HELP);
    ImGui::TextUnformatted(messages::preferences::cResetAllQuestion);
    ImGui::TextColored(GetItemValueColor(), "%s", messages::preferences::cResetAllDetail);
    EndDialogMessage();

    BeginDialogFooter({messages::common::cReset, messages::common::cCancel});

    if (DialogButton(messages::common::cReset))
    {
        sResetAllRequested = true;
        ImGui::CloseCurrentPopup();
    }

    if (DialogButton(messages::common::cCancel))
        ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}

static void DrawAdvancedPage()
{
    DrawPageHeader(ICON_LC_CPU, messages::preferences::cPageAdvanced, nullptr);

    if (BeginGroupBox(messages::preferences::cBoxDebug))
    {
        bool shown = IsSystemWindowShown();

        if (DrawCheckRow(messages::mainmenu::cSystemWindow, "##SystemWindow", &shown))
            SetSystemWindowShown(shown);

        EndGroupBox();
    }

    bool reset = false;

    if (BeginGroupBox(messages::preferences::cBoxSettings, messages::preferences::cResetAll, &reset))
    {
        const std::string folder = GetConfigDir();

        if (DrawValueButtonRow(messages::preferences::cSettingsFolderLabel, CutPathStart(folder, cFolderTextMax).c_str(), false, messages::preferences::cOpenFolder, true))
            OpenFolder(folder);

        EndGroupBox();
    }

    if (fileassoc::IsSupported() && BeginGroupBox(messages::common::cSystem))
    {
        DrawAssociationRow();
        EndGroupBox();
    }

    if (reset)
        ImGui::OpenPopup("###ResetAllSettings");

    DrawResetAllDialog();
}

static const PageInfo cPages[] = {
    { PreferencesPage::File,       ICON_LC_FILE,       messages::common::cFile,       DrawFilePage       },
    { PreferencesPage::Audio,      ICON_LC_VOLUME_2,   messages::preferences::cPageAudio,      DrawAudioPage      },
    { PreferencesPage::Midi,       ICON_LC_PIANO,      messages::preferences::cPageMidi,       DrawMidiPage       },
    { PreferencesPage::Appearance, ICON_LC_PALETTE,    messages::preferences::cPageAppearance, DrawAppearancePage },
    { PreferencesPage::Interface,  ICON_LC_APP_WINDOW, messages::preferences::cPageInterface,  DrawInterfacePage  },
    { PreferencesPage::Advanced,   ICON_LC_CPU,        messages::preferences::cPageAdvanced,   DrawAdvancedPage   },
};

void DrawPreferencesMenu()
{
    for (const PageInfo& info : cPages)
    {
        if (MenuBarItem(sead::FormatFixedSafeString<96>("%s %s", info.icon, info.name).cstr()))
            OpenPreferencesWindow(info.page);
    }
}

void DrawPreferencesWindow()
{
    const sead::FormatFixedSafeString<64> title(ICON_LC_SETTINGS_2 " %s###PrefsPopup", messages::mainmenu::cPreferences);

    if (sRequestOpen)
    {
        sRequestOpen = false;

        snd::SoundSystem::refreshPlaybackDevices();
        MidiInput::refreshDevices();
        UpdateBackupUsage();
        sAssociation = fileassoc::GetState();

        ImGui::OpenPopup(title.cstr());
    }

    const f32 em = ImGui::GetFontSize();

    CenterNextDialog();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ImFloor(em * cWindowPadEm), ImFloor(em * cWindowPadEm)));

    ImVec4 background = ImGui::GetStyleColorVec4(ImGuiCol_PopupBg);
    background.w = 1.0f;

    ImGui::PushStyleColor(ImGuiCol_PopupBg, background);

    const bool open = ImGui::BeginPopupModal(title.cstr(), nullptr, ImGuiWindowFlags_AlwaysAutoResize);

    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    if (!open)
        return;

    AnimateWindow(WindowAnimation::Dialog);

    const ImVec2 size(ImFloor(em * cWindowWidthEm), ImFloor(em * cWindowHeightEm));
    const ImVec2 padding(ImFloor(em * cPagePadXEm), ImFloor(em * cPagePadYEm));
    const ImVec2 origin = ImGui::GetCursorScreenPos();

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, padding);

    if (ImGui::BeginTable("##Pages", 2, ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerV, size))
    {
        ImGui::TableSetupColumn("##Nav", ImGuiTableColumnFlags_WidthFixed, ImFloor(em * cNavWidthEm));
        ImGui::TableSetupColumn("##Page", ImGuiTableColumnFlags_WidthStretch);

        ImGui::TableNextColumn();
        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, ImGui::GetColorU32(ImGuiCol_FrameBg, cNavTintAlpha));
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::GetStyle().FramePadding.y);
        ImGui::PushItemFlag(ImGuiItemFlags_SelectableDontClosePopup, true);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, ImFloor(em * cNavRowGapEm)));

        for (const PageInfo& info : cPages)
        {
            ImGui::PushID(static_cast<s32>(info.page));

            if (DrawNavRow(info.icon, info.name, sPage == info.page))
                sPage = info.page;

            if (sPage == info.page)
            {
                const ImVec2 rowMin = ImGui::GetItemRectMin();
                const ImVec2 rowMax = ImGui::GetItemRectMax();

                ImGui::GetWindowDrawList()->AddRectFilled(rowMin, ImVec2(rowMin.x + ImFloor(em * cNavBarWidthEm), rowMax.y), ImGui::GetColorU32(ImGuiCol_CheckMark));
            }

            ImGui::PopID();
        }

        ImGui::PopStyleVar();
        ImGui::PopItemFlag();

        ImGui::TableNextColumn();

        for (const PageInfo& info : cPages)
        {
            if (info.page != sPage)
                continue;

            ImGui::PushID(static_cast<s32>(info.page));

            if (ImGui::BeginChild("##Page", ImVec2(0.0f, size.y - padding.y * 2.0f), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground))
            {
                ImGuiWindow* page = ImGui::GetCurrentWindow();
                page->WorkRect.Max.x -= 1.0f;
                page->ContentRegionRect.Max.x -= 1.0f;

                info.draw();
            }

            ImGui::EndChild();
            ImGui::PopID();
        }

        ImGui::EndTable();
    }

    ImGui::PopStyleVar();

    if (sResetAllRequested)
    {
        sResetAllRequested = false;
        sThemeChanged = false;
        ResetAllSettings();
    }

    if (sThemeChanged)
    {
        sThemeChanged = false;
        ApplyThemeFromAccent(gAccentColor);
    }

    const f32 closeWidth = ImFloor(em * cCloseWidthEm);

    ImGui::SetCursorScreenPos(ImVec2(origin.x + size.x - closeWidth, ImGui::GetCursorScreenPos().y + ImFloor(em * cFooterGapEm)));

    if (ImGui::Button(messages::common::cClose, ImVec2(closeWidth, 0.0f)) || (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Escape)))
        ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}
