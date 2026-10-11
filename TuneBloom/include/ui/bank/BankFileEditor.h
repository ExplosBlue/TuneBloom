#pragma once

#include <bfsar/files/BankFile.h>

inline constexpr s32 cPianoDefaultCenter = 60;

s32 TakeContextKey();

bool KeyboardFunc(void* UserData, s32 Msg, s32 Key, f32 Vel);
BankFile::VelocityRegion* FindSelectedRegion(BankFile::Instrument* instrument, BankFile::KeyRegion** outKeyRegion);
void DrawRegionPanels(BankFile::Instrument* instrument);
bool DrawNextProgramButton(const char* id);
const char* GetInstrumentLabel(const BankFile::Instrument& instrument, sead::BufferedSafeString& buffer);
void DrawKeyboardWithRegions(f32 width, f32 keyboardHeight, f32 regionHeight, s32 beginNote, s32 endNote, BankFile::Instrument* instrument);
