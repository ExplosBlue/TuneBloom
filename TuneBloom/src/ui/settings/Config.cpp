#include <ui/settings/Config.h>

#include <ui/settings/ConfigFile.h>
#include <core/TestHooks.h>
#include <ui/settings/Theme.h>
#include <ui/settings/UiColors.h>
#include <ui/widgets/TimeUtil.h>
#include <ui/widgets/WindowAnimation.h>

#include <imgui/imgui_custom.h>

#include <platform/SystemTheme.h>

#include <snd/SoundSystem.h>

#include <midi/MidiInput.h>

#include <bfsar/files/BankFile.h>

#include <string>
#include <vector>

static constexpr const char* cThemeSection     = "theme";
static constexpr const char* cMidiSection      = "midi";
static constexpr const char* cAudioSection     = "audio";
static constexpr const char* cArchiveSection   = "archive";
static constexpr const char* cBackupSection    = "backup";
static constexpr const char* cInterfaceSection = "interface";
static constexpr const char* cStartupSection   = "startup";
static constexpr const char* cSessionSection   = "session";

static constexpr s32 cBackupIntervalMin     = 1;
static constexpr s32 cBackupIntervalMax     = 60;
static constexpr s32 cBackupIntervalDefault = 10;

static constexpr f32 cTooltipDelayMin = 0.1f;
static constexpr f32 cTooltipDelayMax = 2.0f;

static constexpr bool cSaveMetadataDefault   = true;
static constexpr bool cForceSaveInUseDefault = false;
static constexpr bool cDeleteUnusedGroupFilesDefault = false;
static constexpr bool cAutoBackupDefault     = true;

f32 gMasterVolume = 1.0f;

bool sSaveMetadataDefault = cSaveMetadataDefault;
bool sForceSaveWhenInUse = cForceSaveInUseDefault;
bool sDeleteUnusedGroupFiles = cDeleteUnusedGroupFilesDefault;

bool sAutoBackupEnabled = cAutoBackupDefault;
s32 sAutoBackupIntervalMinutes = cBackupIntervalDefault;
s32 sAutoBackupMaxBackups = cBackupCountDefault;

static AudioSettings sAudioSettings;
static InterfaceSettings sInterfaceSettings;
static StartupSettings sStartupSettings;
static MidiSettings sMidiSettings;
static std::vector<std::string> sMidiEnabledDevices;

static f32 sUIScale = cUIScaleDefault;

AudioSettings& GetAudioSettings()
{
    return sAudioSettings;
}

const std::vector<std::string>& GetMidiEnabledDevices()
{
    return sMidiEnabledDevices;
}

void SaveThemeConfig()
{
    const ThemeAdjust& adjust = GetThemeAdjust();

    json::Value theme;
    json::Value& accent = theme.setArray("accent");

    for (const f32 channel : { gAccentColor.x, gAccentColor.y, gAccentColor.z, gAccentColor.w })
        accent.pushFloat(channel);

    theme.setBool("dark", IsDarkTheme());
    theme.setFloat("brightness", adjust.brightness);
    theme.setFloat("saturation", adjust.saturation);
    theme.setFloat("contrast", adjust.contrast);
    theme.setFloat("gradient", adjust.gradient);

    SaveConfigSection(cSettingsFile, cThemeSection, theme);
}

void LoadThemeConfig()
{
    ThemeAdjust& adjust = GetThemeAdjust();

    json::Value theme;
    LoadConfigSection(cSettingsFile, cThemeSection, &theme);

    const json::Value* accent = theme.findArray("accent");

    if (accent && accent->items.size() == 4)
    {
        const std::vector<json::Value>& channels = accent->items;

        gAccentColor = ImVec4(static_cast<f32>(channels[0].number), static_cast<f32>(channels[1].number), static_cast<f32>(channels[2].number), static_cast<f32>(channels[3].number));
    }

    SetDarkTheme(theme.getBool("dark", systemtheme::isDark()));
    adjust.brightness = theme.getFloat("brightness", adjust.brightness);
    adjust.saturation = theme.getFloat("saturation", adjust.saturation);
    adjust.contrast = theme.getFloat("contrast", adjust.contrast);
    adjust.gradient = theme.getFloat("gradient", adjust.gradient);
}

MidiSettings& GetMidiSettings()
{
    return sMidiSettings;
}

void SaveMidiConfig()
{
    json::Value midi;
    json::Value& devices = midi.setArray("devices");

    for (const std::string& name : sMidiEnabledDevices)
        devices.pushString(name.c_str());

    midi.setBool("fixedVelocity", sMidiSettings.fixedVelocity);
    midi.setInt("velocity", sMidiSettings.velocity);

    SaveConfigSection(cSettingsFile, cMidiSection, midi);
}

void LoadMidiConfig()
{
    sMidiEnabledDevices.clear();

    json::Value midi;
    LoadConfigSection(cSettingsFile, cMidiSection, &midi);

    sMidiSettings.fixedVelocity = midi.getBool("fixedVelocity", sMidiSettings.fixedVelocity);
    sMidiSettings.velocity = sead::Mathi::clamp2(cMidiVelocityMin, midi.getInt("velocity", sMidiSettings.velocity), cMidiVelocityMax);

    const json::Value* devices = midi.findArray("devices");

    if (!devices)
        return;

    for (const json::Value& device : devices->items)
    {
        if (device.isString() && !device.string.empty())
            sMidiEnabledDevices.push_back(device.string);
    }
}

static bool IsMidiDeviceEnabled(const char* name)
{
    if (!name)
        return false;

    for (const std::string& enabledName : sMidiEnabledDevices)
    {
        if (enabledName == name)
            return true;
    }

    return false;
}

void SetMidiDeviceEnabled(const char* name, bool enabled)
{
    if (!name || !*name || IsMidiDeviceEnabled(name) == enabled)
        return;

    if (enabled)
        sMidiEnabledDevices.push_back(name);
    else
        std::erase(sMidiEnabledDevices, name);

    SaveMidiConfig();
}

void AutoConnectMidiDevices()
{
    const u32 count = MidiInput::getDeviceCount();

    for (u32 i = 0; i < count; i++)
    {
        if (IsMidiDeviceEnabled(MidiInput::getDeviceName(i)))
            MidiConnectDevice(static_cast<s32>(i));
    }
}

void SaveAudioConfig()
{
    json::Value audio;
    audio.setInt("sampleRate", sAudioSettings.outputSampleRate);
    audio.setFloat("volume", gMasterVolume);
    audio.setString("device", sAudioSettings.playbackDeviceName.c_str());
    audio.setBool("mono", sAudioSettings.monoOutput);
    audio.setBool("linearInterpolation", sAudioSettings.linearInterpolation);

    SaveConfigSection(cSettingsFile, cAudioSection, audio);
}

void LoadAudioConfig()
{
    sAudioSettings.playbackDeviceName.clear();

    json::Value audio;
    LoadConfigSection(cSettingsFile, cAudioSection, &audio);

    sAudioSettings.outputSampleRate = static_cast<u32>(audio.getInt("sampleRate", static_cast<s32>(sAudioSettings.outputSampleRate)));
    gMasterVolume = audio.getFloat("volume", gMasterVolume);
    sAudioSettings.monoOutput = audio.getBool("mono", sAudioSettings.monoOutput);
    sAudioSettings.linearInterpolation = audio.getBool("linearInterpolation", sAudioSettings.linearInterpolation);

    if (const std::string* device = audio.findString("device"))
        sAudioSettings.playbackDeviceName = *device;
}

void ApplyAudioConfig()
{
    snd::SoundSystem::refreshPlaybackDevices();
    snd::SoundSystem::setPlaybackDeviceByName(sAudioSettings.playbackDeviceName.c_str());

    if (sAudioSettings.outputSampleRate != snd::SoundSystem::getOutputSampleRate())
        snd::SoundSystem::setOutputSampleRate(sAudioSettings.outputSampleRate);

    snd::SoundSystem::setMasterVolume(gMasterVolume);
    snd::SoundSystem::setOutputMode(sAudioSettings.monoOutput ? snd::OutputMode::Mono : snd::OutputMode::Stereo);
    snd::SoundSystem::setSrcType(sAudioSettings.linearInterpolation ? snd::SrcType::Type_Linear : snd::SrcType::Type_4Tap);
}

void SaveArchiveConfig()
{
    json::Value archive;
    archive.setBool("saveMetadata", sSaveMetadataDefault);
    archive.setBool("forceSaveWhenInUse", sForceSaveWhenInUse);
    archive.setBool("deleteUnusedGroupFiles", sDeleteUnusedGroupFiles);

    SaveConfigSection(cSettingsFile, cArchiveSection, archive);
}

void LoadArchiveConfig()
{
    json::Value archive;
    LoadConfigSection(cSettingsFile, cArchiveSection, &archive);

    sSaveMetadataDefault = archive.getBool("saveMetadata", sSaveMetadataDefault);
    sForceSaveWhenInUse = archive.getBool("forceSaveWhenInUse", sForceSaveWhenInUse);
    sDeleteUnusedGroupFiles = archive.getBool("deleteUnusedGroupFiles", sDeleteUnusedGroupFiles);
}

void SaveBackupConfig()
{
    json::Value backup;
    backup.setBool("enabled", sAutoBackupEnabled);
    backup.setInt("intervalMinutes", sAutoBackupIntervalMinutes);
    backup.setInt("maxBackups", sAutoBackupMaxBackups);

    SaveConfigSection(cSettingsFile, cBackupSection, backup);
}

void LoadBackupConfig()
{
    json::Value backup;
    LoadConfigSection(cSettingsFile, cBackupSection, &backup);

    sAutoBackupEnabled = backup.getBool("enabled", sAutoBackupEnabled);
    sAutoBackupIntervalMinutes = sead::Mathi::clamp2(cBackupIntervalMin, backup.getInt("intervalMinutes", sAutoBackupIntervalMinutes), cBackupIntervalMax);
    sAutoBackupMaxBackups = sead::Mathi::max(backup.getInt("maxBackups", sAutoBackupMaxBackups), cBackupCountMin);
}

InterfaceSettings& GetInterfaceSettings()
{
    return sInterfaceSettings;
}

void SaveInterfaceConfig()
{
    json::Value section;
    section.setBool("animations", sInterfaceSettings.animations);
    section.setBool("smoothScrolling", sInterfaceSettings.smoothScrolling);
    section.setBool("windowShadows", sInterfaceSettings.windowShadows);
    section.setFloat("tooltipDelay", sInterfaceSettings.tooltipDelay);
    section.setFloat("scrollSpeed", sInterfaceSettings.scrollSpeed);
    section.setFloat("uiScale", sInterfaceSettings.uiScale);
    section.setBool("twelveHourClock", sInterfaceSettings.twelveHourClock);

    SaveConfigSection(cSettingsFile, cInterfaceSection, section);
}

void LoadInterfaceConfig()
{
    json::Value section;
    LoadConfigSection(cSettingsFile, cInterfaceSection, &section);

    sInterfaceSettings.animations = section.getBool("animations", sInterfaceSettings.animations);
    sInterfaceSettings.smoothScrolling = section.getBool("smoothScrolling", sInterfaceSettings.smoothScrolling);
    sInterfaceSettings.windowShadows = section.getBool("windowShadows", sInterfaceSettings.windowShadows);
    sInterfaceSettings.tooltipDelay = sead::Mathf::clamp2(cTooltipDelayMin, section.getFloat("tooltipDelay", sInterfaceSettings.tooltipDelay), cTooltipDelayMax);
    sInterfaceSettings.scrollSpeed = sead::Mathf::clamp2(cScrollSpeedMin, section.getFloat("scrollSpeed", sInterfaceSettings.scrollSpeed), cScrollSpeedMax);
    sInterfaceSettings.uiScale = sead::Mathf::clamp2(cUIScaleMin, section.getFloat("uiScale", sInterfaceSettings.uiScale), cUIScaleMax);
    sInterfaceSettings.twelveHourClock = section.getBool("twelveHourClock", sInterfaceSettings.twelveHourClock);
}

void ApplyInterfaceConfig()
{
    const bool motion = IsMotionAllowed();

    SetAnimationEnabled(sInterfaceSettings.animations);
    ImGui::SetHoverAnimation(sInterfaceSettings.animations && motion);
    ImGui::SetSmoothWheelScroll(sInterfaceSettings.smoothScrolling && motion);
    ImGui::SetWheelScrollSpeed(sInterfaceSettings.scrollSpeed);
    ImGui::GetStyle().HoverDelayNormal = sInterfaceSettings.tooltipDelay;
    SetWindowShadows(sInterfaceSettings.windowShadows);
    timeutil::SetTwelveHourClock(sInterfaceSettings.twelveHourClock);
}

void InitUIScale()
{
    sUIScale = sInterfaceSettings.uiScale;
    ImGui::GetStyle().ScaleAllSizes(sUIScale);
}

f32 GetUIScale()
{
    return sUIScale;
}

f32 ScaleSize(f32 size)
{
    return static_cast<f32>(static_cast<s32>(size * sUIScale + 0.5f));
}

StartupSettings& GetStartupSettings()
{
    return sStartupSettings;
}

static constexpr const char* cStartupActionKeys[] = { "startPage", "lastArchive" };

static StartupAction ReadStartupAction(const json::Value& startup)
{
    const std::string* key = startup.findString("action");

    if (!key)
        return startup.getBool("reopenLastArchive", false) ? StartupAction::LastArchive : StartupAction::StartPage;

    for (s32 i = 0; i < IM_ARRAYSIZE(cStartupActionKeys); i++)
    {
        if (*key == cStartupActionKeys[i])
            return static_cast<StartupAction>(i);
    }

    return StartupAction::StartPage;
}

void SaveStartupConfig()
{
    if (testhooks::IsEnabled())
        return;

    json::Value startup;
    startup.setString("action", cStartupActionKeys[static_cast<s32>(sStartupSettings.action)]);
    startup.setInt("recentFileCount", sStartupSettings.recentFileCount);

    SaveConfigSection(cSettingsFile, cStartupSection, startup);
}

void LoadStartupConfig()
{
    json::Value startup;
    LoadConfigSection(cSettingsFile, cStartupSection, &startup);

    sStartupSettings.action = ReadStartupAction(startup);
    sStartupSettings.recentFileCount = sead::Mathi::clamp2(cRecentFileCountMin, startup.getInt("recentFileCount", sStartupSettings.recentFileCount), cRecentFileCountMax);

    json::Value session;
    LoadConfigSection(cStateFile, cSessionSection, &session);

    if (const std::string* path = session.findString("lastArchive"))
        sStartupSettings.lastArchive = *path;
}

void SetLastArchive(const char* path)
{
    if (!path || sStartupSettings.lastArchive == path)
        return;

    sStartupSettings.lastArchive = path;

    if (testhooks::IsEnabled())
        return;

    json::Value session;
    session.setString("lastArchive", path);

    SaveConfigSection(cStateFile, cSessionSection, session);
}

void ResetAllSettings()
{
    const std::string lastArchive = sStartupSettings.lastArchive;

    gAccentColor = uicolors::cAccentDefault;
    SetDarkTheme(systemtheme::isDark());
    GetThemeAdjust() = ThemeAdjust();

    sAudioSettings = AudioSettings();

    sSaveMetadataDefault = cSaveMetadataDefault;
    sForceSaveWhenInUse = cForceSaveInUseDefault;
    sDeleteUnusedGroupFiles = cDeleteUnusedGroupFilesDefault;

    sAutoBackupEnabled = cAutoBackupDefault;
    sAutoBackupIntervalMinutes = cBackupIntervalDefault;
    sAutoBackupMaxBackups = cBackupCountDefault;

    sInterfaceSettings = InterfaceSettings();
    sMidiSettings = MidiSettings();
    sStartupSettings = StartupSettings();
    sStartupSettings.lastArchive = lastArchive;

    ApplyAudioConfig();
    ApplyInterfaceConfig();
    ApplyTheme();

    SaveThemeConfig();
    SaveAudioConfig();
    SaveArchiveConfig();
    SaveBackupConfig();
    SaveInterfaceConfig();
    SaveMidiConfig();
    SaveStartupConfig();
}
