#pragma once

namespace messages
{

struct Message
{
    const char* text = nullptr;
    const char* detail = nullptr;
};

inline constexpr const char* cNotImplemented = "Not implemented";

namespace stream
{

inline constexpr const char* cFileMissingFormat = "Couldn't load '%s' next to your .%s file";
inline constexpr const char* cFileIncompleteFormat = "Couldn't load '%s' because the file is incomplete";
inline constexpr const char* cFileUnreadableFormat = "Couldn't read '%s' as a stream file";
inline constexpr const char* cFileNotStream = "File is not a valid stream file";
inline constexpr const char* cFileNoInfoBlockFormat = "Couldn't find the INFO block in the %s file";
inline constexpr const char* cFileBlocksMisalignedFormat = "The %s file's blocks are not aligned";
inline constexpr const char* cFileNotLoaded = "Stream file isn't loaded";

inline constexpr const char* cAudioUnreadableDetail =
    "The stream file's audio could not be read, so its Stream Sound will have no audio."
    "\n\n"
    "You can still save the archive, and the stream file is kept as-is.";

inline constexpr const char* cSharedTrackCountFormat = "'%s' shares its stream file with '%s' but has %d Tracks instead of %d";
inline constexpr const char* cTrackUnreadableFormat = "Couldn't read Track %u from the stream file";
inline constexpr const char* cTrackLoopInvalidFormat = "Track %u has an invalid loop (%u >= %u)";
inline constexpr const char* cTrackLoopStartMismatchFormat = "Track %u has an invalid loop start (%u instead of %u)";
inline constexpr const char* cTrackLoopEndMismatchFormat = "Track %u has an invalid loop end (%u instead of %u)";
inline constexpr const char* cTrackChannelMissingFormat = "Track %u uses a channel the stream file doesn't have";
inline constexpr const char* cTrackAdpcmUnreadableFormat = "Couldn't read the DSP-ADPCM data for Track %u";

inline constexpr const char* cFileMissingDetail =
    "The stream file could not be loaded, so its Stream Sound will have no audio."
    "\n\n"
    "You can still open the archive, but the stream file will be skipped when saving.";

inline constexpr Message cFileNotCopied = {
    "Stream file is missing",
    "The stream file could not be found next to the archive it was opened from, so there was nothing to copy."
    "\n\n"
    "The archive was saved, but this Stream Sound will have no audio until you put its file next to it."
};

inline constexpr Message cNoPath = {
    "Path is empty"
};

inline constexpr Message cNoTracks = {
    "Couldn't find any Track information",
    "This Stream Sound has no Tracks, so there is nothing to write."
    "\n\n"
    "You can still save the archive, but no stream file will be produced for it."
};

inline constexpr Message cAdtsUnsupported = {
    "ADTS streams are not supported",
    "ADTS streams cannot be read or rebuilt."
    "\n\n"
    "You can still save the archive, but the original stream file is kept as-is."
};

inline constexpr Message cRegionUnsupported = {
    "Stream region (REGN) block is not supported"
};

inline constexpr const char* cCopyFailedFormat = "Couldn't copy the stream file '%s'";

inline constexpr const char* cPathRelativeFormat = "This is relative to your .%s file";

inline constexpr const char* cNeedsTrack = "Streams must have at least 1 Track";
inline constexpr const char* cNoTracksOrChannels = "Stream Sound has no Tracks or channels";
inline constexpr const char* cTracksSameEncoding = "All Stream Tracks must have the same encoding";
inline constexpr const char* cTracksSameSampleRate = "All Stream Tracks must have the same sample rate";

inline constexpr const char* cTrackLimitFormat = "Streams can only have up to %u Tracks";
inline constexpr const char* cTypeUnsupportedFormat = "Only %s and Opus streams are supported";
inline constexpr const char* cTrackNoWaveFileFormat = "Track %u: No Wave File attached";
inline constexpr const char* cTrackWaveFileNoChannelsFormat = "Track %u: Wave File has no channels";

inline constexpr const char* cReloadMenuLabel = "Reload Stream File";
inline constexpr const char* cReloadNotStream = "Only Stream Sounds have a stream file";
inline constexpr const char* cReloadBatchDoneFormat = "Reloaded %u of %u stream files";

inline constexpr const char* cReloadTooltip = "Read the stream file again and update this Stream Sound's Tracks and channels";
inline constexpr const char* cReplaceTooltip = "Choose a different stream file for this Stream Sound";

inline constexpr const char* cReloadNoArchiveFolder = "Save the archive first, since stream paths are relative to it";

inline constexpr const char* cPathLabel = "Path";

inline constexpr const char* cReloadNotFoundFormat = "Couldn't find '%s' next to the archive";
inline constexpr const char* cReloadUnreadableFormat = "Couldn't read '%s' as a %s or Opus stream file";
inline constexpr const char* cReloadNoChannelsFormat = "'%s' has no channels";
inline constexpr const char* cReloadTooManyChannelsFormat = "'%s' has %u channels, but streams can only have up to %u";
inline constexpr const char* cReloadTypeUnsupportedFormat = "'%s' is an Opus stream, which needs the Stream Sound Extension";

inline constexpr const char* cReloadedSummaryFormat = "%u Track%s, %u channel%s, %u Hz";

inline constexpr const char* cReloadLayoutChanged = "Tracks were rebuilt for the new channel count, keeping their mix settings";
inline constexpr const char* cReloadTracksReplaced = "Tracks were read from the stream file, replacing their previous settings";
inline constexpr const char* cReloadTypeChangedFormat = "Stream Type changed to %s";
inline constexpr const char* cReloadLoopChanged = "Loop points updated from the file";
inline constexpr const char* cReloadSharedFormat = "Also updated %u Stream Sound%s using the same file";

inline constexpr const char* cReplaceOutsideArchiveFolderFormat = "'%s' is outside the archive's folder, so the game won't find it";

inline constexpr const char* cTypeAdts = "ADTS (AAC)";
inline constexpr const char* cTypeOpus = "Opus";

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

inline constexpr const char* cWaveFileNoChannels = "Wave File has no channels";
inline constexpr const char* cUnsupportedSoundType = "Unsupported Sound Type";
inline constexpr const char* cNothingToPlay = "Nothing to play";

}

namespace exporting
{

inline constexpr const char* cMidiNeedsSequence = "Only Sequence Sounds can be exported as MIDI";
inline constexpr const char* cSoundSetMidiNeedsSequences = "All Sounds in the Sound Set must be Sequence Sounds";
inline constexpr const char* cNothingToMerge = "No duplicate Wave Files to merge";
inline constexpr const char* cWaveNotExportable = "This Sound cannot be exported as WAV";
inline constexpr const char* cSelectionNotExportable = "Some of the selected Sounds cannot be exported";

}

namespace bank
{

inline constexpr const char* cFileLoadFailed = "Couldn't load the referenced Bank File";
inline constexpr const char* cNoFileAttached = "Bank has no Bank File attached";
inline constexpr const char* cWillHaveNoFile = "This Bank will have no Bank File";

inline constexpr const char* cFileLabel = "Bank File";

inline constexpr const char* cFileModeCreateNew = "Create New";
inline constexpr const char* cFileModeSelectExisting = "Select Existing";
inline constexpr const char* cFileModeNone = "None";

inline constexpr const char* cInstrumentPatchFailedFormat =
    "Instrument %u: Internal error (failed %s patch - waveArchiveId=%u)";

}

namespace sequence
{

inline constexpr const char* cCommandVersionFormat = "Command '{}' requires {} version >= 0x{:08X}";

inline constexpr const char* cNotCompiled = "Sequence File is not compiled";
inline constexpr const char* cInvalidStartLabel = "Invalid Start Label (is the Sequence File compiled?)";

inline constexpr const char* cGoToLabelHint = "Go to label with Bank info";

}

namespace validation
{

inline constexpr const char* cInvalidPlayer = "Invalid Player";
inline constexpr const char* cInvalidSoundType = "Invalid Sound Type";
inline constexpr const char* cInvalidWaveArchive = "Invalid Wave Archive";
inline constexpr const char* cInvalidWaveArchiveType = "Invalid Wave Archive Type";
inline constexpr const char* cInvalidSequenceFile = "Invalid Sequence File";
inline constexpr const char* cInvalidWaveFile = "Invalid Wave File";

}

namespace archive
{

inline constexpr const char* cCorruptFormat = "Your %s file is corrupted beyond repair :(\n%s";
inline constexpr const char* cVersionUnsupportedFormat = "%s version is not supported (0x%08X)";

inline constexpr const char* cWaveSoundDataLoadFailedFormat = "Couldn't load the referenced %s file";
inline constexpr const char* cWaveSoundDataInvalidFormat = "The referenced %s file is invalid";
inline constexpr const char* cWaveSoundDataPatchFailedFormat = "Internal error (failed %s patch)";

inline constexpr const char* cGroupFileFilterFormat = "Group File (*.%s)";

}

namespace import
{

inline constexpr const char* cInstrumentBundleInvalid = "Invalid instrument bundle file";
inline constexpr const char* cInstrumentBundleVersion = "Unsupported instrument bundle version";
inline constexpr const char* cInstrumentBundleCorrupt = "Instrument bundle file is truncated or corrupt";

inline constexpr const char* cBankBundleInvalid = "Invalid bank bundle file";
inline constexpr const char* cBankBundleVersion = "Unsupported bank bundle version";
inline constexpr const char* cBankBundleCorrupt = "Bank bundle file is truncated or corrupt";
inline constexpr const char* cBankBundleTruncated = "Bank bundle file is truncated";
inline constexpr const char* cBankBundleNoInstruments = "Bank bundle has no instruments";

inline constexpr const char* cWaveReadFailed = "Couldn't read the WAV file";
inline constexpr const char* cWaveLoadFailed = "Couldn't load the WAV file";
inline constexpr const char* cNativeDeviceUnavailable = "Couldn't access the native file device";

inline constexpr const char* cReimportWriteFailed = "Couldn't write a temporary WAV file to reimport";
inline constexpr const char* cReimportDecodeFailed = "Couldn't decode this Wave File to reimport";

inline constexpr const char* cBundledWavesFailedFormat = "Couldn't read %u bundled Wave File%s";

}

namespace item
{

inline constexpr const char* cModifiedTooltip = "Edited since the archive was last saved";

}

namespace popup
{

inline constexpr const char* cTitleErrorsOpening = "Errors while opening";
inline constexpr const char* cTitleWarningsOpening = "Warnings while opening";
inline constexpr const char* cTitleSkippedSaving = "Skipped while saving";

inline constexpr const char* cHeadingErrors = "The following Items contain errors that must be fixed before saving";
inline constexpr const char* cHeadingWarnings = "The following Items opened with warnings";
inline constexpr const char* cHeadingSkipped = "The following Items were skipped and not saved";

inline constexpr const char* cBackupReminder = "REMEMBER TO DO A BACKUP";

inline constexpr const char* cDetailMarker = "(?)";
inline constexpr const char* cCountSeparator = "|";
inline constexpr const char* cConfirm = "OK";

inline constexpr const char* cGroupErrors = "Errors";
inline constexpr const char* cGroupWarnings = "Warnings";

inline constexpr const char* cSeverityCountFormat = "%s %zu %s";
inline constexpr const char* cItemSingular = "Item";
inline constexpr const char* cItemPlural = "Items";

}

}
