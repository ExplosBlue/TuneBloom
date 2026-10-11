#pragma once

enum class PreferencesPage
{
    File,
    Audio,
    Midi,
    Appearance,
    Interface,
    Advanced
};

void OpenPreferencesWindow(PreferencesPage page);
void DrawPreferencesWindow();
void DrawPreferencesMenu();
