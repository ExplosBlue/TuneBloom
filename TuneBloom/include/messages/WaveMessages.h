#pragma once

namespace messages
{

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
inline constexpr const char* cReimportDecodeFailed = "The audio could not be decoded for the reimport";

inline constexpr const char* cReimportMenuLabel = "Reimport";
inline constexpr const char* cImportWaveFileTitle = "Import Wave File";
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

inline constexpr const char* cTrackImportMenuLabel = "Add Tracks";
inline constexpr const char* cTrackImportTitle = "Import Tracks";
inline constexpr const char* cTrackImportStreamFactsFormat = "%s  -  %u Hz  -  %s";
inline constexpr const char* cTrackImportStreamFactsLoopFormat = "%s  -  %u Hz  -  %s  -  Loop";
inline constexpr const char* cTrackImportTooManyFilesFormat = "Only the first %u files were kept";
inline constexpr const char* cTrackImportChannelLimitFormat = "Streams can have up to %u channels";
inline constexpr const char* cTrackImportFailedFormat = "'%s' could not be imported as a Track";

inline constexpr const char* cTrackImportAddFiles          = "Add WAV";
inline constexpr const char* cTrackImportEditTip           = "Edit Import Settings";
inline constexpr const char* cTrackImportFollowsFormat     = "Follows Track %d";

inline constexpr const char* cTrackImportLayoutPerFileDetail = "Files with over 2 channels split into pairs";
inline constexpr const char* cTrackImportLayoutPairMonoDetail = "Two adjacent mono files become L and R";

inline constexpr const char* cWaveDiscardTitle = "Cancel import?";
inline constexpr const char* cWaveDiscardQuestion = "Cancel importing this file?";
inline constexpr const char* cWaveRateSourceFormat = "%u Hz (source)";
inline constexpr const char* cWaveSourceSummaryFormat = "%u Hz  -  %s  -  %s";
inline constexpr const char* cWaveSourceLabel = "Source";inline constexpr const char* cWaveNoPreview = "No waveform for this sample format";
inline constexpr const char* cWaveLoopReadoutFormat = "Loop duration: %s";
inline constexpr const char* cWavePreviewResume = "Resume";
inline constexpr const char* cWavePreviewPlay = "Play preview";
inline constexpr const char* cWaveFitAll = "Fit All";
inline constexpr const char* cWaveFitLoop = "Fit Loop";
inline constexpr const char* cWaveSampleRateTip = "Resampled to this rate on import";
inline constexpr const char* cWaveOpusRateTip = "Opus takes only 8, 12, 16, 24 or 48 kHz";
inline constexpr const char* cWaveOpusStreamTip = "Opus is used only for streams";
inline constexpr const char* cWaveOpusOnlyTip = "An Opus stream takes only Opus";
inline constexpr const char* cWaveBitrateOriginalFormat = "%u kbps (original)";
inline constexpr const char* cWaveBitrateTip = "For each channel. Stereo uses twice this rate";
inline constexpr const char* cWaveBitrateOpusTip = "Only Opus uses a bitrate";
inline constexpr const char* cWaveOldDataTitle = "Old Audio Data";
inline constexpr const char* cWaveOldDataWarning = "Kept data may not match the new audio";
inline constexpr const char* cWaveMarkersDetail = "Named positions in the old audio";
inline constexpr const char* cWaveMarkersKeepDetail = "Markers past the new end are trimmed";
inline constexpr const char* cWaveMarkerCountFormat = "%u Markers";
inline constexpr const char* cWaveMarkerCountOneFormat = "%u Marker";
inline constexpr const char* cWaveMarkersPastEndFormat = "%u Markers past the new end will be trimmed";
inline constexpr const char* cWaveMarkersPastEndOneFormat = "%u Marker past the new end will be trimmed";
inline constexpr const char* cWaveBeatGrid = "Beat Grid";
inline constexpr const char* cWaveBeatGridDetail = "Tempo, beats and cues of the old audio";
inline constexpr const char* cWaveBeatGridKeepDetail = "Only the rate, loop and length change";
inline constexpr const char* cWaveMidSide = "Mid/Side";
inline constexpr const char* cWaveMidSideDetail = "Channel 0 is Mid, channel 1 is Side";
inline constexpr const char* cWaveMidSideKeepDetail = "Store the new audio as Mid and Side";
inline constexpr const char* cWaveMidSideClearDetail = "Store the new audio as left and right";
inline constexpr const char* cWaveMidSideStereoReason = "Mid/Side needs two channels";
inline constexpr const char* cWaveRegionsDetail = "Ranges of the old stream, in samples";
inline constexpr const char* cWaveRegionsKeepDetail = "Regions past the new end are removed";
inline constexpr const char* cWaveRegionCountFormat = "%u Regions";
inline constexpr const char* cWaveRegionCountOneFormat = "%u Region";
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
inline constexpr const char* cWaveLoopMarkerTip = "Drag its marker on the waveform to move it";
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

inline constexpr const char* cTrackImportRemoveTip = "Remove from Import";
inline constexpr const char* cTrackImportBitsFloatFormat = "%u-bit float";
inline constexpr const char* cTrackImportBitsFormat = "%u-bit";
inline constexpr const char* cTrackImportTrackRangeFormat = "%u-%u";
inline constexpr const char* cTrackImportTrackSideFormat = "%u (%s)";
inline constexpr const char* cTrackImportLayout = "Layout";
inline constexpr const char* cTrackImportLayoutPerFile = "One Track per file";
inline constexpr const char* cTrackImportLayoutPairMono = "Pair mono files into stereo Tracks";
inline constexpr const char* cTrackImportLayoutPerChannel = "One mono Track per channel";

inline constexpr const char* cInstrumentReplaceTitle   = "Replace Instrument";
inline constexpr const char* cInstrumentImportTitle    = "Import Instrument";
inline constexpr const char* cInstrumentReplacingFormat = "Replacing: Program %d";
inline constexpr const char* cWavesToImportFormat      = "Waves to import: %u";

inline constexpr const char* cWaveImportFailed        = "The WAV file could not be imported";
inline constexpr const char* cWaveDropNeedsRegion     = "Drop a WAV onto a velocity region to import it";
inline constexpr const char* cWaveRegionDrop          = "Import onto velocity region";

}

namespace wavefile
{

inline constexpr const char* cPcm8Detail       = "Uncompressed, half the size of 16-bit";
inline constexpr const char* cPcm16Detail      = "Uncompressed, keeps each sample exactly";
inline constexpr const char* cDspAdpcmDetail   = "Compressed to about 4.6 bits per sample";
inline constexpr const char* cOpusDetail       = "Compressed to the selected bitrate";
inline constexpr const char* cSamplesFormat    = "%u samples";
inline constexpr const char* cTimeFormat       = "%u:%06.3f";

inline constexpr const char* cWaveform         = "Waveform";
inline constexpr const char* cTooLong          = "Too long to show as a waveform";
inline constexpr const char* cNoData           = "No sample data";

inline constexpr const char* cEncodingPcm8 = "PCM 8-bit";
inline constexpr const char* cEncodingPcm16 = "PCM 16-bit";
inline constexpr const char* cEncodingDspAdpcm = "DSP-ADPCM";

inline constexpr const char* cInvalidEncoding = "The encoding is invalid";
inline constexpr const char* cInvalidLoopRange = "The loop start or end is invalid";
inline constexpr const char* cNoDataToWrite = "No data to write";
inline constexpr const char* cOpenFilter = "Wave File (*.bfwav, *.bcwav)";
inline constexpr const char* cFileUnreadableFormat = "'%s' could not be read as a wave file";
inline constexpr const char* cWriteFailedFormat = "The wave file '%s' could not be written";
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
inline constexpr const char* cWavOnlyPcm8Pcm16 = "Only 8-bit and 16-bit PCM WAV files are supported";
inline constexpr const char* cWavNoData = "The 'data' block could not be found";
inline constexpr const char* cWavInvalidInfo = "The .wav format is invalid";

}

}
