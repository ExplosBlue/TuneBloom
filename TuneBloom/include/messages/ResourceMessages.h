#pragma once

namespace messages
{

namespace resource
{

inline constexpr const char* cResourceTypeFormat = "%s Audio Resource";
inline constexpr const char* cWaveTypeFormat = "%s Sound";
inline constexpr const char* cBarsDamaged = "The BARS file is damaged";
inline constexpr const char* cBarsVersionUnsupported = "This BARS version is not supported";
inline constexpr const char* cSarcDamaged = "The SARC file is damaged";
inline constexpr const char* cSarcFormat = "BARS (SARC)";
inline constexpr const char* cOtherFiles = "Other Files";
inline constexpr const char* cOtherFilesDetail = "Not audio. Assets cannot be renamed or added";
inline constexpr const char* cBwavDamaged = "The BWAV file is damaged";
inline constexpr const char* cBwavVersionUnsupported = "This BWAV version is not supported";
inline constexpr const char* cBwavCodecUnsupported = "This BWAV codec is not supported";
inline constexpr const char* cBfstmDamaged = "The BFSTM file is damaged";
inline constexpr const char* cStreamChannelLimit = "A stream can have up to 16 channels";
inline constexpr const char* cAmtaDamaged = "The metadata of an Audio Asset is damaged";
inline constexpr const char* cNameHashCollision = "Two Audio Asset names have the same hash";
inline constexpr const char* cNameInvalidDetail =
    "A name cannot start or end with a space,\nor use / \\ : * ? \" < > |";

inline constexpr const char* cNoAudioFile = "This Audio Asset has no audio file";
inline constexpr const char* cFormatNotPlayable = "This audio format can't be played";
inline constexpr const char* cAudioDamaged = "The audio file is damaged";
inline constexpr const char* cStreamNotLoaded = "The stream file is missing or not loaded";
inline constexpr const char* cStreamFileMissing = "The stream file is missing";
inline constexpr const char* cStreamMismatched = "The stream file does not match this asset";
inline constexpr const char* cStreamsNotSaved = "The archive was saved, but a stream file was not. Save again to retry";
inline constexpr const char* cBwavNeedsAudio = "The BWAV file has no audio";

inline constexpr const char* cNotResource = "Not a valid BARS or BWAV file";
inline constexpr const char* cOpenFilter = "Audio Resource (*.bars, *.bwav, *.zs, *.pack)";
inline constexpr const char* cPackWriteFailed = "The pack file could not be written";
inline constexpr const char* cDecompressFailed = "Couldn't decompress the zstd file";
inline constexpr const char* cZstdNotResource = "The zstd file holds no BARS or BWAV file";
inline constexpr const char* cCompressFailed = "Couldn't compress the file";
inline constexpr const char* cWriteFailed = "Couldn't write the file";
inline constexpr const char* cStreamWriteFailed = "Couldn't write a stream file to the Stream folder";
inline constexpr const char* cEncodeFailed = "Couldn't encode the audio";
inline constexpr const char* cRenameFailed = "Couldn't rename the metadata of this Audio Asset";
inline constexpr const char* cReplaceUnsupported = "Only BWAV, BFWAV and BFSTP can be replaced";
inline constexpr const char* cAddUnsupported = "Audio Assets cannot be added to this file";
inline constexpr const char* cPrefetchBuildFailed = "Couldn't build the start of the stream from the new file";
inline constexpr const char* cLoopUnsupported = "The loop of this format cannot be edited";
inline constexpr const char* cImportKindMismatch = "The file must have the same format as the current audio";
inline constexpr const char* cImportUnsupported = "Only BWAV, BFWAV and BFSTM files can be imported";
inline constexpr const char* cImportNeedsCompleteBwav = "The BWAV must contain the whole sound, not only its start";
inline constexpr const char* cImportReadFailed = "Couldn't read the file";

inline constexpr const char* cImportAudioFile = "Import Audio File";
inline constexpr const char* cExportAudioFile = "Export Audio File";
inline constexpr const char* cSelectOneAudioAsset = "Select one Audio Asset";
inline constexpr const char* cExportMixedFormats = "The selected assets must have one format";

inline constexpr const char* cStoredInArchive = "Stored";
inline constexpr const char* cLoopEndFixed = "The loop always ends at the last sample";
inline constexpr const char* cStreamState = "State";
inline constexpr const char* cStreamLoaded = "Loaded from the Stream folder";
inline constexpr const char* cStreamMissing = "Not in the Stream folder";
inline constexpr const char* cStreamDamaged = "The file in the Stream folder is damaged";
inline constexpr const char* cStreamStateMismatched = "The file in the Stream folder doesn't match";
inline constexpr const char* cStreamNeedsArchive = "Only the start of a stream is in this file";
inline constexpr const char* cStreamPending = "Written to the Stream folder on save";
inline constexpr const char* cNewStreamDetail = "The full sound is saved in the Stream folder";
inline constexpr const char* cMetadataTitle = "Metadata";
inline constexpr const char* cMetadataVersionFormat = "AMTA %s";
inline constexpr const char* cMarkers = "Markers";
inline constexpr const char* cTags = "Tags";
inline constexpr const char* cAddMarker = "Add Marker";
inline constexpr const char* cAddKey = "Add Key";
inline constexpr const char* cNoMarkers = "No Markers";
inline constexpr const char* cMarkersLayoutUnknown = "The layout of this metadata is not supported";

inline constexpr const char* cAudioFormat = "Audio Format";
inline constexpr const char* cAudioFormatDetail = "Used by each Audio Asset that is added";
inline constexpr const char* cAudioFormatFixedDetail = "BARS 1.2 holds BWAV audio";
inline constexpr const char* cAudioFormatBfwavDetail = "As in Breath of the Wild, Mario Kart 8 Deluxe";
inline constexpr const char* cAudioFormatBwavDetail = "As in Super Mario Maker 2";
inline constexpr const char* cAudioFormatBfwavWavReason = "BFWAV needs 8 or 16-bit PCM, mono or stereo";
inline constexpr const char* cBarsVersion11Detail = "AMTA 4 metadata, BFWAV or BWAV audio";
inline constexpr const char* cBarsVersion12Detail = "AMTA 5 metadata, BWAV audio";
inline constexpr const char* cSarcMetaDetail = "BFWAV 1.2 audio, as in Super Mario Maker";
inline constexpr const char* cSarcNoMetaDetail = "BFWAV 1.1 audio, as in Mario Kart 8";

inline constexpr const char* cResourceVersionFormat = "%u.%u";
inline constexpr const char* cCompression = "Compression";
inline constexpr const char* cCompressionZstd = "zstd";
inline constexpr const char* cContentTitle = "Content";
inline constexpr const char* cStreamsDetail = "Sounds that play from the Stream folder";
inline constexpr const char* cStreamsLoadedFormat = "%u of %u in the Stream folder";

}

namespace barslist
{

inline constexpr const char* cFormatName = "BARS List";
inline constexpr const char* cEntry = "BARS File";
inline constexpr const char* cEntries = "BARS Files";
inline constexpr const char* cOpenFilter = "BARS List (*.barslist)";
inline constexpr const char* cNotBarsList = "The file is not a BARS List";
inline constexpr const char* cWriteFailed = "The BARS List could not be written";
inline constexpr const char* cNameInvalidDetail = "A name must end in .bars and cannot use / \\ : * ? \" < > |";
inline constexpr const char* cListName = "List Name";
inline constexpr const char* cListNameDetail = "Always the file name with no extension";
inline constexpr const char* cLocation = "Location";
inline constexpr const char* cFoundFormat = "%u of %u found";
inline constexpr const char* cNotFoundDetail = "Not in the sound folder or in a pack";
inline constexpr const char* cOpenBars = "Open the BARS file";
inline constexpr const char* cRemoveFromListFormat = "Remove %zu BARS Files from List";

}

}
