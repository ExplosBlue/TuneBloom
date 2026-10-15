#include <ui/sound/SoundProperties.h>

#include <ui/Messages.h>
#include <ui/app/PopupMgr.h>
#include <ui/app/UI.h>

#include <imgui/imgui_custom.h>

#include <string>
#include <vector>

void DrawSequenceSoundUI(Sound::SequenceSoundInfo& seqSoundInfo)
{
    {
        Item* seqFile = seqSoundInfo.getSequenceFileRef().getItem();
        if (ItemSelectorRow(messages::common::cSequenceFile,sBfsar.getSequenceFileList(), &seqFile, false, TrailingButtonsWidth(1)))
        {
            seqSoundInfo.getSequenceFileRef().attach(seqFile);
            SetUnsavedChanges(true);
        }

        ImGui::BeginDisabled(!seqFile);

        if (TrailingButton(ICON_LC_FILE_PEN "###EditSeq"))
            OpenFileWindow(seqFile);

        ImGui::EndDisabled();

        if (!seqFile)
            ImGui::SetItemTooltip("%s", messages::reference::cNoSequenceFile);
    }

    for (u32 i = 0; i < 4; i++)
    {
        Item* bank = seqSoundInfo.getBankRef(i).getItem();
        if (ItemSelectorRow(sead::FormatFixedSafeString<16>(messages::sequence::cBankSlotFormat, i).cstr(), sBfsar.getBankList(), &bank, true, TrailingButtonsWidth(1)))
        {
            seqSoundInfo.getBankRef(i).attach(bank);
            SetUnsavedChanges(true);
        }

        Item* bankFile = nullptr;
        if (bank)
        {
            Bank* bankItem = static_cast<Bank*>(bank);
            bankFile = bankItem->getFileRef().getItem();
        }

        ImGui::BeginDisabled(!bankFile);

        if (TrailingButton(sead::FormatFixedSafeString<32>(ICON_LC_FILE_PEN "###EditBank%u", i).cstr()))
            OpenFileWindow(bankFile);

        ImGui::EndDisabled();

        if (!bankFile)
            ImGui::SetItemTooltip("%s", bank ? messages::bank::cNoFileAttached : messages::reference::cNoBank);
    }
}

static bool DrawStartLabelCombo(SequenceFile* seqFile, sead::FixedSafeString<128>* startLabel)
{
    std::vector<const char*> labels;

    if (seqFile)
    {
        labels.push_back(messages::sound::cStartOfFile);

        for (const std::string& label : seqFile->getLabels())
            labels.push_back(label.c_str());
    }

    if (!ImGui::InputTextCombo("##StartLabel", startLabel->getBuffer(), startLabel->getBufferSize(), labels.empty() ? nullptr : labels.data(), static_cast<s32>(labels.size())))
        return false;

    if (*startLabel == messages::sound::cStartOfFile)
        startLabel->clear();

    return true;
}

static void OpenSequenceAtLabel(SequenceFile* seqFile, Sound::SequenceSoundInfo& seqSoundInfo, const sead::FixedSafeString<128>& startLabel)
{
    if (seqFile->getLabelOffset(startLabel) == SequenceFile::cInvaldOffset)
    {
        PopupMgr::instance()->addPopup({ sead::FormatFixedSafeString<128>(messages::sound::cStartLabelNotFoundFormat, startLabel.cstr()).cstr(), nullptr });
        return;
    }

    for (u32 i = 0; i < 4; i++)
        seqFile->getBankRef_(i)->attach(seqSoundInfo.getBankRef(i).getItem());

    seqFile->getStartLabel_() = startLabel;

    OpenFileWindow(seqFile);

    seqFile->setCursorToLabel_(startLabel);
}

void DrawSequenceStartLabelPanel(Sound::SequenceSoundInfo& seqSoundInfo)
{
    bool enableStartOffset = seqSoundInfo.isEnableStartOffset();
    if (DrawSoundPanelHeader(messages::sound::cPanelStartLabel,&enableStartOffset))
    {
        seqSoundInfo.setEnableStartOffset(enableStartOffset);
        SetUnsavedChanges(true);
    }

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImFloor((CalcRegionStripHeight() - ImGui::GetFontSize() - ImGui::GetFrameHeight()) * 0.5f));
    ImGui::SetNextItemWidth(-TrailingButtonsWidth(1));

    Item* seqFile = seqSoundInfo.getSequenceFileRef().getItem();
    sead::FixedSafeString<128> startLabel = enableStartOffset ? seqSoundInfo.getStartLabel() : sead::FixedSafeString<128>();

    ImGui::BeginDisabled(!enableStartOffset);

    if (DrawStartLabelCombo(static_cast<SequenceFile*>(seqFile), &startLabel))
    {
        seqSoundInfo.getStartLabel() = startLabel;
        SetUnsavedChanges(true);
    }

    ImGui::EndDisabled();

    ImGui::BeginDisabled(!seqFile);

    if (TrailingButton(ICON_LC_EXTERNAL_LINK "###GoSeq"))
        OpenSequenceAtLabel(static_cast<SequenceFile*>(seqFile), seqSoundInfo, startLabel);

    ImGui::EndDisabled();

    ImGui::SetItemTooltip("%s", seqFile ? messages::sequence::cGoToLabelHint : messages::reference::cNoSequenceFile);
}
