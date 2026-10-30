#pragma once

namespace messages
{

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
inline constexpr const char* cFileTypeWav              = "WAV, 16-bit";
inline constexpr const char* cLoopCount                = "Loop Count";
inline constexpr const char* cLoopCountDetail          = "Times the loop plays before the fade";
inline constexpr const char* cFadeOut                  = "Fade Out";
inline constexpr const char* cFadeOutFormat            = "%.1f s";
inline constexpr const char* cNoEndLengthDetail        = "Used when a sequence has no loop or end";
inline constexpr const char* cTimeFormat               = "%u:%02u";
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

}
