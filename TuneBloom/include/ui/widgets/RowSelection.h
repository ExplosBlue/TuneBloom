#pragma once

#include <basis/seadTypes.h>

#include <algorithm>
#include <vector>

inline constexpr s32 cRowSelectionPageStep = 20;

template <typename T>
struct RowSelection
{
    std::vector<T>* selected = nullptr;
    T* anchor = nullptr;
    T* focus = nullptr;
};

template <typename T>
bool IsRowInSelection(const std::vector<T>& selected, const T& row)
{
    return std::find(selected.begin(), selected.end(), row) != selected.end();
}

template <typename T>
void ClickRowSelection(const RowSelection<T>& selection, const std::vector<T>& rows, const T& clicked, bool isToggle, bool isExtend, bool canAddFocus)
{
    std::vector<T>& selected = *selection.selected;
    T& anchor = *selection.anchor;
    T& focus = *selection.focus;

    std::vector<T> range;

    if (isExtend)
    {
        const auto anchorIt = std::find(rows.begin(), rows.end(), anchor);
        const auto clickedIt = std::find(rows.begin(), rows.end(), clicked);

        if (anchorIt != rows.end() && clickedIt != rows.end())
            range.assign(std::min(anchorIt, clickedIt), std::max(anchorIt, clickedIt) + 1);
    }

    if (isToggle && isExtend && !range.empty())
    {
        for (const T& row : range)
        {
            if (!IsRowInSelection(selected, row))
                selected.push_back(row);
        }

        focus = clicked;
        return;
    }

    if (isToggle)
    {
        const auto it = std::find(selected.begin(), selected.end(), clicked);

        if (it != selected.end())
        {
            selected.erase(it);

            if (selected.empty())
                focus = T();

            anchor = selected.empty() ? T() : selected.back();
            return;
        }

        if (selected.empty() && canAddFocus && focus != T() && focus != clicked)
            selected.push_back(focus);

        selected.push_back(clicked);
        focus = clicked;
        anchor = clicked;
        return;
    }

    if (isExtend && !range.empty())
    {
        selected = range;
        focus = clicked;
        return;
    }

    selected.clear();
    focus = clicked;
    anchor = clicked;
}
