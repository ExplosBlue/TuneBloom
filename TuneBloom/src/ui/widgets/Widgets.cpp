#define IMGUI_DEFINE_MATH_OPERATORS
#include <ui/widgets/Widgets.h>

#include <ui/items/ItemColumns.h>
#include <ui/widgets/Knob.h>
#include <ui/Messages.h>
#include <ui/widgets/Properties.h>
#include <ui/settings/Theme.h>
#include <ui/file/UnsavedChanges.h>
#include <ui/widgets/WindowAnimation.h>

#include <libs/IconsLucide.h>
#include <imgui/imgui_custom.h>
#include <imgui/imgui_internal.h>

#include <filedevice/seadPath.h>

#include <snd/snd_Util.h>

void NormalizePathSlashes(sead::BufferedSafeString* path)
{
    for (s32 i = 0; i < path->calcLength(); i++)
        if (path->getBuffer()[i] == '\\')
            path->getBuffer()[i] = '/';
}

void GetFileStem(sead::BufferedSafeString* out, const sead::SafeString& path)
{
    sead::Path::getFileName(out, path);

    s32 dotPos = out->rfindIndex(".");
    if (dotPos != -1)
        out->trim(dotPos);
}

static constexpr f32 cRowButtonEm        = 1.15f;
static constexpr f32 cRowButtonIconEm    = 0.8f;
static constexpr f32 cIconButtonLitHover = 0.15f;
static constexpr f32 cVersionFieldEm     = 2.0f;

static ImFont* GetIconFont(f32 size)
{
    for (ImFont* font : ImGui::GetIO().Fonts->Fonts)
    {
        if (font->FontSize == size && font->FindGlyphNoFallback(ICON_MIN_LC))
            return font;
    }

    return ImGui::GetFont();
}

bool DrawIconButton(const char* id, const char* icon, const ImVec2& itemSize, f32 frameSize, f32 iconSize, bool lit, ImGuiButtonFlags flags)
{
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 min(ImFloor(pos.x + (itemSize.x - frameSize) * 0.5f), ImFloor(pos.y + (itemSize.y - frameSize) * 0.5f));
    const ImVec2 max(min.x + frameSize, min.y + frameSize);

    const bool pressed = ImGui::InvisibleButton(id, itemSize, flags);
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();

    const f32 hoverRatio = ImGui::GetHoverRatio(ImGui::GetItemID() + 1, hovered || held);

    ImVec4 color = held ? ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive) : ImLerp(ImGui::GetStyleColorVec4(ImGuiCol_Button), ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered), hoverRatio);

    if (lit)
    {
        color = gAccentColor;
        color.w = 1.0f;

        if (held)
            color = ImLerp(color, ImVec4(0.0f, 0.0f, 0.0f, 1.0f), cIconButtonLitHover);
        else
            color = ImLerp(color, ImVec4(1.0f, 1.0f, 1.0f, 1.0f), cIconButtonLitHover * hoverRatio);
    }

    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImGui::AddRectFilledRaised(draw, min, max, ImGui::GetColorU32(color), ImGui::GetStyle().FrameRounding);

    ImFont* font = GetIconFont(iconSize);
    const ImVec2 textSize = font->CalcTextSizeA(iconSize, FLT_MAX, 0.0f, icon);

    draw->AddText(font, iconSize, ImVec2(ImFloor(min.x + (frameSize - textSize.x) * 0.5f), ImFloor(min.y + (frameSize - textSize.y) * 0.5f)), ImGui::GetColorU32(ImGuiCol_Text), icon);

    return pressed;
}

f32 GetRowButtonSize()
{
    return ImFloor(ImGui::GetFontSize() * cRowButtonEm);
}

static bool DrawRowButton(u32 id, const char* icon, bool lit)
{
    return DrawIconButton(sead::FormatFixedSafeString<32>("###%u", id).cstr(), icon, GetItemActionCellSize(), GetRowButtonSize(), ImFloor(ImGui::GetFontSize() * cRowButtonIconEm), lit);
}

bool DrawRowPlayButton(u32 id, const char* playbackError, bool playing)
{
    ImGui::BeginDisabled(playbackError != nullptr);

    const bool pressed = DrawRowButton(id, ICON_LC_PLAY, playing);

    ImGui::EndDisabled();

    if (playbackError)
        SetDisabledTooltip(playbackError);

    return pressed;
}

bool DrawTableRowSelectable(const char* id, bool selected, bool* outHovered)
{
    const ImU32 rowColor        = ImGui::GetColorU32(ImGuiCol_Header);
    const ImU32 rowHoveredColor = ImGui::GetColorU32(ImGuiCol_HeaderHovered);
    const ImU32 rowActiveColor  = ImGui::GetColorU32(ImGuiCol_HeaderActive);

    ImGui::PushStyleColor(ImGuiCol_Header,        IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive,  IM_COL32(0, 0, 0, 0));

    const bool rowPressed = ImGui::Selectable(id, selected, ImGuiSelectableFlags_SpanAllColumns);

    ImGui::PopStyleColor(3);

    const ImGuiID hoveredId = ImGui::GetHoveredID();
    const bool rowHovered = ImGui::IsItemHovered() && (hoveredId == 0 || hoveredId == ImGui::GetItemID());

    const ImGuiID rowFadeId = ImGui::GetItemID() + 1;
    const f32 rowHoverRatio = ImGui::GetHoverRatio(rowFadeId, rowHovered);

    if (rowHoverRatio > 0.0f || selected)
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, rowHovered && ImGui::IsItemActive() ? rowActiveColor : selected ? rowColor : ImGui::GetHoverColorU32(rowFadeId, rowHovered, 0, rowHoveredColor));

    *outHovered = rowHovered;

    return rowPressed;
}

bool DrawRowOpenButton(u32 id, const char* tooltip, const char* disabledReason)
{
    ImGui::BeginDisabled(disabledReason != nullptr);

    const bool pressed = DrawRowButton(id, ICON_LC_FILE_PEN, false);

    ImGui::EndDisabled();

    if (disabledReason)
        SetDisabledTooltip(disabledReason);
    else
        ImGui::SetItemTooltip("%s", tooltip);

    return pressed;
}

s32 GetItemWheelSteps()
{
    if (!ImGui::IsItemHovered())
        return 0;

    ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);

    return -static_cast<s32>(ImGui::GetIO().MouseWheel);
}

bool ComboScroll(const char *label, int *current_item, const char *const items[], int items_count, int popup_max_height_in_items)
{
    const int oldItem = *current_item;

    ImGui::Combo(label, current_item, items, items_count, popup_max_height_in_items);

    const s32 wheelSteps = GetItemWheelSteps();
    if (wheelSteps != 0)
        *current_item = sead::MathCalcCommon<s32>::clamp2(0, *current_item + wheelSteps, items_count - 1);

    return *current_item != oldItem;
}

bool ComboScroll(const char* label, s32* current, const char* const items[], const char* const details[], s32 itemNum, const char* detail)
{
    const s32 oldItem = *current;
    const bool valid = *current >= 0 && *current < itemNum;

    if (ImGui::BeginCombo(label, valid ? items[*current] : ""))
    {
        for (s32 i = 0; i < itemNum; i++)
        {
            if (ImGui::Selectable(items[i], i == *current))
                *current = i;

            if (i == *current)
                ImGui::SetItemDefaultFocus();

            ListItemDetailTooltip(details[i]);
        }

        ImGui::EndCombo();
    }
    else
        DetailTooltip(detail);

    const s32 wheelSteps = GetItemWheelSteps();
    if (wheelSteps != 0)
        *current = sead::MathCalcCommon<s32>::clamp2(0, *current + wheelSteps, itemNum - 1);

    return *current != oldItem;
}

sead::FixedSafeString<24> FormatPanLabel(f32 pan)
{
    sead::FixedSafeString<24> out;

    if (pan > -0.005f && pan < 0.005f)
        out.copy(messages::sound::cPanCentered);
    else if (pan < 0.0f)
        out.format(messages::sound::cPanLeftFormat, static_cast<s32>(-pan * 100.0f + 0.5f));
    else
        out.format(messages::sound::cPanRightFormat, static_cast<s32>(pan * 100.0f + 0.5f));

    return out;
}

sead::FixedSafeString<24> FormatPanLabel(u8 pan, u8 min, u8 center, u8 max)
{
    f32 normalized;

    if (pan == center)
        normalized = 0.0f;
    else if (pan < center)
        normalized = -static_cast<f32>(center - pan) / static_cast<f32>(center - min);
    else
        normalized = static_cast<f32>(pan - center) / static_cast<f32>(max - center);

    return FormatPanLabel(normalized);
}

static const char* ArchiveVersionRangeStr(bool isBcsar)
{
    return isBcsar ? "2.0.0 - 2.3.2" : "1.0.0 - 2.x";
}

static bool VersionField(u8* value, u8 maxValue)
{
    const u8 minValue = 0;

    ImGui::SetNextItemWidth(ImFloor(ImGui::GetFontSize() * cVersionFieldEm));

    return DragField("##value", ImGuiDataType_U8, value, cPropertyDragSpeed, &minValue, &maxValue, "%d", ImGuiSliderFlags_AlwaysClamp);
}

bool DrawVersionUI(u32 *versionPtr, ArchiveFormat format, bool showLabel)
{
    const bool isBcsar = (format == ArchiveFormat::BCSAR);
    const bool highByteMajor = isBcsar || nw::snd::internal::Util::IsHighByteMajorVersion(*versionPtr);
    const u32 shift[3] = {highByteMajor ? 24u : 16u, highByteMajor ? 16u : 8u, highByteMajor && !isBcsar ? 8u : 0u};

    const u32 version = *versionPtr;
    u8 field[3] = {
        (u8)((version >> shift[0]) & 0xFF),
        (u8)((version >> shift[1]) & 0xFF),
        (u8)((version >> shift[2]) & 0xFF),
    };

    bool updated = false;

    for (int i = 0; i < 3; i++)
    {
        if (i > 0)
        {
            ImGui::SameLine(0.0f, 6.0f);
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled(".");
            ImGui::SameLine(0.0f, 6.0f);
        }

        ImGui::PushID(i);

        if (VersionField(&field[i], 9))
            updated = true;

        ImGui::PopID();
    }

    if (showLabel)
    {
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(messages::project::cVersion);
    }

    if (ImGui::BeginItemTooltip())
    {
        ImGui::Text(messages::project::cVersionRangeFormat, ArchiveVersionRangeStr(isBcsar));
        ImGui::TextDisabled("%s", messages::project::cVersionEditDetail);
        ImGui::EndTooltip();
    }

    if (updated)
        *versionPtr = ((u32)field[0] << shift[0]) | ((u32)field[1] << shift[1]) | ((u32)field[2] << shift[2]);

    return updated;
}

bool DrawInnerVersionUI(u32 *versionPtr, u32 versionByteNum)
{
    const ImU8 stepU8 = 1;

    bool updated = false;
    for (u32 i = 0; i < versionByteNum; i++)
    {
        ImGui::PushID(i);
        ImGui::SetNextItemWidth(80.0f);

        u32 version = *versionPtr;
        u32 versionByteOffset = ((versionByteNum - 1) - i) * 0x8;

        const char *label = (i + 1 == versionByteNum) ? messages::innerfile::cVersion : "";

        u8 versionByte = (version >> versionByteOffset) & 0xFF;
        if (ImGui::InputScalar(label, ImGuiDataType_U8, &versionByte, &stepU8))
        {
            *versionPtr = (version & ~(0xFF << versionByteOffset)) | (versionByte << versionByteOffset);
            updated = true;
        }

        if (i + 1 < versionByteNum)
            ImGui::SameLine();

        ImGui::PopID();
    }

    return updated;
}

void CenteredText(const char* text, const ImVec2& sizeArg)
{
    ImVec2 textSize = ImGui::CalcTextSize(text);

    if (sizeArg.x != -1)
        ImGui::SetCursorPosX((sizeArg.x / 2.0f) - (textSize.x / 2.0f));

    if (sizeArg.y != -1)
        ImGui::SetCursorPosY((sizeArg.y / 2.0f) - (textSize.y / 2.0f));

    ImGui::Text("%s", text);
}

void CenteredTextWrapped(const char* text, f32 width)
{
    const f32 left = ImGui::GetCursorPosX();
    const char* end = text + strlen(text);
    ImFont* font = ImGui::GetFont();
    const f32 scale = ImGui::GetFontSize() / font->FontSize;

    while (text < end)
    {
        const char* lineEnd = font->CalcWordWrapPositionA(scale, text, end, width);
        if (lineEnd == text)
            lineEnd++;

        const char* textEnd = lineEnd;
        while (textEnd > text && textEnd[-1] == ' ')
            textEnd--;

        ImGui::SetCursorPosX(left + ImFloor((width - ImGui::CalcTextSize(text, textEnd).x) * 0.5f));
        ImGui::TextUnformatted(text, textEnd);

        text = lineEnd;
        while (text < end && *text == ' ')
            text++;
    }
}

std::string CutPathStart(const std::string& path, size_t maxLength)
{
    const size_t markLength = strlen(messages::backup::cPathCutMark);

    if (path.size() <= maxLength || maxLength <= markLength)
        return path;

    return messages::backup::cPathCutMark + path.substr(path.size() - maxLength + markLength);
}

// From ImGui Demo
bool IsItemHoveredAllowDisabled()
{
    return ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayNone);
}

bool IsItemHoveredForDetail()
{
    return ImGui::IsItemHovered(ImGuiHoveredFlags_Stationary | ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay);
}

void SetDisabledTooltip(const char* reason)
{
    if (IsItemHoveredAllowDisabled())
    {
        ImGui::SetTooltip("%s", reason);
    }
}

static constexpr f32 cDetailTooltipWrapEm = 32.0f;

static void ShowDetailTooltip(const char* detail, const ImVec2* pos, const ImVec2& pivot)
{
    if (!detail || ImGui::IsItemActive())
        return;

    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_Stationary | ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay | ImGuiHoveredFlags_AllowWhenDisabled))
        return;

    char name[16];
    ImFormatString(name, IM_ARRAYSIZE(name), "##Tooltip_%02d", GImGui->TooltipOverrideCount);
    const ImGuiWindow* tooltip = ImGui::FindWindowByName(name);
    const bool below = tooltip && tooltip->Active;

    if (pos && !below)
        ImGui::SetNextWindowPos(*pos, ImGuiCond_Always, pivot);

    const ImGuiNextItemData nextItemData = GImGui->NextItemData;

    ImGui::BeginTooltip();

    if (below)
        ImGui::Separator();

    ImGui::PushTextWrapPos(ImGui::GetFontSize() * cDetailTooltipWrapEm);
    ImGui::TextUnformatted(detail);
    ImGui::PopTextWrapPos();

    ImGui::EndTooltip();

    GImGui->NextItemData = nextItemData;
}

void DetailTooltip(const char* detail)
{
    ShowDetailTooltip(detail, nullptr, ImVec2(0.0f, 0.0f));
}

void ListItemDetailTooltip(const char* detail)
{
    const ImGuiWindow* list = ImGui::GetCurrentWindow();
    const ImGuiViewport* viewport = list->Viewport;
    const f32 gap = ImGui::GetStyle().ItemSpacing.y;
    const f32 spaceBelow = viewport->Pos.y + viewport->Size.y - (list->Pos.y + list->Size.y);
    const f32 spaceAbove = list->Pos.y - viewport->Pos.y;
    const bool right = list->Pos.x + list->Size.x * 0.5f > viewport->Pos.x + viewport->Size.x * 0.5f;
    const bool below = spaceBelow >= spaceAbove;

    const ImVec2 pos(right ? list->Pos.x + list->Size.x : list->Pos.x, below ? list->Pos.y + list->Size.y + gap : list->Pos.y - gap);
    ShowDetailTooltip(detail, &pos, ImVec2(right ? 1.0f : 0.0f, below ? 0.0f : 1.0f));
}

void HelpMarker(const char* desc)
{
    ImGui::TextDisabled(messages::popup::cDetailMarker);
    if (ImGui::BeginItemTooltip())
    {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(desc);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void InnerFile::drawUI()
{
    {
        static const char* sEndianTypes[] = {
            messages::innerfile::cBigEndian,
            messages::innerfile::cLittleEndian
        };

        sead::Endian::Types endian = mEndian;
        if (ComboScroll(messages::innerfile::cByteOrder, (s32*)&endian, sEndianTypes, IM_ARRAYSIZE(sEndianTypes)))
        {
            mEndian = endian;
            SetUnsavedChanges(true);
        }
    }

    {
        CenteredTextX(messages::innerfile::cVersion);

        u32 version = mVersion;
        if (DrawInnerVersionUI(&version, GetArchiveVersionByteCount(mFormat)))
        {
            mVersion = version;
            SetUnsavedChanges(true);
        }
    }
}
