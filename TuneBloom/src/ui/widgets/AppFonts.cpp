#include <ui/widgets/AppFonts.h>

#include <filedevice/seadFileDeviceMgr.h>

#include <ui/settings/Config.h>
#include <ui/widgets/Dialogs.h>
#include <ui/widgets/Widgets.h>

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "libs/IconsLucide.h"

#include <cstring>

namespace
{

struct FontFile
{
    u8* data;
    s32 size;
};

struct LineMetrics
{
    f32 ascent;
    f32 capHeight;
};

struct FontFace
{
    const char* path;
    LineMetrics metrics;
    f32 scale;
    f32 rasterizerMultiply;
};

struct ScaledFont
{
    ImFont* font;
    f32 lineSize;
    f32 iconGap;
    bool isTabular;
};

}

static constexpr LineMetrics cLayoutMetrics = { 1120.0f / 1481.0f, 700.0f / 1481.0f };
static constexpr f32         cIconGapEm     = 612.0f / 1481.0f;

static constexpr FontFace cTextFace = { "main://fonts/Inter-Medium.ttf", { 1984.0f / 2478.0f, 1490.0f / 2478.0f }, 0.785f, 1.15f };
static constexpr FontFace cCodeFace = { "main://fonts/JetBrainsMono-Regular.ttf", { 1020.0f / 1320.0f, 730.0f / 1320.0f }, 0.85f, 1.0f };

static constexpr f32 cTitleFontSize   = 32.0f;
static constexpr f32 cIconFontSizes[] = { 20.0f, 16.0f, 22.0f };
static constexpr f32 cCodeFontSize    = 20.0f;
static constexpr f32 cIconSizeEm      = 0.9f;
static constexpr f32 cIconOffsetEm    = 0.2f;

static ImFont* sCodeFont = nullptr;

static FontFile LoadFontFile(const char* path)
{
    sead::FileDevice::LoadArg arg;
    arg.path = path;

    u8* data = sead::FileDeviceMgr::instance()->load(arg);

    return { data, static_cast<s32>(arg.read_size) };
}

static f32 CalcAscent(const LineMetrics& metrics, f32 lineSize)
{
    return ImFloor(metrics.ascent * lineSize + 1.0f);
}

static f32 CalcCapCenter(const LineMetrics& metrics, f32 lineSize)
{
    return CalcAscent(metrics, lineSize) - metrics.capHeight * lineSize * 0.5f;
}

static f32 CalcFaceAscent(const FontFace& face, f32 lineSize)
{
    return CalcAscent(face.metrics, lineSize * face.scale);
}

static ScaledFont AddScaledFont(const FontFile& file, const FontFace& face, f32 lineSize, f32 iconGap, bool isTabular = false)
{
    const f32 glyphSize = lineSize * face.scale;

    ImFontConfig config;
    config.FontDataOwnedByAtlas = false;
    config.RasterizerMultiply = face.rasterizerMultiply;
    config.GlyphOffset.y = ImFloor(CalcCapCenter(cLayoutMetrics, lineSize) - CalcCapCenter(face.metrics, glyphSize) + 0.5f);

    return { ImGui::GetIO().Fonts->AddFontFromMemoryTTF(file.data, file.size, glyphSize, &config), lineSize, iconGap, isTabular };
}

static ScaledFont AddTextFont(const FontFile& file, f32 lineSize, bool isTabular = false)
{
    return AddScaledFont(file, cTextFace, lineSize, cIconGapEm * lineSize, isTabular);
}

static void MergeIcons(const FontFile& file, f32 lineSize, f32 baseAscent)
{
    static const ImWchar cIconRanges[] = { ICON_MIN_LC, ICON_MAX_LC, 0 };

    ImFontConfig config;
    config.FontDataOwnedByAtlas = false;
    config.MergeMode = true;
    config.PixelSnapH = true;
    config.OversampleH = 1;
    config.GlyphOffset.y = ImFloor(lineSize * cIconOffsetEm + 0.5f) + CalcAscent(cLayoutMetrics, lineSize) - baseAscent;

    ImGui::GetIO().Fonts->AddFontFromMemoryTTF(file.data, file.size, lineSize * cIconSizeEm, &config, cIconRanges);
}

static void AddDialogIconFont(const FontFile& file)
{
    ImFontConfig config;
    config.FontDataOwnedByAtlas = false;
    config.PixelSnapH = true;
    config.OversampleH = 1;

    ImGui::GetIO().Fonts->AddFontFromMemoryTTF(file.data, file.size, ScaleSize(cDialogIconFontSize), &config, GetDialogIconRanges());
}

static bool IsDigit(char c)
{
    return c >= '0' && c <= '9';
}

static f32 CalcDigitAdvance(const ImFont* font)
{
    f32 width = 0.0f;

    for (ImWchar c = '0'; c <= '9'; c++)
        width = ImMax(width, font->GetCharAdvance(c));

    return width;
}

static void EqualizeDigits(ImFont* font)
{
    const f32 width = CalcDigitAdvance(font);

    for (ImWchar c = '0'; c <= '9'; c++)
    {
        if (c >= font->IndexLookup.Size || font->IndexLookup[c] == static_cast<ImWchar>(-1))
            continue;

        ImFontGlyph& glyph = font->Glyphs[font->IndexLookup[c]];
        const f32 shift = (width - glyph.AdvanceX) * 0.5f;

        glyph.X0 += shift;
        glyph.X1 += shift;
        glyph.AdvanceX = width;
        font->IndexAdvanceX[c] = width;
    }
}

static void FitScaledFont(const ScaledFont& scaled)
{
    ImFont* font = scaled.font;
    const f32 baseline = font->Ascent + font->ConfigData->GlyphOffset.y;

    font->FontSize = scaled.lineSize;
    font->Ascent = baseline;
    font->Descent = baseline - scaled.lineSize;
    font->IconGapAdvanceX = scaled.iconGap;

    if (scaled.isTabular)
        EqualizeDigits(font);
}

void LoadAppFonts()
{
    ImGuiIO& io = ImGui::GetIO();

    const FontFile textFile = LoadFontFile(cTextFace.path);
    const FontFile codeFile = LoadFontFile(cCodeFace.path);
    const FontFile iconsFile = LoadFontFile("main://fonts/" FONT_ICON_FILE_NAME_LC);

    ScaledFont fonts[3 + IM_ARRAYSIZE(cIconFontSizes)];
    s32 fontNum = 0;

    fonts[fontNum++] = AddTextFont(textFile, ScaleSize(cTitleFontSize), true);

    for (f32 baseSize : cIconFontSizes)
    {
        const f32 lineSize = ScaleSize(baseSize);
        const ScaledFont text = AddTextFont(textFile, lineSize);

        fonts[fontNum++] = text;
        MergeIcons(iconsFile, lineSize, CalcFaceAscent(cTextFace, lineSize));

        if (!io.FontDefault)
            io.FontDefault = text.font;
    }

    fonts[fontNum++] = AddTextFont(textFile, ScaleSize(cSmallFontSize));

    AddDialogIconFont(iconsFile);

    const f32 codeSize = ScaleSize(cCodeFontSize);
    const ScaledFont code = AddScaledFont(codeFile, cCodeFace, codeSize, 0.0f);

    fonts[fontNum++] = code;
    sCodeFont = code.font;
    MergeIcons(iconsFile, codeSize, CalcFaceAscent(cCodeFace, codeSize));

    io.Fonts->Build();

    for (s32 i = 0; i < fontNum; i++)
        FitScaledFont(fonts[i]);
}

ImFont* GetCodeFont()
{
    return sCodeFont ? sCodeFont : ImGui::GetFont();
}

f32 AddTabularText(ImDrawList* draw, ImFont* font, f32 fontSize, const ImVec2& pos, u32 color, const char* text, const char* textEnd)
{
    if (!textEnd)
        textEnd = text + strlen(text);

    const f32 digitWidth = CalcDigitAdvance(font) * fontSize / font->FontSize;
    f32 x = pos.x;

    for (const char* run = text; run < textEnd;)
    {
        const bool isDigit = IsDigit(*run);
        const char* runEnd = run + 1;

        while (!isDigit && runEnd < textEnd && !IsDigit(*runEnd))
            runEnd++;

        const f32 width = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, run, runEnd).x;
        const f32 cell = isDigit ? digitWidth : width;

        if (draw)
            draw->AddText(font, fontSize, ImVec2(ImFloor(x + (cell - width) * 0.5f), pos.y), color, run, runEnd);

        x += cell;
        run = runEnd;
    }

    return x - pos.x;
}

f32 CalcTabularTextWidth(ImFont* font, f32 fontSize, const char* text, const char* textEnd)
{
    return AddTabularText(nullptr, font, fontSize, ImVec2(0.0f, 0.0f), 0, text, textEnd);
}
