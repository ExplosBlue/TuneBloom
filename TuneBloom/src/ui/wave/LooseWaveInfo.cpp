#include <ui/wave/LooseWaveInfo.h>

#include <ui/file/LooseFileInfo.h>
#include <ui/project/FileLists.h>
#include <ui/Messages.h>
#include <ui/widgets/Properties.h>
#include <ui/widgets/TabRowTools.h>

#include <bfsar/files/LooseWaveFile.h>

#include <libs/IconsLucide.h>

static void DrawReplaceButton(WaveFile* wave, f32 buttonSize)
{
    if (ImGui::Button(ICON_LC_REPLACE "##Replace", ImVec2(buttonSize, buttonSize)))
        RequestWaveFileReplace(wave);

    ImGui::SetItemTooltip("%s", messages::import::cReplaceWaveFileMenuLabel);
}

static void DrawWaveFileBox(LooseWaveFile& file)
{
    if (!BeginPropertyBox("WaveFile"))
        return;

    const f32 buttonSize = ImGui::GetFrameHeight();

    BeginTitleRowTools(messages::common::cFile, buttonSize);
    DrawReplaceButton(file.getWave(), buttonSize);

    DrawLooseFileRows(file.getTarget(), InnerFileKind::Wave);

    EndPropertyBox();
}

void DrawLooseWaveInfoUI(LooseWaveFile& file)
{
    DrawWaveFileBox(file);
}
