#include <ui/sound/SoundProperties.h>

#include <ui/widgets/ContextMenu.h>
#include <messages/Messages.h>
#include <ui/app/UI.h>
#include <ui/project/FileLists.h>

#include <bfsar/files/BfstmFile.h>

void DrawWaveSoundUI(Sound::WaveSoundInfo& waveSoundInfo)
{
    {
        Item* waveFile = waveSoundInfo.getWaveFileRef().getItem();
        if (ItemSelectorRow(messages::common::cWaveFile,ActiveSoundArchive().getWaveFileList(), &waveFile, false, TrailingButtonsWidth(1)))
        {
            waveSoundInfo.getWaveFileRef().attach(waveFile);
            SetUnsavedChanges(true);
        }

        ImGui::BeginDisabled(!waveFile);

        if (TrailingButton(ICON_LC_ELLIPSIS "###WaveFileMenu"))
            ImGui::OpenPopup("WaveFileMenu");

        ImGui::EndDisabled();

        ImGui::SetItemTooltip("%s", waveFile ? messages::sound::cWaveFileMenuTip : messages::reference::cNoWaveFile);

        if (BeginContextMenu("WaveFileMenu"))
        {
            WaveFile* wave = static_cast<WaveFile*>(waveFile);

            if (ContextMenuItem(ICON_LC_EXTERNAL_LINK, messages::sound::cWaveFileGoTo))
                SelectItem(wave);

            ContextMenuSeparator();
            DrawWaveFileMenuItems(wave, { wave }, !IsStreamTrackWave(*wave));
            EndContextMenu();
        }
    }
}

void DrawWaveEnvelopeSection(Sound::WaveSoundInfo& waveSoundInfo)
{
    bool enableEnvelope = waveSoundInfo.isEnableEnvelope();
    bool changed = false;
    const bool open = BeginSoundSection(sead::FormatFixedSafeString<64>("%s###Envelope", messages::panel::cEnvelope).cstr(), &enableEnvelope, &changed);

    if (changed)
    {
        waveSoundInfo.setEnableEnvelope(enableEnvelope);
        SetUnsavedChanges(true);
    }

    if (!open)
        return;

    snd::AdshrCurve adshrCurve = waveSoundInfo.getAdshrCurve();
    const bool stacked = ImGui::GetContentRegionAvail().x < CalcAdshrEditorWidth(false) - ImGui::GetFontSize();

    if (DrawAdshrEditor(adshrCurve, CalcRegionStripHeight(), stacked))
    {
        waveSoundInfo.setAdshrCurve(adshrCurve);
        SetUnsavedChanges(true);
    }

    EndPropertySection();
}

void DrawWaveExtraOptionsUI(Sound::WaveSoundInfo& waveSoundInfo)
{
    std::vector<std::pair<u32, u32>>& extraOptions = waveSoundInfo.getExtraOptions();

    if (extraOptions.empty() || !ImGui::CollapsingHeader(sead::FormatFixedSafeString<64>("%s###UnknownOptions", messages::sound::cUnknownOptions).cstr()))
        return;

    for (std::pair<u32, u32>& option : extraOptions)
    {
        ImGui::PushID(option.first);

        PropertyRow(sead::FormatFixedSafeString<32>(messages::sound::cOptionBitFormat, option.first).cstr());
        u32 value = option.second;
        if (ImGui::InputScalar("##option", ImGuiDataType_U32, &value, nullptr, nullptr, "%08X", ImGuiInputTextFlags_CharsHexadecimal))
        {
            option.second = value;
            SetUnsavedChanges(true);
        }

        ImGui::PopID();
    }
}
