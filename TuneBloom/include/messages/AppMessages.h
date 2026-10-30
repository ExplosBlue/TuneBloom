#pragma once

namespace messages
{

namespace keyboard
{

inline constexpr const char* cComputerRangeTooltipFormat = "Computer keyboard: %s to %s\nMove: click an octave or press Left/Right";

inline constexpr const char* cMidiOn            = "MIDI: on";
inline constexpr const char* cMidiDevicesFormat = "MIDI: %u devices";
inline constexpr const char* cMidiOff           = "MIDI: off";
inline constexpr const char* cMidiSettingsHint  = "Open MIDI Settings";

inline constexpr const char* cHelpTitle      = "Computer Keyboard Mapping";
inline constexpr const char* cHelpLowerRow   = "Lower row (Z-/):";
inline constexpr const char* cHelpLowerWhite = "  White: Z  X  C  V  B  N  M  ,  .  /";
inline constexpr const char* cHelpLowerBlack = "  Black: S  D     G  H  J     L  ;";
inline constexpr const char* cHelpUpperRow   = "Upper row (Q-]):";
inline constexpr const char* cHelpUpperWhite = "  White: Q  W  E  R  T  Y  U  I  O  P  [  ]";
inline constexpr const char* cHelpUpperBlack = "  Black: 2  3     5  6  7     9  0     =";
inline constexpr const char* cHelpControls   = "Controls:";
inline constexpr const char* cHelpOctave     = "  Left/Right arrows: shift octave";
inline constexpr const char* cHelpReset      = "  Backspace: reset to C4";

}

namespace validation
{

inline constexpr const char* cInvalidPlayer = "The Player is invalid";
inline constexpr const char* cInvalidSoundType = "The Sound Type is invalid";
inline constexpr const char* cInvalidWaveArchive = "The Wave Archive is invalid";
inline constexpr const char* cInvalidSequenceFile = "The Sequence File is invalid";
inline constexpr const char* cInvalidWaveFile = "The Wave File is invalid";

inline constexpr const char* cWaveSoundNeedsSoundSet = "A Wave Sound must be in a Sound Set";
inline constexpr const char* cWaveSoundNeedsWaveSoundSet = "A Wave Sound must be in a Wave Sound Set";
inline constexpr const char* cSharedStreamTrackCountFormat = "Shares its stream path with '%s'\nBoth must have the same number of Tracks";
inline constexpr const char* cSharedStreamWaveFileFormat = "Shares its stream path with '%s'\nTracks must use the same Wave File";
inline constexpr const char* cSharedStreamTracksIdenticalFormat = "Shares its stream path with '%s'\nTracks must be identical";
inline constexpr const char* cOtherArchiveItemFormat = "Uses '%s' from another sound archive";

}

namespace recent
{

inline constexpr const char* cEmpty = "No recent files";
inline constexpr const char* cClear = "Clear Recent Files";

inline constexpr const char* cGroupToday   = "Today";
inline constexpr const char* cGroupWeek    = "Last 7 Days";
inline constexpr const char* cGroupMonth   = "Last 30 Days";
inline constexpr const char* cGroupOlder   = "Older";

#if defined(SEAD_PLATFORM_WINDOWS)
inline constexpr const char* cShowInFileManager = "Show in Explorer";
#elif defined(SEAD_PLATFORM_MACOSX)
inline constexpr const char* cShowInFileManager = "Show in Finder";
#else
inline constexpr const char* cShowInFileManager = "Show in File Manager";
#endif

inline constexpr const char* cNotFoundFormat = "Not found: %s";

}

namespace start
{

inline constexpr const char* cEmptyDetail = "Recently opened archives will appear here.";

}

namespace preferences
{

inline constexpr const char* cRefreshDevices = "Refresh Devices";

inline constexpr const char* cPageAudio      = "Audio";
inline constexpr const char* cPageMidi       = "MIDI Input";
inline constexpr const char* cPageAppearance = "Appearance";
inline constexpr const char* cPageInterface  = "Interface";
inline constexpr const char* cPageAdvanced   = "Advanced";

inline constexpr const char* cBoxSaving       = "Saving";
inline constexpr const char* cBoxOpening      = "Opening";
inline constexpr const char* cBoxColors       = "Colors";
inline constexpr const char* cBoxShading      = "Shading";
inline constexpr const char* cBoxMotion       = "Motion";
inline constexpr const char* cBoxDisplay      = "Display";
inline constexpr const char* cBoxDebug        = "Debug";
inline constexpr const char* cBoxSettings     = "Settings";

inline constexpr const char* cBackupLabel = "Automatic Backup";
inline constexpr const char* cBackupMinute = "Every Minute";
inline constexpr const char* cBackup5Minutes = "Every 5 Minutes";
inline constexpr const char* cBackup10Minutes = "Every 10 Minutes";
inline constexpr const char* cBackup15Minutes = "Every 15 Minutes";
inline constexpr const char* cBackupDetail = "Saves a copy of unsaved work";
inline constexpr const char* cBackupCountLabel = "Maximum Backups";
inline constexpr const char* cBackupCountDisabled = "Automatic Backup is off";
inline constexpr const char* cBackupFolderLabel = "Backup Folder";
inline constexpr const char* cBackupUsageFormat = "%zu %s, %s";

inline constexpr const char* cMetadataLabel = "Archive Metadata";
inline constexpr const char* cMetadataDetail = "Saves names the archive cannot store";

inline constexpr const char* cInUseLabel = "Files in Use";
inline constexpr const char* cInUseAsk = "Ask Before Saving";
inline constexpr const char* cInUseSave = "Always Save";

inline constexpr const char* cUnusedGroupFilesLabel = "Unused Group Files";
inline constexpr const char* cUnusedGroupFilesDelete = "Delete When Saving";
inline constexpr const char* cUnusedGroupFilesDetail = "Files of groups that are no longer external";

inline constexpr const char* cIgnoredLabel = "Ignored Warnings";
inline constexpr const char* cIgnoredCountFormat = "%zu %s";
inline constexpr const char* cIgnoredDetail = "Archives with hidden opening warnings";

inline constexpr const char* cStartupLabel = "On Startup";
inline constexpr const char* cStartupStartPage = "Start Page";
inline constexpr const char* cStartupStartPageDetail = "Shows recent files and links";
inline constexpr const char* cStartupLastArchive = "Last Archive";

inline constexpr const char* cAssociationLabel = "File Association";
inline constexpr const char* cAssociationChoose = "Choose...";
inline constexpr const char* cAssociationCountFormat = "%u of %u Types";
inline constexpr const char* cAssociationDetail = "File types that open in TuneBloom";
inline constexpr const char* cAssociationAppColumn = "Opens With";
inline constexpr const char* cAssociationOnlyApp = "No other app opens this file type";
inline constexpr const char* cAssociationFailed = "Couldn't change the file association";

inline constexpr const char* cDeviceLabel = "Playback Device";
inline constexpr const char* cDeviceEmpty = "No playback device found";
inline constexpr const char* cOutputInfoFormat = "%s, %u Hz, %u ms buffer";
inline constexpr const char* cOutputModeLabel = "Output Mode";
inline constexpr const char* cInterpolationDetail = "Resampling method of Wave Sounds";

inline constexpr const char* cMidiColumnStatus = "Status";
inline constexpr const char* cMidiConnected = "Connected";
inline constexpr const char* cMidiNotFound = "Not Found";
inline constexpr const char* cMidiNotFoundDetail = "Uncheck to remove this device";
inline constexpr const char* cMidiEmpty = "No MIDI input devices found";
inline constexpr const char* cVelocityLabel = "Velocity";
inline constexpr const char* cVelocityDevice = "From Device";
inline constexpr const char* cVelocityFixed = "Fixed";
inline constexpr const char* cFixedVelocityLabel = "Fixed Velocity";
inline constexpr const char* cFixedVelocityDisabled = "Velocity comes from the device";

inline constexpr const char* cHueLabel = "Hue";
inline constexpr const char* cBrightnessLabel = "Brightness";
inline constexpr const char* cSaturationLabel = "Saturation";
inline constexpr const char* cContrastLabel = "Contrast";
inline constexpr const char* cGradientLabel = "Gradient";
inline constexpr const char* cGradientDetail = "Shading of buttons and panels";
inline constexpr const char* cAppearanceReset = "Reset to Default";

inline constexpr const char* cAnimationsLabel = "Animations";
inline constexpr const char* cAnimationsDetail = "Fades windows, menus and hover states";
inline constexpr const char* cScrollSmoothingLabel = "Scroll Smoothing";
inline constexpr const char* cScrollSpeedLabel = "Scroll Speed";
inline constexpr const char* cUIScaleLabel = "UI Scale";
inline constexpr const char* cUIScaleRestart = "Restart to apply";
inline constexpr const char* cWindowLayoutLabel = "Window Layout";
inline constexpr const char* cWindowLayoutDetail = "Place and size of each panel and list column";
inline constexpr const char* cShadowOpacityLabel = "Shadow Opacity";
inline constexpr const char* cTimeFormatLabel = "Time Format";
inline constexpr const char* cTimeFormat24Format = "24-Hour (%s)";
inline constexpr const char* cTimeFormat12Format = "12-Hour (%s)";

inline constexpr const char* cTooltipDelayLabel = "Tooltip Delay";
inline constexpr const char* cTooltipDelayInstant = "Instant";

inline constexpr const char* cSettingsFolderLabel = "Settings Folder";
inline constexpr const char* cResetAll = "Reset All Settings";
inline constexpr const char* cResetAllQuestion = "Reset all settings to their defaults?";
inline constexpr const char* cResetAllDetail = "Volume, recent files, MIDI devices and layout are kept.";

inline constexpr const char* cProcessMeterLabel = "ProcessMeter";

inline constexpr const char* cDeviceSystemDefault = "System Default";

}

namespace menu
{

inline constexpr const char* cInsertAfter          = "Insert After";
inline constexpr const char* cDuplicateItemsFormat = "Duplicate %zu Items";
inline constexpr const char* cDeleteItemsFormat    = "Delete %zu Items";
inline constexpr const char* cDeleteFromArchive    = "Delete from Archive";
inline constexpr const char* cDeleteItemsFromArchiveFormat = "Delete %zu Items from Archive";
inline constexpr const char* cRemoveFromGroup      = "Remove from Group";
inline constexpr const char* cRemoveItemsFromGroupFormat = "Remove %zu Items from Group";
inline constexpr const char* cDeleteTrack          = "Delete Track";
inline constexpr const char* cDeleteRegion         = "Delete Region";
inline constexpr const char* cDeleteMarker         = "Delete Marker";
inline constexpr const char* cDeleteKey            = "Delete Key";
inline constexpr const char* cExportWav            = "Export WAV";
inline constexpr const char* cExportMidi           = "Export MIDI";
inline constexpr const char* cExportGroupFile      = "Export Group File";
inline constexpr const char* cLocateGroupFiles     = "Locate Group Files";
inline constexpr const char* cImportBankBundle     = "Import Bank Bundle";
inline constexpr const char* cExportBankBundle     = "Export Bank Bundle";
inline constexpr const char* cExportSf2            = "Export as SF2";
inline constexpr const char* cExportDls            = "Export as DLS";
inline constexpr const char* cAutoName             = "Auto-Name";
inline constexpr const char* cAutoNameDetail       = "Names it after its first Wave File";
inline constexpr const char* cAutoNameNoWaveFile   = "No Wave File with a name";
inline constexpr const char* cSortByNumber         = "Sort by Number";
inline constexpr const char* cSplitKeyRegion       = "Split Key Region";
inline constexpr const char* cSplitVelocityRegion  = "Split Velocity Region";
inline constexpr const char* cKeyRegionOneKey      = "The key region has only one key";
inline constexpr const char* cVelocityRegionOneVelocity = "The velocity region has only one velocity";
inline constexpr const char* cSelectOneWaveFile    = "Select one Wave File";
inline constexpr const char* cSizeColumnToFit      = "Size Column to Fit";
inline constexpr const char* cSizeAllColumnsToFit  = "Size All Columns to Fit";

}

namespace backup
{

inline constexpr const char* cMenuRestore = "Restore Backup";
inline constexpr const char* cPathCutMark = "...";

inline constexpr const char* cRestoreHeadFormat   = "Backups of %s";
inline constexpr const char* cRestoreDetail       = "The file on disk only changes when you save.";
inline constexpr const char* cRestoreEmpty        = "This archive has no backups yet";
inline constexpr const char* cCountFormat         = "%zu Backups, %s";
inline constexpr const char* cCountOneFormat      = "1 Backup, %s";
inline constexpr const char* cRestoreAnyway       = "Restore Anyway";
inline constexpr const char* cRestoreDiscards     = "The open archive has an error, so its unsaved changes will be lost";
inline constexpr const char* cColumnDate          = "Date";
inline constexpr const char* cKindAuto            = "Automatic";
inline constexpr const char* cKindSave            = "Before Save";
inline constexpr const char* cKindRestore         = "Before Restore";
inline constexpr const char* cSizeKbFormat        = "%.0f KB";
inline constexpr const char* cUntitled            = "Untitled";

inline constexpr const char* cRecoverTitle    = "Recover Unsaved Work?";
inline constexpr const char* cRecoverDetail   = "TuneBloom did not close correctly.\nA backup with unsaved changes is available.";
inline constexpr const char* cRecoverNoFolder = "Not saved yet";
inline constexpr const char* cRecover         = "Recover";
inline constexpr const char* cDiscard         = "Discard";
inline constexpr const char* cNotNow          = "Not Now";

inline constexpr const char* cPausedInvalid = "Automatic backup is paused: the archive has an error";
inline constexpr const char* cPausedWrite   = "The automatic backup could not be written: the folder is read-only or the disk is full";
inline constexpr const char* cNoticePausedTitle      = "Automatic backup is paused";
inline constexpr const char* cNoticePausedText       = "The archive has an error. Save to see it.";
inline constexpr const char* cNoticeFailedTitle      = "The automatic backup could not be written";
inline constexpr const char* cNoticeFailedTextFormat = "The folder is read-only or the disk is full: %s";
inline constexpr const char* cManualInvalidDetail =
    "The backup could not be created: the archive has an error.\n"
    "Save to see it.";
inline constexpr const char* cManualFailedFormat =
    "The backup could not be written. The folder is read-only or the disk is full:\n%s";
inline constexpr const char* cRestoreReadFailed   = "The backup could not be read";
inline constexpr const char* cRestoreStreamFormat = "The stream file '%s' could not be restored from the backup";

}

namespace popup
{

inline constexpr const char* cTitleErrorsOpening = "Errors while opening";
inline constexpr const char* cTitleWarningsOpening = "Warnings while opening";
inline constexpr const char* cTitleSkippedSaving = "Skipped while saving";

inline constexpr const char* cHeadingErrors = "Fix these errors before saving";
inline constexpr const char* cHeadingWarnings = "These items opened with warnings";
inline constexpr const char* cHeadingSkipped = "These items could not be saved";

inline constexpr const char* cBackupReminder = "Remember to make a backup first";

inline constexpr const char* cDetailMarker = "(?)";
inline constexpr const char* cCountSeparator = "|";

inline constexpr const char* cGroupErrors = "Errors";
inline constexpr const char* cGroupWarnings = "Warnings";

inline constexpr const char* cSeverityCountFormat = "%s %zu %s";

inline constexpr const char* cIgnoreWarnings = "Do not show these warnings again";
inline constexpr const char* cIgnoreWarningsDetail = "New warnings still show; reset in Preferences";

inline constexpr const char* cItemInvalidFormat = "Item '%s' is invalid:";

}

namespace unsaved
{

inline constexpr const char* cTitle        = "Unsaved Changes";

inline constexpr const char* cColumnChange = "Change";

inline constexpr const char* cAdded        = "Added";
inline constexpr const char* cRemoved      = "Removed";

inline constexpr const char* cCountFormat  = "%d Changes";
inline constexpr const char* cCountOneFormat = "%d Change";
inline constexpr const char* cNoChanges    = "No unsaved changes";

inline constexpr const char* cDontSave       = "Don't Save";
inline constexpr const char* cExitHeadingFormat = "%d files have unsaved changes";
inline constexpr const char* cExitHeadingOne    = "1 file has unsaved changes";

inline constexpr const char* cNoRowReason   = "Select a row first";
inline constexpr const char* cRemovedReason = "The item is no longer in the archive";

}

namespace about
{

inline constexpr const char* cBy          = "by";
inline constexpr const char* cAuthor      = "stupidestmodder";
inline constexpr const char* cAndMore     = "& more";
inline constexpr const char* cTagline     = "The NintendoWare sound archive editor";
inline constexpr const char* cMadeInLine  = "Made in Brazil";
inline constexpr const char* cVersionFormat = "Version %s";
inline constexpr const char* cBuildFormat   = "Build %s";
inline constexpr const char* cGitHub        = "GitHub";
inline constexpr const char* cDiscord       = "Discord";

}

namespace saving
{

inline constexpr const char* cSaveTitle    = "Save?";
inline constexpr const char* cSaveQuestionFormat = "Save changes to %s?";

inline constexpr const char* cInUseTitle       = "File In Use?";
inline constexpr const char* cInUseDetail      = "Another program is using the file.";
inline constexpr const char* cInUseStill       = "The file is still in use.";
inline constexpr const char* cInUseAlwaysSave  = "Always save anyway";
inline constexpr const char* cInUseWriteFailed = "The file could not be written while it is in use.";

inline constexpr const char* cUnusedTitle       = "Remove Unused Files?";
inline constexpr const char* cUnusedCountFormat = "%zu Wave Files have no references.";
inline constexpr const char* cUnusedCountOneFormat = "%zu Wave File has no references.";

inline constexpr const char* cOpenInOtherTab = "The file could not be saved. It is open in another tab.";

}

namespace app
{

inline constexpr const char* cAssertTitle = "ASSERTION ERROR";
inline constexpr const char* cAssertReportFormat = "%s\nAn unexpected error occurred. Report it at 'https://github.com/stupidestmodder/TuneBloom/issues' or ask at 'https://go.nsmbu.net/discord'";

inline constexpr const char* cDropUnsupported = "This file type is not supported";
inline constexpr const char* cIconLoadFailed = "The icon image could not be loaded";

inline constexpr const char* cDockMoveTip  = "Drag to move or undock";
inline constexpr const char* cDockShiftTip = "Hold Shift to dock";

}

namespace mainmenu
{

inline constexpr const char* cNew         = "New";
inline constexpr const char* cOpenRecent  = "Open Recent";
inline constexpr const char* cSaveAs      = "Save As";
inline constexpr const char* cSaveAll     = "Save All";
inline constexpr const char* cExit        = "Exit";
inline constexpr const char* cPreferences = "Preferences";
inline constexpr const char* cAbout       = "About";
inline constexpr const char* cUnsaved     = "Unsaved";
inline constexpr const char* cSaved       = "Saved";

inline constexpr const char* cShowUnsaved     = "Show Unsaved Changes";
inline constexpr const char* cCreateBackup     = "Create Backup";
inline constexpr const char* cShowBackupFolder = "Show Backup Folder";
inline constexpr const char* cCopyPath         = "Copy Path";
inline constexpr const char* cOpenFullEditor   = "Open in Full Editor";

inline constexpr const char* cInnerFileFormat   = "%s File";
inline constexpr const char* cBankBundle        = "Bank Bundle";
inline constexpr const char* cExportWav         = "WAV Audio";
inline constexpr const char* cExportMidi        = "MIDI File";
inline constexpr const char* cExportSf2         = "SoundFont (SF2)";
inline constexpr const char* cExportDls         = "Downloadable Sounds (DLS)";
inline constexpr const char* cExportNoSelection = "Select a Sound or a file to export";

inline constexpr const char* cEdit      = "Edit";

inline constexpr const char* cView         = "View";
inline constexpr const char* cOutputMeter  = "Output Meter";
inline constexpr const char* cSystemWindow = "System Window";
inline constexpr const char* cThemeDark    = "Dark";
inline constexpr const char* cThemeLight   = "Light";
inline constexpr const char* cResetLayout  = "Reset Layout";

inline constexpr const char* cShortcuts   = "Keyboard Shortcuts";
inline constexpr const char* cRepository  = "GitHub Repository";
inline constexpr const char* cReportIssue = "Report an Issue";
inline constexpr const char* cDiscord     = "Discord Server";

}

namespace documents
{

inline constexpr const char* cPreview      = "Preview";
inline constexpr const char* cPreviewHint  = "Double-click to keep open";
inline constexpr const char* cUnloaded     = "Unloaded";
inline constexpr const char* cUnloadedHint = "Reloads when selected";
inline constexpr const char* cKeepOpen     = "Keep Open";
inline constexpr const char* cReload       = "Reload from Disk";
inline constexpr const char* cReveal       = "Reveal in Project";
inline constexpr const char* cCloseOthers  = "Close Others";
inline constexpr const char* cCloseAll     = "Close All";
inline constexpr const char* cNoOtherTabs  = "No other tabs are open";
inline constexpr const char* cOpenFiles    = "Open Files";
inline constexpr const char* cSwitchLocked = "Close the open dialog first";

}

namespace keys
{

inline constexpr const char* cGroupLists    = "Item Lists";
inline constexpr const char* cGroupSearch   = "Search";
inline constexpr const char* cGroupPiano    = "Piano Keyboard";
inline constexpr const char* cGroupMouse    = "Mouse";

inline constexpr const char* cShowSelection  = "Scroll to Selection";
inline constexpr const char* cCollapseFolder = "Collapse Folder";
inline constexpr const char* cExpandFolder   = "Expand Folder";

inline constexpr const char* cClearSearch    = "Clear Search";
inline constexpr const char* cMatchCase      = "Match Case";
inline constexpr const char* cMatchWholeWord = "Match Whole Word";

inline constexpr const char* cZoomIn      = "Zoom In";
inline constexpr const char* cZoomOut     = "Zoom Out";
inline constexpr const char* cOctaveDown  = "Octave Down";
inline constexpr const char* cOctaveUp    = "Octave Up";
inline constexpr const char* cOctaveReset = "Reset Octave";

inline constexpr const char* cZoomHorizontal = "Zoom Horizontally";
inline constexpr const char* cZoomVertical   = "Zoom Vertically";
inline constexpr const char* cPanHorizontal  = "Scroll Horizontally";
inline constexpr const char* cFineAdjust     = "Fine Adjust";

inline constexpr const char* cKeyCmd      = "Cmd";
inline constexpr const char* cKeyCtrl     = "Ctrl";
inline constexpr const char* cKeyShift    = "Shift";
inline constexpr const char* cKeyAlt      = "Alt";
inline constexpr const char* cKeyEscape   = "Esc";
inline constexpr const char* cKeyWheel    = "Wheel";
inline constexpr const char* cKeyDrag     = "Drag";

}

}
