#pragma once

#include <basis/seadTypes.h>

#include <string>
#include <vector>

struct AudioSettings
{
    u32 outputSampleRate = 48000;
    std::string playbackDeviceName;
    bool monoOutput = false;
    bool linearInterpolation = false;
};

inline constexpr s32 cBackupCountMin     = 1;
inline constexpr s32 cBackupCountDefault = 20;

inline constexpr s32 cRecentFileCountMin     = 5;
inline constexpr s32 cRecentFileCountMax     = 50;
inline constexpr s32 cRecentFileCountDefault = 20;

inline constexpr f32 cTooltipDelayDefault = 0.4f;

inline constexpr f32 cScrollSpeedMin     = 0.5f;
inline constexpr f32 cScrollSpeedMax     = 3.0f;
inline constexpr f32 cScrollSpeedDefault = 1.0f;

inline constexpr f32 cUIScaleMin     = 1.0f;
inline constexpr f32 cUIScaleMax     = 1.5f;
inline constexpr f32 cUIScaleDefault = 1.0f;

inline constexpr s32 cMidiVelocityMin     = 1;
inline constexpr s32 cMidiVelocityMax     = 127;
inline constexpr s32 cMidiVelocityDefault = 100;

struct InterfaceSettings
{
    bool animations = true;
    bool smoothScrolling = true;
    bool windowShadows = true;
    f32 tooltipDelay = cTooltipDelayDefault;
    f32 scrollSpeed = cScrollSpeedDefault;
    f32 uiScale = cUIScaleDefault;
    bool twelveHourClock = false;
};

struct MidiSettings
{
    bool fixedVelocity = false;
    s32 velocity = cMidiVelocityDefault;
};

enum class StartupAction
{
    StartPage,
    LastArchive,
};

struct StartupSettings
{
    StartupAction action = StartupAction::StartPage;
    s32 recentFileCount = cRecentFileCountDefault;
    std::string lastArchive;
};

extern f32 gMasterVolume;

extern bool sSaveMetadataDefault;
extern bool sForceSaveWhenInUse;
extern bool sDeleteUnusedGroupFiles;

extern bool sAutoBackupEnabled;
extern s32 sAutoBackupIntervalMinutes;
extern s32 sAutoBackupMaxBackups;

void SaveAccentColor();
void LoadAccentColor();

const std::vector<std::string>& GetMidiEnabledDevices();
void SetMidiDeviceEnabled(const char* name, bool enabled);
MidiSettings& GetMidiSettings();
void SaveMidiConfig();
void LoadMidiConfig();
void AutoConnectMidiDevices();

AudioSettings& GetAudioSettings();
void SaveAudioConfig();
void LoadAudioConfig();
void ApplyAudioConfig();

void SaveArchiveConfig();
void LoadArchiveConfig();

void SaveBackupConfig();
void LoadBackupConfig();

InterfaceSettings& GetInterfaceSettings();
void SaveInterfaceConfig();
void LoadInterfaceConfig();
void ApplyInterfaceConfig();
void InitUIScale();
f32 GetUIScale();
f32 ScaleSize(f32 size);

StartupSettings& GetStartupSettings();
void SaveStartupConfig();
void LoadStartupConfig();
void SetLastArchive(const char* path);

void ResetAllSettings();
