#pragma once

#include <ui/MessageType.h>

namespace messages
{

inline constexpr const char* cNotImplemented = "Not implemented";

namespace common
{

inline constexpr const char* cSequence = "Sequence";
inline constexpr const char* cWave = "Wave";
inline constexpr const char* cStream = "Stream";
inline constexpr const char* cPlayer = "Player";
inline constexpr const char* cBank = "Bank";
inline constexpr const char* cSound = "Sound";
inline constexpr const char* cGroup = "Group";
inline constexpr const char* cInstrument = "Instrument";
inline constexpr const char* cInstruments = "Instruments";
inline constexpr const char* cItem = "Item";
inline constexpr const char* cItems = "Items";
inline constexpr const char* cTrack = "Track";
inline constexpr const char* cTracks = "Tracks";
inline constexpr const char* cSounds = "Sounds";
inline constexpr const char* cWaveFile = "Wave File";
inline constexpr const char* cBankFile = "Bank File";
inline constexpr const char* cSequenceFile = "Sequence File";
inline constexpr const char* cWaveArchive = "Wave Archive";
inline constexpr const char* cWaveSoundSet = "Wave Sound Set";
inline constexpr const char* cWaveSound = "Wave Sound";
inline constexpr const char* cArchive = "Archive";
inline constexpr const char* cProject = "Project";
inline constexpr const char* cProjectInfo = "Project Info";
inline constexpr const char* cFile = "File";
inline constexpr const char* cFiles = "Files";
inline constexpr const char* cFolder = "Folder";
inline constexpr const char* cGroupFiles = "Group Files";
inline constexpr const char* cBackup = "Backup";
inline constexpr const char* cBackups = "Backups";

inline constexpr const char* cName = "Name";
inline constexpr const char* cType = "Type";
inline constexpr const char* cFormat = "Format";
inline constexpr const char* cLength = "Length";
inline constexpr const char* cSize = "Size";
inline constexpr const char* cSampleRate = "Sample Rate";
inline constexpr const char* cChannels = "Channels";
inline constexpr const char* cEncoding = "Encoding";
inline constexpr const char* cLoop = "Loop";
inline constexpr const char* cStart = "Start";
inline constexpr const char* cEnd = "End";
inline constexpr const char* cVolume = "Volume";
inline constexpr const char* cPan = "Pan";
inline constexpr const char* cPitch = "Pitch";
inline constexpr const char* cSurround = "Surround";
inline constexpr const char* cFrontBypass = "Front Bypass";
inline constexpr const char* cFilter = "Filter";
inline constexpr const char* cLpf = "LPF";
inline constexpr const char* cPriority = "Priority";
inline constexpr const char* cProgram = "Program";
inline constexpr const char* cCurve = "Curve";
inline constexpr const char* cInterpolation = "Interpolation";
inline constexpr const char* cLinear = "Linear";
inline constexpr const char* cHold = "Hold";
inline constexpr const char* cVersion = "Version";
inline constexpr const char* cModified = "Modified";
inline constexpr const char* cLabels = "Labels";
inline constexpr const char* cNotes = "Notes";
inline constexpr const char* cOptions = "Options";
inline constexpr const char* cDevice = "Device";
inline constexpr const char* cPlayback = "Playback";
inline constexpr const char* cTheme = "Theme";
inline constexpr const char* cSystem = "System";
inline constexpr const char* cRecentFiles = "Recent Files";
inline constexpr const char* cSamples = "samples";

inline constexpr const char* cUnknown = "Unknown";
inline constexpr const char* cNone = "None";
inline constexpr const char* cOff = "Off";
inline constexpr const char* cOn = "On";
inline constexpr const char* cDisabled = "Disabled";
inline constexpr const char* cEmpty = "Empty";
inline constexpr const char* cMono = "Mono";
inline constexpr const char* cStereo = "Stereo";
inline constexpr const char* cLeft = "L";
inline constexpr const char* cRight = "R";
inline constexpr const char* cManual = "Manual";
inline constexpr const char* cAll = "All";
inline constexpr const char* cNoValue = "-";
inline constexpr const char* cSilent = "-inf";
inline constexpr const char* cNull = "(null)";

inline constexpr const char* cAdd = "Add";
inline constexpr const char* cCancel = "Cancel";
inline constexpr const char* cClose = "Close";
inline constexpr const char* cDelete = "Delete";
inline constexpr const char* cDuplicate = "Duplicate";
inline constexpr const char* cExport = "Export";
inline constexpr const char* cImport = "Import";
inline constexpr const char* cFind = "Find";
inline constexpr const char* cGoTo = "Go To";
inline constexpr const char* cHelp = "Help";
inline constexpr const char* cNewArchive = "New Archive";
inline constexpr const char* cOpenArchive = "Open Archive";
inline constexpr const char* cPause = "Pause";
inline constexpr const char* cPlay = "Play";
inline constexpr const char* cStop = "Stop";
inline constexpr const char* cRename = "Rename";
inline constexpr const char* cReplace = "Replace";
inline constexpr const char* cReset = "Reset";
inline constexpr const char* cRestore = "Restore";
inline constexpr const char* cSave = "Save";
inline constexpr const char* cSelectAll = "Select All";
inline constexpr const char* cPageUp = "Page Up";
inline constexpr const char* cPageDown = "Page Down";
inline constexpr const char* cWarning = "Warning";

inline constexpr const char* cCountOfFormat = "%d of %d";
inline constexpr const char* cMillisecondsFormat = "%.0f ms";
inline constexpr const char* cMegabytesFormat = "%.1f MB";
inline constexpr const char* cSecondsFormat = "%.2f s";
inline constexpr const char* cPercentFormat = "%d%%";
inline constexpr const char* cPercentFloatFormat = "%.0f%%";
inline constexpr const char* cChannelCountFormat = "%u Channels";

}

namespace param
{

inline constexpr const char* cSurroundPan = "S. Pan";
inline constexpr const char* cCoarse     = "Coarse";
inline constexpr const char* cFine       = "Fine";
inline constexpr const char* cSendMain   = "Main";
inline constexpr const char* cSendFxA    = "FX A";
inline constexpr const char* cSendFxB    = "FX B";
inline constexpr const char* cSendFxC    = "FX C";
inline constexpr const char* cBiquad     = "Biquad";

}

namespace panel
{

inline constexpr const char* cOutput   = "Output";
inline constexpr const char* cEnvelope = "Envelope";
inline constexpr const char* cSend     = "Send";

}

namespace source
{

inline constexpr const char* cSelectExisting = "Select Existing";

}

namespace stream
{

inline constexpr const char* cSampleRateFormat = "Sample rate: %u Hz";

inline constexpr const char* cFileMissingFormat = "'%s' could not be found next to the .%s file";
inline constexpr const char* cFileIncompleteFormat = "'%s' could not be loaded: the file is incomplete";
inline constexpr const char* cFileUnreadableFormat = "'%s' could not be read as a stream file";
inline constexpr const char* cFileNotStream = "Not a valid stream file";
inline constexpr const char* cFileNoInfoBlockFormat = "The INFO block of the %s file could not be found";
inline constexpr const char* cFileBlocksMisalignedFormat = "The %s file has an invalid block size";
inline constexpr const char* cFileNotLoaded = "The stream file is not loaded";

inline constexpr const char* cAudioUnreadableDetail = "The file could not be read. It will not be changed during saving.";

inline constexpr const char* cSharedTrackCountFormat = "'%s' shares its stream file with '%s' but has %d Tracks instead of %d";
inline constexpr const char* cTrackUnreadableFormat = "Track %u could not be read from the stream file";
inline constexpr const char* cTrackLoopInvalidFormat = "The loop of Track %u starts after it ends";
inline constexpr const char* cTrackLoopStartMismatchFormat = "The loop start of Track %u does not match the stream file";
inline constexpr const char* cTrackLoopEndMismatchFormat = "The loop end of Track %u does not match the stream file";
inline constexpr const char* cTrackChannelMissingFormat = "Track %u uses a channel the stream file does not have";
inline constexpr const char* cTrackAdpcmUnreadableFormat = "The DSP-ADPCM data of Track %u could not be read";

inline constexpr const char* cFileMissingDetail = "The file could not be found. Saving skips the stream file.";

inline constexpr Message cFileNotCopied = {
    "The stream file is missing",
    "The file could not be found next to the original archive. Copy it next to the saved archive."
};

inline constexpr Message cNoPath = {
    "The path is empty"
};

inline constexpr const char* cPrefetchUnreadable = "The prefetch data could not be read, so Prefetch was turned off";
inline constexpr const char* cPrefetchRemade     = "The prefetch data could not be read. It will be rebuilt on save";

inline constexpr Message cPrefetchNotSaved = {
    "The prefetch data could not be saved",
    "The stream file could not be read. The sound will play without prefetch."
};

inline constexpr Message cNoTracks = {
    "The Stream Sound has no Tracks",
    "No stream file will be written for it."
};

inline constexpr Message cAdtsUnsupported = {
    "ADTS streams are not supported",
    "The file will not be changed during saving."
};

inline constexpr Message cRegionsUnreadable = {
    "The stream regions could not be read",
    "The region data is damaged. Editing the sound will remove its regions."
};

inline constexpr const char* cCopyFailedFormat = "The stream file '%s' could not be copied";
inline constexpr const char* cWriteFailedFormat = "The stream file '%s' could not be written";

inline constexpr const char* cPathRelativeFormat = "Relative to the .%s file's folder";

inline constexpr const char* cNeedsTrack = "Streams need at least 1 Track";
inline constexpr const char* cNoTracksOrChannels = "The Stream Sound has no Tracks or channels";
inline constexpr const char* cTracksSameEncoding = "All Stream Tracks must have the same encoding";
inline constexpr const char* cTracksSameSampleRate = "All Stream Tracks must have the same sample rate";

inline constexpr const char* cTrackLimitFormat = "Streams can have up to %u Tracks";
inline constexpr const char* cTypeUnsupportedFormat = "Only %s and Opus streams are supported";

inline constexpr const char* cTrackAddTip = "Add Track";
inline constexpr const char* cTrackMixerView = "Mixer View";
inline constexpr const char* cTrackTableView = "Table View";
inline constexpr const char* cTrackFold = "Minimize";
inline constexpr const char* cTrackNoSound = "No Stream Sound Selected";
inline constexpr const char* cTrackNone = "No Tracks";
#ifdef __APPLE__
inline constexpr const char* cTrackSoloTip = "Cmd+click to solo more than one track";
#else
inline constexpr const char* cTrackSoloTip = "Ctrl+click to solo more than one track";
#endif
inline constexpr const char* cTrackSurroundPan = "Surround Pan";
inline constexpr const char* cTrackVolumeFormat = "Volume: %.3f";
inline constexpr const char* cTrackMainSendFormat = "Main Send: %.3f";
inline constexpr const char* cTrackFxSendFormat = "FX %c Send: %.3f";
inline constexpr const char* cTrackLpfFormat = "Low-Pass Filter: %.3f";
inline constexpr const char* cTrackBiquadValueFormat = "Biquad Value: %.3f";

inline constexpr const char* cReloadMenuLabel = "Reload Stream File";
inline constexpr const char* cReloadBatchDoneFormat = "Reloaded %u of %u stream files";

inline constexpr const char* cReloadTooltip = "Read the stream file again";
inline constexpr const char* cReplaceTooltip = "Choose another stream file";

inline constexpr const char* cReloadNoArchiveFolder = "Save the archive first: paths are relative";

inline constexpr const char* cPathLabel = "Path";

inline constexpr const char* cReloadNotFoundFormat = "'%s' is not in the archive's folder";
inline constexpr const char* cReloadUnreadableFormat = "'%s' could not be read as a %s or Opus stream file";
inline constexpr const char* cReloadNoChannelsFormat = "'%s' has no channels";
inline constexpr const char* cReloadTooManyChannelsFormat = "'%s' has %u channels, but streams can have up to %u";
inline constexpr const char* cReloadTypeUnsupportedFormat = "'%s' is an Opus stream, which needs the Stream Sound Extension";

inline constexpr const char* cReloadedSummaryFormat = "%s, %s, %u Hz";
inline constexpr const char* cReloadedTracksFormat = "%u Tracks";
inline constexpr const char* cReloadedTracksOneFormat = "%u Track";
inline constexpr const char* cReloadedChannelsFormat = "%u channels";
inline constexpr const char* cReloadedChannelsOneFormat = "%u channel";

inline constexpr const char* cReloadLayoutChanged = "Tracks rebuilt for the new channel count, mix kept";
inline constexpr const char* cReloadTracksReplaced = "Track settings were read from the stream file";
inline constexpr const char* cReloadTypeChangedFormat = "Stream Type changed to %s";
inline constexpr const char* cReloadLoopChanged = "Loop points updated from the file";
inline constexpr const char* cReloadSharedFormat = "Also updated %u Stream Sounds using the same file";
inline constexpr const char* cReloadSharedOneFormat = "Also updated %u Stream Sound using the same file";

inline constexpr const char* cReplaceOutsideArchiveFolderFormat = "The game will not find '%s' outside the archive's folder";

inline constexpr const char* cTypeAdts = "ADTS (AAC)";
inline constexpr const char* cTypeOpus = "Opus";

inline constexpr const char* cPrefetchDetail   = "Keeps the start of the stream in memory";
inline constexpr const char* cTypeNativeDetail = "NintendoWare stream file";
inline constexpr const char* cTypeInfoDetail   = "Stream type info, no known use";
inline constexpr const char* cLoopFromWaveFile = "Set by the first track's Wave File";


inline constexpr const char* cExtensionTitle = "Extension";
inline constexpr const char* cPrefetchLabel  = "Prefetch";
inline constexpr const char* cTypeLabel      = "Stream Type";
inline constexpr const char* cLoopStartLabel = "Loop Start";
inline constexpr const char* cLoopEndLabel   = "Loop End";

inline constexpr const char* cTrackBypass           = "Bypass";
inline constexpr const char* cTrackBiquadValue      = "Value";
inline constexpr const char* cTrackMute             = "Mute";
inline constexpr const char* cTrackSolo             = "Solo";
inline constexpr const char* cTrackMuteShort        = "M";
inline constexpr const char* cTrackSoloShort        = "S";
inline constexpr const char* cTrackMix              = "Mix";
inline constexpr const char* cTrackChannelsFormat   = "%u ch";
inline constexpr const char* cTrackNameFormat       = "Track %u";


inline constexpr const char* cRegionsWindowTitle    = "Regions";
inline constexpr const char* cRegionNone            = "No Regions";
inline constexpr const char* cRegionNext            = "Next";
inline constexpr const char* cRegionRange           = "Range";
inline constexpr const char* cRegionRangeFormat     = "%u to %u";
inline constexpr const char* cRegionAddTip          = "Add Region";
inline constexpr const char* cRegionImport          = "Import Regions from WAV";
inline constexpr const char* cRegionMovingFormat    = "Moving Region %d";
inline constexpr const char* cRegionDisplayFormat   = "Region %d";
inline constexpr const char* cRegionShortTipFormat  = "Shorter than the minimum of %u samples";
inline constexpr const char* cRegionLimitFormat     = "Streams can have up to %u Regions";
inline constexpr const char* cRegionNoWave          = "Track 0 has no Wave File";
inline constexpr const char* cRegionNamesUnsupported = "This stream file cannot store region names";
inline constexpr const char* cRegionTypeUnsupportedFormat = "Only %s streams have regions";
inline constexpr const char* cRegionNameInvalidDetail = "Region names can have up to 63 printable ASCII characters.";

inline constexpr const char* cRegionImportNoneFormat    = "'%s' has no regions";
inline constexpr const char* cRegionImportInvalidFormat = "The region numbers of '%s' are invalid";
inline constexpr const char* cRegionImportedTitle       = "Regions imported";
inline constexpr const char* cRegionImportedFormat      = "'%s': %u imported";
inline constexpr const char* cRegionImportOutsideFormat = "'%s': %u imported, %u outside the stream";
inline constexpr const char* cRegionReplaceTitle        = "Replace Regions";
inline constexpr const char* cRegionReplaceFormat       = "Regions in '%s': %u\nRegions in the stream: %u\n\nImporting will replace the stream's regions.";

inline constexpr const char* cRegionsRemovedTitle       = "Regions removed";
inline constexpr const char* cRegionsRemovedFormat      = "%u past the end of the new Wave File";
inline constexpr const char* cRegionsReplacedTitle      = "Regions replaced";
inline constexpr const char* cRegionsReplacedFormat     = "Replaced by the %u in Track 0's new Wave File";
inline constexpr const char* cSelectedNotStream = "Selected Sound Is Not a Stream";


}

namespace reference
{

inline constexpr const char* cNoSequenceFile = "No Sequence File attached";
inline constexpr const char* cNoWaveFile = "No Wave File attached";
inline constexpr const char* cNoBank = "No Bank attached";
inline constexpr const char* cNothingAttached = "Nothing attached";
inline constexpr const char* cWaveArchiveNotExplicit = "Automatic Wave Archives cannot be opened";

}

namespace playback
{

inline constexpr const char* cWaveFileNoChannels = "The Wave File has no channels";
inline constexpr const char* cUnsupportedSoundType = "The Sound Type is not supported";
inline constexpr const char* cNothingToPlay = "Nothing to play";
inline constexpr const char* cLastSoundFormat = "Last Sound '%s'";
inline constexpr const char* cStopAllVoices = "Middle-click to stop all voices";
inline constexpr const char* cNoSound = "No sound";
inline constexpr const char* cMasterVolume = "Master volume";
inline constexpr const char* cVolumeDbFormat = "%.1f dB";
inline constexpr const char* cVolumeSilent = "-inf dB";
inline constexpr const char* cVolumeWidest = "-00.0 dB";
inline constexpr const char* cShowRemainingTime = "Click to show the remaining time";
inline constexpr const char* cShowElapsedTime = "Click to show the elapsed time";
inline constexpr const char* cLoopFormat = "Loop from %s to %s\nClick to turn off";
inline constexpr const char* cLoopOffFormat = "Loop off, stops at %s\nClick to turn on";
inline constexpr const char* cNoLoop = "No loop";
inline constexpr const char* cSequenceNoEnd = "The sequence has no end";
inline constexpr const char* cUnknownTime = "--:--.---";

inline constexpr const char* cOverridesPersist = "Changes also apply to other Sounds";
inline constexpr const char* cParametersOpen = "Playback Parameters";
inline constexpr const char* cVariablesOpen = "Sequence Variables";
inline constexpr const char* cParameterFormat = "%s: %.2f";
inline constexpr const char* cPanOffsetFormat = "Pan offset: %s";
inline constexpr const char* cPanOffsetNone = "Pan offset: none";
inline constexpr const char* cTempoFormat = "Tempo: %.2f x";
inline constexpr const char* cVolumeFormat = "Volume: %s";
inline constexpr const char* cTrackVolumeFormat = "Track %u Volume: %s";
inline constexpr const char* cTrackWaitFormat = "Track %u Volume: %s, Wait %02X/%02X";
#ifdef __APPLE__
inline constexpr const char* cTrackSwitchTip = "Click to mute, Cmd+click to solo";
#else
inline constexpr const char* cTrackSwitchTip = "Click to mute, Ctrl+click to solo";
#endif
inline constexpr const char* cRightClickReset = "Right-click to reset";
inline constexpr const char* cOverridesReset = "Reset parameters and variables";
inline constexpr const char* cSeqVarOverrideFormat = "%s, set when the sequence starts\nRight-click to reset";
inline constexpr const char* cSeqVarUsedTooltipFormat = "%s, used by this sequence";
inline constexpr const char* cSeqVarUnusedTooltipFormat = "%s, not used by this sequence";

inline constexpr const char* cMeterResetTooltip = "Click to reset";
inline constexpr const char* cMeterMerged = "Merged";


inline constexpr const char* cPanelTrackVolume = "Track Volume";

inline constexpr const char* cKnobTempo        = "Tempo";
inline constexpr const char* cBiquadInherit    = "Inherit";

inline constexpr const char* cSeqVarGlobal      = "Global";
inline constexpr const char* cSeqVarTrackWidest = "Track 15";

inline constexpr const char* cSeqVarStatusNone         = "No Sequence Sound selected";
inline constexpr const char* cSeqVarStatusUnusedFormat = "%s uses no variables";
inline constexpr const char* cSeqVarStatusOneFormat    = "%s uses 1 variable";
inline constexpr const char* cSeqVarStatusManyFormat   = "%s uses %u variables";

inline constexpr const char* cMeterMono        = "M";

inline constexpr const char* cTrackNoWaveFileFormat = "Track %u has no Wave File attached";
inline constexpr const char* cTrackNoChannelsFormat = "Track %u's Wave File has no channels";

}

namespace exporting
{

inline constexpr const char* cSoundSetMidiNeedsSequences = "All Sounds in the Sound Set must be Sequence Sounds";
inline constexpr const char* cNothingToMerge = "No duplicate Wave Files to merge";
inline constexpr const char* cWaveNotExportable = "This Sound cannot be exported as WAV";
inline constexpr const char* cSelectionNotExportable = "Some selected Sounds cannot be exported";

inline constexpr const char* cSampleRateOriginal = "Original";
inline constexpr const char* cWaveDecodeFailed = "A Wave File could not be decoded for export";

inline constexpr const char* cMultiChannelWriteFailed = "The multi-channel WAV file could not be written";
inline constexpr const char* cTrackWavsWriteFailed    = "Some per-track WAV files could not be written";
inline constexpr const char* cWavWriteFailed          = "The WAV file could not be written";
inline constexpr const char* cWavsWriteFailedFormat   = "%d WAV files could not be written";
inline constexpr const char* cWavsWriteFailedOneFormat = "%d WAV file could not be written";
inline constexpr const char* cMidiExportFailed        = "The MIDI file could not be exported";
inline constexpr const char* cMidisExportFailedFormat = "%d MIDI files could not be exported";
inline constexpr const char* cMidisExportFailedOneFormat = "%d MIDI file could not be exported";

inline constexpr const char* cSelectWavFolder  = "Select a folder for WAV export";
inline constexpr const char* cSelectMidiFolder = "Select a folder for MIDI export";
inline constexpr const char* cSelectFolder     = "Select an export folder";

inline constexpr const char* cSequenceTitle = "Export Sequence";
inline constexpr const char* cStreamTitle   = "Export Stream";
inline constexpr const char* cSoundsTitle   = "Export Sounds";
inline constexpr const char* cCompleteTitle = "Export Complete";

inline constexpr const char* cSoundsFormat             = "%d Sounds";
inline constexpr const char* cHeadHertzFormat          = ", %u Hz";
inline constexpr const char* cHeadMono                 = ", Mono";
inline constexpr const char* cHeadStereo               = ", Stereo";
inline constexpr const char* cHeadChannelsFormat       = ", %u channels";
inline constexpr const char* cHeadTracksFormat         = ", %u tracks";
inline constexpr const char* cHeadSequenceNumFormat    = "%d Sequence";
inline constexpr const char* cHeadStreamNumFormat      = "%d Stream";
inline constexpr const char* cHeadWaveNumFormat        = "%d Wave";
inline constexpr const char* cHeadJoin                 = ", ";
inline constexpr const char* cTimelineBox              = "Timeline";
inline constexpr const char* cSegmentIntro             = "Intro";
inline constexpr const char* cSegmentLoopFormat        = "Loop %d";
inline constexpr const char* cSegmentFade              = "Fade";
inline constexpr const char* cSampleRateOriginalFormat = "Original (%u Hz)";
inline constexpr const char* cFileType                 = "File Type";
inline constexpr const char* cFileTypeWav              = "WAV, 16-bit";
inline constexpr const char* cLoopRangeFormat          = "%s to %s";
inline constexpr const char* cLoopNotFound             = "Not found";
inline constexpr const char* cLoopCount                = "Loop Count";
inline constexpr const char* cLoopCountDetail          = "Times the loop plays before the fade";
inline constexpr const char* cFadeOut                  = "Fade Out";
inline constexpr const char* cFadeOutFormat            = "%.1f s";
inline constexpr const char* cNoEndLengthDetail        = "Used when a sequence has no loop or end";
inline constexpr const char* cTimeFormat               = "%u:%02u";
inline constexpr const char* cTimeTenthsFormat         = "%u:%02u.%u";
inline constexpr const char* cSampleRateOriginalDetail = "Rate of the source; 48000 Hz for sequences";
inline constexpr const char* cFilesOne                 = "One multi-channel file";
inline constexpr const char* cFilesOneDetail           = "All tracks as channels of one WAV file";
inline constexpr const char* cFilesEachTrack           = "One file for each track";
inline constexpr const char* cFilesSeqOne              = "One file";
inline constexpr const char* cFilesSeqOneDetail        = "All tracks mixed in one WAV file";
inline constexpr const char* cFilesSeqEachTrackDetail  = "A WAV file for each track, played solo";
inline constexpr const char* cLoopMarkers              = "Loop Markers";
inline constexpr const char* cLoopMarkersDetail        = "Stores the loop points in the WAV file";
inline constexpr const char* cDiskSpace                = "Disk Space";
inline constexpr const char* cShowFiles                = "Show files when complete";
inline constexpr const char* cProgressFileFormat       = "Exporting %s";
inline constexpr const char* cProgressRendering        = "Rendering";
inline constexpr const char* cProgressFileNumFormat    = "File %d of %d";
inline constexpr const char* cFormatWav                = "WAV";

inline constexpr const char* cSequenceExported   = "Sequence exported.";
inline constexpr const char* cSequenceFailed     = "The sequence could not be exported.";
inline constexpr const char* cStreamExported     = "Stream exported.";
inline constexpr const char* cStreamFailed       = "The stream could not be exported.";
inline constexpr const char* cWaveSoundExported  = "Wave Sound exported.";
inline constexpr const char* cWaveSoundFailed    = "The Wave Sound could not be exported.";

inline constexpr const char* cFilesPartFormat       = "Exported %d of %d %s files. %d could not be exported.";
inline constexpr const char* cFilesExportedFormat   = "Exported %d %s files.";
inline constexpr const char* cFileExportedFormat    = "%s file exported.";
inline constexpr const char* cFileFailedFormat      = "The %s file could not be exported.";
inline constexpr const char* cBankExportedFormat    = "Bank exported as %s.";
inline constexpr const char* cBankFailedFormat      = "The Bank could not be exported as %s: it has no instruments or waves.";
inline constexpr const char* cBankBundleExported    = "Bank bundle exported.";
inline constexpr const char* cInstrumentExported    = "Instrument exported.";

inline constexpr const char* cFallbackSoundFormat = "Sound %u";
inline constexpr const char* cFormatBankBundle = "bank bundle";
inline constexpr const char* cFormatInstrument = "instrument";
inline constexpr const char* cFormatSf2        = "SF2";
inline constexpr const char* cFormatDls        = "DLS";
inline constexpr const char* cFormatSoundFont = "SoundFont";

inline constexpr const char* cInstrumentFileOfBankFormat = "(%s) Instrument %d";
inline constexpr const char* cInstrumentFileFormat       = "Instrument %d";

inline constexpr const char* cMidiTrackNumOne            = "1 Track";
inline constexpr const char* cMidiTrackNumFormat         = "%d Tracks";
inline constexpr const char* cMidiInstrumentNumOne       = "1 Instrument";
inline constexpr const char* cMidiInstrumentNumFormat    = "%d Instruments";
inline constexpr const char* cMidiProgramNameFormat      = "Program %u";
inline constexpr const char* cMidiNotesFormat            = "%u of %u notes";
inline constexpr const char* cMidiNothingSelected        = "No notes are selected";
inline constexpr const char* cMidiListEmpty              = "No notes";
inline constexpr const char* cMidiBankLabel              = "Sound Bank";
inline constexpr const char* cMidiSf2Type                = ".sf2";
inline constexpr const char* cMidiDlsType                = ".dls";

inline constexpr const char* cWindowTitleFormat = "%s - Exporting...";

}

namespace bank
{

inline constexpr const char* cFileLoadFailed = "The Bank File could not be loaded";
inline constexpr const char* cNoFileAttached = "The Bank has no Bank File attached";
inline constexpr const char* cWillHaveNoFile = "This Bank will have no Bank File";
inline constexpr const char* cOpenFileTip = "Open the Bank File";


inline constexpr const char* cNoRegionSelected = "Select a Region";
inline constexpr const char* cIgnoreNoteOff = "Ignore Note Off";
inline constexpr const char* cCoarseTooltipFormat = "%+.0f semitones (pitch ratio %.4f)";
inline constexpr const char* cFineTooltipFormat = "%+.0f cents (pitch ratio %.4f)";
inline constexpr const char* cProgramNumber = "Program Number";
inline constexpr const char* cEnvelopeTimeTooltipFormat = "%s: %s";
inline constexpr const char* cEnvelopeLevelTooltipFormat = "%s: %.1f dB";
inline constexpr const char* cEnvelopeCurveTooltipFormat = "Attack %s, Hold %s, Decay %s, Sustain %.1f dB, Release %s";
inline constexpr const char* cIgnoreNoteOffDetail = "Percussion mode: plays the wave to its end";

inline constexpr const char* cFileModeCreateNew = "Create New";

inline constexpr const char* cInstrumentWaveLinkFailedFormat = "Instrument %u: the Wave File could not be linked";

inline constexpr const char* cKnobAttack  = "Attack";
inline constexpr const char* cKnobDecay   = "Decay";
inline constexpr const char* cKnobSustain = "Sustain";
inline constexpr const char* cKnobRelease = "Release";

inline constexpr const char* cKeyRange      = "Key Range";
inline constexpr const char* cVelocityRange = "Vel. Range";
inline constexpr const char* cRootKey       = "Root Key";
inline constexpr const char* cKeyGroup      = "Key Group";

inline constexpr const char* cPanelSample   = "Sample";

inline constexpr const char* cInterpolationPolyphase = "Polyphase (4-point)";
inline constexpr const char* cInterpolationTypes[] = { cInterpolationPolyphase, common::cLinear };

inline constexpr const char* cPanCenter      = "C";
inline constexpr const char* cPanLeftFormat  = "L%d";
inline constexpr const char* cPanRightFormat = "R%d";

inline constexpr const char* cNoVelocityRegionFormat = "The key region has no velocity region for velocity %d";

inline constexpr const char* cInstrumentWaveFileLoadFailedFormat = "Instrument %u: the Wave File could not be loaded";
inline constexpr const char* cInstrumentWaveIdNullFormat = "Instrument %u: the wave ID could not be found";
inline constexpr const char* cVelocityRegionInvalidFormat = "Instrument %u: a velocity region is invalid";
inline constexpr const char* cInvalidKeyRegionTypeFormat = "Instrument %u: the key region type is invalid";
inline constexpr const char* cVelocityRegionEmptyFormat = "Instrument %u: the velocity region is empty";
inline constexpr const char* cKeyRegionInvalidFormat = "Instrument %u: a key region is invalid";
inline constexpr const char* cInvalidInstrumentTypeFormat = "Instrument %u: the instrument type is invalid";
inline constexpr const char* cKeyRegionEmptyFormat = "Instrument %u: the key region is empty";
inline constexpr const char* cInstrumentCountInvalidFormat = "The bank has an invalid number of instruments (%d)";

inline constexpr const char* cInvalidProgramNumberFormat = "Instrument %u: the program number must be 0 to 32766";
inline constexpr const char* cInstrumentNoKeyRegionFormat = "Instrument %u has no Key Region";
inline constexpr const char* cProgramNumberExistsFormat = "Instrument %u: the program number '%d' is already in use";
inline constexpr const char* cKeyRegionNoVelocityRegionFormat = "Instrument %u: a Key Region has no Velocity Region";
inline constexpr const char* cVelocityRegionNoWaveFileFormat = "Instrument %u: a Velocity Region has no Wave File attached";

inline constexpr const char* cDoubleClickToAdd = "Double-click to add";
inline constexpr const char* cNoInstrumentSelected = "Select an Instrument";

inline constexpr const char* cInstrumentNameFormat = "Instrument %d";
inline constexpr const char* cNextProgramNumberTooltip = "Use the next free program number";
inline constexpr const char* cProgramNumberShared = "Already in use";
inline constexpr const char* cVelocityMinFixed = "The lowest Velocity Region always starts at 0";
inline constexpr const char* cVelocityMaxFixed = "The highest Velocity Region always ends at 127";


}

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

namespace sequence
{

inline constexpr const char* cCommandVersionFormat = "Command '{}' requires {} version >= 0x{:08X}";

inline constexpr const char* cNotCompiled = "The Sequence File is not compiled";
inline constexpr const char* cInvalidStartLabel = "The Start Label is invalid. Compile the Sequence File first";

inline constexpr const char* cGoToLabelHint = "Open the Sequence File at this label";

inline constexpr const char* cPlayTooltip = "Play from the label at the cursor";
inline constexpr const char* cNoBanks = "No Bank";
inline constexpr const char* cBanksMoreFormat = "%s +%u";
inline constexpr const char* cFollowOffButton = "Follow: Off";
inline constexpr const char* cFollowTrackButtonFormat = "Follow: Track %d";
inline constexpr const char* cFollowTip = "Scroll to the playing line of a track";
inline constexpr const char* cTrailLabel = "Trail";
inline constexpr const char* cCompileButton = "Compile";
inline constexpr const char* cStateCompiled = "Compiled";
inline constexpr const char* cStateNotCompiled = "Not compiled";
inline constexpr const char* cStateError = "Error";
inline constexpr const char* cFoldAllTooltip = "Fold all labels";
inline constexpr const char* cUnfoldAllTooltip = "Unfold all labels";
inline constexpr const char* cNoInstrument = "no instrument";
inline constexpr const char* cMoreRegionsFormat = " +%d";
inline constexpr const char* cErrorLineFormat = "Line %d: %s";
inline constexpr const char* cErrorJumpTooltip = "Go to line";
inline constexpr const char* cManualFilterHint = "Filter commands";
inline constexpr const char* cManualNoMatch = "No commands match the filter";

inline constexpr const char* cAutoCompileLabel = "Auto Compile";
inline constexpr const char* cWhitespaceLabel = "Show Whitespace";
inline constexpr const char* cMinimapLabel = "Minimap";
inline constexpr const char* cSuggestLabel = "Suggest While Typing";
inline constexpr const char* cHintsLabel = "Parameter Hints";

inline constexpr const char* cFindNoResults = "No results";
inline constexpr const char* cFindPrevious = "Previous match (Shift+F3)";
inline constexpr const char* cFindNext = "Next match (F3)";
inline constexpr const char* cFindClose = "Close (Escape)";
inline constexpr const char* cShowReplace = "Show Replace";
inline constexpr const char* cHideReplace = "Hide Replace";

inline constexpr const char* cGoToLineHint = "Line or line:column";
inline constexpr const char* cGoToLineCountFormat = "of %d";
inline constexpr const char* cLabelFilterHint = "Go to label";
inline constexpr const char* cNoLabelMatch = "No matching labels";

inline constexpr const char* cStatusPositionFormat = "Line %d, Column %d";
inline constexpr const char* cStatusSelectedFormat = "%d selected";
inline constexpr const char* cStatusSelectedLinesFormat = "%d lines, %d selected";
inline constexpr const char* cStatusOverwrite = "Overwrite";
inline constexpr const char* cStatusPositionTooltip = "Go to line (Ctrl+G)";
inline constexpr const char* cStatusLabelsTooltip = "Go to label (Ctrl+Shift+O)";

inline constexpr const char* cMenuGoToDefinition = "Go to Label";
inline constexpr const char* cMenuCut = "Cut";
inline constexpr const char* cMenuCopy = "Copy";
inline constexpr const char* cMenuPaste = "Paste";
inline constexpr const char* cMenuToggleComment = "Toggle Comment";

inline constexpr const char* cBankSlotFormat = "Bank %u";
inline constexpr const char* cErrorAtLineFormat = "Error at line %d:";
inline constexpr const char* cFoldedLinesFormat = "  ... %d lines";

inline constexpr const char* cVariableIdentifier = "variable identifier";
inline constexpr const char* cNoteIdentifier = "note identifier";
inline constexpr const char* cModTypeIdentifier = "modulation type identifier";
inline constexpr const char* cTrackIdentifier = "track identifier";

inline constexpr const char* cErrorInternal = "Internal error";
inline constexpr const char* cErrorLabelEmpty = "The label is empty";
inline constexpr const char* cErrorLabelStart = "Labels must start with a letter";
inline constexpr const char* cErrorLabelInvalid = "The label is invalid";
inline constexpr const char* cErrorLocalLabelDefinedFormat = "Local label '{}' already defined in line {}";
inline constexpr const char* cErrorGlobalLabelDefinedFormat = "Global label '{}' already defined in line {}";
inline constexpr const char* cErrorLocalLabelNotFoundFormat = "Local label '{}' could not be found in '{}'";
inline constexpr const char* cErrorGlobalLabelNotFoundFormat = "Global label '{}' could not be found";
inline constexpr const char* cErrorJumpOutOfBounds = "Jump target is out of bounds";
inline constexpr const char* cErrorArgRangeFormat = "Argument {} must be in the range [{}, {}]";
inline constexpr const char* cErrorArgRangeOpenFormat = "Argument {} must be in the range [0, {})";
inline constexpr const char* cErrorArgEmptyFormat = "Argument {} is empty";
inline constexpr const char* cErrorArgNumberFormat = "Argument {} must be a number";
inline constexpr const char* cErrorArgNumberOrFormat = "Argument {} must be a number or {}";
inline constexpr const char* cErrorArgLabelFormat = "Argument {} is not a valid label";
inline constexpr const char* cErrorArgInternalFormat = "Argument {}: Internal error";
inline constexpr const char* cErrorArgPairInternalFormat = "Argument ({}, {}): Internal error";
inline constexpr const char* cErrorTooManyArgsFormat = "Too many arguments. Expected {}, got {}";
inline constexpr const char* cErrorMissingArgsFormat = "Missing arguments. Expected {}, got {}";
inline constexpr const char* cErrorTooManyExtensions = "Too many extensions";
inline constexpr const char* cErrorExtensionTwiceFormat = "'_{}' extension is used more than once";
inline constexpr const char* cErrorExtensionFirstFormat = "'_{}' extension must be first";
inline constexpr const char* cErrorExtensionLastFormat = "'_{}' extension must be last";
inline constexpr const char* cErrorExtensionNotAllowedFormat = "Command '{}' does not allow the '_{}' extension";
inline constexpr const char* cErrorExtensionUnknownFormat = "Unknown extension '_{}'";
inline constexpr const char* cErrorCurrentVersionFormat = " (Current: 0x{:08X})";
inline constexpr const char* cErrorUnknownCommandFormat = "Unknown command '{}'";

inline constexpr const char* cNoBytesError = "The Sequence File has no compiled data";
inline constexpr const char* cTrackMarkerFormat = "Track {}";
inline constexpr const char* cTrackMarkerNextFormat = ", Track {}";

enum class ManualRowKind
{
    Section,
    Syntax,
    Command,
};

struct ManualRow
{
    ManualRowKind kind;
    const char* name;
    const char* alt;
    const char* args;
    const char* desc;
    const char* range;
};

inline constexpr ManualRow cManualRows[] = {
    { ManualRowKind::Section, "Editor", nullptr, nullptr, nullptr, nullptr },
    { ManualRowKind::Syntax,  "Margin arrow",  nullptr, "", "Click to fold the label, drag to move it.", nullptr },
    { ManualRowKind::Syntax,  "Alt + Up/Down", nullptr, "", "Moves the label at the cursor.", nullptr },
    { ManualRowKind::Syntax,  "Play icon",     nullptr, "", "Plays from this label.", nullptr },
    { ManualRowKind::Syntax,  "Ctrl + F",      "Ctrl + H", "", "Find or replace.", nullptr },
    { ManualRowKind::Syntax,  "Ctrl + G",      nullptr, "", "Goes to a line.", nullptr },
    { ManualRowKind::Syntax,  "Ctrl + Shift + O", nullptr, "", "Lists the labels.", nullptr },
    { ManualRowKind::Syntax,  "Ctrl + Click",  "F12", "", "Goes to the label; Alt + Left goes back.", nullptr },
    { ManualRowKind::Syntax,  "Ctrl + Space",  nullptr, "", "Suggests commands and labels at the cursor.", nullptr },
    { ManualRowKind::Syntax,  "Ctrl + /",      nullptr, "", "Toggles comments.", nullptr },
    { ManualRowKind::Syntax,  "Ctrl + Shift + K", nullptr, "", "Deletes the selected lines.", nullptr },
    { ManualRowKind::Syntax,  "Shift + Alt + Down", nullptr, "", "Copies the selected lines down.", nullptr },

    { ManualRowKind::Section, "Syntax", nullptr, nullptr, nullptr, nullptr },
    { ManualRowKind::Syntax,  "; text",   nullptr, "", "Comment to the end of the line.", nullptr },
    { ManualRowKind::Syntax,  "name:",    nullptr, "", "Global label, for a Start Label, jump, call or opentrack.", nullptr },
    { ManualRowKind::Syntax,  "_name:",   nullptr, "", "Local label of the global label above it.", nullptr },
    { ManualRowKind::Syntax,  "100 0x64 0b110", nullptr, "", "Decimal, hex or binary number; can be negative.", nullptr },
    { ManualRowKind::Syntax,  "VAR_",     nullptr, "0 - 15", "Sequence variable.", nullptr },
    { ManualRowKind::Syntax,  "GVAR_",    nullptr, "0 - 15", "Global variable, shared by all sequences.", nullptr },
    { ManualRowKind::Syntax,  "TVAR_",    nullptr, "0 - 15", "Track variable.", nullptr },
    { ManualRowKind::Syntax,  "TRACK_",   nullptr, "0 - 15", "Track bit for alloctrack; join with |.", nullptr },

    { ManualRowKind::Section, "Suffixes", nullptr, nullptr, nullptr, nullptr },
    { ManualRowKind::Syntax,  "_r",  nullptr, "min, max",    "Random value from min to max.", "pan_r 32, 96" },
    { ManualRowKind::Syntax,  "_v",  nullptr, "var",         "Value from a variable.", "volume_v VAR_0" },
    { ManualRowKind::Syntax,  "_t",  nullptr, "x, time",     "Slides to x over time ticks (volume, volume2, main_volume, pan, span, pitchbend).", "volume_t 0, 96" },
    { ManualRowKind::Syntax,  "_tr", nullptr, "x, min, max", "_t with a random time.", nullptr },
    { ManualRowKind::Syntax,  "_tv", nullptr, "x, var",      "_t with the time from a variable.", nullptr },
    { ManualRowKind::Syntax,  "_if", nullptr, "",            "Runs only if the last cmp_ was true. Must be the last suffix.", "jump_if _loop" },

    { ManualRowKind::Section, "Notes", nullptr, nullptr, nullptr, nullptr },
    { ManualRowKind::Command, "cn4",            nullptr,          "velocity, length", "Plays a note. Name: key c to b, n (natural) or s (sharp), then octave m1 or 0 - 9. cn4 is key 60.", "velocity 0 - 127, length in ticks" },
    { ManualRowKind::Command, "wait",           nullptr,          "length",           "Waits for length ticks.", "48 = quarter note at timebase 48" },
    { ManualRowKind::Command, "notewait_on",    "notewait_off",   "",                 "On: a note waits for its length. Off: notes start together, for chords.", "default on" },
    { ManualRowKind::Command, "tieon",          "tieoff",         "",                 "Tie mode: a note continues the previous one.", "default off" },
    { ManualRowKind::Command, "monophonic_on",  "monophonic_off", "",                 "One note at a time on this track.", "default off" },
    { ManualRowKind::Command, "damper_on",      "damper_off",     "",                 "Damper (sustain) pedal.", "default off" },
    { ManualRowKind::Command, "fin",            nullptr,          "",                 "Stops the track.", nullptr },

    { ManualRowKind::Section, "Program & Tempo", nullptr, nullptr, nullptr, nullptr },
    { ManualRowKind::Command, "prg",         nullptr, "x", "Selects instrument x of the Bank.", "default 0" },
    { ManualRowKind::Command, "bank_select", nullptr, "x", "Selects one of the Sound's 4 Banks.", "0 - 3, default 0" },
    { ManualRowKind::Command, "tempo",       nullptr, "x", "Tempo in BPM.", "0 - 1023, default 120" },
    { ManualRowKind::Command, "timebase",    nullptr, "x", "Ticks per quarter note.", "1 - 255, default 48" },
    { ManualRowKind::Command, "prio",        nullptr, "x", "Voice priority.", "0 - 127, default 64" },

    { ManualRowKind::Section, "Volume & Pan", nullptr, nullptr, nullptr, nullptr },
    { ManualRowKind::Command, "volume",         nullptr, "x", "Track volume.", "0 - 255, default 127. Above 127 is louder" },
    { ManualRowKind::Command, "volume2",        nullptr, "x", "Second track volume, multiplied with volume.", "0 - 255, default 127. Above 127 is louder" },
    { ManualRowKind::Command, "main_volume",    nullptr, "x", "Sequence volume.", "0 - 255, default 127. Above 127 is louder" },
    { ManualRowKind::Command, "velocity_range", nullptr, "x", "Scales note velocity.", "0 - 127, default 127" },
    { ManualRowKind::Command, "pan",            nullptr, "x", "Track pan.", "0 = left, 64 = center, 127 = right" },
    { ManualRowKind::Command, "init_pan",       nullptr, "x", "Start pan of each note.", "0 = left, 64 = center, 127 = right" },
    { ManualRowKind::Command, "span",           nullptr, "x", "Surround pan. Ignored by the TuneBloom player.", "0 = front, 127 = rear" },

    { ManualRowKind::Section, "Pitch", nullptr, nullptr, nullptr, nullptr },
    { ManualRowKind::Command, "transpose",   nullptr,     "x",   "Transposes notes by x semitones.", "-128 - 127, default 0" },
    { ManualRowKind::Command, "pitchbend",   nullptr,     "x",   "Pitch bend; full bend is bendrange semitones.", "-128 - 127, default 0" },
    { ManualRowKind::Command, "bendrange",   nullptr,     "x",   "Pitch bend range in semitones.", "0 - 255, default 2" },
    { ManualRowKind::Command, "sweep_pitch", nullptr,     "x",   "Each note starts x / 64 semitones away, then slides to its key.", "default 0" },
    { ManualRowKind::Command, "porta",       nullptr,     "key", "The next note slides from key.", "note name or 0 - 127" },
    { ManualRowKind::Command, "porta_on",    "porta_off", "",    "Portamento: each note slides from the previous one.", "default off" },
    { ManualRowKind::Command, "porta_time",  nullptr,     "x",   "Portamento speed.", "0 - 255, default 0" },

    { ManualRowKind::Section, "Envelope", nullptr, nullptr, nullptr, nullptr },
    { ManualRowKind::Command, "attack",    nullptr, "x", "Attack rate, overriding the instrument.", "0 - 127" },
    { ManualRowKind::Command, "env_hold",  nullptr, "x", "Hold time.", "0 - 127" },
    { ManualRowKind::Command, "decay",     nullptr, "x", "Decay rate.", "0 - 127" },
    { ManualRowKind::Command, "sustain",   nullptr, "x", "Sustain level.", "0 - 127" },
    { ManualRowKind::Command, "release",   nullptr, "x", "Release rate.", "0 - 127" },
    { ManualRowKind::Command, "env_reset", nullptr, "",  "Restores the instrument's envelope.", nullptr },

    { ManualRowKind::Section, "Modulation", nullptr, nullptr, nullptr, nullptr },
    { ManualRowKind::Command, "mod_depth",  nullptr, "x",    "Depth.", "0 - 255, default 0" },
    { ManualRowKind::Command, "mod_range",  nullptr, "x",    "Range at full depth, in semitones for pitch.", "0 - 255, default 1" },
    { ManualRowKind::Command, "mod_speed",  nullptr, "x",    "Speed, in steps of 0.39 Hz.", "0 - 255, default 16 = 6.25 Hz" },
    { ManualRowKind::Command, "mod_period", nullptr, "x",    "Sets the speed to 100 / x Hz.", "0 = stopped" },
    { ManualRowKind::Command, "mod_delay",  nullptr, "x",    "Start delay, in steps of 5 ms.", "default 0" },
    { ManualRowKind::Command, "mod_type",   nullptr, "type", "Modulation target.", "MOD_TYPE_PITCH, MOD_TYPE_VOLUME, MOD_TYPE_PAN" },
    { ManualRowKind::Command, "mod_phase",  nullptr, "x",    "Start phase.", "0 - 127 for one cycle, version 0x00020000 or later" },
    { ManualRowKind::Command, "mod_curve",  nullptr, "x",    "Wave shape.", "0 = sine, 1 = triangle, 2 = saw, 3 = square, 4 = random, version 0x00020000 or later" },
    { ManualRowKind::Syntax,  "mod2_ mod3_ mod4_", nullptr, "", "The 8 mod_ commands for modulation 2 to 4.", "mod2_depth 64, version 0x00020000 or later" },

    { ManualRowKind::Section, "Filter & Sends", nullptr, nullptr, nullptr, nullptr },
    { ManualRowKind::Command, "lpf_cutoff",     nullptr,           "x",    "Low-pass filter; 64 is open, lower cuts more.", "0 - 127, default 64" },
    { ManualRowKind::Command, "biquad_type",    nullptr,           "type", "Biquad filter type.", "0 = off, 1 = LPF, 2 = HPF, 3 - 5 = BPF" },
    { ManualRowKind::Command, "biquad_value",   nullptr,           "x",    "Biquad filter amount.", "0 - 127, default 0" },
    { ManualRowKind::Command, "mainsend",       nullptr,           "x",    "Send level to the main bus.", "0 - 127, default 127" },
    { ManualRowKind::Command, "fxsend_a",       nullptr,           "x",    "Send level to aux bus A.", "0 - 127, default 0" },
    { ManualRowKind::Command, "fxsend_b",       nullptr,           "x",    "Send level to aux bus B.", "0 - 127, default 0" },
    { ManualRowKind::Command, "fxsend_c",       nullptr,           "x",    "Send level to aux bus C.", "0 - 127, default 0" },
    { ManualRowKind::Command, "frontbypass_on", "frontbypass_off", "",     "Front bypass. Ignored by the TuneBloom player.", "default off" },

    { ManualRowKind::Section, "Flow", nullptr, nullptr, nullptr, nullptr },
    { ManualRowKind::Command, "jump",       nullptr, "label",     "Jumps to label.", nullptr },
    { ManualRowKind::Command, "call",       nullptr, "label",     "Runs the commands at label, then returns at ret.", nullptr },
    { ManualRowKind::Command, "ret",        nullptr, "",          "Returns from call.", nullptr },
    { ManualRowKind::Command, "loop_start", nullptr, "count",     "Starts a loop.", "0 - 255, 0 = no end" },
    { ManualRowKind::Command, "loop_end",   nullptr, "",          "Ends a loop.", nullptr },
    { ManualRowKind::Command, "alloctrack", nullptr, "tracks",    "Reserves tracks; must be first at a start label.", "alloctrack TRACK_0 | TRACK_1" },
    { ManualRowKind::Command, "opentrack",  nullptr, "no, label", "Starts track no at label.", "no 0 - 15, reserved with alloctrack" },

    { ManualRowKind::Section, "Variables", nullptr, nullptr, nullptr, nullptr },
    { ManualRowKind::Command, "setvar",   nullptr, "var, x", "var = x", nullptr },
    { ManualRowKind::Command, "addvar",   nullptr, "var, x", "var = var + x", nullptr },
    { ManualRowKind::Command, "subvar",   nullptr, "var, x", "var = var - x", nullptr },
    { ManualRowKind::Command, "mulvar",   nullptr, "var, x", "var = var * x", nullptr },
    { ManualRowKind::Command, "divvar",   nullptr, "var, x", "var = var / x", nullptr },
    { ManualRowKind::Command, "modvar",   nullptr, "var, x", "var = var % x", nullptr },
    { ManualRowKind::Command, "shiftvar", nullptr, "var, x", "var = var << x (right shift when x < 0)", nullptr },
    { ManualRowKind::Command, "randvar",  nullptr, "var, x", "var = random 0 to x", nullptr },
    { ManualRowKind::Command, "andvar",   nullptr, "var, x", "var = var AND x", nullptr },
    { ManualRowKind::Command, "orvar",    nullptr, "var, x", "var = var OR x", nullptr },
    { ManualRowKind::Command, "xorvar",   nullptr, "var, x", "var = var XOR x", nullptr },
    { ManualRowKind::Command, "notvar",   nullptr, "var, x", "var = NOT x", nullptr },
    { ManualRowKind::Command, "cmp_eq",   nullptr, "var, x", "True if var = x", nullptr },
    { ManualRowKind::Command, "cmp_ne",   nullptr, "var, x", "True if var != x", nullptr },
    { ManualRowKind::Command, "cmp_lt",   nullptr, "var, x", "True if var < x", nullptr },
    { ManualRowKind::Command, "cmp_le",   nullptr, "var, x", "True if var <= x", nullptr },
    { ManualRowKind::Command, "cmp_gt",   nullptr, "var, x", "True if var > x", nullptr },
    { ManualRowKind::Command, "cmp_ge",   nullptr, "var, x", "True if var >= x", nullptr },
    { ManualRowKind::Command, "printvar", nullptr, "var",    "Prints var to the debug output.", nullptr },

    { ManualRowKind::Section, "Other", nullptr, nullptr, nullptr, nullptr },
    { ManualRowKind::Command, "mute",     nullptr, "mode", "Track mute. Ignored by the TuneBloom player.", nullptr },
    { ManualRowKind::Command, "userproc", nullptr, "id",   "Calls a game function. Ignored by the TuneBloom player.", nullptr },
};

inline constexpr const char* cDataCommandVersionFormat   = "Command %s needs a newer file version";
inline constexpr const char* cDataCommandExVersionFormat = "Command 0x%02X 0x%02X needs a newer file version";
inline constexpr const char* cDataCommandMissingFormat   = "Unknown command 0x%02X";
inline constexpr const char* cDataCommandExMissingFormat = "Unknown command 0x%02X 0x%02X";
inline constexpr const char* cDataCommandInvalidFormat   = "Command 0x%02X is invalid here";
inline constexpr const char* cDataCommandMismatchFormat  = "Command 0x%02X cannot be saved without changes";
inline constexpr const char* cDataLabelNotVisitedFormat  = "Label 0x%X could not be reached";
inline constexpr const char* cDataLabelTable             = "The label table is damaged";
inline constexpr const char* cDataLabelEntryFormat       = "Label %u is damaged";
inline constexpr const char* cDataLabelOffsetFormat      = "Label %u points outside the sequence data";

inline constexpr const char* cCompileFirst = "Compile the sequence first";

}

namespace sound
{

inline constexpr const char* c3DApplyTitle = "Apply To";
inline constexpr const char* c3DDistanceTitle = "Distance";
inline constexpr const char* cWaveFileMenuTip = "Wave File actions";
inline constexpr const char* cWaveFileGoTo = "Go to Wave File";
inline constexpr const char* cPanelEnableDetail = "Off: the sound uses the default values";

inline constexpr const char* cWaveSourceImport = "Import WAV";
inline constexpr const char* cBrowseLabel = "Browse...";
inline constexpr const char* cNoImportFile = "No WAV file selected";
inline constexpr const char* cWillHaveNoWaveFile = "This Sound will have no Wave File";

inline constexpr const char* cStreamFileLabel = "Stream File";
inline constexpr const char* cNoStreamFile = "No stream file selected";
inline constexpr const char* cWillHaveNoTracks = "This Sound will have no Tracks";
inline constexpr const char* cImportFileCountFormat = "%u WAV files";
inline constexpr const char* cStreamNewPathDetail = "Created on save, in the archive's folder";


inline constexpr const char* cCreateNoPlayer = "Select a Player, or add one first.";
inline constexpr const char* cCreateNoImportFile = "Select a WAV file to import.";
inline constexpr const char* cCreateNoWaveFile = "Select a Wave File.";
inline constexpr const char* cCreateNoStreamFile = "Select a stream file.";
inline constexpr const char* cCreateNoStreamPath = "Enter a path for the new stream file.";
inline constexpr const char* cCreateStreamPathUsedFormat = "'%s' is already in use.\nPlease enter a different path.";

inline constexpr const char* cTypeSequenceDetail = "Notes played with Bank instruments";
inline constexpr const char* cTypeStreamDetail   = "Long audio streamed from storage";
inline constexpr const char* cTypeWaveDetail     = "One short Wave File";

inline constexpr const char* cSurroundPanDetail  = "Front to rear position";
inline constexpr const char* cMainSendDetail     = "Level to the main output";
inline constexpr const char* cFxSendDetailFormat = "Level to effect bus %c";
inline constexpr const char* cLpfDetail          = "Low-pass filter. 64 is off";
inline constexpr const char* cBiquadDetail       = "Biquad filter amount. 0 is off";

inline constexpr const char* cBiquadLpfDetail     = "Low-pass: cuts the highs";
inline constexpr const char* cBiquadHpfDetail     = "High-pass: cuts the lows";
inline constexpr const char* cBiquadBpf512Detail  = "Band-pass around 512 Hz";
inline constexpr const char* cBiquadBpf1024Detail = "Band-pass around 1024 Hz";
inline constexpr const char* cBiquadBpf2048Detail = "Band-pass around 2048 Hz";

inline constexpr const char* cPanModeDualDetail    = "Pans each channel like a mono sound";
inline constexpr const char* cPanModeBalanceDetail = "Only changes the left/right balance";

inline constexpr const char* cPanCurveDip3DbDetail    = "-3 dB at center, 0 dB at full pan";
inline constexpr const char* cPanCurveBoost3DbDetail  = "0 dB at center, +3 dB at full pan";
inline constexpr const char* cPanCurveClampDetail     = "0 dB at center and at full pan";
inline constexpr const char* cPanCurveLinearDetail    = "-6 dB at center, 0 dB at full pan";
inline constexpr const char* cPanCurveLinear0DbDetail = "0 dB at center, +6 dB at full pan";

inline constexpr const char* c3DSectionDetail     = "Changes with position in the game world";
inline constexpr const char* c3DVolumeDetail      = "Quieter with distance";
inline constexpr const char* c3DPriorityDetail    = "Lower priority with distance";
inline constexpr const char* c3DPanDetail         = "Pan follows the direction";
inline constexpr const char* c3DSurroundDetail    = "Surround pan follows the direction";
inline constexpr const char* c3DFilterDetail      = "Filtered with distance";
inline constexpr const char* c3DDecayRatioDetail  = "Volume drop per unit of distance";
inline constexpr const char* c3DDopplerDetail     = "Pitch shift from movement. 0 is off";
inline constexpr const char* c3DDecayLogDetail    = "Volume is multiplied by the ratio per unit";
inline constexpr const char* c3DDecayLinearDetail = "Volume drops by the ratio per unit";

inline constexpr const char* cPlayerPriorityDetail = "When the player is full, lower stops first";
inline constexpr const char* cActorPlayerDetail    = "Sound actor slot that plays this sound";

inline constexpr const char* cChannelPriorityDetail = "When voices run out, lower stops first";
inline constexpr const char* cFixAtReleaseDetail    = "Keeps the priority during release";

inline constexpr const char* cUserParamSectionDetail = "Values for the game, no effect on sound";

inline constexpr const char* cRemoteFilterDetail = "Muffles the sound on the Wii Remote";
inline constexpr const char* cRemoteFilterNoCtr  = "Not used on 3DS";
inline constexpr const char* cFrontBypassDetail  = "Plays the front channels without surround";

inline constexpr const char* cKnobDecayRatio   = "Decay Ratio";
inline constexpr const char* cKnobDoppler      = "Doppler";
inline constexpr const char* cKnobRemoteFilter = "Remote Filter";

inline constexpr const char* cBiquadHpf       = "HPF";
inline constexpr const char* cBiquadBpf512    = "BPF 512";
inline constexpr const char* cBiquadBpf1024   = "BPF 1024";
inline constexpr const char* cBiquadBpf2048   = "BPF 2048";


inline constexpr const char* cPanelStartLabel = "Start Label";

inline constexpr const char* cStartOfFile              = "(Start of File)";
inline constexpr const char* cStartLabelNotFoundFormat = "The start label could not be found in the Sequence File\n'%s'";

inline constexpr const char* cSectionPanMode         = "Pan Mode";
inline constexpr const char* cSection3D              = "3D";
inline constexpr const char* cSectionPlayerPriority  = "Player Priority";
inline constexpr const char* cSectionChannelPriority = "Channel Priority";
inline constexpr const char* cSectionUserParam       = "User Parameters";
inline constexpr const char* cSectionOther           = "Other";

inline constexpr const char* cPanModeLabel   = "Mode";
inline constexpr const char* cPanModeDual    = "Dual";
inline constexpr const char* cPanModeBalance = "Balance";

inline constexpr const char* cPanCurveSqrt           = "Sqrt";
inline constexpr const char* cPanCurveSqrt0Db        = "Sqrt0Db";
inline constexpr const char* cPanCurveSqrt0DbClamp   = "Sqrt0DbClamp";
inline constexpr const char* cPanCurveSinCos         = "SinCos";
inline constexpr const char* cPanCurveSinCos0Db      = "SinCos0Db";
inline constexpr const char* cPanCurveSinCos0DbClamp = "SinCos0DbClamp";
inline constexpr const char* cPanCurveLinear0Db      = "Linear0Db";
inline constexpr const char* cPanCurveLinear0DbClamp = "Linear0DbClamp";

inline constexpr const char* c3DDecayCurve  = "Decay Curve";
inline constexpr const char* c3DDecayLog    = "Logarithmic";

inline constexpr const char* cActorPlayer  = "Actor Player";
inline constexpr const char* cFixAtRelease = "Fix At Release";

inline constexpr const char* cUnknownOptions  = "Unknown Options";
inline constexpr const char* cOptionBitFormat = "Option Bit %u";
inline constexpr const char* cUserParamFormat = "Param %u";


inline constexpr const char* cTagLabel = "Tag";


inline constexpr const char* cPanCentered    = "Centered";
inline constexpr const char* cPanLeftFormat  = "%d%% Left";
inline constexpr const char* cPanRightFormat = "%d%% Right";

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

}

namespace archive
{

inline constexpr const char* cCorruptFormat = "Your %s file is corrupted beyond repair :(\n%s";

inline constexpr const char* cWaveSoundDataLoadFailedFormat = "The %s file could not be loaded";
inline constexpr const char* cWaveSoundDataInvalidFormat = "The %s file is invalid";
inline constexpr const char* cWaveFileLinkFailed = "The Wave File could not be linked";

inline constexpr const char* cGroupFileFilterFormat = "Group File (*.%s)";

inline constexpr const char* cMetadataWriteFailedTitle = "The metadata file could not be written";
inline constexpr const char* cMetadataWriteFailedText  = "Archive saved successfully, but metadata could not be saved.";
inline constexpr const char* cMetadataUnreadableFormat =
    "The metadata file could not be read\n%s\n"
    "Names were not loaded. A copy was saved as .bak.";
inline constexpr const char* cMetadataUpgradeTitle = "Update Metadata File";
inline constexpr const char* cMetadataUpgradeDetail = "This archive's metadata file uses an older format.";
inline constexpr const char* cMetadataUpgradeLoadedFormat = "%u names loaded";
inline constexpr const char* cMetadataUpgradeMissingFormat = "%u names loaded, %u could not be found in the archive";
inline constexpr const char* cMetadataUpgradeButtonDetail = "Keeps a copy of the old file (.v1.bak)";
inline constexpr const char* cMetadataUpgradeLaterDetail = "The file will be updated on the next save";
inline constexpr const char* cMetadataUpgradeFailedFormat = "The metadata file could not be updated\n%s";
inline constexpr const char* cMetadataWarningTitle = "Save Names?###MetadataWarning";
inline constexpr const char* cMetadataWarningDetail =
    "Metadata saving is off, so this archive's\n"
    "instrument, wave and sequence names will not be saved.";
inline constexpr const char* cMetadataWarningCheckbox = "Save the names for this archive";

inline constexpr const char* cIncludeStringTable = "Include String Table";

inline constexpr const char* cNotArchive = "Not a valid sound archive";
inline constexpr const char* cBfsarVersionUnsupportedFormat = "Sound archive version is not supported (0x%08X)";
inline constexpr const char* cOpenFailed = "The selected file could not be opened";

inline constexpr const char* cOpenProgressFormat     = "Opening %s";
inline constexpr const char* cOpenStageFile          = "Reading the file";
inline constexpr const char* cOpenStageNames         = "Names";
inline constexpr const char* cCmpbinDecompressFailed = "The cmpbin file could not be decompressed";
inline constexpr const char* cBlockNotFoundFormat = "%s: the %s block could not be found";

inline constexpr const char* cNewFileNameFormat = "New.%s";
inline constexpr const char* cUntitledFileNameFormat = "Untitled.%s";
inline constexpr const char* cOpenFilter = "Sound Archive (*.bfsar, *.bcsar, *.cmpbin)";
inline constexpr const char* cFileTypeName = "Nintendo Sound Archive";
inline constexpr const char* cCompressedFilterFormat = "Compressed %s (*.cmpbin)";

inline constexpr const char* cSaveOpenFailed = "The output file could not be opened";
inline constexpr const char* cSaveWriteFailed = "The archive could not be written. The file on disk was not changed";
inline constexpr const char* cCmpbinTempReadFailed = "The temporary archive could not be read";
inline constexpr const char* cCmpbinCompressFailed = "The cmpbin output could not be compressed";
inline constexpr const char* cCmpbinOpenFailed = "The cmpbin output could not be opened";

inline constexpr const char* cInvalidGroupInfo = "The Group entries are damaged";
inline constexpr const char* cInvalidPlayerInfo = "The Player entries are damaged";
inline constexpr const char* cInvalidWaveArchiveInfo = "The Wave Archive entries are damaged";
inline constexpr const char* cInvalidBankInfo = "The Bank entries are damaged";
inline constexpr const char* cInvalidSoundGroupInfo = "The Sound Set entries are damaged";
inline constexpr const char* cInvalidSoundInfo = "The Sound entries are damaged";
inline constexpr const char* cInvalidFileInfo = "The file entries are damaged";
inline constexpr const char* cInvalidPlayerSettings = "The player limits are invalid";
inline constexpr const char* cInvalidStringTable = "The string table is damaged";
inline constexpr const char* cCorruptHeader = "The file header is damaged";
inline constexpr const char* cCorruptBlockFormat = "The %s block extends past the end of the file";

inline constexpr const char* cGroupFileWriteFailedFormat = "The group file '%s' could not be written";
inline constexpr const char* cGroupFileOfOtherArchive = "The group file does not belong to this archive";
inline constexpr const char* cGroupFileExportOwnFile = "This is the archive's own group file. Save to update it";
inline constexpr const char* cGroupItemInvalidFormat = "Item %u is invalid";
inline constexpr const char* cGroupFileDamaged = "The group file is damaged. Its items could not be read";
inline constexpr const char* cGroupItemOrderNotFound = "The item order could not be found in the group's files";

inline constexpr const char* cWaveArchiveNoWaveCount = "Load Individually is on, but the wave count is missing";
inline constexpr const char* cWaveArchiveFileDamaged = "The wave archive file is damaged. Its Wave Files could not be read";
inline constexpr const char* cInnerFileUnreadable = "The file could not be read. The item is empty";
inline constexpr const char* cNameNotInStringTable = "The name is not in the string table";
inline constexpr const char* cNameCutFormat = "The name was shortened to %d characters";
inline constexpr const char* cStartLabelTooLongFormat = "The start label is over %d characters and could not be read";
inline constexpr const char* cInvalidPlayerReference = "The Player reference is invalid";
inline constexpr const char* cBankReferenceInvalidFormat = "The reference to Bank %u is invalid";
inline constexpr const char* cSequenceFileLoadFailed = "The Sequence File could not be loaded";
inline constexpr const char* cWaveFileLoadFailed = "The Wave File could not be loaded";
inline constexpr const char* cPathDirectoryInvalidFormat = "The path's folder could not be read\n'%s'";
inline constexpr const char* cTrackReadErrorFormat = "Track %u could not be read (internal error)";
inline constexpr const char* cStreamNoTracksSkippedFormat = "The Stream Sound '%s' has no Tracks and was skipped";

}

namespace project
{


inline constexpr const char* cPlatformTitle = "Platform";

inline constexpr const char* cLimitSequenceTitle = "Sequence Limits";
inline constexpr const char* cLimitStreamTitle = "Stream Limits";
inline constexpr const char* cLimitWaveTitle = "Wave Limits";

inline constexpr const char* cLimitBufferTimes = "Buffer Times";

inline constexpr const char* cLimitTracksDetail      = "Maximum tracks playing at once";
inline constexpr const char* cLimitChannelsDetail    = "Maximum channels playing at once";
inline constexpr const char* cLimitBufferTimesDetail = "Stream buffer size multiplier";
inline constexpr const char* cStringTableDetail      = "Names the game uses to find items";
inline constexpr const char* cOptionsDetail          = "Reserved flags, no known use";

inline constexpr const char* cRowSoundSets      = "Sound Sets";
inline constexpr const char* cRowBanks          = "Banks";
inline constexpr const char* cRowWaveArchives   = "Wave Archives";
inline constexpr const char* cRowGroups         = "Groups";
inline constexpr const char* cRowPlayers        = "Players";
inline constexpr const char* cRowWaveFiles      = "Wave Files";
inline constexpr const char* cRowSequenceFiles  = "Sequence Files";
inline constexpr const char* cRowBankFiles      = "Bank Files";
inline constexpr const char* cRowFileStatistics = "File Statistics";

inline constexpr const char* cInfoWindowTitle       = "Info";
inline constexpr const char* cPropertiesWindowTitle = "Properties";

inline constexpr const char* cStringTable        = "String Table";
inline constexpr const char* cVersionRangeFormat = "Valid range: %s";
inline constexpr const char* cVersionEditDetail  = "Drag to adjust, double-click to type";

}

namespace import
{

inline constexpr const char* cInstrumentBundleInvalid = "Not a valid instrument bundle file";
inline constexpr const char* cInstrumentBundleVersion = "The instrument bundle version is not supported";
inline constexpr const char* cInstrumentBundleCorrupt = "The instrument bundle file is damaged";

inline constexpr const char* cBankBundleInvalid = "Not a valid bank bundle file";
inline constexpr const char* cBankBundleVersion = "The bank bundle version is not supported";
inline constexpr const char* cBankBundleCorrupt = "The bank bundle file is damaged";
inline constexpr const char* cBankBundleTruncated = "The bank bundle file is incomplete";
inline constexpr const char* cBankBundleNoInstruments = "The bank bundle has no instruments";

inline constexpr const char* cWaveReadFailed = "The WAV file could not be read";
inline constexpr const char* cWaveLoadFailed = "The WAV file could not be loaded";
inline constexpr const char* cNativeDeviceUnavailable = "The native file device could not be accessed";

inline constexpr const char* cReimportWriteFailed = "A temporary WAV file for the reimport could not be written";
inline constexpr const char* cReimportDecodeFailed = "This Wave File could not be decoded for the reimport";

inline constexpr const char* cReimportMenuLabel = "Reimport";
inline constexpr const char* cReplaceWaveFileMenuLabel = "Replace Wave File";
inline constexpr const char* cReimportWaveFileMenuLabel = "Reimport Wave File";
inline constexpr const char* cReplaceWaveFileTip = "Replace the audio with a WAV file";
inline constexpr const char* cReimportWaveFileTip = "Change this Wave File's import settings";
inline constexpr const char* cWaveFileNotSingleSound = "Select one Wave Sound";

inline constexpr const char* cBundledWavesFailedFormat = "%u bundled Wave Files could not be read";
inline constexpr const char* cBundledWavesFailedOneFormat = "%u bundled Wave File could not be read";

inline constexpr const char* cWaveTrimStartTip = "First sample that is kept";
inline constexpr const char* cWaveTrimEndTip = "First sample that is removed";
inline constexpr const char* cWaveTrimSilenceTip = "Trims audio below -60 dBFS at both ends";

inline constexpr const char* cTrackImportMenuLabel = "Import WAVs as Tracks";
inline constexpr const char* cTrackImportTitle = "Import Tracks";
inline constexpr const char* cTrackImportHeaderFormat = "Import %u WAV files into '%s'";
inline constexpr const char* cTrackImportHeaderOneFormat = "Import %u WAV file into '%s'";
inline constexpr const char* cTrackImportTooManyFilesFormat = "Only the first %u files were kept";
inline constexpr const char* cTrackImportFollowsExisting = "Encoding, sample rate, length and loop follow the existing Tracks";
inline constexpr const char* cTrackImportResampleFormat = "%u files will be resampled to %u Hz";
inline constexpr const char* cTrackImportResampleOneFormat = "%u file will be resampled to %u Hz";
inline constexpr const char* cTrackImportPadFormat = "%u files will be padded with silence to %.2f s";
inline constexpr const char* cTrackImportPadOneFormat = "%u file will be padded with silence to %.2f s";
inline constexpr const char* cTrackImportTrimFormat = "%u files will be trimmed to %.2f s";
inline constexpr const char* cTrackImportTrimOneFormat = "%u file will be trimmed to %.2f s";
inline constexpr const char* cTrackImportChannelLimitFormat = "Streams can have up to %u channels";
inline constexpr const char* cTrackImportFailedFormat = "'%s' could not be imported as a Track";

inline constexpr const char* cTrackImportAddFiles    = "Add WAV";
inline constexpr const char* cTrackImportEditTip     = "Change this file's import settings";
inline constexpr const char* cTrackImportFollowsFirst = "Tracks after Track 0 follow its encoding, sample rate, length and loop";

inline constexpr const char* cTrackImportLayoutPerFileDetail = "Files with over 2 channels split into pairs";
inline constexpr const char* cTrackImportLayoutPairMonoDetail = "Two adjacent mono files become L and R";

inline constexpr const char* cWaveDiscardTitle = "Cancel import?";
inline constexpr const char* cWaveDiscardQuestion = "Cancel importing this file?";
inline constexpr const char* cWaveRegionTitle = "Import Wave File";
inline constexpr const char* cWaveRateSourceFormat = "%u Hz (source)";
inline constexpr const char* cWaveSourceSummaryFormat = "%u Hz  -  %s  -  %s";
inline constexpr const char* cWaveNoPreview = "No waveform for this sample format";
inline constexpr const char* cWaveLoopReadoutPeriodsFormat = "Loop %s  -  %u samples  -  %.2f periods";
inline constexpr const char* cWaveLoopReadoutFormat = "Loop %s  -  %u samples";
inline constexpr const char* cWavePreviewResume = "Resume";
inline constexpr const char* cWavePreviewPlay = "Play preview";
inline constexpr const char* cWaveLoopPlaybackOn = "Loop: on";
inline constexpr const char* cWaveLoopPlaybackOff = "Loop: off";
inline constexpr const char* cWaveFitAll = "Fit All";
inline constexpr const char* cWaveFitLoop = "Fit Loop";
inline constexpr const char* cWaveSampleRateTip = "Resampled to this rate on import";
inline constexpr const char* cWaveChannelsKeepAll = "Keep all channels";
inline constexpr const char* cWaveChannelsLeft = "Left only (mono)";
inline constexpr const char* cWaveChannelsRight = "Right only (mono)";
inline constexpr const char* cWaveChannelsMix = "Mix to mono";
inline constexpr const char* cWaveTrimTitle = "Trim";
inline constexpr const char* cWaveTrimSilence = "Trim Silence";
inline constexpr const char* cWavePitchTip = "Changes pitch and length together";
inline constexpr const char* cWaveNormalize = "Normalize";
inline constexpr const char* cWaveNormalizeTip = "Scales the peak to this level";
inline constexpr const char* cWaveNormalizeFormat = "%.1f dBFS";
inline constexpr const char* cWaveNormalizeLevel = "Level";
inline constexpr const char* cWaveLoopModeDetect = "Detect from WAV";
inline constexpr const char* cWaveSuggestLoop = "Suggest";
inline constexpr const char* cWaveSnapLoop = "Snap";
inline constexpr const char* cWaveSuggestLoopTip = "Finds a loop with a clean seam";
inline constexpr const char* cWaveSnapLoopTip = "Snaps to 14-sample frames and whole periods";
inline constexpr const char* cWaveLoopStartTip = "Left-click the waveform to set it";
inline constexpr const char* cWaveLoopEndTip = "Right-click the waveform to set it";
inline constexpr const char* cWaveCrossfade = "Crossfade";
inline constexpr const char* cWaveEqualPower = "Equal Power";
inline constexpr const char* cWaveEqualPowerTip = "Constant loudness through the crossfade";
inline constexpr const char* cWaveCurveTip = "Volume curve; right-click adds a point";
inline constexpr const char* cWaveCurveLevelTipFormat = "Level: %.1f dB";
inline constexpr const char* cWaveCurveSilentTip = "Level: -inf dB";
inline constexpr const char* cWaveCurveTensionTipFormat = "Tension: %+.0f%%";
inline constexpr const char* cWaveCurveDeletePoint = "Delete Point";
inline constexpr const char* cWaveCurveEndPointReason = "The first and last points cannot be deleted";
inline constexpr const char* cWaveCurveSingle = "Single Curve";
inline constexpr const char* cWaveCurveDouble = "Double Curve";
inline constexpr const char* cWaveCurveStairs = "Stairs";
inline constexpr const char* cWaveCurveSmoothStairs = "Smooth Stairs";
inline constexpr const char* cWaveCurvePulse = "Pulse";
inline constexpr const char* cWaveCurveHalfSine = "Half Sine";

inline constexpr const char* cTrackImportRemoveTip = "Remove the file from this import";
inline constexpr const char* cTrackImportBitsFloatFormat = "%u-bit float";
inline constexpr const char* cTrackImportBitsFormat = "%u-bit";
inline constexpr const char* cTrackImportTrackRangeFormat = "Tracks %u-%u";
inline constexpr const char* cTrackImportTrackSideFormat = "Track %u (%s)";
inline constexpr const char* cTrackImportLoopFromFirst = "From first file";
inline constexpr const char* cTrackImportLayout = "Layout";
inline constexpr const char* cTrackImportLayoutPerFile = "One Track per file";
inline constexpr const char* cTrackImportLayoutPairMono = "Pair mono files into stereo Tracks";
inline constexpr const char* cTrackImportLayoutPerChannel = "One mono Track per channel";

inline constexpr const char* cInstrumentReplaceTitle   = "Replace Instrument";
inline constexpr const char* cInstrumentImportTitle    = "Import Instrument";
inline constexpr const char* cInstrumentReplacingFormat = "Replacing: Program %d";
inline constexpr const char* cWavesToImportFormat      = "Waves to import: %u";


inline constexpr const char* cWaveHeaderFormat        = "Import '%s'";
inline constexpr const char* cWaveReplaceHeaderFormat = "Replace with '%s'";
inline constexpr const char* cWaveRegionHeaderFormat  = "Import '%s' onto velocity region";
inline constexpr const char* cWaveImportFailed        = "The WAV file could not be imported";
inline constexpr const char* cWaveDropNeedsRegion     = "Drop a WAV onto a velocity region to import it";

}

namespace browser
{

inline constexpr const char* cTitleFormat = "Select %s";
inline constexpr const char* cAutoPlay    = "Auto Play";
inline constexpr const char* cSelect      = "Select";
inline constexpr const char* cNoMatch     = "No items match the search";
inline constexpr const char* cNoSelection = "No Wave File selected";
inline constexpr const char* cBrowseTip   = "Click to choose from a list";
inline constexpr const char* cSamples     = "Samples";
inline constexpr const char* cUsedBy      = "Used By";

}

namespace item
{

inline constexpr const char* cModifiedTooltip = "Edited since the last save";

inline constexpr const char* cTypeSoundSet     = "Sound Set";

inline constexpr const char* cSearchHint     = "Search by name or ID...";
inline constexpr const char* cCaseSensitive  = "Match case";
inline constexpr const char* cWholeWord      = "Match whole word";
inline constexpr const char* cListMenu       = "List options";
inline constexpr const char* cCountNoteFormat = " %s";
inline constexpr const char* cCountInFormat   = "in %s";
inline constexpr const char* cCountInPlayerFormat = "in %s - Player %s";
inline constexpr const char* cCountInPlayersFormat = "in %s - %u Players";
inline constexpr const char* cOpenFileTip    = "Open the file (or double-click the row)";

inline constexpr const char* cShowStreamWaves   = "Show Stream Waves";
inline constexpr const char* cStreamWavesHidden = "(stream waves hidden)";
inline constexpr const char* cShowSoundsInSet   = "Show Sounds in a Set";
inline constexpr const char* cSoundsInSetHidden = "(sounds in a set hidden)";
inline constexpr const char* cMergeDuplicates   = "Merge Duplicates";
inline constexpr const char* cMergeDuplicatesDetail = "Keeps one of each identical Wave File";
inline constexpr const char* cNoDuplicates      = "No duplicate Wave Files were found.";
inline constexpr const char* cStatsTotalOneFormat = "%s in %u Wave File";
inline constexpr const char* cStatsTotalFormat  = "%s in %u Wave Files";
inline constexpr const char* cStatsEmpty        = "No matching Wave Files";
inline constexpr const char* cStatsUseUnused     = "Unused";
inline constexpr const char* cStatsUseOnlyFormat = "(%s only)";
inline constexpr const char* cStatsFilesOneFormat = "%u file";
inline constexpr const char* cStatsFilesFormat   = "%u files";
inline constexpr const char* cStatsShareFormat   = "%.1f%%";
inline constexpr const char* cStatsShareDetailFormat = "%.2f%%";
inline constexpr const char* cStatsShare         = "Share";
inline constexpr const char* cCountOneFormat = "%d %s";
inline constexpr const char* cCountAllFormat = "%d %ss";
inline constexpr const char* cNoItemsFormat  = "No %ss";

inline constexpr const char* cColumnId          = "ID";
inline constexpr const char* cColumnRate        = "Rate";
inline constexpr const char* cLoopTooltip       = "Loops";
inline constexpr const char* cLoopStartTooltipFormat = "Loops from %u:%06.3f";
inline constexpr const char* cColumnWaves       = "Waves";
inline constexpr const char* cColumnRegions     = "Regions";
inline constexpr const char* cColumnIds         = "IDs";
inline constexpr const char* cColumnUses        = "Uses";
inline constexpr const char* cSizeAllColumns   = "Size all columns to fit###SizeAll";

inline constexpr const char* cMoreTooltipFormat        = "+%u more";

inline constexpr const char* cNameInvalidDetail =
    "Names can only use letters, numbers and underscores,\nand cannot start with a number.";
inline constexpr const char* cNameDuplicated = "The name is already in use";
inline constexpr const char* cNameEmpty = "The name cannot be empty";

inline constexpr const char* cMovingItemsFormat = "Moving %zu items";
inline constexpr const char* cMovingItemFormat = "Moving '%s'";

inline constexpr const char* cAddDialogTitleFormat = "Add %s";
inline constexpr const char* cDeleteReferencedFormat = "'%s' is referenced by other items.\nDelete anyway?";
inline constexpr const char* cDeleteReferencedMany = "Some items are referenced by other items.\nDelete anyway?";

inline constexpr const char* cSelectorNone = "[-] (none)";
inline constexpr const char* cSelectorAutomaticShared = "Automatic (Shared)";
inline constexpr const char* cSelectorAutomaticIndividual = "Automatic (Individual)";
inline constexpr const char* cSelectorInvalid = "Invalid";


inline constexpr const char* cNoItemSelected = "No Item Selected";

inline constexpr const char* cTypeStreamSound      = "Stream Sound";
inline constexpr const char* cTypeSequenceSound    = "Sequence Sound";
inline constexpr const char* cTypeSequenceSoundSet = "Sequence Sound Set";

inline constexpr const char* cMergeTitle          = "Confirm Merge";
inline constexpr const char* cMergeQuestionFormat = "Merge %u duplicate Wave Files?";
inline constexpr const char* cMergeQuestionOneFormat = "Merge %u duplicate Wave File?";
inline constexpr const char* cMergeDetailFormat   = "References will point to the kept file. Saves ~%s.";
inline constexpr const char* cMergeRemovedFormat  = "Removed: [%u] %s";
inline constexpr const char* cMergeResultFormat   = "%u duplicate Wave Files were removed (~%s).";
inline constexpr const char* cMergeResultOneFormat = "%u duplicate Wave File was removed (~%s).";
inline constexpr const char* cMergeSizeKbFormat   = "%llu KB";
inline constexpr const char* cStatsFilesSample = "0000 files";
inline constexpr const char* cStatsShareSample = "100.0%";

}

namespace soundset
{

inline constexpr const char* cStickyEdit = "Sticky Edit";

inline constexpr const char* cStickyEditDetail = "ID edits also move the adjacent sets";

inline constexpr const char* cEmptyDetail = "No sounds and no ID range";

inline constexpr const char* cSoundTypeLabel   = "Sound Type";
inline constexpr const char* cStartIdLabel     = "Start ID";
inline constexpr const char* cEndIdLabel       = "End ID";
inline constexpr const char* cIdRangeTitle     = "ID Range";


inline constexpr const char* cErrorInvalidRange       = "The Start and End IDs are invalid";
inline constexpr const char* cErrorStartExceedsCount  = "The Start ID is above the sound count";
inline constexpr const char* cErrorEndExceedsCount    = "The End ID is above the sound count";
inline constexpr const char* cErrorSoundInOtherSetFormat = "The Sound '%s' is already in the Sound Set '%s'";

inline constexpr const char* cNoneSelected = "No Set Selected";

}

namespace player
{

inline constexpr const char* cSoundLimitDetail = "Maximum sounds playing at once";
inline constexpr const char* cHeapSizeDetail   = "Memory to load each sound's data";

inline constexpr const char* cSoundLimit = "Sound Limit";
inline constexpr const char* cHeapSize   = "Heap Size";

inline constexpr const char* cHeapSizeUnit = "bytes";

}

namespace group
{

inline constexpr const char* cEmbedDetail      = "Files are stored inside the group";
inline constexpr const char* cLinkDetail       = "Group points to files in the archive";
inline constexpr const char* cExternalDetail   = "Files are stored in a separate group file";

inline constexpr const char* cOutputTypeLabel = "Output Type";
inline constexpr const char* cOutputEmbed     = "Embed";
inline constexpr const char* cOutputLink      = "Link";
inline constexpr const char* cOutputExternal  = "External";

inline constexpr const char* cGroupFileLabel       = "Group File";
inline constexpr const char* cGroupFileNotLoaded   = "Not loaded";
inline constexpr const char* cExtraFilesLabel      = "Extra Files";
inline constexpr const char* cExtraFilesDetail     = "Files in the group file that no item uses";
inline constexpr const char* cOutputTypeLockedTip  = "The group file is not loaded";
inline constexpr const char* cGroupFileExportedTitle = "Exported group file";
inline constexpr const char* cGroupFileDeletedTitle  = "Deleted unused group file";

inline constexpr const char* cLocateHeadOne            = "1 group file could not be found. Without it the archive cannot be saved";
inline constexpr const char* cLocateHeadManyFormat     = "%u group files could not be found. Without them the archive cannot be saved";
inline constexpr const char* cLocateSearchFolder       = "Search Folder";
inline constexpr const char* cLocateSearchFolderDetail = "Finds the files by group name in a folder";
inline constexpr const char* cLocateSearchFolderTitle  = "Select the Folder with the Group Files";
inline constexpr const char* cLocateBrowseTitleFormat  = "Select the Group File of '%s'";
inline constexpr const char* cLocateNotSelected        = "Not selected";
inline constexpr const char* cLocateUnreadable         = "The file could not be read";
inline constexpr const char* cLocateOtherArchive       = "Belongs to another archive";
inline constexpr const char* cLocateBrowse             = "Browse";
inline constexpr const char* cLocateRemember           = "Remember for this archive";
inline constexpr const char* cLocateRememberDetail     = "Uses these files at the next open";
inline constexpr const char* cLocateLoad               = "Load";
inline constexpr const char* cLocateLoadDisabled       = "No group file is selected";
inline constexpr const char* cLocateSkip               = "Skip";
inline constexpr const char* cLocateSkipDetail         = "Leaves these groups with no items";

inline constexpr const char* cItemTypeLabel  = "Item Type";
inline constexpr const char* cLoadItemsLabel = "Load Items";
inline constexpr const char* cItemDisabled   = "(disabled)";

inline constexpr const char* cLoadSequenceAndBank           = "Sequence and Bank";
inline constexpr const char* cLoadSequenceAndWaveArchive    = "Sequence and Wave Archive";
inline constexpr const char* cLoadBankAndWaveArchive        = "Bank and Wave Archive";
inline constexpr const char* cLoadSequenceSetAndBank        = "Sequence Set and Bank";
inline constexpr const char* cLoadSequenceSetAndWaveArchive = "Sequence Set and Wave Archive";
inline constexpr const char* cLoadSequenceSet               = "Sequence Set";

inline constexpr const char* cErrorExternalFileMissing = "The group file is not loaded, so this group cannot be saved";
inline constexpr const char* cErrorInvalidOutputType   = "The Output Type is invalid";
inline constexpr const char* cErrorInvalidItemType     = "The Item Type is invalid";
inline constexpr const char* cErrorItemNotAttached     = "No item is attached";
inline constexpr const char* cErrorSoundNotSequence    = "The Sound must be a Sequence Sound";
inline constexpr const char* cErrorStreamNeedsPrefetch = "A Stream Sound in a Group needs Prefetch on";
inline constexpr const char* cErrorWaveArchiveLoadAll  = "Select 'All' for a Wave Archive";
inline constexpr const char* cErrorSequenceSetLoadDetail =
    "Select 'All', 'Bank and Wave Archive' or 'Sequence Set'\nwhen a Sequence Sound's Bank uses an automatic Wave Archive";
inline constexpr const char* cErrorSequenceLoadDetail =
    "Select 'All', 'Bank and Wave Archive' or 'Sequence'\nwhen the Sequence Sound's Bank uses an automatic Wave Archive";
inline constexpr const char* cErrorAutomaticLoadAllDetail = "Select 'All' for an automatic Wave Archive";


inline constexpr const char* cNoneSelected   = "No Group Selected";
inline constexpr const char* cAddItem        = "Add Item";
inline constexpr const char* cAddTitleFormat = "Add %s to Group";
inline constexpr const char* cAddNoItems     = "The archive has no items of this type";

}

namespace wavearchive
{

inline constexpr const char* cLoadIndividuallyDetail = "The game can load single waves from it";

inline constexpr const char* cLoadIndividually = "Load Individually";

}

namespace wavefile
{

inline constexpr const char* cPcm8Detail       = "8 bits per sample, uncompressed";
inline constexpr const char* cPcm16Detail      = "16 bits per sample, uncompressed";
inline constexpr const char* cDspAdpcmDetail   = "Compressed to about 4.6 bits per sample";
inline constexpr const char* cSamplesFormat    = "%u samples";
inline constexpr const char* cTimeFormat       = "%u:%06.3f";

inline constexpr const char* cWaveform         = "Waveform";
inline constexpr const char* cPlayLeft         = "Play the left channel";
inline constexpr const char* cPlayRight        = "Play the right channel";
inline constexpr const char* cTooLong          = "Too long to show as a waveform";
inline constexpr const char* cNoData           = "No sample data";
inline constexpr const char* cLoopReadoutFormat      = "Loop %u:%06.3f";
inline constexpr const char* cLoopDirtyReadoutFormat = "Loop %u:%06.3f  -  rebuilt on play or save";


inline constexpr const char* cEncodingPcm8 = "Pcm8";
inline constexpr const char* cEncodingPcm16 = "Pcm16";
inline constexpr const char* cEncodingDspAdpcm = "DspAdpcm";

inline constexpr const char* cInvalidEncoding = "The encoding is invalid";
inline constexpr const char* cInvalidLoopRange = "The loop start or end is invalid";
inline constexpr const char* cNoDataToWrite = "No data to write";
inline constexpr const char* cNeedsChannel = "A Wave File needs at least 1 channel";
inline constexpr const char* cChannelLimit = "A Wave File can have up to 2 channels";
inline constexpr const char* cLoopInvalidFormat = "The loop starts after it ends";
inline constexpr const char* cDataBlockDamaged = "The Wave File's data block is damaged";
inline constexpr const char* cSampleOffsetInvalidFormat = "Channel %d points outside the audio data";

inline constexpr const char* cEncodingWarningDetail = "Changing the Wave File encoding can reduce audio quality.\nConvert anyway?";

inline constexpr const char* cWavOpenFailedFormat = "The file could not be opened\n%s";
inline constexpr const char* cNotWavFile = "Not a valid .wav file";
inline constexpr const char* cWavCorruptChunk = "A chunk size in the .wav file is damaged";
inline constexpr const char* cWavOnlyPcm = "Only PCM .wav files are supported";
inline constexpr const char* cWavOnlyMonoStereo = "Only mono and stereo .wav files are supported";
inline constexpr const char* cWavOnlyPcmBits = "Only 8, 16, 24 and 32-bit .wav files are supported";
inline constexpr const char* cWavOnlyPcm8Pcm16 = "Only Pcm8 and Pcm16 .wav files are supported";
inline constexpr const char* cWavNoData = "The 'data' block could not be found";
inline constexpr const char* cWavInvalidInfo = "The .wav format is invalid";

}

namespace innerfile
{

inline constexpr const char* cNoNameHint         = "No name";
inline constexpr const char* cReferences         = "References";
inline constexpr const char* cNoReferences       = "No references";
inline constexpr const char* cArchiveDetail      = "Set by the archive, read only";
inline constexpr const char* cByteOrder          = "Byte Order";
inline constexpr const char* cBigEndian          = "Big Endian";
inline constexpr const char* cLittleEndian       = "Little Endian";
inline constexpr const char* cStreamVersion      = "Stream Version";

}

namespace recent
{

inline constexpr const char* cEmpty = "No recent files";
inline constexpr const char* cClear = "Clear Recent Files";

inline constexpr const char* cPinnedTitle = "Pinned";


inline constexpr const char* cPin = "Pin";
inline constexpr const char* cUnpin = "Unpin";
inline constexpr const char* cRemove = "Remove from List";
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

namespace date
{

inline constexpr const char* cTodayFormat     = "Today %s";
inline constexpr const char* cYesterdayFormat = "Yesterday %s";
inline constexpr const char* cThisYearFormat  = "%s %d, %s";
inline constexpr const char* cOtherYearFormat = "%s %d, %d";
inline constexpr const char* cFullFormat      = "%s %d, %d, %s";
inline constexpr const char* cAmFormat        = "%d:%02d AM";
inline constexpr const char* cPmFormat        = "%d:%02d PM";

inline constexpr const char* cJustNow          = "Just now";
inline constexpr const char* cMinuteAgo        = "1 minute ago";
inline constexpr const char* cMinutesAgoFormat = "%d minutes ago";
inline constexpr const char* cHourAgo          = "1 hour ago";
inline constexpr const char* cHoursAgoFormat   = "%d hours ago";
inline constexpr const char* cYesterday        = "Yesterday";
inline constexpr const char* cDaysAgoFormat    = "%d days ago";
inline constexpr const char* cMonthAgo         = "1 month ago";
inline constexpr const char* cMonthsAgoFormat  = "%d months ago";
inline constexpr const char* cYearAgo          = "1 year ago";
inline constexpr const char* cYearsAgoFormat   = "%d years ago";

inline constexpr const char* cMonths[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

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
inline constexpr const char* cBoxMotion       = "Motion";
inline constexpr const char* cBoxDisplay      = "Display";
inline constexpr const char* cBoxDebug        = "Debug";
inline constexpr const char* cBoxSettings     = "Settings";

inline constexpr const char* cOpenFolder = "Open Folder";

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
inline constexpr const char* cUnusedGroupFilesKeep = "Keep";
inline constexpr const char* cUnusedGroupFilesDelete = "Delete When Saving";
inline constexpr const char* cUnusedGroupFilesDetail = "Files of groups that are no longer external";

inline constexpr const char* cIgnoredLabel = "Ignored Warnings";
inline constexpr const char* cIgnoredCountFormat = "%zu %s";
inline constexpr const char* cIgnoredArchivePlural = "Archives";
inline constexpr const char* cIgnoredDetail = "Archives with hidden opening warnings";

inline constexpr const char* cStartupLabel = "On Startup";
inline constexpr const char* cStartupStartPage = "Start Page";
inline constexpr const char* cStartupStartPageDetail = "Shows recent files and links";
inline constexpr const char* cStartupLastArchive = "Last Archive";

inline constexpr const char* cAssociationLabel = "File Association";
inline constexpr const char* cAssociationNone = "Not Associated";
inline constexpr const char* cAssociationOn = "Associated";
inline constexpr const char* cAssociationAdd = "Associate";
inline constexpr const char* cAssociationRemove = "Remove";
inline constexpr const char* cAssociationDetail = "Opens sound archive files in TuneBloom";
inline constexpr const char* cAssociationFailed = "The file association could not be changed.";

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

namespace notify
{

inline constexpr const char* cTitle       = "Notifications";
inline constexpr const char* cClear       = "Clear";
inline constexpr const char* cEmpty       = "No notifications in this session";

}

namespace backup
{

inline constexpr const char* cMenuRestore = "Restore Backup";
inline constexpr const char* cRecoverBackupFormat = "%s, %s";
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

namespace dialog
{

inline constexpr const char* cOk               = "OK";
inline constexpr const char* cYes              = "Yes";
inline constexpr const char* cNo               = "No";
inline constexpr const char* cSaveAnyway       = "Save Anyway";
inline constexpr const char* cTryAgain         = "Try Again";
inline constexpr const char* cUpdate           = "Update";
inline constexpr const char* cLater            = "Later";
inline constexpr const char* cRemoveAllAndSave = "Remove All And Save";
inline constexpr const char* cCreate           = "Create";
inline constexpr const char* cMerge            = "Merge";
inline constexpr const char* cConvert          = "Convert";

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

}

namespace format
{

inline constexpr const char* cLabelCafe = "Cafe Sound Archive";
inline constexpr const char* cLabelCtr  = "CTR Sound Archive";

inline constexpr const char* cSystemWiiU   = "Wii U";
inline constexpr const char* cSystem3ds    = "3DS";
inline constexpr const char* cSystemSwitch = "Switch";


}

namespace opus
{

inline constexpr const char* cNotOpusStream = "Not a valid OpusStream file (missing info/data chunk)";
inline constexpr const char* cChannelCountUnsupported = "The OpusStream must be mono or stereo";
inline constexpr const char* cSampleRateUnsupported = "The OpusStream sample rate is not supported";
inline constexpr const char* cAudioDataDamaged = "The Opus audio data is damaged";
inline constexpr const char* cNoAudibleSamples = "The OpusStream has no audible samples";
inline constexpr const char* cDecoderCreateFailed = "The Opus decoder could not be created";
inline constexpr const char* cDecodeFailed = "The Opus data could not be decoded";

inline constexpr const char* cEncodeFailedFormat = "The Opus stream could not be encoded\n%s";
inline constexpr const char* cEncodeChannelLimit = "Only 1 or 2 channels in total are supported";
inline constexpr const char* cEncodeSampleRate = "The sample rate must be 8000, 12000, 16000, 24000 or 48000 Hz";
inline constexpr const char* cEncodeDecodeFailed = "A Track's Wave File could not be decoded";
inline constexpr const char* cEncoderCreateFailed = "The Opus encoder could not be created";
inline constexpr const char* cEncodeError = "The encoder returned an error";

}

namespace reader
{

inline constexpr const char* cNoFile = "No file";
inline constexpr const char* cNotBankFile = "Not a valid bank file";
inline constexpr const char* cNotGroupFile = "Not a valid group file";
inline constexpr const char* cNotSequenceFile = "Not a valid sequence file";
inline constexpr const char* cNotWaveArchiveFile = "Not a valid wave archive file";
inline constexpr const char* cNotWaveFile = "Not a valid wave file";
inline constexpr const char* cNotWaveSoundFile = "Not a valid wave sound file";
inline constexpr const char* cVersionUnsupportedFormat = "The %s version is not supported (0x%08X)";

}

namespace filedialog
{

inline constexpr const char* cFilterWave                   = "Wave (*.wav)";
inline constexpr const char* cFilterMidi                   = "MIDI (*.midi)";
inline constexpr const char* cFilterAllAudio               = "All supported audio (*.wav, *.bcwav, *.bfwav)";
inline constexpr const char* cFilterFileFormat             = "%s file (*.%s)";
inline constexpr const char* cFilterBankBundleFormat       = "Bank Bundle (*.%s)";
inline constexpr const char* cFilterInstrumentBundleFormat = "Instrument Bundle (*.%s)";

inline constexpr const char* cAllFilesFilter = "All Files (*.*)";

}

namespace unit
{

inline constexpr const char* cHertzFormat = "%u Hz";
inline constexpr const char* cHertzSample = "00000 Hz";

inline constexpr const char* cSampleRate8000  = "8000 Hz";
inline constexpr const char* cSampleRate11025 = "11025 Hz";
inline constexpr const char* cSampleRate16000 = "16000 Hz";
inline constexpr const char* cSampleRate22050 = "22050 Hz";
inline constexpr const char* cSampleRate32000 = "32000 Hz";
inline constexpr const char* cSampleRate44100 = "44100 Hz";
inline constexpr const char* cSampleRate48000 = "48000 Hz";
inline constexpr const char* cSampleRate96000 = "96000 Hz";

inline constexpr const char* cBytesFormat = "%u B";
inline constexpr const char* cKilobytesFormat = "%.1f KB";
inline constexpr const char* cMegabytesFormat = "%.2f MB";
inline constexpr const char* cGigabytesFormat = "%.2f GB";
inline constexpr const char* cSizeSample = "000.00 MB";
inline constexpr const char* cKilobytesSample = "000.0 KB";

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
inline constexpr const char* cOpen        = "Open";
inline constexpr const char* cOpenRecent  = "Open Recent";
inline constexpr const char* cSaveAs      = "Save As";
inline constexpr const char* cExit        = "Exit";
inline constexpr const char* cPreferences = "Preferences";
inline constexpr const char* cAbout       = "About";
inline constexpr const char* cUnsaved     = "Unsaved";
inline constexpr const char* cSaved       = "Saved";

inline constexpr const char* cShowUnsaved      = "Show Unsaved Changes";
inline constexpr const char* cCreateBackup     = "Create Backup";
inline constexpr const char* cShowBackupFolder = "Show Backup Folder";
inline constexpr const char* cCopyPath         = "Copy Path";

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

inline constexpr const char* cTransport = "Transport";

inline constexpr const char* cShortcuts   = "Keyboard Shortcuts";
inline constexpr const char* cRepository  = "GitHub Repository";
inline constexpr const char* cReportIssue = "Report an Issue";
inline constexpr const char* cDiscord     = "Discord Server";

}

namespace keys
{


inline constexpr const char* cGroupLists    = "Item Lists";
inline constexpr const char* cGroupSearch   = "Search";
inline constexpr const char* cGroupPiano    = "Piano Keyboard";
inline constexpr const char* cGroupMouse    = "Mouse";

inline constexpr const char* cCloseFileWindow = "Close File Window";

inline constexpr const char* cPreviousItem   = "Previous Item";
inline constexpr const char* cNextItem       = "Next Item";
inline constexpr const char* cFirstItem      = "First Item";
inline constexpr const char* cLastItem       = "Last Item";
inline constexpr const char* cShowSelection  = "Scroll to Selection";

inline constexpr const char* cPlayPause = "Play / Pause";

inline constexpr const char* cClearSearch = "Clear Search";

inline constexpr const char* cZoomIn      = "Zoom In";
inline constexpr const char* cZoomOut     = "Zoom Out";
inline constexpr const char* cOctaveDown  = "Octave Down";
inline constexpr const char* cOctaveUp    = "Octave Up";
inline constexpr const char* cOctaveReset = "Reset Octave";

inline constexpr const char* cAddToSelection = "Add to Selection";
inline constexpr const char* cSelectRange    = "Select a Range";
inline constexpr const char* cZoomHorizontal = "Zoom Horizontally";
inline constexpr const char* cZoomVertical   = "Zoom Vertically";
inline constexpr const char* cPanHorizontal  = "Scroll Horizontally";

inline constexpr const char* cKeyCmd      = "Cmd";
inline constexpr const char* cKeyCtrl     = "Ctrl";
inline constexpr const char* cKeyShift    = "Shift";
inline constexpr const char* cKeyAlt      = "Alt";
inline constexpr const char* cKeyEscape   = "Esc";
inline constexpr const char* cKeyClick    = "Click";
inline constexpr const char* cKeyWheel    = "Wheel";

}

}
