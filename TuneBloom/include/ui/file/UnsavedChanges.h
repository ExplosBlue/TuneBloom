#pragma once

#include <basis/seadTypes.h>

class Document;

bool HasUnsavedChanges();
bool DocumentHasChanges(Document& document);
void SetUnsavedChanges(bool dirty);
u64 GetEditGeneration();

void CaptureSavedState();

void OpenUnsavedChangesDialog();
void OpenExitChangesDialog();
void DrawUnsavedChangesDialog();
