#pragma once

#include <string>
#include <vector>
#include <array>
#include <memory>
#include <unordered_set>
#include <unordered_map>
#include <map>
#include <regex>
#include "imgui.h"

class TextEditor
{
public:
	enum class PaletteIndex
	{
		Default,
		Keyword,
		Number,
		String,
		CharLiteral,
		Punctuation,
		Preprocessor,
		Identifier,
		KnownIdentifier,
		PreprocIdentifier,
		Comment,
		MultiLineComment,
		Background,
		Cursor,
		Selection,
		ErrorMarker,
		Breakpoint,
		LineNumber,
		CurrentLineFill,
		CurrentLineFillInactive,
		CurrentLineEdge,
		Max
	};

	enum class SelectionMode
	{
		Normal,
		Word,
		Line
	};

	enum class WhitespaceMode
	{
		None,
		Selection,
		All
	};

	enum class Request
	{
		None,
		GoToLine,
		Symbols,
		ContextMenu
	};

	struct Breakpoint
	{
		int mLine;
		bool mEnabled;
		std::string mCondition;

		Breakpoint()
			: mLine(-1)
			, mEnabled(false)
		{}
	};

	// Represents a character coordinate from the user's point of view,
	// i. e. consider an uniform grid (assuming fixed-width font) on the
	// screen as it is rendered, and each cell has its own coordinate, starting from 0.
	// Tabs are counted as [1..mTabSize] count empty spaces, depending on
	// how many space is necessary to reach the next tab stop.
	// For example, coordinate (1, 5) represents the character 'B' in a line "\tABC", when mTabSize = 4,
	// because it is rendered as "    ABC" on the screen.
	struct Coordinates
	{
		int mLine, mColumn;
		Coordinates() : mLine(0), mColumn(0) {}
		Coordinates(int aLine, int aColumn) : mLine(aLine), mColumn(aColumn)
		{
			assert(aLine >= 0);
			assert(aColumn >= 0);
		}
		static Coordinates Invalid() { static Coordinates invalid(-1, -1); return invalid; }

		bool operator ==(const Coordinates& o) const
		{
			return
				mLine == o.mLine &&
				mColumn == o.mColumn;
		}

		bool operator !=(const Coordinates& o) const
		{
			return
				mLine != o.mLine ||
				mColumn != o.mColumn;
		}

		bool operator <(const Coordinates& o) const
		{
			if (mLine != o.mLine)
				return mLine < o.mLine;
			return mColumn < o.mColumn;
		}

		bool operator >(const Coordinates& o) const
		{
			if (mLine != o.mLine)
				return mLine > o.mLine;
			return mColumn > o.mColumn;
		}

		bool operator <=(const Coordinates& o) const
		{
			if (mLine != o.mLine)
				return mLine < o.mLine;
			return mColumn <= o.mColumn;
		}

		bool operator >=(const Coordinates& o) const
		{
			if (mLine != o.mLine)
				return mLine > o.mLine;
			return mColumn >= o.mColumn;
		}
	};

	struct Identifier
	{
		Coordinates mLocation;
		std::string mDeclaration;
	};

	struct Marker
	{
		// Marker()
		// 	: mDetail()
		// 	, mColor(IM_COL32(0, 0, 0.5f * 255, 255))
		// 	, mIsError(true)
		// 	, mShowDetail(true)
		// {
		// }

		std::string mDetail;
		ImU32 mColor;
		bool mIsError;
		bool mShowDetail;
	};

	struct Completion
	{
		std::string mLabel;
		std::string mDetail;
		std::string mDocumentation;
		std::string mIcon;
		PaletteIndex mColorIndex = PaletteIndex::Identifier;
	};

	struct Signature
	{
		std::string mName;
		std::vector<std::string> mParameters;
		int mActiveParameter = -1;
		std::string mDocumentation;
	};

	struct Symbol
	{
		std::string mName;
		int mLine;
		bool mGlobal;
	};

	typedef std::string String;
	typedef std::unordered_map<std::string, Identifier> Identifiers;
	typedef std::unordered_set<std::string> Keywords;
	typedef std::map<int, Marker> Markers;
	typedef std::map<int, std::string> Annotations;
	typedef std::unordered_set<int> Breakpoints;
	typedef std::array<ImU32, (unsigned)PaletteIndex::Max> Palette;
	typedef uint8_t Char;

	struct Glyph
	{
		Char mChar;
		PaletteIndex mColorIndex = PaletteIndex::Default;
		bool mComment : 1;
		bool mMultiLineComment : 1;
		bool mPreprocessor : 1;

		Glyph(Char aChar, PaletteIndex aColorIndex) : mChar(aChar), mColorIndex(aColorIndex),
			mComment(false), mMultiLineComment(false), mPreprocessor(false) {}
	};

	typedef std::vector<Glyph> Line;
	typedef std::vector<Line> Lines;

	struct LanguageDefinition
	{
		typedef std::pair<std::string, PaletteIndex> TokenRegexString;
		typedef std::vector<TokenRegexString> TokenRegexStrings;
		typedef bool(*TokenizeCallback)(const char * in_begin, const char * in_end, const char *& out_begin, const char *& out_end, PaletteIndex & paletteIndex);
		typedef bool(*DescribeCallback)(const std::string & aWord, std::string & aOutText);
		typedef bool(*FoldHeaderCallback)(const char * in_begin, const char * in_end);
		typedef bool(*SymbolCallback)(const char * in_begin, const char * in_end, const char *& out_begin, const char *& out_end);
		typedef void(*CompleteCallback)(const std::string & aLineBefore, const std::string & aPrefix, std::vector<Completion> & aOut, bool & aOutAddSymbols);
		typedef bool(*SignatureCallback)(const std::string & aLineBefore, Signature & aOut);

		std::string mName;
		Keywords mKeywords;
		Identifiers mIdentifiers;
		Identifiers mPreprocIdentifiers;
		std::string mCommentStart, mCommentEnd, mSingleLineComment;
		char mPreprocChar;
		bool mAutoIndentation;

		TokenizeCallback mTokenize;
		DescribeCallback mDescribe = nullptr;
		FoldHeaderCallback mIsFoldHeader = nullptr;
		SymbolCallback mFindSymbol = nullptr;
		CompleteCallback mComplete = nullptr;
		SignatureCallback mSignature = nullptr;
		std::string mHeaderActionIcon;
		std::string mSymbolIcon;
		std::string mFoldOpenIcon;
		std::string mFoldClosedIcon;

		TokenRegexStrings mTokenRegexStrings;

		bool mCaseSensitive;

		LanguageDefinition()
			: mPreprocChar('#'), mAutoIndentation(true), mTokenize(nullptr), mCaseSensitive(true)
		{
		}

		static const LanguageDefinition& CPlusPlus();
		static const LanguageDefinition& HLSL();
		static const LanguageDefinition& GLSL();
		static const LanguageDefinition& C();
		static const LanguageDefinition& SQL();
		static const LanguageDefinition& AngelScript();
		static const LanguageDefinition& Lua();
	};

	TextEditor();
	~TextEditor();

	void SetLanguageDefinition(const LanguageDefinition& aLanguageDef);
	const LanguageDefinition& GetLanguageDefinition() const { return mLanguageDefinition; }

	const Palette& GetPalette() const { return mPaletteBase; }
	void SetPalette(const Palette& aValue);

	void SetMarkers(const Markers& aMarkers) { mMarkers = aMarkers; }
	void SetAnnotations(const Annotations& aAnnotations) { mAnnotations = aAnnotations; }

	void FoldAll();
	void UnfoldAll();
	bool IsAnyFolded() const;

	int ConsumeHeaderAction() { int line = mHeaderActionLine; mHeaderActionLine = -1; return line; }
	int GetCursorBlockHeader() const { return FindBlockHeader(mState.mCursorPosition.mLine); }
	std::string GetLineText(int aLine) const;
	void SetBreakpoints(const Breakpoints& aMarkers) { mBreakpoints = aMarkers; }

	void Render(const char* aTitle, const ImVec2& aSize = ImVec2(), bool aBorder = false);
	void SetText(const std::string& aText);
	std::string GetText() const;

	void SetTextLines(const std::vector<std::string>& aLines);
	std::vector<std::string> GetTextLines() const;

	std::string GetSelectedText() const;
	std::string GetCurrentLineText()const;

	int GetTotalLines() const { return (int)mLines.size(); }
	bool IsOverwrite() const { return mOverwrite; }

	void SetReadOnly(bool aValue);
	bool IsReadOnly() const { return mReadOnly; }
	bool IsTextChanged() const { return mTextChanged; }
	bool IsCursorPositionChanged() const { return mCursorPositionChanged; }

	bool IsColorizerEnabled() const { return mColorizerEnabled; }
	void SetColorizerEnable(bool aValue);

	Coordinates GetCursorPosition() const { return GetActualCursorCoordinates(); }
	void SetCursorPosition(const Coordinates& aPosition);

	inline void SetHandleMouseInputs    (bool aValue){ mHandleMouseInputs    = aValue;}
	inline bool IsHandleMouseInputsEnabled() const { return mHandleMouseInputs; }

	inline void SetHandleKeyboardInputs (bool aValue){ mHandleKeyboardInputs = aValue;}
	inline bool IsHandleKeyboardInputsEnabled() const { return mHandleKeyboardInputs; }

	inline void SetImGuiChildIgnored    (bool aValue){ mIgnoreImGuiChild     = aValue;}
	inline bool IsImGuiChildIgnored() const { return mIgnoreImGuiChild; }

	inline void SetShowWhitespaces(bool aValue) { mWhitespaceMode = aValue ? WhitespaceMode::All : WhitespaceMode::None; }
	inline bool IsShowingWhitespaces() const { return mWhitespaceMode == WhitespaceMode::All; }
	inline void SetWhitespaceMode(WhitespaceMode aValue) { mWhitespaceMode = aValue; }

	inline void SetAutoComplete(bool aValue) { mAutoComplete = aValue; }
	inline void SetMinimap(bool aValue) { mMinimapEnabled = aValue; }
	inline float GetMinimapWidth() const { return mMinimapWidth; }
	inline void SetParameterHints(bool aValue) { mParameterHints = aValue; }

	Request ConsumeRequest() { Request request = mRequest; mRequest = Request::None; return request; }
	void RequestFocus() { mFocusRequest = true; }

	void SetTabSize(int aValue);
	inline int GetTabSize() const { return mTabSize; }

	void InsertText(const std::string& aValue);
	void InsertText(const char* aValue);

	void MoveUp(int aAmount = 1, bool aSelect = false);
	void MoveDown(int aAmount = 1, bool aSelect = false);
	void MoveLeft(int aAmount = 1, bool aSelect = false, bool aWordMode = false);
	void MoveRight(int aAmount = 1, bool aSelect = false, bool aWordMode = false);
	void MoveTop(bool aSelect = false);
	void MoveBottom(bool aSelect = false);
	void MoveHome(bool aSelect = false);
	void MoveEnd(bool aSelect = false);

	void SetSelectionStart(const Coordinates& aPosition);
	void SetSelectionEnd(const Coordinates& aPosition);
	void SetSelection(const Coordinates& aStart, const Coordinates& aEnd, SelectionMode aMode = SelectionMode::Normal);
	void SelectWordUnderCursor();
	void SelectAll();
	bool HasSelection() const;

	void Copy();
	void Cut();
	void Paste();
	void Delete();

	bool CanUndo() const;
	bool CanRedo() const;
	void Undo(int aSteps = 1);
	void Redo(int aSteps = 1);

	static const Palette& GetDarkPalette();
	static const Palette& GetLightPalette();
	static const Palette& GetRetroBluePalette();

	void EnsureCursorVisible();
	void ScrollToLine(int aLine, bool aSmooth = false);

	void RebuildFolds();
	void RevealLine(int aLine);
	int LineToRow(int aLine) const;
	int RowToLine(int aRow) const;
	int GetRowCount() const;
	bool IsLineHidden(int aLine) const;
	bool IsFoldedHeader(int aLine) const;
	void ToggleFold(int aLine);
	int FindBlockHeader(int aLine) const;
	int FindNextHeader(int aLine) const;
	void MoveBlock(int aHeader, int aBefore);
	void MoveBlockStep(int aDirection);

	void FindNext();
	void FindPrevious();

	bool IsSearchActive() const { return mSearch.active; }
	void OpenSearch(bool aReplace);
	void CloseSearch();
	bool IsReplaceVisible() const { return mSearch.replace; }
	void SetReplaceVisible(bool aValue) { mSearch.replace = aValue; }
	const std::string& GetSearchQuery() const { return mSearch.query; }
	void SetSearchQuery(const std::string& aQuery);
	bool IsSearchCaseSensitive() const { return mSearch.caseSensitive; }
	bool IsSearchWholeWord() const { return mSearch.wholeWord; }
	void SetSearchOptions(bool aCaseSensitive, bool aWholeWord);
	int GetSearchMatchCount() const { return (int)mSearch.matches.size(); }
	int GetSearchCurrentMatch() const { return mSearch.currentMatch; }
	bool ConsumeSearchFocusRequest() { bool request = mSearch.focusRequested; mSearch.focusRequested = false; return request; }
	void ReplaceCurrent(const std::string& aText);
	void ReplaceAll(const std::string& aText);

	void GoToLine(int aLine, int aColumn = 0, bool aAddHistory = true);
	bool CanGoToDefinition(const Coordinates& aAt) const;
	bool GoToDefinition(const Coordinates& aAt);
	void NavigateBack();
	void NavigateForward();
	bool GoToErrorMarker(int aDirection);

	const std::vector<Symbol>& GetSymbols() const { return mSymbols; }
	int FindCursorSymbol(bool aGlobal) const;
	void GetSelectionSize(int& aOutLines, int& aOutChars) const;
	Coordinates GetContextMenuPosition() const { return mContextMenuPos; }
	bool GetFirstError(int& aOutLine, std::string& aOutText) const;

	void ToggleLineComment();
	void DeleteLines();
	void DuplicateLines(bool aMoveCursor);
	void InsertLineBelow();
	void InsertLineAbove();
	void IndentLines(bool aOutdent);
	void SelectLine();
	void DeleteWord(bool aForward);

private:
	typedef std::vector<std::pair<std::regex, PaletteIndex>> RegexList;

	struct EditorState
	{
		Coordinates mSelectionStart;
		Coordinates mSelectionEnd;
		Coordinates mCursorPosition;
	};

	class UndoRecord
	{
	public:
		UndoRecord() {}
		~UndoRecord() {}

		UndoRecord(
			const std::string& aAdded,
			const TextEditor::Coordinates aAddedStart,
			const TextEditor::Coordinates aAddedEnd,

			const std::string& aRemoved,
			const TextEditor::Coordinates aRemovedStart,
			const TextEditor::Coordinates aRemovedEnd,

			TextEditor::EditorState& aBefore,
			TextEditor::EditorState& aAfter);

		void Undo(TextEditor* aEditor);
		void Redo(TextEditor* aEditor);

		std::string mAdded;
		Coordinates mAddedStart;
		Coordinates mAddedEnd;

		std::string mRemoved;
		Coordinates mRemovedStart;
		Coordinates mRemovedEnd;

		EditorState mBefore;
		EditorState mAfter;
	};

	typedef std::vector<UndoRecord> UndoBuffer;

	void ProcessInputs();
	void Colorize(int aFromLine = 0, int aCount = -1);
	void ColorizeRange(int aFromLine = 0, int aToLine = 0);
	void ColorizeInternal();
	float TextDistanceToLineStart(const Coordinates& aFrom) const;
	int GetPageSize() const;
	std::string GetText(const Coordinates& aStart, const Coordinates& aEnd) const;
	Coordinates GetActualCursorCoordinates() const;
	Coordinates SanitizeCoordinates(const Coordinates& aValue) const;
	void Advance(Coordinates& aCoordinates) const;
	void DeleteRange(const Coordinates& aStart, const Coordinates& aEnd);
	int InsertTextAt(Coordinates& aWhere, const char* aValue);
	void AddUndo(UndoRecord& aValue, bool aMerge = false);
	Coordinates ReplaceRange(const Coordinates& aStart, const Coordinates& aEnd, const std::string& aText, UndoRecord& aUndo);
	void ReplaceLines(int aFirst, int aLast, const std::vector<std::string>& aLines);
	void GetSelectedLines(int& aOutFirst, int& aOutLast) const;
	std::string GetIdentifierAt(const Coordinates& aAt, int* aOutStart = nullptr, int* aOutEnd = nullptr) const;
	int FindDefinitionLine(const std::string& aName, int aFromLine) const;
	void PushNavigation();
	void UpdateCompletion(bool aManual);
	void AcceptCompletion(int aIndex);
	void CloseCompletion();
	bool HandleCompletionKeys();
	void UpdateSignature();
	void DrawCompletion(const ImVec2& aOrigin);
	void DrawSignature(const ImVec2& aOrigin);
	void DrawScrollbarMarks();
	void UpdateMinimap();
	void DrawMinimap();
	void ScrollMinimapTo(float aMouseY);
	Coordinates ScreenPosToCoordinates(const ImVec2& aPosition) const;
	Coordinates FindWordStart(const Coordinates& aFrom) const;
	Coordinates FindWordEnd(const Coordinates& aFrom) const;
	Coordinates FindNextWord(const Coordinates& aFrom) const;
	int GetCharacterIndex(const Coordinates& aCoordinates) const;
	int GetCharacterColumn(int aLine, int aIndex) const;
	int GetLineCharacterCount(int aLine) const;
	int GetLineMaxColumn(int aLine) const;
	bool IsOnWordBoundary(const Coordinates& aAt) const;
	static bool IsWordChar(Char aChar);
	void RemoveLine(int aStart, int aEnd);
	void RemoveLine(int aIndex);
	Line& InsertLine(int aIndex);
	void EnterCharacter(ImWchar aChar, bool aShift);
	void Backspace();
	void DeleteSelection();
	std::string GetWordUnderCursor() const;
	std::string GetWordAt(const Coordinates& aCoords) const;
	ImU32 GetGlyphColor(const Glyph& aGlyph) const;

	void HandleKeyboardInputs();
	void HandleMouseInputs();
	void Render();
	void FindAll(bool aGoTo = true);
	void GoToMatch(int index);

	struct SearchState
	{
		std::string query;
		bool active = false;
		bool caseSensitive = false;
		int currentMatch = -1;
		bool focusRequested = false;
		bool wholeWord = false;
		bool replace = false;
		std::vector<std::pair<Coordinates, Coordinates>> matches;
		std::vector<std::pair<int, int>> ranges;
	};

	struct CompleteState
	{
		bool active = false;
		bool manual = false;
		int selected = 0;
		int first = 0;
		float wheel = 0.0f;
		int pending = -1;
		int startIndex = 0;
		Coordinates cursor;
		std::string prefix;
		std::vector<Completion> items;
		ImVec2 rectMin;
		ImVec2 rectMax;
	};

	float mLineSpacing;
	Lines mLines;
	EditorState mState;
	UndoBuffer mUndoBuffer;
	int mUndoIndex;
	SearchState mSearch;
	CompleteState mComplete;
	Signature mSignature;
	bool mSignatureVisible = false;
	int mSignatureLine = -1;
	Coordinates mSignatureCursor;
	bool mCompleteTrigger = false;
	bool mAutoComplete = true;
	bool mParameterHints = true;
	bool mFocusRequest = false;
	bool mUndoMerge = false;
	Request mRequest = Request::None;
	Coordinates mContextMenuPos;
	std::vector<Symbol> mSymbols;
	std::vector<Coordinates> mNavBack;
	std::vector<Coordinates> mNavForward;
	std::string mLineCopyText;
	int mLinkLine = -1;
	int mLinkStart = 0;
	int mLinkEnd = 0;
	WhitespaceMode mWhitespaceMode = WhitespaceMode::Selection;
	bool mMinimapEnabled = false;
	bool mMinimapHovered = false;
	bool mMinimapDrag = false;
	float mMinimapWidth = 0.0f;
	ImVec2 mMinimapMin;
	ImVec2 mMinimapMax;

	int mTabSize;
	bool mOverwrite;
	bool mReadOnly;
	bool mWithinRender;
	bool mScrollToCursor;
	bool mScrollToTop;
	int mScrollToLine = -1;
	bool mFoldDirty = true;
	std::vector<unsigned char> mFoldFlags;
	std::vector<int> mFoldEnd;
	std::vector<unsigned char> mHeaderFlags;
	std::vector<unsigned char> mGuideFlags;
	int mDragBlockLine = -1;
	int mDropLine = -1;
	bool mDragBlockMoved = false;
	std::vector<int> mVisibleLines;
	std::vector<int> mLineRows;
	Annotations mAnnotations;
	int mHoverFoldLine = -1;
	int mHoverActionLine = -1;
	int mHeaderActionLine = -1;
	bool mScrollSmooth = false;
	float mScrollTargetY = -1.0f;
	bool mDraggingText = false;
	float mFoldColumnWidth = 0.0f;
	std::string mHoverWord;
	double mHoverTime = 0.0;
	bool mTextChanged;
	bool mColorizerEnabled;
	float mTextStart;                   // position (in pixels) where a code line starts relative to the left of the TextEditor.
	int  mLeftMargin;
	bool mCursorPositionChanged;
	int mColorRangeMin, mColorRangeMax;
	SelectionMode mSelectionMode;
	bool mHandleKeyboardInputs;
	bool mHandleMouseInputs;
	bool mIgnoreImGuiChild;

	Palette mPaletteBase;
	Palette mPalette;
	LanguageDefinition mLanguageDefinition;
	RegexList mRegexList;

	bool mCheckComments;
	Breakpoints mBreakpoints;
	Markers mMarkers;
	ImVec2 mCharAdvance;
	Coordinates mInteractiveStart, mInteractiveEnd;
	std::string mLineBuffer;
	uint64_t mStartTime;

	float mLastClick;
};
