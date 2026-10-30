#pragma once

namespace messages
{

inline constexpr const char* cNotImplemented = "Not implemented";

namespace common
{

inline constexpr const char* cNotFound = "Not found";
inline constexpr const char* cRemoveFromList = "Remove from List";
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
inline constexpr const char* cAudioAsset = "Audio Asset";
inline constexpr const char* cWaveSoundSet = "Wave Sound Set";
inline constexpr const char* cWaveSound = "Wave Sound";
inline constexpr const char* cArchive = "Archive";
inline constexpr const char* cProject = "Project";
inline constexpr const char* cProjectInfo = "Project Info";
inline constexpr const char* cFile = "File";
inline constexpr const char* cFiles = "Files";
inline constexpr const char* cBrowser = "Browser";
inline constexpr const char* cFolder = "Folder";
inline constexpr const char* cGroupFiles = "Group Files";
inline constexpr const char* cBackup = "Backup";
inline constexpr const char* cBackups = "Backups";

inline constexpr const char* cName = "Name";
inline constexpr const char* cType = "Type";
inline constexpr const char* cFormat = "Format";
inline constexpr const char* cLength = "Length";
inline constexpr const char* cTimeTenthsFormat = "%u:%02u.%u";
inline constexpr const char* cSize = "Size";
inline constexpr const char* cSampleRate = "Sample Rate";
inline constexpr const char* cChannels = "Channels";
inline constexpr const char* cEncoding = "Encoding";
inline constexpr const char* cBitrate = "Bitrate";
inline constexpr const char* cLoop = "Loop";
inline constexpr const char* cLoopRangeFormat = "%s to %s";
inline constexpr const char* cLoopOn = "Loop: on";
inline constexpr const char* cLoopOff = "Loop: off";
inline constexpr const char* cStart = "Start";
inline constexpr const char* cEnd = "End";
inline constexpr const char* cRange = "Range";
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
inline constexpr const char* cUndo = "Undo";
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
inline constexpr const char* cFileUnreadable = "The file could not be read";

inline constexpr const char* cAdd = "Add";
inline constexpr const char* cCancel = "Cancel";
inline constexpr const char* cClose = "Close";
inline constexpr const char* cDelete = "Delete";
inline constexpr const char* cDuplicate = "Duplicate";
inline constexpr const char* cCompressedFilterFormat = "Compressed %s (*.%s.%s)";
inline constexpr const char* cCompressedFileTypeFormat = "%s Compressed File";
inline constexpr const char* cExport = "Export";
inline constexpr const char* cImport = "Import";
inline constexpr const char* cFind = "Find";
inline constexpr const char* cGoTo = "Go To";
inline constexpr const char* cHelp = "Help";
inline constexpr const char* cNewFile = "New File";
inline constexpr const char* cSoundArchive = "Sound Archive";
inline constexpr const char* cAudioResource = "Audio Resource";
inline constexpr const char* cFileType = "File Type";
inline constexpr const char* cExtension = "Extension";
inline constexpr const char* cPause = "Pause";
inline constexpr const char* cPlay = "Play";
inline constexpr const char* cStop = "Stop";
inline constexpr const char* cRename = "Rename";
inline constexpr const char* cReplace = "Replace";
inline constexpr const char* cReplaceAudio = "Replace Audio";
inline constexpr const char* cReset = "Reset";
inline constexpr const char* cRestore = "Restore";
inline constexpr const char* cMinimize = "Minimize";
inline constexpr const char* cSave = "Save";
inline constexpr const char* cSelectAll = "Select All";
inline constexpr const char* cPageUp = "Page Up";
inline constexpr const char* cPageDown = "Page Down";
inline constexpr const char* cWarning = "Warning";
inline constexpr const char* cOpen = "Open";
inline constexpr const char* cOpenFolder = "Open Folder";
inline constexpr const char* cArchives = "Archives";
inline constexpr const char* cStreams = "Streams";
inline constexpr const char* cWaves = "Waves";
inline constexpr const char* cWav = "WAV";
inline constexpr const char* cMove = "Move";
inline constexpr const char* cLoops = "Loops";
inline constexpr const char* cRegions = "Regions";
inline constexpr const char* cKeep = "Keep";
inline constexpr const char* cClear = "Clear";

inline constexpr const char* cCountOfFormat = "%s of %s";
inline constexpr const char* cDigitGroupSeparator = ",";
inline constexpr const char* cPairFormat = "%s, %s";
inline constexpr const char* cFileCountFormat = "%s files";
inline constexpr const char* cMillisecondsFormat = "%.0f ms";
inline constexpr const char* cMegabytesFormat = "%.1f MB";
inline constexpr const char* cSecondsFormat = "%.2f s";
inline constexpr const char* cPercentFormat = "%d%%";
inline constexpr const char* cPercentFloatFormat = "%.0f%%";
inline constexpr const char* cChannelCountFormat = "%u Channels";
inline constexpr const char* cChannelCountOneFormat = "%u Channel";
inline constexpr const char* cTrackCountFormat = "%u Tracks";
inline constexpr const char* cTrackCountOneFormat = "%u Track";

}

namespace panel
{

inline constexpr const char* cOutput   = "Output";
inline constexpr const char* cEnvelope = "Envelope";
inline constexpr const char* cSend     = "Send";

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

namespace notify
{

inline constexpr const char* cTitle       = "Notifications";
inline constexpr const char* cEmpty       = "No notifications in this session";

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

namespace format
{

inline constexpr const char* cLabelCafe = "Cafe Sound Archive";
inline constexpr const char* cLabelCtr  = "CTR Sound Archive";

inline constexpr const char* cSystemWiiU   = "Wii U";
inline constexpr const char* cSystem3ds    = "3DS";
inline constexpr const char* cSystemSwitch = "Switch";

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
inline constexpr const char* cKilobitsFormat = "%u kbps";
inline constexpr const char* cBpmFormat = "%g BPM";
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

}
