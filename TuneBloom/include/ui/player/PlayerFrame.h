#pragma once

#include <ui/file/OpenQueue.h>

class Document;

bool IsPlayerFrameShown();
bool IsPlayerDocument(Document* document);
void PreparePlayerFrame(const openqueue::Request& launch);
void NotifyPlayerFrameOpen(const openqueue::Request& request, Document* previous, Document* opened);
void ApplyPlayerFrameStart();
void DrawPlayerFrame();
void RequestFullEditor();
