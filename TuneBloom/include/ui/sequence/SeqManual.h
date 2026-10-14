#pragma once

#include <basis/seadTypes.h>

#include <libs/TextEditor.h>

struct SeqEditorSettings
{
    bool showManual = false;
    bool autoCompile = false;
    bool follow = false;
    bool showTrail = true;
    bool showWhitespace = false;
    bool suggest = true;
    bool parameterHints = true;
    bool minimap = true;
    s32 followTrack = 0;
    f32 splitPos = 0.6f;
};

extern SeqEditorSettings gSeqEditorSettings;

void RegisterSeqEditorSettings();

const TextEditor::LanguageDefinition& GetSeqLanguageDefinition();
const TextEditor::Palette& GetSeqPalette();

void DrawSeqManual();
