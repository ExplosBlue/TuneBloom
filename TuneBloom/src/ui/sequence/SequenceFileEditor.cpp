#include <bfsar/files/SequenceFile.h>

#include <bfsar/model/Bank.h>
#include <bfsar/files/BankFile.h>
#include <bfsar/seq/SeqParseUtil.h>
#include <bfsar/model/Sound.h>

#include <snd/SoundThread.h>

#include <ui/widgets/ContextMenu.h>
#include <ui/Messages.h>
#include <ui/sequence/SeqEditor.h>
#include <ui/sequence/SeqManual.h>
#include <ui/app/UI.h>

#include <imgui/imgui.h>
#include <imgui/imgui_custom.h>
#include <imgui/imgui_internal.h>

#include <libs/TextEditor.h>

#include <string>
#include <vector>

static constexpr f64 cAutoCompileDelay = 0.4;

static constexpr f32 cToolGroupGapEm = 0.6f;
static constexpr f32 cToolNameMaxEm  = 9.0f;

void SequenceFile::onOpenFileWindow()
{
    SEAD_ASSERT(!mTextEditor);
    mTextEditor = new TextEditor();
    mTextEditor->SetLanguageDefinition(GetSeqLanguageDefinition());
    mTextEditor->SetPalette(GetSeqPalette(gThemeIsDark));

    mIsDirty = false;
    mHasPlayMarkers = false;
    mAnnotationsDirty = true;

    if (mSeqText)
    {
        mTextEditor->SetText(mSeqText->cstr());
    }

    bool anyBankSet = false;

    for (u32 i = 0; i < 4; i++)
    {
        if (mBankRefs[i]->isAttached())
        {
            anyBankSet = true;
            break;
        }
    }

    if (!anyBankSet)
    {
        for (const Item *soundItem : sBfsar.getSoundList())
        {
            const Sound *sound = static_cast<const Sound *>(soundItem);

            if (sound->getSoundType() != Sound::SoundType::Seq)
                continue;

            const Sound::SequenceSoundInfo &seqInfo = sound->getSequenceSoundInfo();

            if (seqInfo.getSequenceFileRef().getItem() != this)
                continue;

            for (u32 i = 0; i < 4; i++)
            {
                const Item *bank = seqInfo.getBankRef(i).getItem();

                if (bank)
                    mBankRefs[i]->attach(const_cast<Item *>(bank));
            }

            break;
        }
    }
}

void SequenceFile::onCloseFileWindow()
{
    mSeqTextInfo.reset();

    if (mTextEditor)
    {
        if (mIsValid)
        {
            if (mSeqText)
            {
                delete mSeqText;
                mSeqText = nullptr;
            }

            std::string text = mTextEditor->GetText();
            text.pop_back(); // Remove new line at the end

            mSeqText = new sead::HeapSafeString(nullptr, text.c_str());
        }

        delete mTextEditor;
        mTextEditor = nullptr;
    }
}

std::vector<std::string> SequenceFile::getEditorLines_() const
{
    return mTextEditor->GetTextLines();
}

void SequenceFile::showEditorError_(u32 line, const std::string& errorMsg, bool setCursorPos)
{
    TextEditor::Marker error;
    error.mColor = 0;
    error.mIsError = true;
    error.mShowDetail = true;
    error.mDetail = errorMsg;

    mTextEditor->SetMarkers({ { line + 1, error } });

    if (setCursorPos)
    {
        mTextEditor->SetCursorPosition({ static_cast<s32>(line), 0 });
        mTextEditor->EnsureCursorVisible();
    }
}

void SequenceFile::clearEditorMarkers_()
{
    mTextEditor->SetMarkers({ });
}

void SequenceFile::deleteEditor_()
{
    if (mTextEditor)
    {
        delete mTextEditor;
        mTextEditor = nullptr;
    }
}

void SequenceFile::drawUI()
{
    mVersion = sBfsar.getVersionForBfseq();
    mEndian = sBfsar.getEndian();

    DrawItemNameRow(this);
}

static void SeqOptionItem(const char* label, bool* value)
{
    if (!ContextMenuCheckItem(label, *value))
        return;

    *value = !*value;
    ImGui::MarkIniSettingsDirty();
}

void SequenceFile::play_(s32 headerLine)
{
    {
        snd::internal::driver::SoundThreadLock lock;
        mSeqTextInfo.reset();
    }

    if (mIsDirty || !mIsValid)
        compile_();

    if (!mIsValid)
        return;

    mStartLabel.clear();

    if (headerLine >= 0)
    {
        const std::string text = mTextEditor->GetLineText(headerLine);
        const std::string::size_type begin = text.find_first_not_of(" \t");
        const std::string::size_type end = text.find_first_of(" \t:", begin);

        if (begin != std::string::npos && end != std::string::npos)
        {
            const std::string label = text.substr(begin, end - begin);
            if (static_cast<s32>(label.size()) < mStartLabel.getBufferSize() && mLabelsStartInfo.find(label) != mLabelsStartInfo.end())
                mStartLabel = label.c_str();
        }
    }

    const Bank* banks[nw::snd::SoundArchive::SEQ_BANK_MAX];
    for (u32 i = 0; i < nw::snd::SoundArchive::SEQ_BANK_MAX; i++)
    {
        const Item* item = mBankRefs[i]->getItem();
        SEAD_ASSERT(!item || item->getItemType() == Item::ItemType::Bank);

        banks[i] = static_cast<const Bank*>(item);
    }

    sSoundPlayer.playSeqFile(*this, mStartLabel, banks);
}

static f32 CalcToolMenuButtonWidth(const char* icon, const char* text)
{
    const ImGuiStyle& style = ImGui::GetStyle();

    if (!text)
        return ImGui::GetFrameHeight();

    return style.FramePadding.x * 2.0f + ImGui::CalcTextSize(icon).x + ImGui::CalcTextSize(text).x + ImGui::CalcTextSize(ICON_LC_CHEVRON_DOWN).x + style.ItemInnerSpacing.x * 2.0f;
}

static bool DrawToolMenuButton(const char* id, const char* icon, const char* text, f32 width, bool open)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 max(min.x + width, min.y + ImGui::GetFrameHeight());

    const bool pressed = ImGui::InvisibleButton(id, ImVec2(width, ImGui::GetFrameHeight()));
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();

    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(min, max, ImGui::GetHoverColorU32(ImGui::GetItemID() + 1, hovered || open, held, ImGuiCol_FrameBg, ImGuiCol_FrameBgHovered, ImGuiCol_FrameBgActive), style.FrameRounding);

    const f32 textY = min.y + style.FramePadding.y;
    const f32 iconWidth = ImGui::CalcTextSize(icon).x;

    if (!text)
    {
        draw->AddText(ImVec2(ImFloor(min.x + (width - iconWidth) * 0.5f), textY), ImGui::GetColorU32(ImGuiCol_Text), icon);
        return pressed;
    }

    const f32 chevronX = max.x - style.FramePadding.x - ImGui::CalcTextSize(ICON_LC_CHEVRON_DOWN).x;
    const f32 textMin = min.x + style.FramePadding.x + iconWidth + style.ItemInnerSpacing.x;
    const f32 textMax = chevronX - style.ItemInnerSpacing.x;

    draw->AddText(ImVec2(min.x + style.FramePadding.x, textY), ImGui::GetColorU32(ImGuiCol_Text), icon);
    ImGui::RenderTextEllipsis(draw, ImVec2(textMin, textY), ImVec2(textMax, max.y), textMax, textMax, text, nullptr, nullptr);
    draw->AddText(ImVec2(chevronX, textY), ImGui::GetColorU32(ImGuiCol_TextDisabled), ICON_LC_CHEVRON_DOWN);

    return pressed;
}

static void SetNextToolMenuPos()
{
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetItemRectMax().x, ImGui::GetItemRectMax().y + ImGui::GetStyle().ItemSpacing.y), ImGuiCond_Appearing, ImVec2(1.0f, 0.0f));
}

void SequenceFile::drawBankSelector_(const char* name, f32 width)
{
    if (DrawToolMenuButton("##Banks", ICON_LC_PIANO, name, width, ImGui::IsPopupOpen("##SeqBanks")))
        ImGui::OpenPopup("##SeqBanks");

    const ImVec2 popupPos(ImGui::GetCurrentWindow()->WorkRect.Max.x, ImGui::GetItemRectMax().y + ImGui::GetStyle().ItemSpacing.y);

    u32 bankNum = 0;
    for (u32 i = 0; i < nw::snd::SoundArchive::SEQ_BANK_MAX; i++)
        bankNum += mBankRefs[i]->getItem() ? 1 : 0;

    const bool nameInView = name && bankNum <= 1 && CalcToolMenuButtonWidth(ICON_LC_PIANO, name) <= width;

    if (!nameInView && IsItemHoveredForDetail() && !ImGui::IsPopupOpen("##SeqBanks"))
    {
        ImGui::BeginTooltip();

        bool anyBank = false;
        for (u32 i = 0; i < nw::snd::SoundArchive::SEQ_BANK_MAX; i++)
        {
            const Item* bank = mBankRefs[i]->getItem();
            if (!bank)
                continue;

            ImGui::Text("%u  %s", i, bank->getFormattedName().cstr());
            anyBank = true;
        }

        if (!anyBank)
            ImGui::TextUnformatted(messages::sequence::cNoBanks);

        ImGui::EndTooltip();
    }

    ImVec4 background = ImGui::GetStyleColorVec4(ImGuiCol_PopupBg);
    background.w = 1.0f;

    ImGui::SetNextWindowPos(popupPos, ImGuiCond_Appearing, ImVec2(1.0f, 0.0f));

    ImGui::PushStyleColor(ImGuiCol_PopupBg, background);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, ImFloor(ImGui::GetFontSize() * cMenuRoundingEm));

    const bool open = ImGui::BeginPopup("##SeqBanks");

    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    if (!open)
        return;

    for (u32 i = 0; i < nw::snd::SoundArchive::SEQ_BANK_MAX; i++)
    {
        ImGui::AlignTextToFramePadding();
        ImGui::Text(messages::sequence::cBankSlotFormat, i);
        ImGui::SameLine();

        Item* bank = mBankRefs[i]->getItem();

        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 22.0f);
        ImGui::PushID(i);
        const bool changed = ItemSelector("", sBfsar.getBankList(), &bank, true);
        ImGui::PopID();

        if (changed)
        {
            mBankRefs[i]->attach(bank);
            mAnnotationsDirty = true;
            SetUnsavedChanges(true);
        }

        Item* bankFile = bank ? static_cast<Bank*>(bank)->getFileRef().getItem() : nullptr;

        ImGui::SameLine();

        ImGui::BeginDisabled(!bankFile);
        if (ImGui::Button(sead::FormatFixedSafeString<32>(ICON_LC_FILE_PEN "###EditBank%u", i).cstr()))
        {
            OpenFileWindow(bankFile);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();

        if (bankFile)
            ImGui::SetItemTooltip("%s", messages::bank::cOpenFileTip);
        else
            SetDisabledTooltip(bank ? messages::bank::cNoFileAttached : messages::reference::cNoBank);
    }

    ImGui::EndPopup();
}

struct SeqToolbarState
{
    f32 em = 0.0f;
    f32 frame = 0.0f;
    f32 spacing = 0.0f;
    f32 groupGap = 0.0f;
    ImVec2 buttonSize;

    bool playing = false;
    bool canCompile = false;

    sead::FixedSafeString<64> state;
    ImVec4 stateColor;
    sead::FixedSafeString<288> bankName;
    sead::FixedSafeString<32> followName;
    sead::FixedSafeString<64> compileLabel;

    f32 startX = 0.0f;
    f32 avail = 0.0f;
    f32 compileWidth = 0.0f;
    f32 banksWidth = 0.0f;
    f32 followWidth = 0.0f;
    f32 leftWidth = 0.0f;
    f32 rightWidth = 0.0f;
    bool showNames = false;
    bool showState = false;

    bool followChanged = false;
};

void SequenceFile::calcToolbarState_(SeqToolbarState& bar) const
{
    const SeqEditorSettings& settings = gSeqEditorSettings;
    const ImGuiStyle& style = ImGui::GetStyle();

    bar.em = ImGui::GetFontSize();
    bar.frame = ImGui::GetFrameHeight();
    bar.spacing = style.ItemSpacing.x;
    bar.groupGap = ImFloor(bar.em * cToolGroupGapEm);
    bar.buttonSize = ImVec2(bar.frame, bar.frame);

    bar.playing = mIsValid && sSoundPlayer.getSequencePlayer().isPlayingFile(*this);
    bar.canCompile = mIsDirty || !mIsValid;

    // Compile state
    int errorLine = 0;
    std::string errorText;

    bar.stateColor = style.Colors[ImGuiCol_TextDisabled];

    if (mTextEditor->GetFirstError(errorLine, errorText))
    {
        bar.state.format("%s %s", ICON_LC_CIRCLE_X, messages::sequence::cStateError);
        bar.stateColor = cSeqErrorColor;
    }
    else if (bar.canCompile)
    {
        bar.state.format("%s %s", ICON_LC_CIRCLE_DOT, messages::sequence::cStateNotCompiled);
        bar.stateColor = style.Colors[ImGuiCol_Text];
    }
    else
    {
        bar.state.format("%s %s", ICON_LC_CHECK, messages::sequence::cStateCompiled);
    }

    // Banks
    bar.bankName = messages::sequence::cNoBanks;
    {
        const Item* firstBank = nullptr;
        u32 bankNum = 0;

        for (u32 i = 0; i < nw::snd::SoundArchive::SEQ_BANK_MAX; i++)
        {
            const Item* bank = mBankRefs[i]->getItem();
            if (!bank)
                continue;

            if (!firstBank)
                firstBank = bank;

            bankNum++;
        }

        if (bankNum > 1)
            bar.bankName.format(messages::sequence::cBanksMoreFormat, firstBank->getFormattedName().cstr(), bankNum - 1);
        else if (firstBank)
            bar.bankName.format("%s", firstBank->getFormattedName().cstr());
    }

    // Follow
    bar.followName = messages::sequence::cFollowOffButton;
    if (settings.follow)
        bar.followName.format(messages::sequence::cFollowTrackButtonFormat, settings.followTrack);

    const sead::FormatFixedSafeString<64> stateWidest("%s %s", ICON_LC_CIRCLE_DOT, messages::sequence::cStateNotCompiled);
    bar.compileLabel.format("%s %s###Compile", ICON_LC_HAMMER, messages::sequence::cCompileButton);

    // The parts that fit
    bar.startX = ImGui::GetCursorPosX();
    bar.avail = ImGui::GetContentRegionAvail().x;

    bar.compileWidth = ImGui::CalcTextSize(bar.compileLabel.cstr(), nullptr, true).x + style.FramePadding.x * 2.0f;
    bar.banksWidth = ImMin(CalcToolMenuButtonWidth(ICON_LC_PIANO, bar.bankName.cstr()), CalcToolMenuButtonWidth(ICON_LC_PIANO, nullptr) + bar.em * cToolNameMaxEm);
    bar.followWidth = CalcToolMenuButtonWidth(ICON_LC_LOCATE_FIXED, bar.followName.cstr());

    const f32 stateWidth = ImGui::CalcTextSize(stateWidest.cstr()).x;
    const f32 viewWidth = bar.spacing * 2.0f + 1.0f + bar.frame * 3.0f + bar.spacing * 2.0f;

    const f32 namesWidth = bar.frame + bar.spacing + bar.compileWidth + bar.groupGap + bar.banksWidth + bar.spacing + bar.followWidth + viewWidth;
    bar.showNames = namesWidth <= bar.avail;
    bar.showState = bar.showNames && namesWidth + bar.groupGap + stateWidth <= bar.avail;

    bar.leftWidth = bar.frame + bar.spacing + (bar.showNames ? bar.compileWidth : bar.frame) + (bar.showState ? bar.groupGap + stateWidth : 0.0f);
    bar.rightWidth = bar.showNames ? bar.banksWidth + bar.spacing + bar.followWidth + viewWidth : bar.frame + bar.spacing + bar.frame + viewWidth;
}

void SequenceFile::drawTransportTools_(const SeqToolbarState& bar)
{
    // Play
    if (DrawIconButton("##Play", ICON_LC_PLAY, bar.buttonSize, bar.frame, bar.em, bar.playing))
        play_(mTextEditor->GetCursorBlockHeader());
    ImGui::SetItemTooltip("%s", messages::sequence::cPlayTooltip);

    ImGui::SameLine();

    // Compile
    ImGui::BeginDisabled(!bar.canCompile);
    if (bar.showNames ? ImGui::Button(bar.compileLabel.cstr()) : DrawIconButton("##Compile", ICON_LC_HAMMER, bar.buttonSize, bar.frame, bar.em, false))
        compile_();
    ImGui::EndDisabled();

    if (!bar.showNames && IsItemHoveredAllowDisabled())
        ImGui::SetTooltip("%s", bar.canCompile ? messages::sequence::cCompileButton : messages::sequence::cStateCompiled);

    if (bar.showState)
    {
        ImGui::SameLine(0.0f, bar.groupGap);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(bar.stateColor, "%s", bar.state.cstr());
    }
}

static void DrawFollowTool(SeqToolbarState& bar)
{
    SeqEditorSettings& settings = gSeqEditorSettings;

    if (DrawToolMenuButton("##Follow", ICON_LC_LOCATE_FIXED, bar.showNames ? bar.followName.cstr() : nullptr, bar.showNames ? bar.followWidth : bar.frame, ImGui::IsPopupOpen("##SeqFollow")))
        ImGui::OpenPopup("##SeqFollow");

    if (!ImGui::IsPopupOpen("##SeqFollow"))
        ImGui::SetItemTooltip("%s", messages::sequence::cFollowTip);

    SetNextToolMenuPos();

    if (BeginContextMenu("##SeqFollow"))
    {
        if (ContextMenuCheckItem(messages::sequence::cFollowOffItem, !settings.follow))
        {
            settings.follow = false;
            bar.followChanged = true;
        }

        ContextMenuSeparator();

        for (s32 i = 0; i < SequenceSoundPlayer::cTrackNumPerPlayer; i++)
        {
            if (ContextMenuCheckItem(sead::FormatFixedSafeString<32>(messages::sequence::cFollowTrackItemFormat, i).cstr(), settings.follow && settings.followTrack == i))
            {
                settings.follow = true;
                settings.followTrack = i;
                bar.followChanged = true;
            }
        }

        EndContextMenu();
    }

    if (bar.followChanged)
        ImGui::MarkIniSettingsDirty();
}

static void DrawToolGroupLine(f32 frame)
{
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const f32 inset = ImFloor(frame * 0.2f);

    ImGui::GetWindowDrawList()->AddLine(ImVec2(pos.x + 0.5f, pos.y + inset), ImVec2(pos.x + 0.5f, pos.y + frame - inset), ImGui::GetColorU32(ImGuiCol_Separator));
    ImGui::Dummy(ImVec2(1.0f, frame));
}

void SequenceFile::drawViewTools_(const SeqToolbarState& bar)
{
    SeqEditorSettings& settings = gSeqEditorSettings;

    // Manual
    if (DrawIconButton("##Manual", ICON_LC_BOOK_OPEN_TEXT, bar.buttonSize, bar.frame, bar.em, settings.showManual))
    {
        settings.showManual = !settings.showManual;
        ImGui::MarkIniSettingsDirty();
    }
    ImGui::SetItemTooltip("%s", messages::sequence::cManualTooltip);

    ImGui::SameLine();

    // Fold
    const bool anyFolded = mTextEditor->IsAnyFolded();

    if (DrawIconButton("##FoldAll", anyFolded ? ICON_LC_LIST_CHEVRONS_UP_DOWN : ICON_LC_LIST_CHEVRONS_DOWN_UP, bar.buttonSize, bar.frame, bar.em, false))
    {
        if (anyFolded)
            mTextEditor->UnfoldAll();
        else
            mTextEditor->FoldAll();
    }
    ImGui::SetItemTooltip("%s", anyFolded ? messages::sequence::cUnfoldAllTooltip : messages::sequence::cFoldAllTooltip);

    ImGui::SameLine();

    // Options
    if (DrawIconButton("##SeqOptionsButton", ICON_LC_ELLIPSIS, bar.buttonSize, bar.frame, bar.em, false))
        ImGui::OpenPopup("##SeqOptions");

    if (!ImGui::IsPopupOpen("##SeqOptions"))
        ImGui::SetItemTooltip("%s", messages::sequence::cOptionsMenu);

    SetNextToolMenuPos();
}

static void DrawToolbarBottomLine(f32 width)
{
    const ImVec2 pos = ImGui::GetCursorScreenPos();

    ImGui::GetWindowDrawList()->AddLine(ImVec2(pos.x, pos.y + 0.5f), ImVec2(pos.x + width, pos.y + 0.5f), ImGui::GetColorU32(ImGuiCol_Separator));
    ImGui::Dummy(ImVec2(width, 1.0f));
}

static void DrawSeqOptionsMenu()
{
    SeqEditorSettings& settings = gSeqEditorSettings;

    if (!BeginContextMenu("##SeqOptions"))
        return;

    SeqOptionItem(messages::sequence::cAutoCompileLabel, &settings.autoCompile);
    SeqOptionItem(messages::sequence::cTrailLabel, &settings.showTrail);
    SeqOptionItem(messages::sequence::cSuggestLabel, &settings.suggest);
    SeqOptionItem(messages::sequence::cHintsLabel, &settings.parameterHints);
    SeqOptionItem(messages::sequence::cMinimapLabel, &settings.minimap);
    SeqOptionItem(messages::sequence::cWhitespaceLabel, &settings.showWhitespace);

    EndContextMenu();
}

void SequenceFile::updatePlayMarkers_(const SeqToolbarState& bar)
{
    const SeqEditorSettings& settings = gSeqEditorSettings;

    if (bar.playing)
    {
        mSeqTextInfo.update(sSoundPlayer.getSequencePlayer(), *mTextEditor, mOffsetToLine, settings.showTrail);
        mHasPlayMarkers = true;
    }
    else if (mHasPlayMarkers)
    {
        if (mIsValid)
            mTextEditor->SetMarkers({ });

        mHasPlayMarkers = false;
    }

    if (settings.follow && bar.playing && sSoundPlayer.isCurrentPlayerSequence())
    {
        const SeqTextInfo::Track& track = mSeqTextInfo.mTracks[settings.followTrack];
        if (track.active && track.line > 0 && (track.updated || bar.followChanged))
            mTextEditor->ScrollToLine(track.line - 1, true);
    }
}

void SequenceFile::drawToolbar_()
{
    SeqToolbarState bar;
    calcToolbarState_(bar);

    drawTransportTools_(bar);

    ImGui::SameLine();
    ImGui::SetCursorPosX(bar.startX + ImMax(bar.leftWidth + bar.groupGap, bar.avail - bar.rightWidth));

    DrawFollowTool(bar);

    ImGui::SameLine();

    drawBankSelector_(bar.showNames ? bar.bankName.cstr() : nullptr, bar.showNames ? bar.banksWidth : bar.frame);

    ImGui::SameLine();

    DrawToolGroupLine(bar.frame);

    ImGui::SameLine();

    drawViewTools_(bar);

    DrawToolbarBottomLine(bar.avail);
    DrawSeqOptionsMenu();

    updatePlayMarkers_(bar);
}

void SequenceFile::drawFileUI()
{
    SEAD_ASSERT(mTextEditor);

    SeqEditorSettings& settings = gSeqEditorSettings;

    if (mLoadError)
    {
        mLoadError = false;
        compile_();
    }

    drawToolbar_();

    if (mAnnotationsDirty || ImGui::GetFrameCount() % 30 == 0)
        updateAnnotations_();

    mTextEditor->SetPalette(GetSeqPalette(gThemeIsDark));

    const f32 availWidth = ImGui::GetContentRegionAvail().x;
    const f32 splitterWidth = ImGui::GetStyle().ItemSpacing.x;
    const f32 editorWidth = settings.showManual ? ImFloor(availWidth * settings.splitPos) : 0.0f;

    ImGui::BeginChild("SeqEditorPane", ImVec2(editorWidth, 0.0f), false);
    DrawSeqEditor(*mTextEditor);
    ImGui::EndChild();

    if (settings.showManual)
    {
        ImGui::SameLine(0.0f, 0.0f);

        ImGui::InvisibleButton("##SeqSplitter", ImVec2(splitterWidth, ImMax(1.0f, ImGui::GetContentRegionAvail().y)));

        const bool splitterActive = ImGui::IsItemActive();
        const bool splitterHovered = ImGui::IsItemHovered();

        if (splitterActive && availWidth > 0.0f)
        {
            settings.splitPos = ImClamp(settings.splitPos + ImGui::GetIO().MouseDelta.x / availWidth, 0.15f, 0.85f);
            ImGui::MarkIniSettingsDirty();
        }

        if (splitterActive || splitterHovered)
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

        const ImGuiCol splitterColor = splitterActive ? ImGuiCol_SeparatorActive : splitterHovered ? ImGuiCol_SeparatorHovered : ImGuiCol_Separator;
        const f32 splitterX = ImFloor((ImGui::GetItemRectMin().x + ImGui::GetItemRectMax().x) * 0.5f);
        ImGui::GetWindowDrawList()->AddLine(ImVec2(splitterX, ImGui::GetItemRectMin().y), ImVec2(splitterX, ImGui::GetItemRectMax().y), ImGui::GetColorU32(splitterColor));

        ImGui::SameLine(0.0f, 0.0f);

        ImGui::BeginChild("SeqManualPane", ImVec2(0.0f, 0.0f), false);
        DrawSeqManual();
        ImGui::EndChild();
    }

    const s32 playLine = mTextEditor->ConsumeHeaderAction();
    if (playLine >= 0)
        play_(playLine);

    if (!mTextEditor->IsTextChanged())
    {
        if (mAutoCompileTime > 0.0 && ImGui::GetTime() >= mAutoCompileTime)
        {
            mAutoCompileTime = 0.0;

            if (!mIsValid)
                compile_(false);
        }

        return;
    }

    if (sSoundPlayer.getSequencePlayer().isPlayingFile(*this))
        sSoundPlayer.fadeOutPlayers();

    {
        snd::internal::driver::SoundThreadLock lock;
        mSeqTextInfo.reset();
    }

    mTextEditor->SetMarkers({ });
    mHasPlayMarkers = false;
    mAnnotationsDirty = true;

    mIsValid = false;
    mIsDirty = true;
    SetUnsavedChanges(true);

    mAutoCompileTime = settings.autoCompile ? ImGui::GetTime() + cAutoCompileDelay : 0.0;
}

void SequenceFile::setCursorToLabel_(const sead::SafeString& label)
{
    if (!mIsValid || !mTextEditor)
    {
        return;
    }

    auto it = mLabelsStartInfo.find(label.cstr());
    if (it == mLabelsStartInfo.end())
    {
        return;
    }

    u32 offset = it->second.offset;

    auto lineIt = mOffsetToLine.find(offset);
    if (lineIt == mOffsetToLine.end())
    {
        return;
    }

    u32 line = lineIt->second;
    if (line == 0)
        return;

    mTextEditor->SetCursorPosition({ static_cast<s32>(line - 1), 0 });
    mTextEditor->EnsureCursorVisible();
}

const char* GetInstrumentLabel(const BankFile::Instrument& instrument, sead::BufferedSafeString& buffer)
{
    if (instrument.isNameValid() && instrument.getNameOrNull() != "Instrument")
    {
        buffer = instrument.getNameOrNull();
        return buffer.cstr();
    }

    const Item* wave = nullptr;
    for (const Item* item : instrument.getKeyRegionList())
    {
        const BankFile::VelocityRegion* velocityRegion = static_cast<const BankFile::KeyRegion*>(item)->getVelocityRegion(127);
        wave = velocityRegion ? velocityRegion->getWaveFileRef().getItem() : nullptr;
        break;
    }

    if (!wave)
        return nullptr;

    buffer = wave->getFormattedName();

    const s32 regionNum = instrument.getKeyRegionList().size();
    if (regionNum > 1)
        buffer.appendWithFormat(messages::sequence::cMoreRegionsFormat, regionNum - 1);

    return buffer.cstr();
}

void SequenceFile::updateAnnotations_()
{
    mAnnotationsDirty = false;

    TextEditor::Annotations annotations;
    u32 bankIndex = 0;

    const std::vector<std::string> lines = mTextEditor->GetTextLines();
    for (u32 i = 0; i < lines.size(); i++)
    {
        std::string line = lines[i].substr(0, lines[i].find(';'));

        const std::string::size_type labelDelimiter = line.rfind(':');
        if (labelDelimiter != std::string::npos)
        {
            std::string label = line.substr(0, labelDelimiter);
            ltrim(label);

            if (!label.starts_with('_'))
                bankIndex = 0;

            line = line.substr(labelDelimiter + 1);
        }

        trim(line);

        const std::string cmdStr = line.substr(0, line.find_first_of(" \t"));
        const bool isBankSelect = cmdStr == "bank_select";
        if (!isBankSelect && cmdStr != "prg" && cmdStr != "prg_if")
            continue;

        std::string argStr = line.substr(cmdStr.size());
        trim(argStr);

        int arg = 0;
        if (!ParseSeqInt(&arg, argStr))
            continue;

        if (isBankSelect)
        {
            bankIndex = arg;
            continue;
        }

        if (bankIndex >= nw::snd::SoundArchive::SEQ_BANK_MAX)
            continue;

        const Item* bank = mBankRefs[bankIndex]->getItem();
        const Item* bankFileItem = bank ? static_cast<const Bank*>(bank)->getFileRef().getItem() : nullptr;
        if (!bankFileItem)
            continue;

        const BankFile::Instrument* instrument = static_cast<const BankFile*>(bankFileItem)->getInstrument(arg);
        if (!instrument)
        {
            annotations[i] = messages::sequence::cNoInstrument;
            continue;
        }

        sead::FixedSafeString<256> buffer;
        const char* label = GetInstrumentLabel(*instrument, buffer);
        if (label)
            annotations[i] = label;
    }

    mTextEditor->SetAnnotations(annotations);
}
