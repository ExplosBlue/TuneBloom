#include <ui/sound/SoundProperties.h>

#include <ui/widgets/ContextMenu.h>
#include <ui/Messages.h>
#include <ui/app/UI.h>

void DrawWaveSoundUI(Sound::WaveSoundInfo& waveSoundInfo)
{
    {
        Item* waveFile = waveSoundInfo.getWaveFileRef().getItem();
        if (ItemSelectorRow(messages::common::cWaveFile,sBfsar.getWaveFileList(), &waveFile, false, TrailingButtonsWidth(1)))
        {
            waveSoundInfo.getWaveFileRef().attach(waveFile);
            SetUnsavedChanges(true);
        }

        ImGui::BeginDisabled(!waveFile);

        if (TrailingButton(ICON_LC_ELLIPSIS "###WaveFileMenu"))
            ImGui::OpenPopup("WaveFileMenu");

        ImGui::EndDisabled();

        if (!waveFile)
            SetDisabledTooltip(messages::reference::cNoWaveFile);
        else
            ImGui::SetItemTooltip("%s", messages::sound::cWaveFileMenuTip);

        if (BeginContextMenu("WaveFileMenu"))
        {
            if (ContextMenuItem(ICON_LC_EXTERNAL_LINK, messages::sound::cWaveFileGoTo))
                SelectItem(waveFile);

            ContextMenuSeparator();

            if (ContextMenuItem(ICON_LC_REPLACE, messages::common::cReplace))
                RequestWaveFileReplace(static_cast<WaveFile*>(waveFile));

            ImGui::SetItemTooltip("%s", messages::import::cReplaceWaveFileTip);

            if (ContextMenuItem(ICON_LC_SLIDERS_HORIZONTAL, messages::import::cReimportMenuLabel))
                RequestWaveFileReimport(static_cast<WaveFile*>(waveFile));

            ImGui::SetItemTooltip("%s", messages::import::cReimportWaveFileTip);

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
