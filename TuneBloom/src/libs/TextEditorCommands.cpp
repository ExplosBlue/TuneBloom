#include <libs/TextEditor.h>

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <vector>

static constexpr int   cCompleteVisibleRows = 8;
static constexpr float cCompleteWheelRows   = 3.0f;
static constexpr float cCompleteMinWidthEm  = 16.0f;
static constexpr float cCompleteMaxWidthEm  = 34.0f;
static constexpr float cSignatureWrapEm     = 30.0f;
static constexpr int   cNavigationMax       = 64;
static constexpr int   cScrollbarMarkMax    = 4000;
static constexpr float cMinimapWidthEm      = 6.0f;
static constexpr float cMinimapMinWindowEm  = 28.0f;
static constexpr float cMinimapRowPitch     = 4.0f;
static constexpr float cMinimapRowHeight    = 3.0f;
static constexpr float cMinimapTextHeight   = 2.0f;
static constexpr float cMinimapCharWidth    = 2.0f;
static constexpr float cMinimapPadding      = 6.0f;
static constexpr float cMinimapHeaderAlpha  = 0.09f;
static constexpr int   cMinimapCommentAlpha = 120;

bool TextEditor::IsWordChar(Char aChar)
{
    return isalnum(aChar) || aChar == '_' || aChar >= 0x80;
}

static bool IsBlankChar(TextEditor::Char c)
{
    return c == ' ' || c == '\t';
}

static std::string GetLineIndent(const std::string& aText)
{
    return aText.substr(0, aText.find_first_not_of(" \t"));
}

static std::string ToLower(std::string text)
{
    for (char& c : text)
        c = static_cast<char>(tolower(static_cast<unsigned char>(c)));

    return text;
}

TextEditor::Coordinates TextEditor::ReplaceRange(const Coordinates& aStart, const Coordinates& aEnd, const std::string& aText, UndoRecord& aUndo)
{
    aUndo.mRemovedStart = aStart;
    aUndo.mRemovedEnd = aEnd;
    aUndo.mRemoved = GetText(aStart, aEnd);

    DeleteRange(aStart, aEnd);

    int line = aStart.mLine;
    int index = GetCharacterIndex(aStart);

    Coordinates where = aStart;
    InsertTextAt(where, aText.c_str());

    for (char c : aText)
    {
        if (c == '\n')
        {
            ++line;
            index = 0;
        }
        else if (c != '\r')
        {
            ++index;
        }
    }

    const Coordinates end(line, GetCharacterColumn(line, index));

    aUndo.mAddedStart = aStart;
    aUndo.mAddedEnd = end;
    aUndo.mAdded = aText;

    mTextChanged = true;
    mFoldDirty = true;

    Colorize(aStart.mLine - 1, line - aStart.mLine + 3);

    return end;
}

void TextEditor::GetSelectedLines(int& aOutFirst, int& aOutLast) const
{
    aOutFirst = aOutLast = GetActualCursorCoordinates().mLine;

    if (HasSelection())
    {
        aOutFirst = mState.mSelectionStart.mLine;
        aOutLast = mState.mSelectionEnd.mLine;

        if (aOutLast > aOutFirst && mState.mSelectionEnd.mColumn == 0)
            --aOutLast;
    }

    aOutLast = std::min(aOutLast, (int)mLines.size() - 1);
    aOutFirst = std::min(aOutFirst, aOutLast);

    if (IsFoldedHeader(aOutLast))
        aOutLast = mFoldEnd[aOutLast];
}

void TextEditor::ReplaceLines(int aFirst, int aLast, const std::vector<std::string>& aLines)
{
    std::string text;
    for (size_t i = 0; i < aLines.size(); ++i)
    {
        if (i > 0)
            text += '\n';

        text += aLines[i];
    }

    const Coordinates start(aFirst, 0);
    const Coordinates oldEnd(aLast, GetLineMaxColumn(aLast));

    if (GetText(start, oldEnd) == text)
        return;

    const bool hadSelection = HasSelection();
    const Coordinates cursor = GetActualCursorCoordinates();
    const int cursorIndex = GetCharacterIndex(cursor);
    const int oldSize = (int)mLines[cursor.mLine].size();

    UndoRecord u;
    u.mBefore = mState;

    const Coordinates end = ReplaceRange(start, oldEnd, text, u);

    if (hadSelection)
    {
        mState.mSelectionStart = mInteractiveStart = start;
        mState.mSelectionEnd = mInteractiveEnd = end;
        mState.mCursorPosition = end;
    }
    else
    {
        Coordinates position = cursor;

        if (cursor.mLine > aLast)
        {
            position.mLine += end.mLine - aLast;
        }
        else if (cursor.mLine >= aFirst)
        {
            const int line = std::min(cursor.mLine, end.mLine);
            const int index = std::max(0, cursorIndex + (int)mLines[line].size() - oldSize);

            position = Coordinates(line, GetCharacterColumn(line, index));
        }

        mState.mCursorPosition = position;
        mState.mSelectionStart = mState.mSelectionEnd = mInteractiveStart = mInteractiveEnd = position;
    }

    u.mAfter = mState;
    AddUndo(u);

    EnsureCursorVisible();
}

void TextEditor::ToggleLineComment()
{
    const std::string& comment = mLanguageDefinition.mSingleLineComment;

    if (mReadOnly || comment.empty())
        return;

    int first, last;
    GetSelectedLines(first, last);

    std::vector<std::string> lines;
    bool allCommented = true;
    bool anyText = false;

    for (int i = first; i <= last; ++i)
    {
        lines.push_back(GetLineText(i));

        const std::string::size_type pos = lines.back().find_first_not_of(" \t");
        if (pos == std::string::npos)
            continue;

        anyText = true;
        allCommented &= lines.back().compare(pos, comment.size(), comment) == 0;
    }

    if (!anyText)
        return;

    for (std::string& line : lines)
    {
        const std::string::size_type pos = line.find_first_not_of(" \t");
        if (pos == std::string::npos)
            continue;

        if (!allCommented)
        {
            line.insert(pos, comment + " ");
            continue;
        }

        line.erase(pos, comment.size());

        if (pos < line.size() && line[pos] == ' ')
            line.erase(pos, 1);
    }

    ReplaceLines(first, last, lines);
}

void TextEditor::IndentLines(bool aOutdent)
{
    if (mReadOnly)
        return;

    int first, last;
    GetSelectedLines(first, last);

    std::vector<std::string> lines;

    for (int i = first; i <= last; ++i)
    {
        std::string line = GetLineText(i);

        if (!aOutdent)
        {
            if (!line.empty())
                line.insert(line.begin(), '\t');
        }
        else if (!line.empty() && line[0] == '\t')
        {
            line.erase(0, 1);
        }
        else
        {
            int count = 0;
            while (count < mTabSize && count < (int)line.size() && line[count] == ' ')
                ++count;

            line.erase(0, count);
        }

        lines.push_back(line);
    }

    ReplaceLines(first, last, lines);
}

void TextEditor::DeleteLines()
{
    if (mReadOnly)
        return;

    int first, last;
    GetSelectedLines(first, last);

    const int lineCount = (int)mLines.size();
    const int column = GetActualCursorCoordinates().mColumn;

    Coordinates start(first, 0);
    Coordinates end(last, GetLineMaxColumn(last));

    if (last < lineCount - 1)
        end = Coordinates(last + 1, 0);
    else if (first > 0)
        start = Coordinates(first - 1, GetLineMaxColumn(first - 1));

    if (start == end)
        return;

    UndoRecord u;
    u.mBefore = mState;

    ReplaceRange(start, end, std::string(), u);

    const Coordinates position = SanitizeCoordinates(Coordinates(std::min(first, (int)mLines.size() - 1), column));

    mState.mCursorPosition = position;
    mState.mSelectionStart = mState.mSelectionEnd = mInteractiveStart = mInteractiveEnd = position;

    u.mAfter = mState;
    AddUndo(u);

    EnsureCursorVisible();
}

void TextEditor::DuplicateLines(bool aMoveCursor)
{
    if (mReadOnly)
        return;

    int first, last;
    GetSelectedLines(first, last);

    std::string text;
    for (int i = first; i <= last; ++i)
        text += "\n" + GetLineText(i);

    UndoRecord u;
    u.mBefore = mState;

    const Coordinates at(last, GetLineMaxColumn(last));
    ReplaceRange(at, at, text, u);

    if (aMoveCursor)
    {
        const int count = last - first + 1;

        mState.mCursorPosition.mLine += count;
        mState.mSelectionStart.mLine += count;
        mState.mSelectionEnd.mLine += count;
        mInteractiveStart = mState.mSelectionStart;
        mInteractiveEnd = mState.mSelectionEnd;
    }

    u.mAfter = mState;
    AddUndo(u);

    EnsureCursorVisible();
}

void TextEditor::InsertLineBelow()
{
    if (mReadOnly)
        return;

    int line = GetActualCursorCoordinates().mLine;

    const std::string indent = GetLineIndent(GetLineText(line));

    if (IsFoldedHeader(line))
        line = mFoldEnd[line];

    UndoRecord u;
    u.mBefore = mState;

    const Coordinates at(line, GetLineMaxColumn(line));
    const Coordinates end = ReplaceRange(at, at, "\n" + indent, u);

    mState.mCursorPosition = end;
    mState.mSelectionStart = mState.mSelectionEnd = mInteractiveStart = mInteractiveEnd = end;

    u.mAfter = mState;
    AddUndo(u);

    EnsureCursorVisible();
}

void TextEditor::InsertLineAbove()
{
    if (mReadOnly)
        return;

    const int line = GetActualCursorCoordinates().mLine;

    const std::string indent = GetLineIndent(GetLineText(line));

    UndoRecord u;
    u.mBefore = mState;

    const Coordinates at(line, 0);
    ReplaceRange(at, at, indent + "\n", u);

    const Coordinates position(line, GetLineMaxColumn(line));

    mState.mCursorPosition = position;
    mState.mSelectionStart = mState.mSelectionEnd = mInteractiveStart = mInteractiveEnd = position;

    u.mAfter = mState;
    AddUndo(u);

    EnsureCursorVisible();
}

void TextEditor::SelectLine()
{
    int first, last;
    GetSelectedLines(first, last);

    const bool isLineSelection = HasSelection() && mState.mSelectionStart.mColumn == 0 && mState.mSelectionEnd.mColumn == 0;
    if (isLineSelection)
        last = std::min(mState.mSelectionEnd.mLine, (int)mLines.size() - 1);

    if (IsFoldedHeader(last))
        last = mFoldEnd[last];

    const Coordinates start(first, 0);
    const Coordinates end = last + 1 < (int)mLines.size() ? Coordinates(last + 1, 0) : Coordinates(last, GetLineMaxColumn(last));

    mSelectionMode = SelectionMode::Normal;
    mState.mSelectionStart = mInteractiveStart = start;
    mState.mSelectionEnd = mInteractiveEnd = end;
    mState.mCursorPosition = end;

    EnsureCursorVisible();
}

void TextEditor::DeleteWord(bool aForward)
{
    if (mReadOnly)
        return;

    const Coordinates cursor = GetActualCursorCoordinates();
    const Line& line = mLines[cursor.mLine];
    const int size = (int)line.size();
    const int index = GetCharacterIndex(cursor);

    const bool atEdge = aForward ? index >= size : index == 0;

    if (HasSelection() || atEdge)
    {
        if (aForward)
            Delete();
        else
            Backspace();

        return;
    }

    int target = index;

    if (aForward)
    {
        while (target < size && IsBlankChar(line[target].mChar))
            ++target;

        if (target < size && IsWordChar(line[target].mChar))
        {
            while (target < size && IsWordChar(line[target].mChar))
                ++target;
        }
        else if (target < size && target == index)
        {
            ++target;
        }
    }
    else
    {
        while (target > 0 && IsBlankChar(line[target - 1].mChar))
            --target;

        if (target > 0 && IsWordChar(line[target - 1].mChar))
        {
            while (target > 0 && IsWordChar(line[target - 1].mChar))
                --target;
        }
        else if (target > 0 && target == index)
        {
            --target;
        }
    }

    const Coordinates start(cursor.mLine, GetCharacterColumn(cursor.mLine, std::min(index, target)));
    const Coordinates end(cursor.mLine, GetCharacterColumn(cursor.mLine, std::max(index, target)));

    UndoRecord u;
    u.mBefore = mState;

    ReplaceRange(start, end, std::string(), u);

    mState.mCursorPosition = start;
    mState.mSelectionStart = mState.mSelectionEnd = mInteractiveStart = mInteractiveEnd = start;

    u.mAfter = mState;
    AddUndo(u);

    EnsureCursorVisible();
}

std::string TextEditor::GetIdentifierAt(const Coordinates& aAt, int* aOutStart, int* aOutEnd) const
{
    if (aAt.mLine < 0 || aAt.mLine >= (int)mLines.size())
        return std::string();

    const Line& line = mLines[aAt.mLine];

    int start = std::min(GetCharacterIndex(aAt), (int)line.size());
    int end = start;

    while (start > 0 && IsWordChar(line[start - 1].mChar))
        --start;

    while (end < (int)line.size() && IsWordChar(line[end].mChar))
        ++end;

    if (aOutStart)
        *aOutStart = start;

    if (aOutEnd)
        *aOutEnd = end;

    std::string word;
    for (int i = start; i < end; ++i)
        word.push_back(line[i].mChar);

    return word;
}

int TextEditor::FindDefinitionLine(const std::string& aName, int aFromLine) const
{
    if (aName.empty())
        return -1;

    int scope = -1;
    for (int i = 0; i < (int)mSymbols.size() && mSymbols[i].mLine <= aFromLine; ++i)
    {
        if (mSymbols[i].mGlobal)
            scope = i;
    }

    for (int i = scope + 1; i < (int)mSymbols.size() && !mSymbols[i].mGlobal; ++i)
    {
        if (mSymbols[i].mName == aName)
            return mSymbols[i].mLine;
    }

    for (const Symbol& symbol : mSymbols)
    {
        if (symbol.mGlobal && symbol.mName == aName)
            return symbol.mLine;
    }

    return -1;
}

bool TextEditor::CanGoToDefinition(const Coordinates& aAt) const
{
    const int line = FindDefinitionLine(GetIdentifierAt(aAt), aAt.mLine);

    return line >= 0 && line != aAt.mLine;
}

bool TextEditor::GoToDefinition(const Coordinates& aAt)
{
    const int line = FindDefinitionLine(GetIdentifierAt(aAt), aAt.mLine);

    if (line < 0 || line == aAt.mLine)
        return false;

    int index = 0;
    while (index < (int)mLines[line].size() && IsBlankChar(mLines[line][index].mChar))
        ++index;

    GoToLine(line, GetCharacterColumn(line, index));

    return true;
}

void TextEditor::PushNavigation()
{
    mNavBack.push_back(GetActualCursorCoordinates());

    if ((int)mNavBack.size() > cNavigationMax)
        mNavBack.erase(mNavBack.begin());

    mNavForward.clear();
}

void TextEditor::GoToLine(int aLine, int aColumn, bool aAddHistory)
{
    if (mLines.empty())
        return;

    if (aAddHistory)
        PushNavigation();

    const int line = std::max(0, std::min(aLine, (int)mLines.size() - 1));
    const Coordinates position = SanitizeCoordinates(Coordinates(line, std::max(0, aColumn)));

    mSelectionMode = SelectionMode::Normal;
    mState.mCursorPosition = position;
    mState.mSelectionStart = mState.mSelectionEnd = mInteractiveStart = mInteractiveEnd = position;
    mCursorPositionChanged = true;

    ScrollToLine(line, true);
}

void TextEditor::NavigateBack()
{
    if (mNavBack.empty())
        return;

    const Coordinates position = mNavBack.back();
    mNavBack.pop_back();
    mNavForward.push_back(GetActualCursorCoordinates());

    GoToLine(position.mLine, position.mColumn, false);
}

void TextEditor::NavigateForward()
{
    if (mNavForward.empty())
        return;

    const Coordinates position = mNavForward.back();
    mNavForward.pop_back();
    mNavBack.push_back(GetActualCursorCoordinates());

    GoToLine(position.mLine, position.mColumn, false);
}

bool TextEditor::GoToErrorMarker(int aDirection)
{
    const int cursorLine = GetActualCursorCoordinates().mLine;

    int first = -1;
    int last = -1;
    int next = -1;
    int previous = -1;

    for (const auto& marker : mMarkers)
    {
        if (!marker.second.mIsError)
            continue;

        const int line = marker.first - 1;

        if (first < 0)
            first = line;

        last = line;

        if (line > cursorLine && next < 0)
            next = line;

        if (line < cursorLine)
            previous = line;
    }

    if (first < 0)
        return false;

    if (aDirection > 0)
        GoToLine(next >= 0 ? next : first);
    else
        GoToLine(previous >= 0 ? previous : last);

    return true;
}

bool TextEditor::GetFirstError(int& aOutLine, std::string& aOutText) const
{
    for (const auto& marker : mMarkers)
    {
        if (!marker.second.mIsError)
            continue;

        aOutLine = marker.first - 1;
        aOutText = marker.second.mDetail;
        return true;
    }

    return false;
}

int TextEditor::FindCursorSymbol(bool aGlobal) const
{
    const int cursorLine = GetActualCursorCoordinates().mLine;

    int global = -1;
    int local = -1;

    for (int i = 0; i < (int)mSymbols.size() && mSymbols[i].mLine <= cursorLine; ++i)
    {
        if (mSymbols[i].mGlobal)
        {
            global = i;
            local = -1;
        }
        else
        {
            local = i;
        }
    }

    return aGlobal ? global : local;
}

void TextEditor::GetSelectionSize(int& aOutLines, int& aOutChars) const
{
    aOutLines = 0;
    aOutChars = 0;

    if (!HasSelection())
        return;

    const Coordinates start = mState.mSelectionStart;
    const Coordinates end = SanitizeCoordinates(mState.mSelectionEnd);

    aOutLines = end.mLine - start.mLine + 1;

    for (int i = start.mLine; i <= end.mLine && i < (int)mLines.size(); ++i)
    {
        const Line& line = mLines[i];
        const int from = i == start.mLine ? GetCharacterIndex(start) : 0;
        const int to = i == end.mLine ? GetCharacterIndex(end) : (int)line.size();

        for (int j = from; j < to && j < (int)line.size(); ++j)
        {
            if ((line[j].mChar & 0xC0) != 0x80)
                ++aOutChars;
        }

        if (i < end.mLine)
            ++aOutChars;
    }
}

void TextEditor::OpenSearch(bool aReplace)
{
    const bool selectionInLine = HasSelection() && mState.mSelectionStart.mLine == mState.mSelectionEnd.mLine;

    if (selectionInLine)
        mSearch.query = GetSelectedText();
    else if (mSearch.query.empty())
        mSearch.query = GetIdentifierAt(GetActualCursorCoordinates());

    mSearch.active = true;
    mSearch.replace |= aReplace;
    mSearch.focusRequested = true;

    CloseCompletion();
    FindAll(!selectionInLine);
}

void TextEditor::CloseSearch()
{
    mSearch.active = false;
    mSearch.matches.clear();
    mSearch.ranges.clear();
    mSearch.currentMatch = -1;

    RequestFocus();
}

void TextEditor::SetSearchQuery(const std::string& aQuery)
{
    if (mSearch.query == aQuery)
        return;

    mSearch.query = aQuery;
    FindAll();
}

void TextEditor::SetSearchOptions(bool aCaseSensitive, bool aWholeWord)
{
    if (mSearch.caseSensitive == aCaseSensitive && mSearch.wholeWord == aWholeWord)
        return;

    mSearch.caseSensitive = aCaseSensitive;
    mSearch.wholeWord = aWholeWord;
    FindAll();
}

void TextEditor::ReplaceCurrent(const std::string& aText)
{
    if (mReadOnly || mSearch.matches.empty())
        return;

    if (mSearch.currentMatch < 0)
    {
        FindNext();
        return;
    }

    const auto match = mSearch.matches[mSearch.currentMatch];

    UndoRecord u;
    u.mBefore = mState;

    const Coordinates end = ReplaceRange(match.first, match.second, aText, u);

    mState.mCursorPosition = end;
    mState.mSelectionStart = mState.mSelectionEnd = mInteractiveStart = mInteractiveEnd = end;

    u.mAfter = mState;
    AddUndo(u);

    FindAll();
}

void TextEditor::ReplaceAll(const std::string& aText)
{
    if (mReadOnly || mSearch.matches.empty())
        return;

    const int first = mSearch.matches.front().first.mLine;
    const int last = mSearch.matches.back().first.mLine;

    std::vector<std::string> lines;
    size_t match = 0;

    for (int i = first; i <= last; ++i)
    {
        const std::string text = GetLineText(i);

        std::string result;
        int from = 0;

        for (; match < mSearch.matches.size() && mSearch.matches[match].first.mLine == i; ++match)
        {
            const std::pair<int, int>& range = mSearch.ranges[match];

            result.append(text, from, range.first - from);
            result += aText;
            from = range.second;
        }

        result.append(text, from, std::string::npos);
        lines.push_back(result);
    }

    const Coordinates cursor = GetActualCursorCoordinates();
    mState.mSelectionStart = mState.mSelectionEnd = mInteractiveStart = mInteractiveEnd = cursor;

    ReplaceLines(first, last, lines);
    FindAll(false);
}

static int CalcCompleteFirst(int aSelected, int aCount)
{
    const int visible = std::min(aCount, cCompleteVisibleRows);

    return std::max(0, std::min(aSelected - visible / 2, aCount - visible));
}

void TextEditor::CloseCompletion()
{
    mComplete.active = false;
    mComplete.manual = false;
    mComplete.pending = -1;
    mComplete.wheel = 0.0f;
    mComplete.items.clear();
}

void TextEditor::UpdateCompletion(bool aManual)
{
    const bool wasActive = mComplete.active;
    const std::string selectedLabel = wasActive && mComplete.selected < (int)mComplete.items.size() ? mComplete.items[mComplete.selected].mLabel : std::string();
    const bool manual = aManual || (wasActive && mComplete.manual);

    CloseCompletion();

    if (mLanguageDefinition.mComplete == nullptr || mReadOnly || HasSelection())
        return;

    const Coordinates cursor = GetActualCursorCoordinates();
    const Line& line = mLines[cursor.mLine];
    const int index = GetCharacterIndex(cursor);

    if (index < (int)line.size() && IsWordChar(line[index].mChar))
        return;

    int start = index;
    while (start > 0 && IsWordChar(line[start - 1].mChar))
        --start;

    const int probe = start < (int)line.size() ? start : start - 1;
    if (probe >= 0 && (line[probe].mComment || line[probe].mColorIndex == PaletteIndex::Comment))
        return;

    std::string before;
    std::string prefix;

    for (int i = 0; i < start; ++i)
        before.push_back(line[i].mChar);

    for (int i = start; i < index; ++i)
        prefix.push_back(line[i].mChar);

    if (!manual && (prefix.empty() || isdigit(static_cast<unsigned char>(prefix[0]))))
        return;

    std::vector<Completion> items;
    bool addSymbols = false;

    mLanguageDefinition.mComplete(before, prefix, items, addSymbols);

    if (addSymbols)
    {
        int scope = -1;
        for (int i = 0; i < (int)mSymbols.size() && mSymbols[i].mLine <= cursor.mLine; ++i)
        {
            if (mSymbols[i].mGlobal)
                scope = i;
        }

        const auto addSymbol = [&](const Symbol& symbol)
        {
            if (symbol.mLine == cursor.mLine)
                return;

            Completion item;
            item.mLabel = symbol.mName;
            item.mIcon = mLanguageDefinition.mSymbolIcon;
            item.mColorIndex = symbol.mGlobal ? PaletteIndex::String : PaletteIndex::CharLiteral;
            items.push_back(item);
        };

        for (int i = scope + 1; i < (int)mSymbols.size() && !mSymbols[i].mGlobal; ++i)
            addSymbol(mSymbols[i]);

        for (const Symbol& symbol : mSymbols)
        {
            if (symbol.mGlobal)
                addSymbol(symbol);
        }
    }

    const std::string lowerPrefix = ToLower(prefix);
    std::vector<Completion> starts;
    std::vector<Completion> contains;

    for (Completion& item : items)
    {
        const std::string::size_type pos = ToLower(item.mLabel).find(lowerPrefix);

        if (pos == 0)
            starts.push_back(std::move(item));
        else if (pos != std::string::npos)
            contains.push_back(std::move(item));
    }

    starts.insert(starts.end(), contains.begin(), contains.end());

    if (starts.empty() || (starts.size() == 1 && starts[0].mLabel == prefix))
        return;

    mComplete.active = true;
    mComplete.manual = manual;
    mComplete.startIndex = start;
    mComplete.cursor = cursor;
    mComplete.prefix = prefix;
    mComplete.items = std::move(starts);
    mComplete.selected = 0;

    for (int i = 0; i < (int)mComplete.items.size(); ++i)
    {
        if (mComplete.items[i].mLabel == selectedLabel)
            mComplete.selected = i;
    }

    mComplete.first = CalcCompleteFirst(mComplete.selected, (int)mComplete.items.size());
}

void TextEditor::AcceptCompletion(int aIndex)
{
    if (!mComplete.active || aIndex < 0 || aIndex >= (int)mComplete.items.size())
    {
        CloseCompletion();
        return;
    }

    const std::string label = mComplete.items[aIndex].mLabel;
    const int line = mComplete.cursor.mLine;
    const Coordinates start(line, GetCharacterColumn(line, mComplete.startIndex));
    const Coordinates cursor = GetActualCursorCoordinates();

    CloseCompletion();

    if (cursor.mLine != line || cursor < start)
        return;

    UndoRecord u;
    u.mBefore = mState;

    const Coordinates end = ReplaceRange(start, cursor, label, u);

    mState.mCursorPosition = end;
    mState.mSelectionStart = mState.mSelectionEnd = mInteractiveStart = mInteractiveEnd = end;

    u.mAfter = mState;
    AddUndo(u);

    mSignatureLine = line;
    EnsureCursorVisible();
}

bool TextEditor::HandleCompletionKeys()
{
    if (mComplete.pending >= 0)
        AcceptCompletion(mComplete.pending);

    if (!mComplete.active)
        return false;

    const ImGuiIO& io = ImGui::GetIO();
    const int count = (int)mComplete.items.size();

    if (io.KeyCtrl || io.KeyAlt || io.KeySuper)
        return false;

    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
        mComplete.selected = (mComplete.selected + count - 1) % count;
    else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
        mComplete.selected = (mComplete.selected + 1) % count;
    else if (ImGui::IsKeyPressed(ImGuiKey_PageUp))
        mComplete.selected = std::max(0, mComplete.selected - cCompleteVisibleRows);
    else if (ImGui::IsKeyPressed(ImGuiKey_PageDown))
        mComplete.selected = std::min(count - 1, mComplete.selected + cCompleteVisibleRows);
    else if (ImGui::IsKeyPressed(ImGuiKey_Tab, false))
        AcceptCompletion(mComplete.selected);
    else if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false))
    {
        if (mComplete.items[mComplete.selected].mLabel == mComplete.prefix)
        {
            CloseCompletion();
            return false;
        }

        AcceptCompletion(mComplete.selected);
    }
    else
        return false;

    if (mComplete.active)
        mComplete.first = CalcCompleteFirst(mComplete.selected, count);

    return true;
}

void TextEditor::UpdateSignature()
{
    mSignatureVisible = false;

    const Coordinates cursor = GetActualCursorCoordinates();

    if (cursor.mLine != mSignatureLine)
        mSignatureLine = -1;

    if (mSignatureLine < 0 || !mParameterHints || mReadOnly || HasSelection() || mLanguageDefinition.mSignature == nullptr)
        return;

    const Line& line = mLines[cursor.mLine];
    const int index = std::min(GetCharacterIndex(cursor), (int)line.size());

    std::string before;
    for (int i = 0; i < index; ++i)
    {
        if (line[i].mComment || line[i].mColorIndex == PaletteIndex::Comment)
            return;

        before.push_back(line[i].mChar);
    }

    mSignature = Signature();
    mSignatureVisible = mLanguageDefinition.mSignature(before, mSignature);
}

void TextEditor::DrawCompletion(const ImVec2& aOrigin)
{
    if (!mComplete.active || mComplete.items.empty())
        return;

    const ImGuiStyle& style = ImGui::GetStyle();
    const float em = ImGui::GetFontSize();
    const float rowHeight = ImGui::GetTextLineHeight() + floorf(em * 0.3f);
    const float padding = floorf(em * 0.35f);
    const float gap = floorf(em * 1.5f);

    const int count = (int)mComplete.items.size();
    const int visible = std::min(count, cCompleteVisibleRows);

    if (ImGui::IsMouseHoveringRect(mComplete.rectMin, mComplete.rectMax, false))
    {
        ImGui::SetKeyOwner(ImGuiKey_MouseWheelY, ImGui::GetID("##TextEditorCompleteWheel"));

        mComplete.wheel -= ImGui::GetIO().MouseWheel * cCompleteWheelRows;

        const int steps = static_cast<int>(mComplete.wheel);
        mComplete.wheel -= steps;
        mComplete.first += steps;
    }
    else
        mComplete.wheel = 0.0f;

    mComplete.first = std::max(0, std::min(mComplete.first, count - visible));

    const int first = mComplete.first;

    float iconWidth = 0.0f;
    float labelWidth = 0.0f;
    float detailWidth = 0.0f;

    for (const Completion& item : mComplete.items)
    {
        iconWidth = std::max(iconWidth, ImGui::CalcTextSize(item.mIcon.c_str()).x);
        labelWidth = std::max(labelWidth, ImGui::CalcTextSize(item.mLabel.c_str()).x);
        detailWidth = std::max(detailWidth, ImGui::CalcTextSize(item.mDetail.c_str()).x);
    }

    iconWidth += floorf(em * 0.5f);

    const float contentWidth = iconWidth + labelWidth + (detailWidth > 0.0f ? gap + detailWidth : 0.0f) + padding;
    const float rowWidth = std::max(em * cCompleteMinWidthEm, std::min(em * cCompleteMaxWidthEm, contentWidth));

    const Completion& selected = mComplete.items[mComplete.selected];
    const float docHeight = selected.mDocumentation.empty() ? 0.0f : ImGui::CalcTextSize(selected.mDocumentation.c_str(), nullptr, false, rowWidth - padding * 2.0f).y + padding * 2.0f + 1.0f;
    const float height = visible * rowHeight + docHeight + padding * 2.0f;

    const int line = mComplete.cursor.mLine;
    const Coordinates start(line, GetCharacterColumn(line, mComplete.startIndex));
    const float lineY = aOrigin.y + LineToRow(line) * mCharAdvance.y;
    const ImGuiViewport* viewport = ImGui::GetWindowViewport();

    ImVec2 pos(aOrigin.x + mTextStart + TextDistanceToLineStart(start) - padding - iconWidth, lineY + mCharAdvance.y + 2.0f);

    if (pos.y + height > viewport->Pos.y + viewport->Size.y)
        pos.y = lineY - height - 2.0f;

    pos.x = std::max(viewport->Pos.x, std::min(pos.x, viewport->Pos.x + viewport->Size.x - rowWidth - padding * 2.0f));

    ImVec4 background = ImGui::GetStyleColorVec4(ImGuiCol_PopupBg);
    background.w = 1.0f;

    ImGui::SetNextWindowPos(pos);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, background);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padding, padding));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, floorf(em * 0.4f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));

    const ImGuiWindowFlags flags = ImGuiWindowFlags_Tooltip | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMouseInputs;

    if (ImGui::Begin("##TextEditorComplete", nullptr, flags))
    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const float textOffsetY = floorf((rowHeight - ImGui::GetTextLineHeight()) * 0.5f);
        const ImVec2 listPos = ImGui::GetCursorScreenPos();

        for (int i = first; i < first + visible; ++i)
        {
            const Completion& item = mComplete.items[i];
            const ImVec2 rowMin = ImGui::GetCursorScreenPos();
            const ImVec2 rowMax(rowMin.x + rowWidth, rowMin.y + rowHeight);
            const bool hovered = ImGui::IsMouseHoveringRect(rowMin, rowMax, false);

            if (i == mComplete.selected)
                drawList->AddRectFilled(rowMin, rowMax, ImGui::GetColorU32(ImGuiCol_Header), style.FrameRounding);
            else if (hovered)
                drawList->AddRectFilled(rowMin, rowMax, ImGui::GetColorU32(ImGuiCol_Header, 0.5f), style.FrameRounding);

            const float textY = rowMin.y + textOffsetY;

            drawList->AddText(ImVec2(rowMin.x + floorf(em * 0.25f), textY), mPalette[(int)item.mColorIndex], item.mIcon.c_str());
            drawList->AddText(ImVec2(rowMin.x + iconWidth, textY), ImGui::GetColorU32(ImGuiCol_Text), item.mLabel.c_str());

            if (!item.mDetail.empty())
            {
                drawList->PushClipRect(rowMin, ImVec2(rowMax.x - padding, rowMax.y), true);
                drawList->AddText(ImVec2(rowMin.x + iconWidth + labelWidth + gap, textY), ImGui::GetColorU32(ImGuiCol_TextDisabled), item.mDetail.c_str());
                drawList->PopClipRect();
            }

            if (hovered && ImGui::IsMouseClicked(0))
                mComplete.pending = i;

            ImGui::Dummy(ImVec2(rowWidth, rowHeight));
        }

        if (count > visible)
        {
            const float trackHeight = visible * rowHeight;
            const float thumbHeight = std::max(em * 0.8f, trackHeight * visible / count);
            const float thumbY = listPos.y + (trackHeight - thumbHeight) * first / (count - visible);

            drawList->AddRectFilled(ImVec2(listPos.x + rowWidth - 3.0f, thumbY), ImVec2(listPos.x + rowWidth - 1.0f, thumbY + thumbHeight), ImGui::GetColorU32(ImGuiCol_ScrollbarGrab), 1.0f);
        }

        if (!selected.mDocumentation.empty())
        {
            const ImVec2 docPos = ImGui::GetCursorScreenPos();

            drawList->AddLine(ImVec2(docPos.x, docPos.y + padding), ImVec2(docPos.x + rowWidth, docPos.y + padding), ImGui::GetColorU32(ImGuiCol_Separator));

            ImGui::Dummy(ImVec2(rowWidth, padding * 2.0f + 1.0f));
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + padding);
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + rowWidth - padding * 2.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextUnformatted(selected.mDocumentation.c_str());
            ImGui::PopStyleColor();
            ImGui::PopTextWrapPos();
        }

        mComplete.rectMin = ImGui::GetWindowPos();
        mComplete.rectMax = ImVec2(mComplete.rectMin.x + ImGui::GetWindowSize().x, mComplete.rectMin.y + ImGui::GetWindowSize().y);
    }

    ImGui::End();

    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();
}

void TextEditor::DrawSignature(const ImVec2& aOrigin)
{
    if (!mSignatureVisible || mComplete.active)
        return;

    const float em = ImGui::GetFontSize();
    const float padding = floorf(em * 0.4f);

    const int line = GetActualCursorCoordinates().mLine;
    const float lineY = aOrigin.y + LineToRow(line) * mCharAdvance.y;

    int index = 0;
    while (index < (int)mLines[line].size() && IsBlankChar(mLines[line][index].mChar))
        ++index;

    const float x = aOrigin.x + mTextStart + TextDistanceToLineStart(Coordinates(line, GetCharacterColumn(line, index))) - padding;
    const bool above = lineY - ImGui::GetWindowPos().y > mCharAdvance.y * 3.0f;

    ImVec4 background = ImGui::GetStyleColorVec4(ImGuiCol_PopupBg);
    background.w = 1.0f;

    ImGui::SetNextWindowPos(ImVec2(x, above ? lineY - 2.0f : lineY + mCharAdvance.y + 2.0f), ImGuiCond_Always, ImVec2(0.0f, above ? 1.0f : 0.0f));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, background);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padding * 1.5f, padding));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, floorf(em * 0.4f));

    const ImGuiWindowFlags flags = ImGuiWindowFlags_Tooltip | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;

    if (ImGui::Begin("##TextEditorSignature", nullptr, flags))
    {
        const ImVec4 dimColor = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, ImGui::GetStyle().ItemSpacing.y));

        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(mPalette[(int)PaletteIndex::Keyword]), "%s", mSignature.mName.c_str());

        for (int i = 0; i < (int)mSignature.mParameters.size(); ++i)
        {
            ImGui::SameLine();
            ImGui::TextColored(dimColor, i == 0 ? " " : ", ");
            ImGui::SameLine();

            if (i == mSignature.mActiveParameter)
                ImGui::TextUnformatted(mSignature.mParameters[i].c_str());
            else
                ImGui::TextColored(dimColor, "%s", mSignature.mParameters[i].c_str());
        }

        ImGui::PopStyleVar();

        if (!mSignature.mDocumentation.empty())
        {
            ImGui::PushTextWrapPos(em * cSignatureWrapEm);
            ImGui::TextColored(dimColor, "%s", mSignature.mDocumentation.c_str());
            ImGui::PopTextWrapPos();
        }
    }

    ImGui::End();

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

void TextEditor::UpdateMinimap()
{
    const ImGuiWindow* window = ImGui::GetCurrentWindow();
    const float em = ImGui::GetFontSize();

    mMinimapWidth = 0.0f;

    if (!mMinimapEnabled || window->Size.x < em * cMinimapMinWindowEm)
        return;

    mMinimapWidth = floorf(em * cMinimapWidthEm);
    mMinimapMax = ImVec2(window->Pos.x + window->Size.x - window->ScrollbarSizes.x, window->Pos.y + window->Size.y - window->ScrollbarSizes.y);
    mMinimapMin = ImVec2(mMinimapMax.x - mMinimapWidth, window->Pos.y);
}

void TextEditor::ScrollMinimapTo(float aMouseY)
{
    const float height = mMinimapMax.y - mMinimapMin.y;
    const float total = GetRowCount() * cMinimapRowPitch;
    const float scrollMax = ImGui::GetScrollMaxY();
    const float sliderHeight = height / mCharAdvance.y * cMinimapRowPitch;

    float target = 0.0f;

    if (total > height && height > sliderHeight)
        target = (aMouseY - mMinimapMin.y - sliderHeight * 0.5f) / (height - sliderHeight) * scrollMax;
    else
        target = (aMouseY - mMinimapMin.y) / cMinimapRowPitch * mCharAdvance.y - height * 0.5f;

    ImGui::SetScrollY(std::max(0.0f, std::min(target, scrollMax)));
    mScrollTargetY = -1.0f;
}

void TextEditor::DrawMinimap()
{
    if (mMinimapWidth <= 0.0f)
        return;

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    const float height = mMinimapMax.y - mMinimapMin.y;
    const int rows = GetRowCount();
    const float total = rows * cMinimapRowPitch;
    const float scrollMax = ImGui::GetScrollMaxY();
    const float mapTop = total > height && scrollMax > 0.0f ? floorf((total - height) * ImGui::GetScrollY() / scrollMax) : 0.0f;
    const float textX = mMinimapMin.x + cMinimapPadding;
    const int columnMax = (int)((mMinimapWidth - cMinimapPadding * 2.0f) / cMinimapCharWidth);

    drawList->AddRectFilled(mMinimapMin, mMinimapMax, ImGui::GetColorU32(ImGuiCol_WindowBg) | IM_COL32_A_MASK);
    drawList->PushClipRect(mMinimapMin, mMinimapMax, true);

    const int firstRow = std::max(0, (int)(mapTop / cMinimapRowPitch));
    const int lastRow = std::min(rows - 1, (int)((mapTop + height) / cMinimapRowPitch));

    for (int row = firstRow; row <= lastRow; ++row)
    {
        const int lineNo = RowToLine(row);
        const Line& line = mLines[lineNo];
        const float y = floorf(mMinimapMin.y + row * cMinimapRowPitch - mapTop);
        const bool isHeader = lineNo < (int)mHeaderFlags.size() && mHeaderFlags[lineNo];
        const float rowHeight = isHeader ? cMinimapRowHeight : cMinimapTextHeight;

        if (isHeader)
            drawList->AddRectFilled(ImVec2(mMinimapMin.x, y - 1.0f), ImVec2(mMinimapMax.x, y + cMinimapRowHeight), ImGui::GetColorU32(ImGuiCol_Text, cMinimapHeaderAlpha));

        auto marker = mMarkers.find(lineNo + 1);
        if (marker != mMarkers.end() && (marker->second.mIsError || marker->second.mShowDetail))
        {
            const ImU32 color = marker->second.mIsError ? mPalette[(int)PaletteIndex::ErrorMarker] : marker->second.mColor;

            drawList->AddRectFilled(ImVec2(mMinimapMin.x, y - 1.0f), ImVec2(mMinimapMax.x, y + cMinimapRowHeight + 1.0f), (color & ~IM_COL32_A_MASK) | IM_COL32(0, 0, 0, 150));
        }

        int column = 0;

        for (int i = 0; i < (int)line.size() && column < columnMax;)
        {
            const Char c = line[i].mChar;

            if (c == '\t')
            {
                column = (column / mTabSize) * mTabSize + mTabSize;
                ++i;
                continue;
            }

            if (c == ' ')
            {
                ++column;
                ++i;
                continue;
            }

            const ImU32 color = GetGlyphColor(line[i]);
            const int startColumn = column;

            while (i < (int)line.size() && line[i].mChar != ' ' && line[i].mChar != '\t' && GetGlyphColor(line[i]) == color)
            {
                if ((line[i].mChar & 0xC0) != 0x80)
                    ++column;

                ++i;
            }

            const float x0 = textX + startColumn * cMinimapCharWidth;
            const float x1 = textX + std::min(column, columnMax) * cMinimapCharWidth;

            const bool isComment = color == mPalette[(int)PaletteIndex::Comment];

            drawList->AddRectFilled(ImVec2(x0, y), ImVec2(x1 - 1.0f, y + rowHeight), (color & ~IM_COL32_A_MASK) | IM_COL32(0, 0, 0, isComment ? cMinimapCommentAlpha : 255));
        }
    }

    const int matchCount = (int)mSearch.matches.size();
    const int matchStep = std::max(1, matchCount / cScrollbarMarkMax);

    for (int i = 0; i < matchCount; i += matchStep)
    {
        const auto& match = mSearch.matches[i];
        const float y = floorf(mMinimapMin.y + LineToRow(match.first.mLine) * cMinimapRowPitch - mapTop);
        const float x0 = textX + std::min(match.first.mColumn, columnMax) * cMinimapCharWidth;
        const float x1 = textX + std::min(match.second.mColumn, columnMax) * cMinimapCharWidth;

        drawList->AddRectFilled(ImVec2(x0 - 1.0f, y - 1.0f), ImVec2(x1 + 1.0f, y + cMinimapRowHeight + 1.0f), IM_COL32(255, 200, 50, 200));
    }

    // Visible part of the text
    const float sliderTop = floorf(mMinimapMin.y + ImGui::GetScrollY() / mCharAdvance.y * cMinimapRowPitch - mapTop);
    const float sliderHeight = floorf(ImGui::GetWindowHeight() / mCharAdvance.y * cMinimapRowPitch);
    const float sliderAlpha = mMinimapDrag ? 0.22f : mMinimapHovered ? 0.16f : 0.1f;
    const ImU32 sliderEdgeColor = ImGui::GetColorU32(ImGuiCol_Text, sliderAlpha * 2.0f);

    drawList->AddRectFilled(ImVec2(mMinimapMin.x, sliderTop), ImVec2(mMinimapMax.x, sliderTop + sliderHeight), ImGui::GetColorU32(ImGuiCol_Text, sliderAlpha));
    drawList->AddLine(ImVec2(mMinimapMin.x, sliderTop + 0.5f), ImVec2(mMinimapMax.x, sliderTop + 0.5f), sliderEdgeColor);
    drawList->AddLine(ImVec2(mMinimapMin.x, sliderTop + sliderHeight - 0.5f), ImVec2(mMinimapMax.x, sliderTop + sliderHeight - 0.5f), sliderEdgeColor);

    drawList->PopClipRect();
    drawList->AddLine(ImVec2(mMinimapMin.x + 0.5f, mMinimapMin.y), ImVec2(mMinimapMin.x + 0.5f, mMinimapMax.y), ImGui::GetColorU32(ImGuiCol_Separator, 0.5f));
}

void TextEditor::DrawScrollbarMarks()
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();

    if (!window->ScrollbarY)
        return;

    const ImRect bar = ImGui::GetWindowScrollbarRect(window, ImGuiAxis_Y);
    const float rows = (float)std::max(1, GetRowCount());
    const float width = bar.GetWidth();

    ImDrawList* drawList = window->DrawList;
    drawList->PushClipRect(bar.Min, bar.Max, false);

    const auto mark = [&](int aLine, ImU32 aColor, float aFrom, float aTo)
    {
        const float y = floorf(bar.Min.y + (LineToRow(aLine) + 0.5f) / rows * bar.GetHeight());

        drawList->AddRectFilled(ImVec2(bar.Min.x + floorf(width * aFrom), y - 1.0f), ImVec2(bar.Min.x + floorf(width * aTo), y + 2.0f), aColor);
    };

    const int matchCount = (int)mSearch.matches.size();
    const int matchStep = std::max(1, matchCount / cScrollbarMarkMax);

    for (int i = 0; i < matchCount; i += matchStep)
        mark(mSearch.matches[i].first.mLine, IM_COL32(255, 200, 50, 200), 0.25f, 0.75f);

    for (const auto& marker : mMarkers)
    {
        if (marker.second.mIsError)
            mark(marker.first - 1, mPalette[(int)PaletteIndex::ErrorMarker] | IM_COL32_A_MASK, 0.4f, 1.0f);
        else if (marker.second.mShowDetail)
            mark(marker.first - 1, marker.second.mColor | IM_COL32_A_MASK, 0.0f, 0.4f);
    }

    drawList->PopClipRect();
}
