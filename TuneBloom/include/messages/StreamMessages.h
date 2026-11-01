#pragma once

#include <messages/MessageType.h>

namespace messages
{

namespace stream
{

inline constexpr const char* cSampleRateFormat = "Sample rate: %u Hz";

inline constexpr const char* cFileMissingFormat = "'%s' could not be found next to the .%s file";
inline constexpr const char* cFileIncompleteFormat = "'%s' could not be loaded: the file is incomplete";
inline constexpr const char* cFileUnreadableFormat = "'%s' could not be read as a stream file";
inline constexpr const char* cFileNotStream = "Not a valid stream file";
inline constexpr const char* cOpenFilter = "Stream File (*.bfstm, *.bcstm, *.opus)";
inline constexpr const char* cStreamTypeFormat = "%s Stream";
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

inline constexpr const char* cTrackNoSound = "No Stream Sound Selected";
inline constexpr const char* cTrackNone = "No Tracks";
inline constexpr const char* cTrackSurroundPan = "Surround Pan";
inline constexpr const char* cTrackVolumeFormat = "Volume: %.3f";
inline constexpr const char* cTrackMainSendFormat = "Main Send: %.3f";
inline constexpr const char* cTrackFxSendFormat = "FX %c Send: %.3f";
inline constexpr const char* cTrackLpfFormat = "Low-Pass Filter: %.3f";
inline constexpr const char* cTrackBiquadValueFormat = "Biquad Value: %.3f";

inline constexpr const char* cReloadMenuLabel = "Reload Stream File";
inline constexpr const char* cReloadBatchDoneFormat = "Reloaded %u of %u stream files";

inline constexpr const char* cReplaceTooltip = "Replace Stream File";

inline constexpr const char* cReloadNoArchiveFolder = "Save the archive first: paths are relative";

inline constexpr const char* cPathLabel = "Path";

inline constexpr const char* cReloadNotFoundFormat = "'%s' is not in the archive's folder";
inline constexpr const char* cReloadUnreadableFormat = "'%s' could not be read as a %s or Opus stream file";
inline constexpr const char* cReloadNoChannelsFormat = "'%s' has no channels";
inline constexpr const char* cReloadTooManyChannelsFormat = "'%s' has %u channels, but streams can have up to %u";
inline constexpr const char* cReloadTypeUnsupportedFormat = "'%s' is an Opus stream, which needs the Stream Sound Extension";

inline constexpr const char* cReloadedSummaryFormat = "%s, %s, %u Hz";

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

inline constexpr const char* cPrefetchLabel  = "Prefetch";
inline constexpr const char* cTypeLabel      = "Stream Type";
inline constexpr const char* cLoopStartLabel = "Loop Start";
inline constexpr const char* cLoopEndLabel   = "Loop End";

inline constexpr const char* cTrackBypass           = "Bypass";
inline constexpr const char* cTrackBiquadValue      = "Value";
inline constexpr const char* cTrackMenuTip          = "Track Actions";
inline constexpr const char* cTrackMix              = "Mix";
inline constexpr const char* cTrackLaneNoPreview    = "No Waveform Preview";
inline constexpr const char* cTrackChannelsFormat   = "%u ch";
inline constexpr const char* cTrackNameFormat       = "Track %u";

inline constexpr const char* cRegionNone            = "No Regions";
inline constexpr const char* cRegionRangeFormat     = "%u to %u";
inline constexpr const char* cRegionAddTip          = "Add Region";
inline constexpr const char* cRegionMenuTip         = "Region Actions";
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
inline constexpr const char* cRegionsPastEndOneFormat   = "%u Region past the new end will be removed";
inline constexpr const char* cRegionsPastEndFormat      = "%u Regions past the new end will be removed";
inline constexpr const char* cRegionsReplacedTitle      = "Regions replaced";
inline constexpr const char* cRegionsReplacedFormat     = "Replaced by the %u in Track 0's new Wave File";
inline constexpr const char* cSelectedNotStream = "Selected Sound Is Not a Stream";

}

namespace opus
{

inline constexpr const char* cNotOpusStream = "Not a valid OpusStream file (missing info/data chunk)";
inline constexpr const char* cChannelCountUnsupported = "The OpusStream channel layout is not supported";
inline constexpr const char* cSampleRateUnsupported = "The OpusStream sample rate is not supported";
inline constexpr const char* cAudioDataDamaged = "The Opus audio data is damaged";
inline constexpr const char* cNoAudibleSamples = "The OpusStream has no audible samples";
inline constexpr const char* cDecoderCreateFailed = "The Opus decoder could not be created";
inline constexpr const char* cDecodeFailed = "The Opus data could not be decoded";

inline constexpr const char* cEncodeFailedFormat = "The Opus stream could not be encoded\n%s";
inline constexpr const char* cEncodeChannelLimit = "The Tracks must have 1 to 255 channels in total";
inline constexpr const char* cEncodeSampleRate = "The sample rate must be 8000, 12000, 16000, 24000 or 48000 Hz";
inline constexpr const char* cEncodeDecodeFailed = "A Track's Wave File could not be decoded";
inline constexpr const char* cEncoderCreateFailed = "The Opus encoder could not be created";
inline constexpr const char* cEncodeError = "The encoder returned an error";

}

}
