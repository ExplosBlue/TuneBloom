#pragma once

#include <basis/seadTypes.h>
#include <prim/seadSafeString.h>

class BankFile;
class Sound;
class SoundSet;

bool exportBankToSf2(const sead::SafeString &path, const BankFile &bank);
bool exportSeqSoundToSf2(const sead::SafeString &path, const Sound &sound);
bool exportSeqSoundSetToSf2Dir(const sead::SafeString &dirPath, const SoundSet &soundSet);

bool exportBankToDls(const sead::SafeString &path, const BankFile &bank);
bool exportSeqSoundToDls(const sead::SafeString &path, const Sound &sound);
bool exportSeqSoundSetToDlsDir(const sead::SafeString &dirPath, const SoundSet &soundSet);
