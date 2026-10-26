#pragma once

#include <basis/seadTypes.h>

bool HasUnsavedChanges();
void SetUnsavedChanges(bool dirty);
u64 GetEditGeneration();

void CaptureSavedState();

void OpenUnsavedChangesDialog();
void OpenExitChangesDialog();
void DrawUnsavedChangesDialog();
