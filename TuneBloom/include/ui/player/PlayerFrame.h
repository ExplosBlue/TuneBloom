#pragma once

#include <ui/file/OpenQueue.h>

#include <basis/seadTypes.h>

class Document;

bool IsPlayerFrameShown();
bool IsPlayerDocument(Document* document);
void PreparePlayerFrame(const openqueue::Request& launch);
void PrepareEmptyPlayerFrame();
void NotifyPlayerFrameOpen(const openqueue::Request& request, Document* previous, Document* opened);
void ForgetPlayerFrameDocument(const Document* document);
void ApplyPlayerFrameStart();
void DrawPlayerFrame();
f32 DrawPlayerFrameTools(f32 right, f32 top, f32 bottom);
void RequestFullEditor();
