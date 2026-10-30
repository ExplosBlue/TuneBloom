#pragma once

#include <basis/seadTypes.h>

struct TitleRowPlace
{
    f32 columnLeft = 0.0f;
    f32 left = 0.0f;
    f32 right = 0.0f;
    f32 maxRight = 0.0f;
    f32 top = 0.0f;
    f32 bottom = 0.0f;
};

void DrawDocumentTabStrip();
f32 DrawDocumentTabsInTitleRow(const TitleRowPlace& place);
f32 CalcTitleRowStripMinRight(const TitleRowPlace& place);
