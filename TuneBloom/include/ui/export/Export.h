#pragma once

#include <basis/seadTypes.h>

#include <vector>

class BankFile;
class SequenceFile;
class Sound;
class SoundSet;
class WaveFile;

void RequestExportSounds(const std::vector<Sound*>& sounds);
void CancelSoundExport();
void DrawSoundExportDialog();
void DrawSoundExportProgress();
void DrawSoundExportProgressPreview(f32 fraction);

void RequestExportSequenceFiles(const std::vector<SequenceFile*>& files);
void RequestImportSequenceFile();
void RequestExportWaveFiles(const std::vector<WaveFile*>& files);
void RequestExportWaveFilesToWav(const std::vector<WaveFile*>& files);
void RequestExportBankSf2(const std::vector<BankFile*>& files);
void RequestExportBankDls(const std::vector<BankFile*>& files);
void RequestExportMidi(const std::vector<Sound*>& sounds);
void RequestExportMidi(const std::vector<SoundSet*>& soundSets);
void CancelFileExports();
void DrawFileExportDialogs();
void DrawMidiExportDialog();

void DrawExportResultPopup();
