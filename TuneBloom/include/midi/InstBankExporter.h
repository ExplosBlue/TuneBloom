#pragma once

#include <basis/seadTypes.h>
#include <prim/seadSafeString.h>

class BankFile;
class Sound;
struct SeqMidiFilter;

bool exportBankToSf2(const sead::SafeString& path, const BankFile& bank);
bool exportSeqSoundToSf2(const sead::SafeString& path, const Sound& sound, const SeqMidiFilter* filter = nullptr);

bool exportBankToDls(const sead::SafeString& path, const BankFile& bank);
bool exportSeqSoundToDls(const sead::SafeString& path, const Sound& sound, const SeqMidiFilter* filter = nullptr);
