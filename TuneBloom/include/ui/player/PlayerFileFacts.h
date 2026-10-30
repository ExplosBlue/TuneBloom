#pragma once

#include <basis/seadTypes.h>

class Document;

bool DrawFileFactsLine(const Document& document, f32 gap);
f32 DrawFileDetailsButton(const Document& document, f32 right, f32 top, f32 bottom);
