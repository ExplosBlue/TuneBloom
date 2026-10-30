#pragma once

#include <ui/file/LooseFileInfo.h>

class LooseStreamFile;

LooseFileFormat GetLooseStreamFormat(const LooseStreamFile& file);
void DrawLooseStreamInfoUI(LooseStreamFile& file);
void DrawLooseStreamContentUI(LooseStreamFile& file);
