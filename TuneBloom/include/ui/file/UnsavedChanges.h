#pragma once

#include <basis/seadTypes.h>

extern bool gUnsavedChanges;

void SetUnsavedChanges(bool dirty);
u64 GetEditGeneration();

void CaptureSavedState();

void OpenUnsavedChangesDialog();
void DrawUnsavedChangesDialog();
