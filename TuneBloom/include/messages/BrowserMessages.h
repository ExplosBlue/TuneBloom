#pragma once

namespace messages
{

namespace browser
{

inline constexpr const char* cTitleFormat  = "Select %s";
inline constexpr const char* cAutoPlay     = "Auto Play";
inline constexpr const char* cGroupPrefix  = "Group by Name Prefix";
inline constexpr const char* cExtensions   = "File Extensions";
inline constexpr const char* cFileTypes    = "File Types";
inline constexpr const char* cCount        = "Count";
inline constexpr const char* cSelect       = "Select";
inline constexpr const char* cNoMatch      = "No items match the search";
inline constexpr const char* cNoSelection  = "No Wave File selected";
inline constexpr const char* cBrowseTip    = "Click to choose from a list";
inline constexpr const char* cSamples      = "Samples";
inline constexpr const char* cUsedBy       = "Used By";

}

namespace files
{

inline constexpr const char* cAddFolder        = "Add Folder";
inline constexpr const char* cAddFile          = "Add File";
inline constexpr const char* cAddTip           = "Add Folder or File";
inline constexpr const char* cChooseFolderTitle = "Choose a Folder to Browse";
inline constexpr const char* cChooseFilesTitle  = "Choose Files to Browse";
inline constexpr const char* cRescan            = "Scan Again";
inline constexpr const char* cMoveUp            = "Move Up";
inline constexpr const char* cMoveDown          = "Move Down";
inline constexpr const char* cSearchHint        = "Search files";
inline constexpr const char* cSearchInFolder    = "Search in Folder";
inline constexpr const char* cSearchAllFolders  = "Search all folders";

inline constexpr const char* cNewSection       = "New Section";
inline constexpr const char* cSection          = "Section";
inline constexpr const char* cMoveToSection    = "Move to Section";
inline constexpr const char* cNoSection        = "No Section";
inline constexpr const char* cRemoveSection    = "Remove Section";
inline constexpr const char* cFold             = "Fold";
inline constexpr const char* cUnfold           = "Unfold";
inline constexpr const char* cFoldAll          = "Fold All Sections";
inline constexpr const char* cUnfoldAll        = "Unfold All Sections";
inline constexpr const char* cAddToBrowser     = "Add to Browser";
inline constexpr const char* cRemoveFromBrowser = "Remove from Browser";

inline constexpr const char* cDropAddTip = "Drop a folder or file here to add it";

inline constexpr const char* cRemovedTitle        = "Removed from Browser";
inline constexpr const char* cSectionRemovedTitle = "Removed section";
inline constexpr const char* cAddedOneTitle       = "Added 1 item to the Browser";
inline constexpr const char* cAddedTitleFormat    = "Added %s items to the Browser";
inline constexpr const char* cNoAudio    = "No sound files in this folder";
inline constexpr const char* cNoMatch     = "No files match the search";
inline constexpr const char* cRootMissing = "This folder no longer exists";
inline constexpr const char* cWaiting     = "Waiting to scan";

inline constexpr const char* cKindResources = "Resources";

inline constexpr const char* cKindFilterFormat = "%s (%s)";
inline constexpr const char* cColumnTooNarrow  = "The panel is too narrow for this column";

inline constexpr const char* cIndexingFormat   = "Scanning... %s files";
inline constexpr const char* cReadingFormat    = "Reading %s of %s files";
inline constexpr const char* cSummaryFormat    = "%s files, %s";
inline constexpr const char* cSummaryOneFormat = "1 file, %s";
inline constexpr const char* cShownFormat      = "%s of %s files";
inline constexpr const char* cFileCountOne     = "1 file";
inline constexpr const char* cSoundCountFormat = "%s Sounds";
inline constexpr const char* cSoundCountOne    = "1 Sound";

inline constexpr const char* cCopyName = "Copy Name";

inline constexpr const char* cSelectedFormat        = "%s files selected, %s";
inline constexpr const char* cExportNeedsAudio      = "Select a wave, stream or resource file";
inline constexpr const char* cFormatAudio           = "audio";
inline constexpr const char* cSkippedFilesFormat    = "Skipped %s files";
inline constexpr const char* cSkippedFileFormat     = "Skipped %s";
inline constexpr const char* cSkippedFilesDetail    = "The Browser exports wave, stream and resource files";

inline constexpr const char* cFormatVersionFormat = "%s %s";
inline constexpr const char* cCompressed          = "zstd compressed";
inline constexpr const char* cAssetCountFormat    = "%s Audio Assets";
inline constexpr const char* cAssetCountOne       = "1 Audio Asset";
inline constexpr const char* cStreamStart         = "Start of a stream";
inline constexpr const char* cSoundInFileFormat   = "%s in %s";

inline constexpr const char* cPreviewFailedTitle     = "The file could not be played";
inline constexpr const char* cPreviewUndecodableText = "The sound data could not be decoded";

}

}
