#include <ui/sequence/SeqManual.h>

#include <ui/Messages.h>
#include <ui/app/UI.h>

#include <imgui/imgui.h>
#include <imgui/imgui_custom.h>
#include <imgui/imgui_internal.h>

#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{

using messages::sequence::ManualRow;
using RowKind = messages::sequence::ManualRowKind;

constexpr auto& cRows = messages::sequence::cManualRows;

constexpr f32 cManualMinDescWidth = 14.0f;
constexpr f32 cSeqPlainTextAlpha   = 0.85f;
constexpr f32 cSeqCurrentLineAlpha = 0.06f;
constexpr f32 cSeqSelectionAlpha   = 0.6f;

constexpr const char* cSuffixes[] = { "r", "v", "t", "tr", "tv", "if" };

constexpr const char* cConstantPrefixes[] = { "VAR_", "GVAR_", "TVAR_", "TRACK_" };

bool IsNoteName(const std::string& word)
{
    if (word.size() < 3 || word.size() > 4)
        return false;

    if (word[0] < 'a' || word[0] > 'g')
        return false;

    if (word[1] != 'n' && word[1] != 's')
        return false;

    if (word.size() == 4)
        return word[2] == 'm' && word[3] == '1';

    return ::isdigit(static_cast<unsigned char>(word[2])) != 0;
}

const ManualRow* FindExactRow(const std::string& name)
{
    static std::unordered_map<std::string, const ManualRow*> sRows;

    if (sRows.empty())
    {
        for (const ManualRow& row : cRows)
        {
            if (row.kind != RowKind::Command)
                continue;

            sRows[row.name] = &row;

            if (row.alt)
                sRows[row.alt] = &row;
        }
    }

    if (IsNoteName(name))
        return sRows["cn4"];

    auto it = sRows.find(name);
    if (it != sRows.end())
        return it->second;

    if (name.size() > 5 && name.compare(0, 3, "mod") == 0 && name[3] >= '2' && name[3] <= '4' && name[4] == '_')
    {
        it = sRows.find("mod_" + name.substr(5));
        if (it != sRows.end())
            return it->second;
    }

    return nullptr;
}

const ManualRow* FindCommandRow(std::string word)
{
    for (s32 i = 0; i < 4; i++)
    {
        const ManualRow* row = FindExactRow(word);
        if (row)
            return row;

        const std::string::size_type pos = word.rfind('_');
        if (pos == std::string::npos)
            return nullptr;

        bool isSuffix = false;
        for (const char* suffix : cSuffixes)
            isSuffix |= word.compare(pos + 1, std::string::npos, suffix) == 0;

        if (!isSuffix)
            return nullptr;

        word.erase(pos);
    }

    return nullptr;
}

const ManualRow* FindConstantRow(const std::string& word)
{
    for (const char* prefix : cConstantPrefixes)
    {
        const size_t len = std::strlen(prefix);
        if (word.size() <= len || word.compare(0, len, prefix) != 0)
            continue;

        bool digits = true;
        for (size_t i = len; i < word.size(); i++)
            digits &= ::isdigit(static_cast<unsigned char>(word[i])) != 0;

        if (!digits)
            return nullptr;

        for (const ManualRow& row : cRows)
        {
            if (row.kind == RowKind::Syntax && std::strcmp(row.name, prefix) == 0)
                return &row;
        }
    }

    return nullptr;
}

bool IsIdentifierStart(char c)
{
    return ::isalpha(static_cast<unsigned char>(c)) || c == '_';
}

bool IsIdentifierChar(char c)
{
    return ::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

bool TokenizeSeq(const char* begin, const char* end, const char*& outBegin, const char*& outEnd, TextEditor::PaletteIndex& paletteIndex)
{
    const char* p = begin;
    outBegin = begin;

    if (*p == ' ' || *p == '\t')
    {
        while (p < end && (*p == ' ' || *p == '\t'))
            p++;

        outEnd = p;
        paletteIndex = TextEditor::PaletteIndex::Default;
        return true;
    }

    if (*p == ';')
    {
        outEnd = end;
        paletteIndex = TextEditor::PaletteIndex::Comment;
        return true;
    }

    if (IsIdentifierStart(*p))
    {
        while (p < end && IsIdentifierChar(*p))
            p++;

        outEnd = p;

        while (p < end && (*p == ' ' || *p == '\t'))
            p++;

        const std::string word(begin, outEnd);

        if (p < end && *p == ':')
            paletteIndex = word[0] == '_' ? TextEditor::PaletteIndex::CharLiteral : TextEditor::PaletteIndex::String;
        else if (IsNoteName(word.substr(0, word.find('_'))) && FindCommandRow(word))
            paletteIndex = TextEditor::PaletteIndex::KnownIdentifier;
        else if (FindCommandRow(word))
            paletteIndex = TextEditor::PaletteIndex::Keyword;
        else if (FindConstantRow(word) || word.compare(0, 9, "MOD_TYPE_") == 0)
            paletteIndex = TextEditor::PaletteIndex::PreprocIdentifier;
        else
            paletteIndex = TextEditor::PaletteIndex::Identifier;

        return true;
    }

    const bool sign = (*p == '-' || *p == '+') && p + 1 < end && ::isdigit(static_cast<unsigned char>(p[1]));
    if (sign || ::isdigit(static_cast<unsigned char>(*p)))
    {
        p++;

        while (p < end && ::isalnum(static_cast<unsigned char>(*p)))
            p++;

        outEnd = p;
        paletteIndex = TextEditor::PaletteIndex::Number;
        return true;
    }

    outEnd = begin + 1;
    paletteIndex = TextEditor::PaletteIndex::Punctuation;
    return true;
}

static std::string GetRowSummary(const ManualRow& row)
{
    const std::string desc = row.desc;
    std::string text = desc.substr(0, desc.find(". "));

    if (row.range)
        text += std::string(desc.find(". ") == std::string::npos && desc.ends_with('.') ? " " : ". ") + row.range;

    return text;
}

bool DescribeSeqWord(const std::string& word, std::string& outText)
{
    const ManualRow* row = FindCommandRow(word);
    if (!row)
        row = FindConstantRow(word);

    if (!row)
        return false;

    outText = row->name;

    if (row->alt)
        outText += std::string(" / ") + row->alt;

    if (row->args[0] != '\0')
        outText += std::string(" ") + row->args;

    outText += "\n" + GetRowSummary(*row);

    return true;
}

bool IsSeqFoldHeader(const char* begin, const char* end)
{
    const char* p = begin;

    while (p < end && (*p == ' ' || *p == '\t'))
        p++;

    if (p == end || !::isalpha(static_cast<unsigned char>(*p)))
        return false;

    while (p < end && IsIdentifierChar(*p))
        p++;

    while (p < end && (*p == ' ' || *p == '\t'))
        p++;

    return p < end && *p == ':';
}

bool FindSeqSymbol(const char* begin, const char* end, const char*& outBegin, const char*& outEnd)
{
    const char* p = begin;

    while (p < end && (*p == ' ' || *p == '\t'))
        p++;

    if (p == end || !IsIdentifierStart(*p))
        return false;

    outBegin = p;

    while (p < end && IsIdentifierChar(*p))
        p++;

    outEnd = p;

    while (p < end && (*p == ' ' || *p == '\t'))
        p++;

    return p < end && *p == ':';
}

std::string GetSeqStatement(const std::string& line)
{
    const std::string::size_type labelEnd = line.rfind(':');
    const std::string::size_type begin = line.find_first_not_of(" \t", labelEnd == std::string::npos ? 0 : labelEnd + 1);

    return begin == std::string::npos ? std::string() : line.substr(begin);
}

void CompleteSeqWord(const std::string& lineBefore, const std::string&, std::vector<TextEditor::Completion>& out, bool& outAddSymbols)
{
    const std::string statement = GetSeqStatement(lineBefore);

    if (statement.empty())
    {
        for (const ManualRow& row : cRows)
        {
            if (row.kind != RowKind::Command)
                continue;

            TextEditor::Completion item;
            item.mLabel = row.name;
            item.mDetail = row.args;
            item.mDocumentation = GetRowSummary(row);
            item.mIcon = IsNoteName(row.name) ? ICON_LC_MUSIC : ICON_LC_SQUARE_FUNCTION;
            item.mColorIndex = IsNoteName(row.name) ? TextEditor::PaletteIndex::KnownIdentifier : TextEditor::PaletteIndex::Keyword;
            out.push_back(item);

            if (!row.alt)
                continue;

            item.mLabel = row.alt;
            out.push_back(item);
        }

        return;
    }

    const std::string command = statement.substr(0, statement.find_first_of(" \t"));
    const ManualRow* row = FindCommandRow(command);

    if (row && (std::strcmp(row->name, "jump") == 0 || std::strcmp(row->name, "call") == 0 || std::strcmp(row->name, "opentrack") == 0))
    {
        outAddSymbols = true;
        return;
    }

    for (const ManualRow& constant : cRows)
    {
        bool isConstant = false;
        for (const char* prefix : cConstantPrefixes)
            isConstant |= constant.kind == RowKind::Syntax && std::strcmp(constant.name, prefix) == 0;

        if (!isConstant)
            continue;

        for (s32 i = 0; i < 16; i++)
        {
            TextEditor::Completion item;
            item.mLabel = constant.name + std::to_string(i);
            item.mDocumentation = constant.desc;
            item.mIcon = ICON_LC_VARIABLE;
            item.mColorIndex = TextEditor::PaletteIndex::PreprocIdentifier;
            out.push_back(item);
        }
    }

    if (row && std::strcmp(row->name, "mod_type") == 0)
    {
        for (const char* name : { "MOD_TYPE_PITCH", "MOD_TYPE_VOLUME", "MOD_TYPE_PAN" })
        {
            TextEditor::Completion item;
            item.mLabel = name;
            item.mIcon = ICON_LC_VARIABLE;
            item.mColorIndex = TextEditor::PaletteIndex::PreprocIdentifier;
            out.push_back(item);
        }
    }
}

bool SignatureSeqWord(const std::string& lineBefore, TextEditor::Signature& out)
{
    const std::string statement = GetSeqStatement(lineBefore);
    const std::string::size_type commandEnd = statement.find_first_of(" \t");

    if (commandEnd == std::string::npos)
        return false;

    std::string command = statement.substr(0, commandEnd);

    const ManualRow* row = FindExactRow(command.ends_with("_if") ? command.substr(0, command.size() - 3) : command);

    if (!row || row->args[0] == '\0')
        return false;

    out.mName = command;
    out.mDocumentation = GetRowSummary(*row);

    const std::string args = row->args;
    for (std::string::size_type begin = 0; begin < args.size();)
    {
        std::string::size_type end = args.find(", ", begin);
        if (end == std::string::npos)
            end = args.size();

        out.mParameters.push_back(args.substr(begin, end - begin));
        begin = end + 2;
    }

    out.mActiveParameter = 0;
    for (std::string::size_type i = commandEnd; i < statement.size(); i++)
        out.mActiveParameter += statement[i] == ',' ? 1 : 0;

    return true;
}

void* SeqEditorSettingsReadOpen(ImGuiContext*, ImGuiSettingsHandler*, const char*)
{
    return &gSeqEditorSettings;
}

void SeqEditorSettingsReadLine(ImGuiContext*, ImGuiSettingsHandler*, void*, const char* line)
{
    SeqEditorSettings& settings = gSeqEditorSettings;

    int value = 0;
    f32 split = 0.0f;

    if (sscanf(line, "Manual=%d", &value) == 1)
        settings.showManual = value != 0;
    else if (sscanf(line, "AutoCompile=%d", &value) == 1)
        settings.autoCompile = value != 0;
    else if (sscanf(line, "Follow=%d", &value) == 1)
        settings.follow = value != 0;
    else if (sscanf(line, "Trail=%d", &value) == 1)
        settings.showTrail = value != 0;
    else if (sscanf(line, "Whitespace=%d", &value) == 1)
        settings.showWhitespace = value != 0;
    else if (sscanf(line, "Suggest=%d", &value) == 1)
        settings.suggest = value != 0;
    else if (sscanf(line, "Hints=%d", &value) == 1)
        settings.parameterHints = value != 0;
    else if (sscanf(line, "Minimap=%d", &value) == 1)
        settings.minimap = value != 0;
    else if (sscanf(line, "FollowTrack=%d", &value) == 1)
        settings.followTrack = ImClamp(value, 0, 15);
    else if (sscanf(line, "Split=%f", &split) == 1)
        settings.splitPos = ImClamp(split, 0.15f, 0.85f);
}

void SeqEditorSettingsWriteAll(ImGuiContext*, ImGuiSettingsHandler* handler, ImGuiTextBuffer* buf)
{
    const SeqEditorSettings& settings = gSeqEditorSettings;

    buf->appendf("[%s][Editor]\n", handler->TypeName);
    buf->appendf("Manual=%d\n", settings.showManual ? 1 : 0);
    buf->appendf("AutoCompile=%d\n", settings.autoCompile ? 1 : 0);
    buf->appendf("Follow=%d\n", settings.follow ? 1 : 0);
    buf->appendf("Trail=%d\n", settings.showTrail ? 1 : 0);
    buf->appendf("Whitespace=%d\n", settings.showWhitespace ? 1 : 0);
    buf->appendf("Suggest=%d\n", settings.suggest ? 1 : 0);
    buf->appendf("Hints=%d\n", settings.parameterHints ? 1 : 0);
    buf->appendf("Minimap=%d\n", settings.minimap ? 1 : 0);
    buf->appendf("FollowTrack=%d\n", settings.followTrack);
    buf->appendf("Split=%.3f\n\n", settings.splitPos);
}

bool RowPassesFilter(const ManualRow& row, const char* filter)
{
    std::string text = row.name;

    if (row.alt)
        text.append(" ").append(row.alt);

    text.append(" ").append(row.desc);

    if (row.range)
        text.append(" ").append(row.range);

    return ImGui::TextMatchesQuery(text.c_str(), filter);
}

f32 CalcNameColumnWidth()
{
    static f32 sWidth = 0.0f;
    static f32 sFontSize = 0.0f;

    if (sFontSize == ImGui::GetFontSize())
        return sWidth;

    sFontSize = ImGui::GetFontSize();
    sWidth = 0.0f;

    const f32 space = ImGui::CalcTextSize(" ").x;

    for (const ManualRow& row : cRows)
    {
        if (row.kind == RowKind::Section)
            continue;

        f32 width = ImGui::CalcTextSize(row.name).x;

        if (row.args[0] != '\0')
            width += space + ImGui::CalcTextSize(row.args).x;

        if (row.alt)
            width = ImMax(width, ImGui::CalcTextSize(row.alt).x);

        sWidth = ImMax(sWidth, width);
    }

    return sWidth;
}

void DrawManualRow(const ManualRow& row, const ImVec4& nameColor, bool stacked)
{
    if (!stacked)
    {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
    }

    ImGui::TextColored(nameColor, "%s", row.name);

    if (row.args[0] != '\0')
    {
        ImGui::SameLine(0.0f, ImGui::CalcTextSize(" ").x);
        ImGui::TextUnformatted(row.args);
    }

    if (row.alt && stacked)
    {
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::TextDisabled(" / ");
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::TextColored(nameColor, "%s", row.alt);
    }
    else if (row.alt)
    {
        ImGui::TextColored(nameColor, "%s", row.alt);
    }

    if (stacked)
        ImGui::Indent();
    else
        ImGui::TableSetColumnIndex(1);

    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(row.desc);

    if (row.range)
        ImGui::TextDisabled("%s", row.range);

    ImGui::PopTextWrapPos();

    if (stacked)
        ImGui::Unindent();
}

}

SeqEditorSettings gSeqEditorSettings;

void RegisterSeqEditorSettings()
{
    ImGuiSettingsHandler handler;
    handler.TypeName = "Sequence";
    handler.TypeHash = ImHashStr("Sequence");
    handler.ReadOpenFn = SeqEditorSettingsReadOpen;
    handler.ReadLineFn = SeqEditorSettingsReadLine;
    handler.WriteAllFn = SeqEditorSettingsWriteAll;

    ImGui::AddSettingsHandler(&handler);
}

const TextEditor::LanguageDefinition& GetSeqLanguageDefinition()
{
    static TextEditor::LanguageDefinition sLangDef;
    static bool sInited = false;

    if (sInited)
        return sLangDef;

    sInited = true;

    sLangDef.mName = messages::sequence::cLanguageName;
    sLangDef.mTokenize = &TokenizeSeq;
    sLangDef.mDescribe = &DescribeSeqWord;
    sLangDef.mIsFoldHeader = &IsSeqFoldHeader;
    sLangDef.mFindSymbol = &FindSeqSymbol;
    sLangDef.mComplete = &CompleteSeqWord;
    sLangDef.mSignature = &SignatureSeqWord;
    sLangDef.mHeaderActionIcon = ICON_LC_PLAY;
    sLangDef.mSymbolIcon = ICON_LC_TAG;
    sLangDef.mFoldOpenIcon = ICON_LC_CHEVRON_DOWN;
    sLangDef.mFoldClosedIcon = ICON_LC_CHEVRON_RIGHT;
    sLangDef.mSingleLineComment = ";";
    sLangDef.mCommentStart = "\x01\x01";
    sLangDef.mCommentEnd = "\x02\x02";
    sLangDef.mPreprocChar = '\0';
    sLangDef.mCaseSensitive = true;
    sLangDef.mAutoIndentation = true;

    return sLangDef;
}

const TextEditor::Palette& GetSeqPalette(bool dark)
{
    static TextEditor::Palette sPalette;

    const auto set = [](TextEditor::PaletteIndex index, const ImVec4& color)
    {
        sPalette[static_cast<s32>(index)] = ImGui::ColorConvertFloat4ToU32(color);
    };

    sPalette = dark ? TextEditor::GetDarkPalette() : TextEditor::GetLightPalette();

    if (dark)
    {
        sPalette[static_cast<s32>(TextEditor::PaletteIndex::Number)]  = IM_COL32(181, 206, 168, 255);
        sPalette[static_cast<s32>(TextEditor::PaletteIndex::Comment)] = IM_COL32(106, 153, 85, 255);
        sPalette[static_cast<s32>(TextEditor::PaletteIndex::CharLiteral)] = IM_COL32(214, 160, 120, 255);
    }
    else
    {
        sPalette[static_cast<s32>(TextEditor::PaletteIndex::Comment)] = IM_COL32(96, 128, 96, 255);
        sPalette[static_cast<s32>(TextEditor::PaletteIndex::CharLiteral)] = IM_COL32(176, 104, 48, 255);
    }

    // Surface of the theme
    const ImVec4* colors = ImGui::GetStyle().Colors;

    ImVec4 background = colors[ImGuiCol_ChildBg];
    background.w = 1.0f;

    ImVec4 text = colors[ImGuiCol_Text];
    text.w = 1.0f;

    set(TextEditor::PaletteIndex::Background, background);
    set(TextEditor::PaletteIndex::Cursor, text);
    set(TextEditor::PaletteIndex::Punctuation, text);
    set(TextEditor::PaletteIndex::Default, ImVec4(text.x, text.y, text.z, cSeqPlainTextAlpha));
    set(TextEditor::PaletteIndex::Identifier, ImVec4(text.x, text.y, text.z, cSeqPlainTextAlpha));
    ImVec4 selection = colors[ImGuiCol_TextSelectedBg];
    selection.w = ImMax(selection.w, cSeqSelectionAlpha);

    set(TextEditor::PaletteIndex::Selection, selection);
    set(TextEditor::PaletteIndex::LineNumber, colors[ImGuiCol_TextDisabled]);
    set(TextEditor::PaletteIndex::CurrentLineFill, ImVec4(text.x, text.y, text.z, cSeqCurrentLineAlpha));
    set(TextEditor::PaletteIndex::CurrentLineFillInactive, ImVec4(text.x, text.y, text.z, cSeqCurrentLineAlpha * 0.5f));
    set(TextEditor::PaletteIndex::CurrentLineEdge, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

    return sPalette;
}

void DrawSeqManual()
{
    static char sFilter[256] = "";
    static f32 sScrollY = 0.0f;

    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##SeqManualFilter", messages::sequence::cManualFilterHint, sFilter, IM_ARRAYSIZE(sFilter));

    if (!ImGui::BeginChild("SeqManualList", ImVec2(0.0f, 0.0f), false))
    {
        ImGui::EndChild();
        return;
    }

    // Shared scroll position
    const f32 scrollY = ImGui::GetScrollY();
    f32* seenScrollY = ImGui::GetStateStorage()->GetFloatRef(ImGui::GetID("##SharedScrollY"), -1.0f);
    const bool clamped = scrollY >= ImGui::GetScrollMaxY() && sScrollY > scrollY;

    if (sScrollY != *seenScrollY)
        ImGui::SetScrollY(sScrollY);
    else if (scrollY != sScrollY && !clamped)
        sScrollY = scrollY;

    *seenScrollY = sScrollY;

    const TextEditor::Palette& palette = GetSeqPalette(gThemeIsDark);
    const ImVec4 nameColor = ImGui::ColorConvertU32ToFloat4(palette[static_cast<s32>(TextEditor::PaletteIndex::Keyword)]);
    const f32 nameWidth = CalcNameColumnWidth();
    const bool stacked = ImGui::GetContentRegionAvail().x - nameWidth < ImGui::GetFontSize() * cManualMinDescWidth;

    const s32 rowNum = IM_ARRAYSIZE(cRows);
    bool anyRow = false;

    for (s32 i = 0; i < rowNum; i++)
    {
        const ManualRow& section = cRows[i];
        if (section.kind != RowKind::Section)
            continue;

        s32 end = i + 1;
        while (end < rowNum && cRows[end].kind != RowKind::Section)
            end++;

        const bool sectionPasses = ImGui::TextMatchesQuery(section.name, sFilter);

        bool anyInSection = false;
        for (s32 j = i + 1; j < end; j++)
            anyInSection |= sectionPasses || RowPassesFilter(cRows[j], sFilter);

        if (!anyInSection)
            continue;

        anyRow = true;

        ImGui::SeparatorText(section.name);

        if (stacked)
        {
            for (s32 j = i + 1; j < end; j++)
            {
                if (sectionPasses || RowPassesFilter(cRows[j], sFilter))
                    DrawManualRow(cRows[j], nameColor, true);
            }

            continue;
        }

        ImGui::PushID(i);
        if (ImGui::BeginTable("##Rows", 2, ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_NoPadOuterX))
        {
            ImGui::TableSetupColumn("##Name", ImGuiTableColumnFlags_WidthFixed, nameWidth);
            ImGui::TableSetupColumn("##Desc", ImGuiTableColumnFlags_WidthStretch);

            for (s32 j = i + 1; j < end; j++)
            {
                if (sectionPasses || RowPassesFilter(cRows[j], sFilter))
                    DrawManualRow(cRows[j], nameColor, false);
            }

            ImGui::EndTable();
        }
        ImGui::PopID();
    }

    if (!anyRow)
        ImGui::TextDisabled("%s", messages::sequence::cManualNoMatch);

    ImGui::EndChild();
}
