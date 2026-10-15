#include <ui/items/ItemBrowser.h>

#include <ui/widgets/Dialogs.h>
#include <ui/items/ItemColumns.h>
#include <ui/items/ItemSelector.h>
#include <ui/widgets/ListFilter.h>
#include <ui/Messages.h>
#include <ui/player/PlayerViews.h>
#include <ui/app/Shortcuts.h>
#include <ui/widgets/Widgets.h>

#include <bfsar/player/SoundPlayer.h>
#include <bfsar/files/WaveFile.h>
#include <bfsar/audio/WaveFileEditDecode.h>

#include <libs/IconsLucide.h>
#include <imgui/imgui_custom.h>
#include <imgui/imgui_internal.h>

#include <algorithm>
#include <vector>

namespace
{

struct Peak
{
    f32 min;
    f32 max;
};

struct BrowserTable
{
    std::vector<Item*>& rows;
    std::vector<Item*>& allRows;
    const ItemColumn* columns = nullptr;
    u32 columnCount = 0;
    f32 autoWidths[cItemColumnMax] = {};
    s32 rowNum = 0;
    s32 highlightRow = -1;
};

struct BrowserResult
{
    bool accept = false;
    bool acceptNone = false;
    bool close = false;
};

}

static constexpr f32 cWaveBrowserWidthEm  = 64.0f;
static constexpr f32 cWaveBrowserHeightEm = 36.0f;
static constexpr f32 cItemBrowserWidthEm  = 40.0f;
static constexpr f32 cItemBrowserHeightEm = 32.0f;
static constexpr f32 cDetailWidthEm       = 21.0f;
static constexpr f32 cDetailLabelEm       = 7.5f;
static constexpr f32 cWaveformHeightEm    = 7.0f;
static constexpr f32 cFooterGapEm         = 0.6f;
static constexpr s32 cPageRows            = 10;
static constexpr s32 cPeakNum             = 512;

static constexpr s32 cBrowserNameColumn       = 1;
static constexpr s32 cBrowserFirstValueColumn = 2;

static constexpr const char* cWavePopupId = "###WaveBrowser";
static constexpr const char* cItemPopupId = "###ItemBrowser";

static char sQuery[128] = "";
static Item* sHighlight = nullptr;
static const Item* sPeakWave = nullptr;
static Peak sPeaks[cPeakNum];
static bool sAutoPlay = false;
static bool sCaseSensitive = false;
static bool sFocusSearch = false;
static bool sScrollToHighlight = false;
static bool sRowsDirty = true;

static Item::ItemType GetListItemType(const Item::List& list)
{
    if (!list.front())
        return Item::ItemType::Invalid;

    return list.front()->val()->getItemType();
}

static const char* GetPopupId(Item::ItemType itemType)
{
    return itemType == Item::ItemType::WaveFile ? cWavePopupId : cItemPopupId;
}

static bool MatchesQuery(const Item* item)
{
    if (sQuery[0] == '\0')
        return true;

    return ImGui::TextMatchesQuery(item->getFormattedName().cstr(), sQuery, sCaseSensitive);
}

static void BuildRows(std::vector<Item*>& rows, const Item::List& list, const ImGuiTableSortSpecs* sortSpecs, const ItemColumn* columns, u32 columnCount)
{
    rows.clear();

    for (auto it = list.robustBegin(); it != list.robustEnd(); ++it)
    {
        Item* item = (*it).val();

        if (MatchesQuery(item))
            rows.push_back(item);
    }

    SortItemRows(&rows, sortSpecs, columns, columnCount);
}

static void BuildPeaks(const WaveFile& wave)
{
    for (Peak& peak : sPeaks)
        peak = { 0.0f, 0.0f };

    const DecodedPcm pcm = decodeWaveFileForEditing(wave);
    if (!pcm.isValid())
        return;

    for (s32 i = 0; i < cPeakNum; i++)
    {
        const u32 begin = static_cast<u32>(static_cast<u64>(pcm.sampleCount) * i / cPeakNum);
        const u32 end = std::max(begin + 1, static_cast<u32>(static_cast<u64>(pcm.sampleCount) * (i + 1) / cPeakNum));

        Peak peak = { 1.0f, -1.0f };

        for (const std::vector<float>& channel : pcm.channels)
        {
            for (u32 sample = begin; sample < end && sample < pcm.sampleCount; sample++)
            {
                peak.min = std::min(peak.min, channel[sample]);
                peak.max = std::max(peak.max, channel[sample]);
            }
        }

        if (peak.min > peak.max)
            peak = { 0.0f, 0.0f };

        sPeaks[i] = peak;
    }
}

static void StopPreview()
{
    if (sSoundPlayer.isActive() && sHighlight && sSoundPlayer.getPlayingWaveFile() == sHighlight)
        sSoundPlayer.fadeOutPlayers();
}

static void PlayPreview(const WaveFile& wave)
{
    sead::FixedSafeString<SoundPlayer::cPlaybackErrorLength> error;
    if (SoundPlayer::GetWaveFilePlaybackError(wave, &error))
        return;

    sSoundPlayer.playWaveFile(wave, -1, nullptr, 0, false);
}

static void SetHighlight(Item* item)
{
    if (item == sHighlight)
        return;

    StopPreview();
    sHighlight = item;

    if (item && sAutoPlay && item->getItemType() == Item::ItemType::WaveFile)
        PlayPreview(*static_cast<const WaveFile*>(item));
}

static void DrawWaveform(const WaveFile& wave, const ImVec2& size)
{
    if (sPeakWave != &wave)
    {
        BuildPeaks(wave);
        sPeakWave = &wave;
    }

    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    ImGui::Dummy(size);
    ImGui::RenderFrame(pos, ImVec2(pos.x + size.x, pos.y + size.y), ImGui::GetColorU32(ImGuiCol_FrameBg), true, style.FrameRounding);

    const f32 left = pos.x + style.FramePadding.x;
    const f32 width = size.x - style.FramePadding.x * 2.0f;
    const f32 midY = pos.y + size.y * 0.5f;
    const f32 halfHeight = size.y * 0.5f - style.FramePadding.y;

    if (wave.getIsLoop() && wave.getSampleCount() > 0)
    {
        const f32 loopStart = static_cast<f32>(wave.getOriginalLoopStartFrame()) / wave.getSampleCount();
        const f32 loopEnd = std::min(1.0f, static_cast<f32>(wave.getOriginalLoopEndFrame()) / wave.getSampleCount());

        draw->AddRectFilled(ImVec2(left + width * loopStart, pos.y + 1.0f), ImVec2(left + width * loopEnd, pos.y + size.y - 1.0f), ImGui::GetColorU32(ImGuiCol_CheckMark, 0.12f));
    }

    draw->AddLine(ImVec2(left, midY), ImVec2(left + width, midY), ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.4f));

    const ImU32 color = ImGui::GetColorU32(ImGuiCol_PlotLines);
    const s32 columns = std::max(1, static_cast<s32>(width));

    for (s32 x = 0; x < columns; x++)
    {
        const s32 begin = x * cPeakNum / columns;
        const s32 end = std::max(begin + 1, (x + 1) * cPeakNum / columns);

        Peak peak = sPeaks[begin];
        for (s32 i = begin + 1; i < end && i < cPeakNum; i++)
        {
            peak.min = std::min(peak.min, sPeaks[i].min);
            peak.max = std::max(peak.max, sPeaks[i].max);
        }

        const f32 top = midY - sead::Mathf::clamp2(-1.0f, peak.max, 1.0f) * halfHeight;
        const f32 bottom = midY - sead::Mathf::clamp2(-1.0f, peak.min, 1.0f) * halfHeight;

        draw->AddLine(ImVec2(left + x, top), ImVec2(left + x, std::max(bottom, top + 1.0f)), color);
    }
}

static void DetailRow(const char* label, const char* value)
{
    ImGui::TextDisabled("%s", label);
    ImGui::SameLine(ImGui::GetFontSize() * cDetailLabelEm);
    ImGui::TextUnformatted(value);
}

static void DrawWaveDetails()
{
    const f32 em = ImGui::GetFontSize();

    if (!sHighlight)
    {
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const ImVec2 textSize = ImGui::CalcTextSize(messages::browser::cNoSelection);

        ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + (avail.x - textSize.x) * 0.5f, ImGui::GetCursorPosY() + (avail.y - textSize.y) * 0.5f));
        ImGui::TextDisabled("%s", messages::browser::cNoSelection);
        return;
    }

    const WaveFile& wave = *static_cast<const WaveFile*>(sHighlight);

    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(wave.getNameOrNull().cstr());
    ImGui::PopTextWrapPos();
    ImGui::TextDisabled("%s %u", messages::item::cColumnId, wave.getId());

    ImGui::Spacing();
    DrawWaveform(wave, ImVec2(ImGui::GetContentRegionAvail().x, ImFloor(em * cWaveformHeightEm)));

    // Preview
    {
        sead::FixedSafeString<SoundPlayer::cPlaybackErrorLength> error;
        const bool canPlay = !SoundPlayer::GetWaveFilePlaybackError(wave, &error);
        const bool playing = sSoundPlayer.isActive() && sSoundPlayer.getPlayingWaveFile() == &wave;

        ImGui::BeginDisabled(!canPlay);

        if (PlayToggleButton(playing ? TransportGlyph::Stop : TransportGlyph::Play, "###Preview", ImVec2(ImGui::GetFrameHeight() * 1.5f, 0.0f), playing))
        {
            if (playing)
                StopPreview();
            else
                PlayPreview(wave);
        }

        ImGui::EndDisabled();

        if (!canPlay)
            ImGui::SetItemTooltip("%s", error.cstr());

        ImGui::SameLine();
        if (ImGui::Checkbox(messages::browser::cAutoPlay, &sAutoPlay) && sAutoPlay && canPlay)
            PlayPreview(wave);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    sead::FixedSafeString<24> length;
    FormatWaveFileLength(&wave, &length);

    sead::FixedSafeString<24> channels;
    FormatWaveFileChannels(&wave, &channels);

    sead::FixedSafeString<24> size;
    FormatItemSize(&size, wave.getFileSize());

    DetailRow(messages::common::cLength, length.cstr());
    DetailRow(messages::browser::cSamples, sead::FormatFixedSafeString<24>("%u", wave.getSampleCount()).cstr());
    DetailRow(messages::common::cSampleRate, sead::FormatFixedSafeString<24>(messages::unit::cHertzFormat, wave.getSampleRate()).cstr());
    DetailRow(messages::common::cChannels, channels.cstr());
    DetailRow(messages::common::cEncoding, WaveFile::sEncodingTypes[static_cast<s32>(wave.getEncoding())]);

    if (wave.getIsLoop())
        DetailRow(messages::common::cLoop, sead::FormatFixedSafeString<48>("%u - %u", wave.getOriginalLoopStartFrame(), wave.getOriginalLoopEndFrame()).cstr());
    else
        DetailRow(messages::common::cLoop, "-");

    DetailRow(messages::common::cSize, size.cstr());
    DetailRow(messages::browser::cUsedBy, sead::FormatFixedSafeString<24>("%d", wave.getReferences().size()).cstr());
}

static bool BeginBrowserTable(const Item::List& list, Item::ItemType itemType, const ImVec2& size, BrowserTable* table)
{
    table->columns = GetItemColumns(itemType, &table->columnCount);

    table->allRows.clear();

    for (auto it = list.robustBegin(); it != list.robustEnd(); ++it)
        if (it->val())
            table->allRows.push_back(it->val());

    f32 columnWidths[cItemColumnMax] = {};
    CalcItemColumnAutoWidths(table->columns, table->columnCount, table->allRows, table->autoWidths);

    const ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_SizingFixedFit |
                                  ImGuiTableFlags_Sortable | ImGuiTableFlags_SortTristate | ImGuiTableFlags_Resizable | ImGuiTableFlags_Hideable;

    PushItemTablePadding();

    if (!ImGui::BeginTable(sead::FormatFixedSafeString<24>("##BrowserValues%d", static_cast<s32>(itemType)).cstr(), cBrowserFirstValueColumn + table->columnCount, flags, size))
    {
        ImGui::PopStyleVar();
        return false;
    }

    ImGui::TableSetupScrollFreeze(0, 1);
    SetupItemIdColumn(itemType);
    ImGui::TableSetupColumn(messages::common::cName, ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoHide, 0.0f, static_cast<ImGuiID>(ItemColumnId::Name));
    SetupItemColumns(table->columns, table->columnCount);

    for (u32 i = 0; i < table->columnCount; i++)
        columnWidths[i] = GetItemColumnLayoutWidth(ImGui::GetCurrentTable(), cBrowserFirstValueColumn + i, table->autoWidths[i]);

    DrawItemHeadersRow(table->columns, table->columnCount, columnWidths);

    return true;
}

static void UpdateBrowserRows(const Item::List& list, BrowserTable* table)
{
    ImGuiTableSortSpecs* sortSpecs = ImGui::TableGetSortSpecs();
    if (sRowsDirty || (sortSpecs && sortSpecs->SpecsDirty) || table->rows.size() > static_cast<size_t>(list.size()))
    {
        BuildRows(table->rows, list, sortSpecs, table->columns, table->columnCount);
        sRowsDirty = false;

        if (sortSpecs)
            sortSpecs->SpecsDirty = false;
    }

    table->rowNum = static_cast<s32>(table->rows.size());
    table->highlightRow = -1;

    for (s32 i = 0; i < table->rowNum; i++)
    {
        if (table->rows[i] != sHighlight)
            continue;

        table->highlightRow = i;
        break;
    }
}

static void HandleBrowserKeys(BrowserTable* table)
{
    if (table->rowNum <= 0)
        return;

    s32 step = 0;

    if (shortcuts::Pressed(shortcuts::Action::SelectNext))
        step = 1;
    else if (shortcuts::Pressed(shortcuts::Action::SelectPrevious))
        step = -1;
    else if (shortcuts::Pressed(shortcuts::Action::SelectPageDown))
        step = cPageRows;
    else if (shortcuts::Pressed(shortcuts::Action::SelectPageUp))
        step = -cPageRows;

    if (step == 0)
        return;

    table->highlightRow = sead::MathCalcCommon<s32>::clamp2(0, table->highlightRow < 0 ? 0 : table->highlightRow + step, table->rowNum - 1);
    SetHighlight(table->rows[table->highlightRow]);
    sScrollToHighlight = true;
}

static void DrawBrowserRowName(Item* item, bool isCurrent, ImGuiID rowId, bool rowHovered)
{
    const f32 checkWidth = ImGui::CalcTextSize(ICON_LC_CHECK).x + ImGui::GetStyle().ItemSpacing.x;
    const sead::SafeString name = item->getNameOrNull();
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const f32 right = min.x + ImGui::GetContentRegionAvail().x - (isCurrent ? checkWidth : 0.0f);
    const f32 width = std::max(0.0f, right - min.x);

    const bool notLoaded = IsItemNotLoaded(item);

    if (notLoaded)
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));

    DrawSelectorName(rowId, name.cstr(), min, ImVec2(right, min.y + ImGui::GetFontSize()), rowHovered, true);

    if (notLoaded)
        ImGui::PopStyleColor();

    ImGui::Dummy(ImVec2(width, ImGui::GetFontSize()));

    if (isCurrent)
    {
        ImGui::SameLine();
        ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_CheckMark), ICON_LC_CHECK);
    }
}

static bool DrawBrowserRow(const BrowserTable& table, s32 row, const Item* current)
{
    Item* item = table.rows[row];
    bool accept = false;

    ImGui::PushID(item);
    ImGui::TableNextRow();

    ImGui::TableNextColumn();
    ImGui::PushStyleColor(ImGuiCol_Text, GetItemValueColor());
    const bool rowPressed = ImGui::Selectable(sead::FormatFixedSafeString<16>("%u", item->getId()).cstr(), item == sHighlight, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_DontClosePopups);
    ImGui::PopStyleColor();

    if (rowPressed)
    {
        SetHighlight(item);

        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            accept = true;
    }

    const ImGuiID rowId = ImGui::GetItemID();
    const bool rowHovered = ImGui::IsItemHovered();

    if (sScrollToHighlight && row == table.highlightRow)
    {
        ImGui::ScrollToItem(ImGuiScrollFlags_KeepVisibleCenterY);
        sScrollToHighlight = false;
    }

    if (ImGui::TableNextColumn())
        DrawBrowserRowName(item, item == current, rowId, rowHovered);

    DrawItemColumnCells(item, table.columns, table.columnCount);

    ImGui::PopID();

    return accept;
}

static bool DrawBrowserTable(const Item::List& list, Item::ItemType itemType, const Item* current, const ImVec2& size, s32* outRowNum)
{
    static std::vector<Item*> sRows;
    static std::vector<Item*> sAllRows;

    BrowserTable table{sRows, sAllRows};

    *outRowNum = 0;

    if (!BeginBrowserTable(list, itemType, size, &table))
        return false;

    UpdateBrowserRows(list, &table);
    HandleBrowserKeys(&table);

    bool accept = false;

    ImGuiListClipper clipper;
    clipper.Begin(table.rowNum);

    if (sScrollToHighlight && table.highlightRow >= 0)
        clipper.IncludeItemByIndex(table.highlightRow);

    while (clipper.Step())
    {
        for (s32 row = clipper.DisplayStart; row < clipper.DisplayEnd; row++)
        {
            if (DrawBrowserRow(table, row, current))
                accept = true;
        }
    }

    if (table.rowNum == 0)
    {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(cBrowserNameColumn);
        ImGui::TextDisabled("%s", messages::browser::cNoMatch);
    }

    sScrollToHighlight = false;

    EndItemTable(table.columns, table.columnCount, table.allRows, table.autoWidths);

    ImGui::PopStyleVar();

    *outRowNum = table.rowNum;

    return accept;
}

void OpenItemBrowser(const char* id, const Item::List& list, Item* current)
{
    sQuery[0] = '\0';
    sHighlight = current;
    sFocusSearch = true;
    sScrollToHighlight = true;
    sRowsDirty = true;

    ImGui::PushID(id);
    ImGui::OpenPopup(GetPopupId(GetListItemType(list)));
    ImGui::PopID();
}

bool IsItemBrowserOpen(const char* id)
{
    ImGui::PushID(id);
    const bool open = ImGui::IsPopupOpen(cWavePopupId) || ImGui::IsPopupOpen(cItemPopupId);
    ImGui::PopID();

    return open;
}

static void ForgetHighlight()
{
    StopPreview();
    sHighlight = nullptr;
    sPeakWave = nullptr;
}

static bool BeginBrowserPopup(Item::ItemType itemType, const char* customTitle)
{
    const bool isWave = itemType == Item::ItemType::WaveFile;
    const char* popupId = GetPopupId(itemType);

    if (!ImGui::IsPopupOpen(popupId))
        return false;

    const f32 em = ImGui::GetFontSize();
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 size(em * (isWave ? cWaveBrowserWidthEm : cItemBrowserWidthEm), em * (isWave ? cWaveBrowserHeightEm : cItemBrowserHeightEm));

    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(size.x, viewport->Size.x * 0.9f), std::min(size.y, viewport->Size.y * 0.9f)), ImGuiCond_FirstUseEver);

    sead::FixedSafeString<64> title;
    if (customTitle)
        title = customTitle;
    else
        title.format(messages::browser::cTitleFormat, GetItemTypeName(itemType));

    const sead::FormatFixedSafeString<128> popupName("%s %s%s", isWave ? ICON_LC_AUDIO_WAVEFORM : ICON_LC_LIST, title.cstr(), popupId);

    bool open = true;

    if (!BeginDialog(popupName.cstr(), &open, ImGuiWindowFlags_NoScrollbar))
    {
        if (!open)
            ForgetHighlight();

        return false;
    }

    return true;
}

static void DrawBrowserFooter(const Item::List& list, Item::ItemType itemType, s32 rowNum, bool allowNone, const char* acceptLabel, BrowserResult* result)
{
    const char* selectLabel = acceptLabel ? acceptLabel : messages::browser::cSelect;

    ImGui::AlignTextToFramePadding();
    PushItemTablePadding();
    DrawItemCount(rowNum, list.size(), GetItemTypeName(itemType));
    ImGui::PopStyleVar();

    ImGui::SameLine();
    if (allowNone)
    {
        BeginDialogFooter({ messages::common::cNone, selectLabel, messages::common::cCancel }, false);

        if (DialogButton(messages::common::cNone))
            result->acceptNone = true;
    }
    else
    {
        BeginDialogFooter({ selectLabel, messages::common::cCancel }, false);
    }

    ImGui::BeginDisabled(!sHighlight);
    if (DialogButton(selectLabel))
        result->accept = true;
    ImGui::EndDisabled();

    if (DialogButton(messages::common::cCancel))
        result->close = true;
}

static bool ApplyBrowserResult(BrowserResult* result, Item** itemPtr)
{
    bool changed = false;

    if (result->accept && sHighlight)
    {
        changed = *itemPtr != sHighlight;
        *itemPtr = sHighlight;
        result->close = true;
    }
    else if (result->acceptNone)
    {
        changed = *itemPtr != nullptr;
        *itemPtr = nullptr;
        result->close = true;
    }

    return changed;
}

static bool DrawBrowserPopup(const Item::List& list, Item::ItemType itemType, Item** itemPtr, bool allowNone, const char* customTitle, const char* acceptLabel)
{
    const bool isWave = itemType == Item::ItemType::WaveFile;
    const ImGuiStyle& style = ImGui::GetStyle();
    const f32 em = ImGui::GetFontSize();

    if (!BeginBrowserPopup(itemType, customTitle))
        return false;

    BrowserResult result;

    if (ListSearchField("##Query", sQuery, sizeof(sQuery), 0.0f, &sCaseSensitive, sFocusSearch))
    {
        sRowsDirty = true;
        sScrollToHighlight = true;
    }

    sFocusSearch = false;

    const f32 footerGap = ImFloor(em * cFooterGapEm);
    const f32 footerHeight = CalcDialogFooterHeight() + footerGap;
    const f32 detailWidth = isWave ? ImFloor(em * cDetailWidthEm) + style.ItemSpacing.x : 0.0f;
    const f32 bodyHeight = std::max(em * 6.0f, ImGui::GetContentRegionAvail().y - footerHeight);

    s32 rowNum = 0;
    if (DrawBrowserTable(list, itemType, *itemPtr, ImVec2(std::max(em * 10.0f, ImGui::GetContentRegionAvail().x - detailWidth), bodyHeight), &rowNum))
        result.accept = true;

    if (isWave)
    {
        ImGui::SameLine();
        if (ImGui::BeginChild("##Details", ImVec2(0.0f, bodyHeight), ImGuiChildFlags_Border))
            DrawWaveDetails();
        ImGui::EndChild();
    }

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + footerGap - style.ItemSpacing.y);
    DrawBrowserFooter(list, itemType, rowNum, allowNone, acceptLabel, &result);

    if (sHighlight && shortcuts::Pressed(shortcuts::Action::ConfirmSelection))
        result.accept = true;

    if (shortcuts::Pressed(shortcuts::Action::CloseDialog))
        result.close = true;

    const bool changed = ApplyBrowserResult(&result, itemPtr);

    if (result.close)
    {
        ForgetHighlight();
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();

    return changed;
}

bool DrawItemBrowser(const char* id, const Item::List& list, Item** itemPtr, bool allowNone, const char* title, const char* acceptLabel)
{
    ImGui::PushID(id);
    const bool changed = DrawBrowserPopup(list, GetListItemType(list), itemPtr, allowNone, title, acceptLabel);
    ImGui::PopID();

    return changed;
}
