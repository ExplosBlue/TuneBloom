#pragma once

namespace messages
{

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

namespace source
{

inline constexpr const char* cSelectExisting = "Select Existing";

}

namespace reference
{

inline constexpr const char* cNoSequenceFile = "No Sequence File attached";
inline constexpr const char* cNoWaveFile = "No Wave File attached";
inline constexpr const char* cNoBank = "No Bank attached";
inline constexpr const char* cNothingAttached = "Nothing attached";
inline constexpr const char* cWaveArchiveNotExplicit = "Automatic Wave Archives cannot be opened";

}

namespace archive
{

inline constexpr const char* cCorruptFormat = "Your %s file is corrupted beyond repair :(\n%s";

inline constexpr const char* cWaveSoundDataLoadFailedFormat = "The %s file could not be loaded";
inline constexpr const char* cWaveSoundDataInvalidFormat = "The %s file is invalid";
inline constexpr const char* cWaveFileLinkFailed = "The Wave File could not be linked";

inline constexpr const char* cGroupFileFilterFormat = "Group File (*.%s)";
inline constexpr const char* cGroupPackFilter       = "Group File in SARC (*.sarc, *.aras)";

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

inline constexpr const char* cNotArchive = "Not a valid sound archive";
inline constexpr const char* cBfsarVersionUnsupportedFormat = "Sound archive version is not supported (0x%08X)";
inline constexpr const char* cOpenFailed = "The selected file could not be opened";

inline constexpr const char* cOpenProgressFormat     = "Opening %s";
inline constexpr const char* cOpenStageFile          = "Reading the file";
inline constexpr const char* cOpenStageNames         = "Names";
inline constexpr const char* cCmpbinDecompressFailed = "The cmpbin file could not be decompressed";
inline constexpr const char* cBeaDamaged = "The BEA file is damaged";
inline constexpr const char* cBeaNoSoundArchive = "The BEA file holds no sound archive";
inline constexpr const char* cBeaFilter = "Sound Archive in BEA (*.bea)";
inline constexpr const char* cBlockNotFoundFormat = "%s: the %s block could not be found";

inline constexpr const char* cUntitledFileNameFormat = "Untitled.%s";
inline constexpr const char* cNewFileResult          = "Creates";
inline constexpr const char* cOpenFilter = "Sound Archive (*.bfsar, *.bcsar, *.cmpbin, *.bea)";
inline constexpr const char* cOpenFilterAll = "All Supported Files (*.bfsar, *.bcsar, *.cmpbin, *.bea, *.bars, *.barslist, *.bwav, *.bfstm, *.bcstm, *.bfwav, *.bcwav, *.zs, *.pack)";
inline constexpr const char* cSoundArchiveTypeFormat = "%s Sound Archive";

inline constexpr const char* cSaveOpenFailed = "The output file could not be opened";
inline constexpr const char* cSaveWriteFailed = "The archive could not be written. The file on disk was not changed";
inline constexpr const char* cCmpbinTempReadFailed = "The temporary archive could not be read";
inline constexpr const char* cCmpbinCompressFailed = "The cmpbin output could not be compressed";
inline constexpr const char* cCmpbinOpenFailed = "The cmpbin output could not be opened";
inline constexpr const char* cBeaPackFailed = "Couldn't pack the archive into the BEA file";
inline constexpr const char* cBeaNoSource = "Only an archive opened from a BEA file can be saved as BEA";

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

inline constexpr const char* cPlatform = "Platform";

inline constexpr const char* cLimitsTitle         = "Playback Limits";
inline constexpr const char* cFileStatisticsTitle = "File Statistics";

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

inline constexpr const char* cInfoWindowTitle       = "Info";
inline constexpr const char* cPropertiesWindowTitle = "Properties";

inline constexpr const char* cViewUnavailable = "Only sound archives have a project";

inline constexpr const char* cStringTable        = "String Table";
inline constexpr const char* cVersionRangeFormat = "Valid range: %s";
inline constexpr const char* cVersionEditDetail  = "Drag to adjust, double-click to type";

}

namespace item
{

inline constexpr const char* cModifiedTooltip = "Edited since the last save";

inline constexpr const char* cTypeSoundSet     = "Sound Set";

inline constexpr const char* cSearchHint     = "Search by name or ID...";
inline constexpr const char* cListMenu       = "List Options";
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
inline constexpr const char* cStatsShareFormat   = "%.1f%%";
inline constexpr const char* cStatsShareDetailFormat = "%.2f%%";
inline constexpr const char* cStatsShare         = "Share";
inline constexpr const char* cCountOneFormat = "1 %s";
inline constexpr const char* cCountAllFormat = "%s %ss";
inline constexpr const char* cNoItemsFormat  = "No %ss";

inline constexpr const char* cColumnId          = "ID";
inline constexpr const char* cColumnRate        = "Rate";
inline constexpr const char* cLoopStartTooltipFormat = "Loops from %u:%06.3f";
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

inline constexpr const char* cListSharedSoundsFormat = "The Sound list could not be changed, since the Sound Sets '%s' and '%s' share Sounds";
inline constexpr const char* cListInvalidRangeFormat = "The Sound list could not be changed, since the ID range of the Sound Set '%s' is invalid";
inline constexpr const char* cListSplitsSetFormat    = "The Sound list could not be changed without splitting the Sound Set '%s'";
inline constexpr const char* cMoveWrongTypeFormat    = "'%s' could not be moved into the %s '%s'";

inline constexpr const char* cNoneSelected = "No Set Selected";

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

}
