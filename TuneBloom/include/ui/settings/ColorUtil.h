#pragma once

#include <basis/seadTypes.h>

#include <ui/settings/Theme.h>

#include <imgui/imgui.h>

namespace uicolors
{

constexpr f32 ByteToUnit(u32 value)
{
    return static_cast<f32>(value & 0xFF) / 255.0f;
}

constexpr u32 UnitToByte(f32 value)
{
    return static_cast<u32>(value * 255.0f + 0.5f);
}

constexpr ImVec4 HexA(u32 rgba)
{
    return ImVec4(ByteToUnit(rgba >> 24), ByteToUnit(rgba >> 16), ByteToUnit(rgba >> 8), ByteToUnit(rgba));
}

constexpr ImVec4 Hex(u32 rgb)
{
    return HexA((rgb << 8) | 0xFF);
}

constexpr ImU32 HexU32A(u32 rgba)
{
    return IM_COL32((rgba >> 24) & 0xFF, (rgba >> 16) & 0xFF, (rgba >> 8) & 0xFF, rgba & 0xFF);
}

constexpr ImU32 HexU32(u32 rgb)
{
    return HexU32A((rgb << 8) | 0xFF);
}

constexpr ImU32 ToU32(const ImVec4& color, u32 alpha)
{
    return IM_COL32(UnitToByte(color.x), UnitToByte(color.y), UnitToByte(color.z), alpha);
}

constexpr ImU32 ToU32(const ImVec4& color)
{
    return ToU32(color, UnitToByte(color.w));
}

constexpr ImVec4 WithAlpha(const ImVec4& color, u32 alpha)
{
    return ImVec4(color.x, color.y, color.z, ByteToUnit(alpha));
}

constexpr ImU32 WithAlpha(ImU32 color, u32 alpha)
{
    return (color & ~IM_COL32_A_MASK) | (alpha << IM_COL32_A_SHIFT);
}

constexpr ImVec4 Opaque(const ImVec4& color)
{
    return ImVec4(color.x, color.y, color.z, 1.0f);
}

constexpr ImVec4 Scale(const ImVec4& color, f32 factor)
{
    return ImVec4(color.x * factor, color.y * factor, color.z * factor, color.w);
}

constexpr ImVec4 Darken(const ImVec4& color, f32 mix)
{
    return Scale(color, 1.0f - mix);
}

constexpr f32 MixTowardWhite(f32 channel, f32 mix)
{
    return channel + (1.0f - channel) * mix;
}

constexpr ImVec4 Lighten(const ImVec4& color, f32 mix)
{
    return ImVec4(MixTowardWhite(color.x, mix), MixTowardWhite(color.y, mix), MixTowardWhite(color.z, mix), color.w);
}

inline ImVec4 FromHsv(f32 hue, f32 saturation, f32 value, f32 alpha = 1.0f)
{
    ImVec4 color;
    ImGui::ColorConvertHSVtoRGB(hue, saturation, value, color.x, color.y, color.z);
    color.w = alpha;

    return color;
}

template <typename T>
struct Themed
{
    T dark;
    T light;
};

template <typename T>
constexpr const T& Pick(const Themed<T>& value, bool dark)
{
    return dark ? value.dark : value.light;
}

template <typename T>
inline const T& Pick(const Themed<T>& value)
{
    return Pick(value, IsDarkTheme());
}

}
