#pragma once

#include <basis/seadTypes.h>
#include <prim/seadSafeString.h>

#include <vector>

class BankFile;
class Sound;
class SoundSet;

inline constexpr u32 cSeqSoundBankNum = 4;

bool WriteExportFile(const char* path, const std::vector<u8>& data);
void SanitizeFileName(sead::BufferedSafeString* name);
std::vector<const Sound*> GetSeqSoundsOfSet(const SoundSet& soundSet);
const BankFile* GetSeqSoundBankFile(const Sound& sound, u32 bankIndex);
