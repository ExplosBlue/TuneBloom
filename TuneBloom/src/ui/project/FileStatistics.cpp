#include <ui/project/ProjectViews.h>

#include <ui/widgets/ContextMenu.h>
#include <ui/file/File.h>
#include <ui/items/ItemColumns.h>
#include <ui/widgets/ListFilter.h>
#include <ui/Messages.h>
#include <ui/widgets/PanelLayout.h>
#include <ui/items/Selection.h>
#include <ui/settings/Theme.h>

#include <bfsar/files/WaveFile.h>

#include <imgui/imgui_custom.h>
#include <imgui/imgui_internal.h>

#include <algorithm>
#include <vector>

namespace
{

enum class WaveUse
{
    Stream,
    WaveSound,
    Instrument,
    Unused,

    Num
};

struct StatBlock
{
    WaveFile* wave;
    u32 size;
    ImRect rect;
};

struct StatGroup
{
    u64 size;
    std::vector<StatBlock> blocks;
};

struct StatRegion
{
    s32 use;
    u64 size;
    ImRect rect;
};

}

static constexpr s32 cUseNum = static_cast<s32>(WaveUse::Num);
static constexpr s32 cUseAll = -1;

static constexpr f32 cBarHeightEm      = 0.6f;
static constexpr f32 cBarGap           = 2.0f;
static constexpr f32 cBarRounding      = 3.0f;
static constexpr f32 cChipEm           = 0.6f;
static constexpr f32 cChipRounding     = 2.0f;
static constexpr f32 cTileGapEm        = 1.5f;
static constexpr f32 cTileLineGapEm    = 0.2f;
static constexpr f32 cTileTintAlpha    = 0.35f;
static constexpr f32 cMapMinHeightEm   = 12.0f;
static constexpr f32 cGroupGap         = 4.0f;
static constexpr f32 cBlockGap         = 1.0f;
static constexpr f32 cBlockLineMinSize = 3.0f;
static constexpr f32 cBlockLineFade    = 6.0f;
static constexpr f32 cBlockPadX        = 5.0f;
static constexpr f32 cBlockPadY        = 3.0f;
static constexpr f32 cBlockLineGap     = 2.0f;
static constexpr f32 cBlockNameMinEm   = 3.0f;
static constexpr f32 cBlockMix         = 0.62f;
static constexpr f32 cBlockMixRange    = 0.16f;
static constexpr f32 cBlockHueRange    = 0.018f;
static constexpr f32 cBlockHoverMix    = 0.95f;
static constexpr f32 cBlockSizeAlpha   = 0.7f;
static constexpr f32 cHeaderMix        = 0.3f;
static constexpr f32 cHeaderMinWidthEm = 6.0f;
static constexpr f32 cHeaderMinBodyEm  = 2.0f;
static constexpr f32 cDimAlpha         = 0.35f;

static const char* cTileSizeSample  = messages::unit::cSizeSample;
static const char* cTileFilesSample = messages::item::cStatsFilesSample;
static const char* cTileShareSample = messages::item::cStatsShareSample;

static const char* const cUseNames[cUseNum] = {
    messages::common::cStream,
    messages::common::cWaveSound,
    messages::common::cInstrument,
    messages::item::cStatsUseUnused
};

static const ImVec4 cUseColorsDark[cUseNum] = {
    ImVec4(0.224f, 0.529f, 0.898f, 1.0f),
    ImVec4(0.851f, 0.349f, 0.149f, 1.0f),
    ImVec4(0.098f, 0.620f, 0.439f, 1.0f),
    ImVec4(0.537f, 0.529f, 0.506f, 1.0f)
};

static const ImVec4 cUseColorsLight[cUseNum] = {
    ImVec4(0.165f, 0.471f, 0.839f, 1.0f),
    ImVec4(0.922f, 0.408f, 0.204f, 1.0f),
    ImVec4(0.106f, 0.686f, 0.478f, 1.0f),
    ImVec4(0.537f, 0.529f, 0.506f, 1.0f)
};

static bool sShowStreamWaves = true;
static bool sCaseSensitive = false;
static char sQuery[128] = "";
static s32 sIsolatedUse = cUseAll;

static ImVec4 GetUseColor(s32 use)
{
    return gThemeIsDark ? cUseColorsDark[use] : cUseColorsLight[use];
}

static ImU32 MixUseColor(s32 use, f32 mix)
{
    return ImGui::GetColorU32(ImLerp(ImGui::GetStyleColorVec4(ImGuiCol_ChildBg), GetUseColor(use), mix));
}

static ImU32 CalcBlockColor(s32 use, f32 sizeRatio)
{
    const f32 shift = sizeRatio * 2.0f - 1.0f;

    const ImVec4 color = GetUseColor(use);
    f32 h, s, v;
    ImGui::ColorConvertRGBtoHSV(color.x, color.y, color.z, h, s, v);
    h = ImFmod(h + shift * cBlockHueRange + 1.0f, 1.0f);

    ImVec4 shaded(0.0f, 0.0f, 0.0f, color.w);
    ImGui::ColorConvertHSVtoRGB(h, s, v, shaded.x, shaded.y, shaded.z);

    return ImGui::GetColorU32(ImLerp(ImGui::GetStyleColorVec4(ImGuiCol_ChildBg), shaded, cBlockMix + shift * cBlockMixRange));
}

static WaveUse GetWaveUse(const WaveFile* wave)
{
    bool waveSound = false;
    bool instrument = false;

    for (auto it = wave->getReferences().robustBegin(); it != wave->getReferences().robustEnd(); ++it)
    {
        const ItemReference* ref = it->val();

        if (!ref || !ref->getOwner())
            continue;

        switch (ref->getOwner()->getItemType())
        {
            case Item::ItemType::StreamTrack:
                return WaveUse::Stream;

            case Item::ItemType::Sound:
                waveSound = true;
                break;

            case Item::ItemType::BankFileVelocityRegion:
                instrument = true;
                break;

            default:
                break;
        }
    }

    if (waveSound)
        return WaveUse::WaveSound;

    return instrument ? WaveUse::Instrument : WaveUse::Unused;
}

static f32 CalcShare(u64 size, u64 totalSize)
{
    return totalSize > 0 ? static_cast<f32>(static_cast<f64>(size) / static_cast<f64>(totalSize) * 100.0) : 0.0f;
}

static void FormatTotalSize(sead::BufferedSafeString* out, u64 size)
{
    if (size >= 1024ull * 1024ull * 1024ull)
        out->format(messages::unit::cGigabytesFormat, static_cast<f64>(size) / (1024.0 * 1024.0 * 1024.0));
    else
        FormatItemSize(out, static_cast<u32>(size));
}

template <typename T>
static void LayoutTreemap(T* nodes, size_t nodeNum, ImRect area)
{
    f64 totalSize = 0.0;
    for (size_t i = 0; i < nodeNum; i++)
        totalSize += static_cast<f64>(nodes[i].size);

    size_t begin = 0;

    while (begin < nodeNum)
    {
        const f32 width = area.GetWidth();
        const f32 height = area.GetHeight();

        if (width <= 0.0f || height <= 0.0f || totalSize <= 0.0)
        {
            for (size_t i = begin; i < nodeNum; i++)
                nodes[i].rect = ImRect(area.Min, area.Min);

            return;
        }

        const bool column = width >= height;
        const f32 side = column ? height : width;
        const f64 scale = static_cast<f64>(width) * height / totalSize;

        size_t end = begin;
        f64 rowSize = 0.0;
        f32 best = FLT_MAX;

        while (end < nodeNum)
        {
            const f64 size = rowSize + static_cast<f64>(nodes[end].size);
            const f32 thickness = static_cast<f32>(size * scale / side);
            const f32 longest = static_cast<f32>(static_cast<f64>(nodes[begin].size) * scale / thickness);
            const f32 shortest = static_cast<f32>(static_cast<f64>(nodes[end].size) * scale / thickness);
            const f32 worst = sead::Mathf::max(longest / thickness, thickness / shortest);

            if (end > begin && worst > best)
                break;

            best = worst;
            rowSize = size;
            end++;
        }

        const f32 thickness = static_cast<f32>(rowSize * scale / side);
        f32 pos = column ? area.Min.y : area.Min.x;

        for (size_t i = begin; i < end; i++)
        {
            const f32 length = static_cast<f32>(side * (static_cast<f64>(nodes[i].size) / rowSize));

            if (column)
                nodes[i].rect = ImRect(area.Min.x, pos, area.Min.x + thickness, pos + length);
            else
                nodes[i].rect = ImRect(pos, area.Min.y, pos + length, area.Min.y + thickness);

            pos += length;
        }

        if (column)
            area.Min.x += thickness;
        else
            area.Min.y += thickness;

        totalSize -= rowSize;
        begin = end;
    }
}

static void DrawSearchRow(f32 padding)
{
    bool menuPressed = false;

    SearchFieldExtra extra;
    extra.menuPressed = &menuPressed;
    extra.menuActive = !sShowStreamWaves;

    ListSearchField("##Search", sQuery, sizeof(sQuery), ImGui::GetContentRegionAvail().x - padding, &sCaseSensitive, false, &extra);

    if (menuPressed)
        ImGui::OpenPopup("##ListMenu");

    if (BeginContextMenu("##ListMenu"))
    {
        if (ContextMenuCheckItem(messages::item::cShowStreamWaves, sShowStreamWaves))
            sShowStreamWaves = !sShowStreamWaves;

        EndContextMenu();
    }
}

static void CollectGroups(StatGroup* groups, u64* totalSize, u32* fileNum)
{
    *totalSize = 0;
    *fileNum = 0;

    for (s32 i = 0; i < cUseNum; i++)
    {
        groups[i].size = 0;
        groups[i].blocks.clear();
    }

    for (Item* item : sBfsar.getWaveFileList())
    {
        WaveFile* wave = static_cast<WaveFile*>(item);

        if (!sShowStreamWaves && wave->getIsStreamExtended())
            continue;

        if (sQuery[0] != '\0' && !ImGui::TextMatchesQuery(wave->getFormattedName().cstr(), sQuery, sCaseSensitive))
            continue;

        StatBlock block;
        block.wave = wave;
        block.size = wave->getFileSize() > 0 ? wave->getFileSize() : 1;

        StatGroup& group = groups[static_cast<s32>(GetWaveUse(wave))];
        group.size += block.size;
        group.blocks.push_back(block);

        *totalSize += block.size;
        (*fileNum)++;
    }

    for (s32 i = 0; i < cUseNum; i++)
    {
        std::sort(groups[i].blocks.begin(), groups[i].blocks.end(), [](const StatBlock& a, const StatBlock& b)
        {
            return a.size > b.size;
        });
    }
}

static void DrawUseBar(const StatGroup* groups, u64 totalSize, s32 isolated, f32 width)
{
    const f32 height = ImFloor(ImGui::GetFontSize() * cBarHeightEm);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    s32 segmentNum = 0;
    for (s32 i = 0; i < cUseNum; i++)
        segmentNum += groups[i].size > 0 ? 1 : 0;

    if (segmentNum == 0)
        drawList->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), ImGui::GetColorU32(ImGuiCol_FrameBg), cBarRounding);

    const f32 fillWidth = width - cBarGap * (segmentNum - 1);
    f32 x = pos.x;
    s32 segment = 0;

    for (s32 i = 0; i < cUseNum; i++)
    {
        if (groups[i].size == 0)
            continue;

        const bool first = segment == 0;
        const bool last = ++segment == segmentNum;
        const f32 right = last ? pos.x + width : x + sead::Mathf::max(ImFloor(fillWidth * CalcShare(groups[i].size, totalSize) / 100.0f), cBarGap);

        ImVec4 color = GetUseColor(i);
        if (isolated != cUseAll && isolated != i)
            color.w *= cDimAlpha;

        const ImDrawFlags flags = (first ? ImDrawFlags_RoundCornersLeft : 0) | (last ? ImDrawFlags_RoundCornersRight : 0);

        drawList->AddRectFilled(ImVec2(x, pos.y), ImVec2(sead::Mathf::max(right, x + 1.0f), pos.y + height), ImGui::GetColorU32(color), cBarRounding, flags ? flags : ImDrawFlags_RoundCornersNone);

        x = right + cBarGap;
    }

    ImGui::Dummy(ImVec2(width, height));
}

static void DrawUseTiles(const StatGroup* groups, u64 totalSize, s32 isolated, f32 width)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const f32 em = ImGui::GetFontSize();
    const f32 chipSize = ImFloor(em * cChipEm);
    const f32 chipWidth = chipSize + style.ItemInnerSpacing.x;
    const f32 gap = em * cTileGapEm;

    sead::FixedSafeString<32> sizes[cUseNum];
    sead::FixedSafeString<32> files[cUseNum];
    sead::FixedSafeString<32> shares[cUseNum];
    const f32 sizeWidth = ImGui::CalcTextSize(cTileSizeSample).x;
    const f32 filesWidth = ImGui::CalcTextSize(cTileFilesSample).x;
    const f32 shareWidth = ImGui::CalcTextSize(cTileShareSample).x;

    f32 wideWidth = chipWidth + filesWidth + gap + shareWidth;
    f32 narrowWidth = sizeWidth + gap + shareWidth;

    for (s32 i = 0; i < cUseNum; i++)
    {
        const u32 fileNum = static_cast<u32>(groups[i].blocks.size());

        FormatTotalSize(&sizes[i], groups[i].size);
        files[i].format(fileNum == 1 ? messages::item::cStatsFilesOneFormat : messages::item::cStatsFilesFormat, fileNum);
        shares[i].format(messages::item::cStatsShareFormat, CalcShare(groups[i].size, totalSize));

        const f32 nameWidth = chipWidth + ImGui::CalcTextSize(cUseNames[i]).x;

        wideWidth = sead::Mathf::max(wideWidth, nameWidth + gap + sizeWidth);
        narrowWidth = sead::Mathf::max(narrowWidth, nameWidth);
    }

    const ImVec2 padding(ImFloor(em * cSoundPanelPadXEm), ImFloor(em * cSoundPanelPadYEm));
    const f32 tileExtra = padding.x * 2.0f + 1.0f;
    const bool wide = (wideWidth + tileExtra) * cUseNum <= width;
    const bool narrow = !wide && (narrowWidth + tileExtra) * cUseNum <= width;
    const s32 columnNum = wide || narrow ? cUseNum : ((wideWidth + tileExtra) * 2.0f <= width ? 2 : 1);
    const s32 lineNum = narrow ? 3 : 2;
    const f32 lineHeight = em + ImFloor(em * cTileLineGapEm);

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, padding);

    if (ImGui::BeginTable("##Uses", columnNum, ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInner | ImGuiTableFlags_SizingStretchSame, ImVec2(width, 0.0f)))
    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        const ImU32 textColor = ImGui::GetColorU32(ImGuiCol_Text);
        const ImU32 dimColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);
        const ImU32 valueColor = ImGui::GetColorU32(GetItemValueColor());

        for (s32 i = 0; i < cUseNum; i++)
        {
            ImGui::TableNextColumn();
            ImGui::PushID(i);

            const bool empty = groups[i].blocks.empty();
            const bool dim = empty || (isolated != cUseAll && isolated != i);
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            const ImVec2 size(ImGui::GetContentRegionAvail().x, lineHeight * (lineNum - 1) + em);
            const f32 right = pos.x + size.x;

            if (empty)
                ImGui::Dummy(size);
            else if (ImGui::InvisibleButton("##Use", size))
                sIsolatedUse = sIsolatedUse == i ? cUseAll : i;

            if (isolated == i)
                ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, ImGui::GetColorU32(ImGuiCol_Header, cTileTintAlpha));
            else if (!empty && ImGui::IsItemHovered())
                ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, ImGui::GetColorU32(ImGuiCol_HeaderHovered, cTileTintAlpha));

            ImVec4 chipColor = GetUseColor(i);
            if (dim)
                chipColor.w *= cDimAlpha;

            const f32 chipY = pos.y + ImFloor((em - chipSize) * 0.5f);

            drawList->AddRectFilled(ImVec2(pos.x, chipY), ImVec2(pos.x + chipSize, chipY + chipSize), ImGui::GetColorU32(chipColor), cChipRounding);

            const f32 shareX = right - ImGui::CalcTextSize(shares[i].cstr()).x;

            drawList->AddText(ImVec2(pos.x + chipWidth, pos.y), dim ? dimColor : textColor, cUseNames[i]);

            if (narrow)
            {
                drawList->AddText(ImVec2(pos.x, pos.y + lineHeight), dim ? dimColor : textColor, sizes[i].cstr());
                drawList->AddText(ImVec2(shareX, pos.y + lineHeight), dim ? dimColor : valueColor, shares[i].cstr());
                drawList->AddText(ImVec2(pos.x, pos.y + lineHeight * 2.0f), dimColor, files[i].cstr());
            }
            else
            {
                drawList->AddText(ImVec2(right - ImGui::CalcTextSize(sizes[i].cstr()).x, pos.y), dim ? dimColor : textColor, sizes[i].cstr());
                drawList->AddText(ImVec2(pos.x + chipWidth, pos.y + lineHeight), dimColor, files[i].cstr());
                drawList->AddText(ImVec2(shareX, pos.y + lineHeight), dim ? dimColor : valueColor, shares[i].cstr());
            }

            ImGui::PopID();
        }

        ImGui::EndTable();
    }

    ImGui::PopStyleVar();
}

static void DrawBlockName(ImDrawList* drawList, const ImVec2& pos, f32 width, const char* name, ImU32 color)
{
    if (ImGui::CalcTextSize(name).x <= width)
        drawList->AddText(pos, color, name);
    else
        DrawTextEnd(drawList, pos, pos.x + width, name, color);
}

static void DrawBlockText(ImDrawList* drawList, const StatBlock& block, const ImRect& rect)
{
    const f32 em = ImGui::GetFontSize();
    const f32 width = rect.GetWidth() - cBlockPadX * 2.0f;
    const f32 height = rect.GetHeight() - cBlockPadY * 2.0f;

    if (width < em * cBlockNameMinEm || height < em)
        return;

    const ImVec2 pos(rect.Min.x + cBlockPadX, rect.Min.y + cBlockPadY);

    DrawBlockName(drawList, pos, width, block.wave->getNameOrNull().cstr(), ImGui::GetColorU32(ImGuiCol_Text));

    if (height < em * 2.0f + cBlockLineGap)
        return;

    sead::FixedSafeString<32> sizeText;
    FormatItemSize(&sizeText, block.wave->getFileSize());

    if (ImGui::CalcTextSize(sizeText.cstr()).x <= width)
        drawList->AddText(ImVec2(pos.x, pos.y + em + cBlockLineGap), ImGui::GetColorU32(ImGuiCol_Text, cBlockSizeAlpha), sizeText.cstr());
}

static const StatBlock* DrawRegion(ImDrawList* drawList, StatGroup& group, const StatRegion& region, bool mapHovered)
{
    const f32 em = ImGui::GetFontSize();
    const ImVec2 mousePos = ImGui::GetIO().MousePos;

    ImRect rect(ImFloor(region.rect.Min), ImFloor(region.rect.Max));
    rect.Max.x -= cGroupGap;
    rect.Max.y -= cGroupGap;

    if (rect.GetWidth() < 1.0f || rect.GetHeight() < 1.0f)
        return nullptr;

    const f32 headerHeight = em + cBlockPadY * 2.0f;

    if (rect.GetWidth() >= em * cHeaderMinWidthEm && rect.GetHeight() >= headerHeight + em * cHeaderMinBodyEm)
    {
        const char* title = cUseNames[region.use];
        const f32 left = rect.Min.x + cBlockPadX;
        const f32 right = rect.Max.x - cBlockPadX;
        const f32 textY = rect.Min.y + cBlockPadY;

        sead::FixedSafeString<32> sizeText;
        FormatTotalSize(&sizeText, group.size);

        const f32 sizeWidth = ImGui::CalcTextSize(sizeText.cstr()).x;
        const bool showSize = ImGui::CalcTextSize(title).x + em + sizeWidth <= right - left;
        const f32 titleRight = showSize ? right - sizeWidth - em : right;

        drawList->AddRectFilled(rect.Min, ImVec2(rect.Max.x, rect.Min.y + headerHeight), MixUseColor(region.use, cHeaderMix));

        ImGui::RenderTextEllipsis(drawList, ImVec2(left, textY), ImVec2(titleRight, textY + em), titleRight, titleRight, title, nullptr, nullptr);

        if (showSize)
            drawList->AddText(ImVec2(right - sizeWidth, textY), ImGui::GetColorU32(GetItemValueColor()), sizeText.cstr());

        rect.Min.y += headerHeight + cBlockGap;
    }

    LayoutTreemap(group.blocks.data(), group.blocks.size(), ImRect(rect.Min, ImVec2(rect.Max.x + cBlockGap, rect.Max.y + cBlockGap)));

    const ImVec4 lineColor = ImGui::GetStyleColorVec4(ImGuiCol_ChildBg);
    const StatBlock* hoveredBlock = nullptr;
    u64 sizeAbove = 0;
    u64 sizeAboveEqual = 0;
    u32 lastSize = 0;

    for (const StatBlock& block : group.blocks)
    {
        if (block.size != lastSize)
        {
            sizeAboveEqual = sizeAbove;
            lastSize = block.size;
        }

        sizeAbove += block.size;

        const f32 sizeRatio = 1.0f - static_cast<f32>(sizeAboveEqual) / static_cast<f32>(group.size);

        ImRect blockRect(ImFloor(block.rect.Min), ImFloor(block.rect.Max));

        if (blockRect.GetWidth() < 1.0f || blockRect.GetHeight() < 1.0f)
            continue;

        const bool hovered = mapHovered && !hoveredBlock && block.rect.Contains(mousePos);

        if (hovered)
            hoveredBlock = &block;

        drawList->AddRectFilled(blockRect.Min, blockRect.Max, hovered ? MixUseColor(region.use, cBlockHoverMix) : CalcBlockColor(region.use, sizeRatio));

        const f32 minSize = sead::Mathf::min(blockRect.GetWidth(), blockRect.GetHeight());
        const f32 lineAlpha = sead::Mathf::clamp2(0.0f, (minSize - cBlockLineMinSize) / cBlockLineFade, 1.0f);

        if (lineAlpha > 0.0f)
        {
            const ImU32 color = ImGui::GetColorU32(ImVec4(lineColor.x, lineColor.y, lineColor.z, lineAlpha));

            drawList->AddRectFilled(ImVec2(blockRect.Max.x - cBlockGap, blockRect.Min.y), blockRect.Max, color);
            drawList->AddRectFilled(ImVec2(blockRect.Min.x, blockRect.Max.y - cBlockGap), ImVec2(blockRect.Max.x - cBlockGap, blockRect.Max.y), color);
        }

        blockRect.Max.x -= cBlockGap;
        blockRect.Max.y -= cBlockGap;

        if (hovered)
            drawList->AddRect(blockRect.Min, blockRect.Max, ImGui::GetColorU32(ImGuiCol_Text));

        DrawBlockText(drawList, block, blockRect);
    }

    return hoveredBlock;
}

static void DrawBlockTooltip(const StatBlock& block, s32 use, u64 totalSize)
{
    if (!ImGui::BeginTooltip())
        return;

    const f32 em = ImGui::GetFontSize();
    const f32 chipSize = ImFloor(em * cChipEm);

    ImGui::TextUnformatted(block.wave->getFormattedName().cstr());

    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const f32 chipY = pos.y + ImFloor((em - chipSize) * 0.5f);

    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(pos.x, chipY), ImVec2(pos.x + chipSize, chipY + chipSize), ImGui::GetColorU32(GetUseColor(use)), cChipRounding);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + chipSize + ImGui::GetStyle().ItemInnerSpacing.x);
    ImGui::TextUnformatted(cUseNames[use]);

    if (ImGui::BeginTable("##Values", 2, ImGuiTableFlags_SizingFixedFit))
    {
        u32 columnCount = 0;
        const ItemColumn* columns = GetItemColumns(Item::ItemType::WaveFile, &columnCount);

        for (u32 i = 0; i < columnCount; i++)
        {
            sead::FixedSafeString<128> text;
            columns[i].format(block.wave, &text);

            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", columns[i].title);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(text.cstr());
        }

        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s", messages::item::cStatsShare);
        ImGui::TableNextColumn();
        ImGui::Text(messages::item::cStatsShareDetailFormat, CalcShare(block.size, totalSize));

        ImGui::EndTable();
    }

    ImGui::EndTooltip();
}

static void DrawTreemap(StatGroup* groups, u64 totalSize, s32 isolated, const ImVec2& size)
{
    const ImVec2 pos = ImGui::GetCursorScreenPos();

    const bool pressed = ImGui::InvisibleButton("##Map", size);
    const bool mapHovered = ImGui::IsItemHovered();

    StatRegion regions[cUseNum];
    s32 regionNum = 0;

    for (s32 i = 0; i < cUseNum; i++)
    {
        if (groups[i].blocks.empty() || (isolated != cUseAll && isolated != i))
            continue;

        regions[regionNum].use = i;
        regions[regionNum].size = groups[i].size;
        regionNum++;
    }

    std::sort(regions, regions + regionNum, [](const StatRegion& a, const StatRegion& b)
    {
        return a.size > b.size;
    });

    LayoutTreemap(regions, regionNum, ImRect(pos, ImVec2(pos.x + size.x + cGroupGap, pos.y + size.y + cGroupGap)));

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const StatBlock* hoveredBlock = nullptr;
    s32 hoveredUse = cUseAll;

    for (s32 i = 0; i < regionNum; i++)
    {
        const StatBlock* block = DrawRegion(drawList, groups[regions[i].use], regions[i], mapHovered);

        if (block)
        {
            hoveredBlock = block;
            hoveredUse = regions[i].use;
        }
    }

    if (!hoveredBlock)
        return;

    DrawBlockTooltip(*hoveredBlock, hoveredUse, totalSize);

    if (pressed)
        SelectItem(hoveredBlock->wave);
}

static void DrawTotalLine(const StatGroup* groups, u64 totalSize, u32 fileNum, s32 isolated)
{
    if (isolated != cUseAll)
    {
        totalSize = groups[isolated].size;
        fileNum = static_cast<u32>(groups[isolated].blocks.size());
    }

    sead::FixedSafeString<32> sizeText;
    FormatTotalSize(&sizeText, totalSize);

    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetStyle().CellPadding.x);
    ImGui::TextDisabled(fileNum == 1 ? messages::item::cStatsTotalOneFormat : messages::item::cStatsTotalFormat, sizeText.cstr(), fileNum);

    if (isolated != cUseAll)
    {
        const sead::FormatFixedSafeString<48> note(messages::item::cStatsUseOnlyFormat, cUseNames[isolated]);

        ImGui::SameLine(0.0f, 0.0f);
        ImGui::TextDisabled(messages::item::cCountNoteFormat, note.cstr());
    }

    if (!sShowStreamWaves)
    {
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::TextDisabled(messages::item::cCountNoteFormat, messages::item::cStreamWavesHidden);
    }
}

void DrawFileStatisticsUI()
{
    static StatGroup sGroups[cUseNum];

    const ImGuiStyle& style = ImGui::GetStyle();
    const f32 padding = BeginListRow();

    DrawSearchRow(padding);

    u64 totalSize = 0;
    u32 fileNum = 0;

    CollectGroups(sGroups, &totalSize, &fileNum);

    const s32 isolated = sIsolatedUse != cUseAll && !sGroups[sIsolatedUse].blocks.empty() ? sIsolatedUse : cUseAll;

    if (padding > 0.0f)
        ImGui::Indent(padding);

    const f32 width = ImGui::GetContentRegionAvail().x - padding;

    DrawUseBar(sGroups, totalSize, isolated, width);
    DrawUseTiles(sGroups, totalSize, isolated, width);

    const f32 footerHeight = ImGui::GetTextLineHeightWithSpacing() + style.WindowPadding.y;
    const ImVec2 mapSize(width, sead::Mathf::max(ImGui::GetContentRegionAvail().y - footerHeight, ImGui::GetFontSize() * cMapMinHeightEm));

    if (fileNum == 0)
    {
        const ImVec2 pos = ImGui::GetCursorPos();

        ImGui::SetCursorPosX(pos.x + style.CellPadding.x);
        ImGui::TextDisabled("%s", messages::item::cStatsEmpty);

        ImGui::SetCursorPos(pos);
        ImGui::Dummy(mapSize);
    }
    else if (width >= 1.0f)
    {
        DrawTreemap(sGroups, totalSize, isolated, mapSize);
    }

    DrawTotalLine(sGroups, totalSize, fileNum, isolated);

    if (padding > 0.0f)
        ImGui::Unindent(padding);
}
