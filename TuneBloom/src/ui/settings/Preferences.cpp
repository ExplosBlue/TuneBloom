#include <ui/settings/Preferences.h>

#include <ui/backup/Backup.h>
#include <ui/settings/AppearancePage.h>
#include <ui/settings/Config.h>
#include <ui/settings/ConfigFile.h>
#include <ui/settings/FileAssociationDialog.h>
#include <ui/widgets/Dialogs.h>
#include <ui/file/FileAssociation.h>
#include <ui/items/ItemColumns.h>
#include <ui/app/MenuBar.h>
#include <messages/Messages.h>
#include <ui/player/OutputMeter.h>
#include <ui/widgets/PanelLayout.h>
#include <ui/app/PopupMgr.h>
#include <ui/widgets/Properties.h>
#include <ui/file/RecentFiles.h>
#include <ui/app/Shortcuts.h>
#include <ui/file/SystemShell.h>
#include <ui/settings/Theme.h>
#include <ui/widgets/TimeUtil.h>
#include <ui/settings/UiColors.h>
#include <ui/widgets/Widgets.h>
#include <ui/app/Workspace.h>

#include <libs/IconsLucide.h>
#include <imgui/imgui_custom.h>
#include <imgui/imgui_internal.h>

#include <bfsar/files/BankFile.h>
#include <midi/MidiInput.h>
#include <platform/SingleInstance.h>

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

}

static constexpr f32 cWindowWidthEm  = 38.0f;
static constexpr f32 cWindowHeightEm = 29.0f;
static constexpr f32 cNavWidthEm     = 8.0f;
static constexpr f32 cLabelWidthEm   = 8.0f;
static constexpr f32 cRowButtonEm    = 6.5f;

static constexpr f32 cMidiNotesHeightEm = 7.0f;
static constexpr f32 cUIScales[]        = { 1.0f, 1.25f, 1.5f };

static constexpr uicolors::Themed<f32> cSliderFillAlpha = { 0.55f, 0.22f };
static constexpr f32 cSliderValueWidthEm = 3.0f;

static constexpr size_t cFolderTextMax = 22;

static constexpr s32 cBackupCountMax      = 100;
static constexpr s32 cBackupDefaultPreset = 2;
static constexpr s32 cSampleRateDefault   = 5;

static constexpr f32 cPercentScale            = 100.0f;

static constexpr const char* cAmountFormat = "%.2f";

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

static PreferencesPage sPage = PreferencesPage::File;
static bool sRequestOpen = false;
static bool sThemeChanged = false;
static bool sResetAllRequested = false;

static size_t sBackupCount = 0;
static u64 sBackupSize = 0;
static bool sBackupFolderFound = false;


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

struct SliderFill
{
    ImDrawList* drawList;
    bool drawText;
};

static f32 CalcSliderValueWidth()
{
    return ImFloor(ImGui::GetFontSize() * cSliderValueWidthEm);
}

static void SetNextSliderWidth()
{
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - CalcSliderValueWidth() - ImGui::GetStyle().ItemSpacing.x);
}

static SliderFill BeginSliderFill(const char* id)
{
    SetNextSliderWidth();

    SliderFill fill;
    fill.drawList = ImGui::GetWindowDrawList();
    fill.drawText = !ImGui::TempInputIsActive(ImGui::GetID(id));

    fill.drawList->ChannelsSplit(2);
    fill.drawList->ChannelsSetCurrent(1);

    ImGui::PushStyleColor(ImGuiCol_FrameBg, uicolors::cTransparent);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, uicolors::cTransparent);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, uicolors::cTransparent);
    ImGui::PushStyleColor(ImGuiCol_Text, fill.drawText ? uicolors::cTransparent : ImGui::GetStyleColorVec4(ImGuiCol_Text));

    return fill;
}

static void EndSliderFill(const SliderFill& fill, f32 fraction, const char* text)
{
    constexpr f32 cGrabPadding = 2.0f;

    ImGui::PopStyleColor(4);

    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    const f32 travel = max.x - min.x - cGrabPadding * 2.0f - style.GrabMinSize;
    const f32 fillX = ImFloor(min.x + cGrabPadding + travel * sead::Mathf::clamp2(0.0f, fraction, 1.0f) + style.GrabMinSize * 0.5f);
    const ImGuiCol frameColor = ImGui::IsItemActive() ? ImGuiCol_FrameBgActive : ImGui::IsItemHovered() ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg;

    ImDrawList* drawList = fill.drawList;

    if (fill.drawText)
        drawList->AddText(ImVec2(max.x + style.ItemSpacing.x, min.y + style.FramePadding.y), ImGui::GetColorU32(ImGuiCol_Text), text);

    drawList->ChannelsSetCurrent(0);
    drawList->AddRectFilled(min, max, ImGui::GetColorU32(frameColor), style.FrameRounding);
    drawList->AddRectFilled(min, ImVec2(fillX, max.y), ImGui::GetColorU32(ImGuiCol_SliderGrab, uicolors::Pick(cSliderFillAlpha)), style.FrameRounding, ImDrawFlags_RoundCornersLeft);
    drawList->ChannelsMerge();
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

    const SliderFill fill = BeginSliderFill(id);
    ImGui::SliderInt(id, value, min, max, "%d", ImGuiSliderFlags_AlwaysClamp);

    char text[32];
    ImFormatString(text, sizeof(text), "%d", *value);
    EndSliderFill(fill, static_cast<f32>(*value - min) / (max - min), text);

    const bool done = ResetItemOnRightClick(value, reset) || ImGui::IsItemDeactivatedAfterEdit();

    if (!disabledReason)
        DrawResetTooltip(*value == reset);

    ImGui::EndDisabled();

    if (disabledReason)
        ImGui::SetItemTooltip("%s", disabledReason);

    return done;
}

static bool DrawAmountSliderRow(const char* label, const char* id, f32* value, f32 min, f32 max, f32 reset, const char* detail, bool* done, const char* format = cAmountFormat)
{
    DrawRowLabel(label);

    const SliderFill fill = BeginSliderFill(id);
    bool changed = ImGui::SliderFloat(id, value, min, max, format, ImGuiSliderFlags_AlwaysClamp);

    char text[64];
    ImFormatString(text, sizeof(text), format, *value);
    EndSliderFill(fill, (*value - min) / (max - min), text);

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

static bool DrawPercentSliderRow(const char* label, const char* id, f32* amount, f32 min, f32 max, f32 reset, bool* done)
{
    f32 percent = ImFloor(*amount * cPercentScale + 0.5f);
    const char* format = percent > 0.0f ? messages::common::cPercentFloatFormat : messages::common::cOff;
    const bool changed = DrawAmountSliderRow(label, id, &percent, min * cPercentScale, max * cPercentScale, reset * cPercentScale, nullptr, done, format);

    if (changed)
        *amount = percent / cPercentScale;

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

    if (DrawValueButtonRow(messages::preferences::cBackupFolderLabel, empty ? messages::common::cEmpty : usage.cstr(), empty, messages::common::cOpenFolder, sBackupFolderFound))
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
        const char* labels[] = { messages::common::cKeep, messages::preferences::cUnusedGroupFilesDelete };
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

static bool DrawAssociationRow()
{
    const u32 onCount = GetFileAssociationOnCount();
    const u32 typeCount = GetFileAssociationTypeCount();
    const sead::FormatFixedSafeString<32> count(messages::preferences::cAssociationCountFormat, onCount, typeCount);
    const char* text = onCount == typeCount ? messages::common::cAll : onCount == 0 ? messages::common::cNone : count.cstr();

    return DrawValueButtonRow(messages::preferences::cAssociationLabel, text, onCount == 0, messages::preferences::cAssociationChoose, true, messages::preferences::cAssociationDetail);
}

template <typename T, s32 N>
static void DrawStartupChoiceRow(const char* label, const char* id, T* value, const char* const (&labels)[N], const char* const (&details)[N])
{
    s32 selected = static_cast<s32>(*value);

    DrawRowLabel(label);
    if (!ComboScroll(id, &selected, labels, details, N))
        return;

    *value = static_cast<T>(selected);
    SaveStartupConfig();
}

static void DrawSystemOpenRow()
{
    if (!singleinstance::IsSupported())
        return;

    const char* labels[] = { messages::preferences::cSystemOpenNewTab, messages::preferences::cSystemOpenNewWindow };
    const char* details[] = { messages::preferences::cSystemOpenNewTabDetail, nullptr };

    DrawStartupChoiceRow(messages::preferences::cSystemOpenLabel, "##SystemOpen", &GetStartupSettings().systemOpen, labels, details);
}

static void DrawOpeningRows()
{
    StartupSettings& settings = GetStartupSettings();

    // Startup
    {
        const char* labels[] = { messages::preferences::cStartupStartPage, messages::preferences::cStartupLastArchive };
        const char* details[] = { messages::preferences::cStartupStartPageDetail, nullptr };

        DrawStartupChoiceRow(messages::preferences::cStartupLabel, "##Startup", &settings.action, labels, details);
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
        const sead::FormatFixedSafeString<64> text(messages::preferences::cIgnoredCountFormat, count, count == 1 ? messages::common::cArchive : messages::common::cArchives);

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
        s32 selected = settings.linearInterpolation ? 1 : 0;

        DrawRowLabel(messages::common::cInterpolation);
        if (ComboScroll("##Interpolation", &selected, messages::bank::cInterpolationTypes, IM_ARRAYSIZE(messages::bank::cInterpolationTypes)))
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
        ImGui::TextColored(uicolors::Warning(), "%s", messages::preferences::cMidiNotFound);

        ImGui::SetItemTooltip("%s", messages::preferences::cMidiNotFoundDetail);
    }
    else if (*on)
    {
        ImGui::TextColored(uicolors::Success(), "%s", messages::preferences::cMidiConnected);
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
    AutoConnectMidiDevices();

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

static void DrawAppearance()
{
    if (DrawAppearancePage())
        sThemeChanged = true;
}

static bool DrawScrollSmoothingRow(f32* amount, bool* done)
{
    return DrawPercentSliderRow(messages::preferences::cScrollSmoothingLabel, "##ScrollSmoothing", amount, cScrollSmoothingMin, cScrollSmoothingMax, cScrollSmoothingDefault, done);
}

static bool DrawTooltipDelayRow(f32* seconds, bool* done)
{
    const char* format = *seconds > 0.0f ? messages::common::cSecondsFormat : messages::preferences::cTooltipDelayInstant;

    return DrawAmountSliderRow(messages::preferences::cTooltipDelayLabel, "##TooltipDelay", seconds, cTooltipDelayMin, cTooltipDelayMax, cTooltipDelayDefault, nullptr, done, format);
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

static void DrawWindowLayoutRow()
{
    DrawRowLabel(messages::preferences::cWindowLayoutLabel);

    if (ImGui::Button(messages::common::cReset, ImVec2(ImFloor(ImGui::GetFontSize() * cRowButtonEm), 0.0f)))
        RequestResetLayout();

    DrawDetailTooltip(messages::preferences::cWindowLayoutDetail);
}

static void DrawInterfacePage()
{
    InterfaceSettings& settings = GetInterfaceSettings();
    bool selected = false;
    bool dragged = false;
    bool released = false;

    DrawPageHeader(ICON_LC_APP_WINDOW, messages::preferences::cPageInterface, nullptr);

    const bool restart = settings.uiScale != GetUIScale();

    if (restart ? BeginGroupBoxWithStatus(messages::preferences::cBoxDisplay, messages::preferences::cUIScaleRestart) : BeginGroupBox(messages::preferences::cBoxDisplay))
    {
        selected |= DrawUIScaleRow(&settings.uiScale);

        DrawWindowLayoutRow();

        selected |= DrawTimeFormatRow(&settings.twelveHourClock);
        dragged |= DrawTooltipDelayRow(&settings.tooltipDelay, &released);
        EndGroupBox();
    }

    if (BeginGroupBox(messages::preferences::cBoxMotion))
    {
        selected |= DrawCheckRow(messages::preferences::cAnimationsLabel, "##Animations", &settings.animations, messages::preferences::cAnimationsDetail);
        dragged |= DrawScrollSmoothingRow(&settings.scrollSmoothing, &released);
        dragged |= DrawAmountSliderRow(messages::preferences::cScrollSpeedLabel, "##ScrollSpeed", &settings.scrollSpeed, cScrollSpeedMin, cScrollSpeedMax, cScrollSpeedDefault, nullptr, &released);
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

    bool reset = false;

    if (BeginGroupBox(messages::preferences::cBoxSettings, messages::preferences::cResetAll, &reset))
    {
        const std::string folder = GetConfigDir();

        if (DrawValueButtonRow(messages::preferences::cSettingsFolderLabel, CutPathStart(folder, cFolderTextMax).c_str(), false, messages::common::cOpenFolder, true))
            OpenFolder(folder);

        EndGroupBox();
    }

    bool chooseTypes = false;

    if (fileassoc::IsSupported() && BeginGroupBox(messages::common::cSystem))
    {
        chooseTypes = DrawAssociationRow();
        DrawSystemOpenRow();
        EndGroupBox();
    }

    if (BeginGroupBox(messages::preferences::cBoxDebug))
    {
        bool shown = IsSystemWindowShown();

        if (DrawCheckRow(messages::mainmenu::cSystemWindow, "##SystemWindow", &shown))
            SetSystemWindowShown(shown);

        EndGroupBox();
    }

    if (reset)
        ImGui::OpenPopup("###ResetAllSettings");

    if (chooseTypes)
        OpenFileAssociationDialog();

    DrawResetAllDialog();
    DrawFileAssociationDialog();
}

static const PageInfo cPages[] = {
    { PreferencesPage::File,       ICON_LC_FILE,       messages::common::cFile,       DrawFilePage       },
    { PreferencesPage::Audio,      ICON_LC_VOLUME_2,   messages::preferences::cPageAudio,      DrawAudioPage      },
    { PreferencesPage::Midi,       ICON_LC_PIANO,      messages::preferences::cPageMidi,       DrawMidiPage       },
    { PreferencesPage::Appearance, ICON_LC_PALETTE,    messages::preferences::cPageAppearance, DrawAppearance },
    { PreferencesPage::Interface,  ICON_LC_APP_WINDOW, messages::preferences::cPageInterface,  DrawInterfacePage  },
    { PreferencesPage::Advanced,   ICON_LC_CPU,        messages::preferences::cPageAdvanced,   DrawAdvancedPage   },
};

void DrawPreferencesMenu()
{
    for (const PageInfo& info : cPages)
    {
        if (MenuBarIconItem(info.icon, info.name))
            OpenPreferencesWindow(info.page);
    }
}

static void DrawNavRows()
{
    for (const PageInfo& info : cPages)
    {
        ImGui::PushID(static_cast<s32>(info.page));

        if (DialogNavRow(info.icon, info.name, sPage == info.page))
            sPage = info.page;

        ImGui::PopID();
    }
}

static void DrawPage()
{
    for (const PageInfo& info : cPages)
    {
        if (info.page == sPage)
            info.draw();
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
        if (fileassoc::IsSupported())
            UpdateFileAssociations();

        ImGui::OpenPopup(title.cstr());
    }

    CenterNextDialog();

    if (!BeginDialog(title.cstr(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    const f32 em = ImGui::GetFontSize();
    const ImVec2 size(ImFloor(em * cWindowWidthEm), ImFloor(em * cWindowHeightEm));

    if (BeginNavDialog("##Pages", size, ImFloor(em * cNavWidthEm)))
    {
        DrawNavRows();

        if (BeginNavDialogPage(static_cast<s32>(sPage)))
            DrawPage();

        EndNavDialog();
    }

    if (sResetAllRequested)
    {
        sResetAllRequested = false;
        sThemeChanged = false;
        ResetAllSettings();
    }

    if (sThemeChanged)
    {
        sThemeChanged = false;
        ApplyTheme();
    }

    GroupBoxGap();
    BeginDialogFooter({messages::common::cClose}, false);

    if (DialogButton(messages::common::cClose) || (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows | ImGuiFocusedFlags_NoPopupHierarchy) && shortcuts::Pressed(shortcuts::Action::CloseDialog)))
        ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}
