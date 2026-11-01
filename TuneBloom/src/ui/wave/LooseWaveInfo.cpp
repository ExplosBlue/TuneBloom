#include <ui/wave/LooseWaveInfo.h>

#include <ui/file/LooseFileInfo.h>
#include <messages/Messages.h>
#include <ui/widgets/Knob.h>
#include <ui/widgets/Properties.h>

#include <bfsar/files/LooseWaveFile.h>

static void DrawWaveFileBox(LooseWaveFile& file)
{
    if (!BeginPropertyBox("WaveFile"))
        return;

    RegionHeader(messages::common::cFile);
    DrawLooseFileRows(GetLooseFileFormat(file.getTarget(), InnerFileKind::Wave));

    EndPropertyBox();
}

void DrawLooseWaveInfoUI(LooseWaveFile& file)
{
    DrawWaveFileBox(file);
}
