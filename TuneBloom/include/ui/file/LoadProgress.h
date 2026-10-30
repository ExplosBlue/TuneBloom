#pragma once

#include <bfsar/model/ModelEvents.h>

#include <basis/seadTypes.h>

namespace loadprogress
{

using Stage = modelevents::LoadStage;

void Begin(const char* fileName);
void End();
void Repaint();

void SetStageCount(Stage stage, u32 count);
void BeginStage(Stage stage);
void Step();
void Update();

void DrawPreview(const char* fileName, Stage stage, f32 fraction);

void DrawRows(const char* icon, const char* title, const char* stageText, f32 fraction);

}
