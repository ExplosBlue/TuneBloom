#include <bfsar/files/SequenceFile.h>

#include <bfsar/seq/SeqCommand.h>
#include <bfsar/seq/SeqParseUtil.h>
#include <bfsar/model/Sound.h>

#include <ui/Messages.h>
#include <ui/app/UI.h>

#include <core/Debug.h>

#include <algorithm>
#include <cctype>
#include <format>
#include <string>
#include <unordered_map>
#include <vector>

struct SeqCompileState
{
    ~SeqCompileState()
    {
        for (MmlCommandBase* cmd : commands)
            delete cmd;
    }

    bool setCursorPos = true;

    std::unordered_map<std::string, std::pair<u32, MmlCommandBase*>> globalLabels; // <global label, <line, command after it>>
    std::unordered_map<std::string, std::unordered_map<std::string, std::pair<u32, MmlCommandBase*>>> scopedLocalLabels; // <global label, <local label, <line, command after it>>>
    std::vector<MmlCommandBase*> commands;
    std::unordered_map<MmlCommandBase*, u32> commandLine; // <command, line>
    std::unordered_map<MmlCommandBase*, std::string> commandGlobalScope; // <command, global label>
    std::unordered_map<MmlCommandBase*, u32> commandOffset; // <command, byte offset>
    std::vector<MmlCommandBase*> labelCommands;
    std::vector<u8> seqBytes;

    std::string currentGlobalLabel;
    std::vector<std::pair<std::string, std::string>> pendingLabels; // <scope, label>
};

void SequenceFile::setCompileError_(const SeqCompileState& state, u32 line, const std::string& errorMsg)
{
    discardSeqBytes_();

    mLabels.clear();
    mOffsetToLine.clear();

    if (mTextEditor)
        showEditorError_(line, errorMsg, state.setCursorPos);
}

std::vector<std::string> SequenceFile::getSourceLines_() const
{
    if (mTextEditor)
        return getEditorLines_();

    return SplitString(mSeqText ? mSeqText->cstr() : "", "\n");
}

static void BindPendingLabels(SeqCompileState& state, MmlCommandBase* cmd)
{
    for (const auto& [scope, label] : state.pendingLabels)
    {
        if (label.starts_with('_'))
            state.scopedLocalLabels[scope][label].second = cmd;
        else
            state.globalLabels[label].second = cmd;
    }

    state.pendingLabels.clear();
}

bool SequenceFile::defineLabel_(SeqCompileState& state, const std::string& label, u32 line)
{
    if (label.empty())
    {
        setCompileError_(state, line, messages::sequence::cErrorLabelEmpty);
        return false;
    }

    if (!label.starts_with('_') && !::isalpha(label[0]))
    {
        setCompileError_(state, line, messages::sequence::cErrorLabelStart);
        return false;
    }

    if (!std::all_of(label.begin(), label.end(), IsSeqLabelChar))
    {
        setCompileError_(state, line, messages::sequence::cErrorLabelInvalid);
        return false;
    }

    if (label.starts_with('_'))
    {
        // TODO: commented out for v3 bfsar to work, which seems to load and run fine ingame
        // i believe kirby rtdl on wii also does this, so maybe it's fine
        // worth rechecking later
        //
        // if (currentGlobalLabel.empty())
        // {
        //     setError(i, "Local label found outside any global label scope");
        //     return;
        // }

        std::unordered_map<std::string, std::pair<u32, MmlCommandBase*>>& localLabels = state.scopedLocalLabels[state.currentGlobalLabel];

        auto it = localLabels.find(label);
        if (it != localLabels.end())
        {
            u32 labelLine = it->second.first;
            setCompileError_(state, line, std::format(messages::sequence::cErrorLocalLabelDefinedFormat, label, labelLine + 1));
            return false;
        }

        localLabels[label] = { line, nullptr };
    }
    else
    {
        auto it = state.globalLabels.find(label);
        if (it != state.globalLabels.end())
        {
            u32 labelLine = it->second.first;
            setCompileError_(state, line, std::format(messages::sequence::cErrorGlobalLabelDefinedFormat, label, labelLine + 1));
            return false;
        }

        state.globalLabels[label] = { line, nullptr };
        state.currentGlobalLabel = label;

        mLabels.push_back(label);
    }

    state.pendingLabels.push_back({ label.starts_with('_') ? state.currentGlobalLabel : std::string(), label });
    return true;
}

bool SequenceFile::parseLines_(SeqCompileState& state)
{
    u32 lastNonEmptyLine = 0;

    std::vector<std::string> lines = getSourceLines_();
    for (u32 i = 0; i < lines.size(); i++)
    {
        std::string line = lines[i];
        trim(line);

        if (line.empty() || line.starts_with(';'))
            continue;

        lastNonEmptyLine = i;

        line = line.substr(0, line.find(';'));
        rtrim(line);

        std::string::size_type labelDelimiter = line.rfind(':');
        if (labelDelimiter != std::string::npos)
        {
            std::string label = line.substr(0, labelDelimiter);
            rtrim(label);

            line = line.substr(labelDelimiter + 1);
            ltrim(line);

            if (!defineLabel_(state, label, i))
                return false;
        }

        if (line.empty())
            continue;

        std::string cmdStr = line.substr(0, line.find_first_of(" \t"));
        std::string argsStr = line.substr(cmdStr.size());
        ltrim(argsStr);

        std::vector<std::string> args = SplitString(argsStr, ",");
        for (auto& arg : args)
        {
            trim(arg);
        }

        std::string errorMsg;
        MmlCommandBase* cmd = ParseSeqCommand(cmdStr, args, errorMsg);
        if (!cmd)
        {
            setCompileError_(state, i, errorMsg);

            return false;
        }

        state.commands.push_back(cmd);
        state.commandLine[cmd] = i;
        state.commandGlobalScope[cmd] = state.currentGlobalLabel;

        BindPendingLabels(state, cmd);
    }

    //? FIN
    {
        MmlCommandBase* cmd = new MmlCommandFin(false);
        state.commands.push_back(cmd);

        BindPendingLabels(state, cmd);

        state.commandLine[cmd] = lastNonEmptyLine;
    }

    return true;
}

bool SequenceFile::encodeCommands_(SeqCompileState& state)
{
    for (MmlCommandBase* cmd : state.commands)
    {
        SEAD_ASSERT(cmd);

        u32 offset = state.seqBytes.size();
        state.commandOffset[cmd] = offset;

        std::vector<u8> cmdBytes = cmd->encode();
        if (cmdBytes.empty())
        {
            setCompileError_(state, state.commandLine[cmd], messages::sequence::cErrorInternal);
            return false;
        }

        state.seqBytes.insert(state.seqBytes.end(), cmdBytes.begin(), cmdBytes.end());

        if (sead::IsDerivedTypes<MmlCommandOpenTrack>(cmd) || sead::IsDerivedTypes<MmlCommandJump>(cmd) || sead::IsDerivedTypes<MmlCommandCall>(cmd))
        {
            state.labelCommands.push_back(cmd);
        }

        mOffsetToLine[offset] = state.commandLine[cmd] + 1;
    }

    return true;
}

static bool FindLabelTarget(MmlCommandBase** outTarget, SeqCompileState& state, MmlCommandBase* cmd, const std::string& label, std::string& errorMsg)
{
    if (!label.starts_with('_'))
    {
        auto it = state.globalLabels.find(label);
        if (it == state.globalLabels.end())
        {
            errorMsg = std::format(messages::sequence::cErrorGlobalLabelNotFoundFormat, label);
            return false;
        }

        *outTarget = it->second.second;
        return true;
    }

    const std::string& scope = state.commandGlobalScope[cmd];

    auto scopeIt = state.scopedLocalLabels.find(scope);
    if (scopeIt != state.scopedLocalLabels.end())
    {
        auto it = scopeIt->second.find(label);
        if (it != scopeIt->second.end())
        {
            *outTarget = it->second.second;
            return true;
        }
    }

    errorMsg = std::format(messages::sequence::cErrorLocalLabelNotFoundFormat, label, scope);
    return false;
}

bool SequenceFile::resolveLabels_(SeqCompileState& state)
{
    //? Fix commands that needs labels
    for (MmlCommandBase* cmd : state.labelCommands)
    {
        u32 offset = state.commandOffset[cmd];

        LabelObj* labelObj = nullptr;

        MmlCommandOpenTrack* openTrack = sead::DynamicCast<MmlCommandOpenTrack>(cmd);
        MmlCommandJump* jump = sead::DynamicCast<MmlCommandJump>(cmd);
        MmlCommandCall* call = sead::DynamicCast<MmlCommandCall>(cmd);
        if (openTrack)
        {
            labelObj = &openTrack->mLabelObj;
        }
        else if (jump)
        {
            labelObj = &jump->mLabelObj;
        }
        else if (call)
        {
            labelObj = &call->mLabelObj;
        }
        else
        {
            SEAD_ASSERT(false);
            return false;
        }

        MmlCommandBase* dstCmd = nullptr;
        std::string errorMsg;
        if (!FindLabelTarget(&dstCmd, state, cmd, labelObj->mLabel, errorMsg))
        {
            setCompileError_(state, state.commandLine[cmd], errorMsg);
            return false;
        }

        labelObj->mOffset = state.commandOffset[dstCmd];

        if (labelObj->mOffset > 0xFFFFFF)
        {
            setCompileError_(state, state.commandLine[cmd], messages::sequence::cErrorJumpOutOfBounds);
            return false;
        }

        std::vector<u8> cmdBytes = cmd->encode();
        sead::MemUtil::copy(state.seqBytes.data() + offset, cmdBytes.data(), cmdBytes.size());
    }

    return true;
}

void SequenceFile::storeCompiled_(SeqCompileState& state)
{
    discardSeqBytes_();
    setSeqBytes_(state.seqBytes.data(), state.seqBytes.size());

    auto getStartInfo = [&](MmlCommandBase* cmd) -> LabelStartInfo
    {
        u32 offset = state.commandOffset[cmd];

        u32 parsedOffset = offset;
        u32 allocTrackFlags = 1;
        MmlCommandAllocTrack* allocTrack = sead::DynamicCast<MmlCommandAllocTrack>(cmd);
        if (allocTrack)
        {
            SeqArg16* arg = sead::DynamicCast<SeqArg16>(allocTrack->mArg);
            SEAD_ASSERT(arg);

            parsedOffset += 3; // Skip alloctrack command
            allocTrackFlags = arg->mValue | 1; // Always enable track 0
        }

        return { offset, parsedOffset, allocTrackFlags };
    };

    for (const auto& [label, target] : state.globalLabels)
    {
        mLabelsStartInfo[label] = getStartInfo(target.second);
    }

    mLabelsStartInfo[""] = getStartInfo(state.commands[0]); // Empty label points to start

    mIsValid = true;
    mIsDirty = false;
    mMd5Dirty = true;

    if (mTextEditor)
    {
        clearEditorMarkers_();
        syncRenamedLabels_();
    }

    mCompiledLabels = mLabels;

    LOG_FMT("%s: compiled OK, %u bytes", getNameOrNull().cstr(), mSeqBytesSize);
}

void SequenceFile::compile_(bool setCursorPos)
{
    MmlParser::sSeqParamEndian = getSeqParamEndian();
    LOG_FMT("%s: compiling...", getNameOrNull().cstr());
    if (mSeqText)
    {
        LOG_FMT("--- sequence code for %s ---\n%s\n--- end sequence code ---", getNameOrNull().cstr(), mSeqText->cstr());
    }
    mVersion = sBfsar.getVersionForBfseq();
    mEndian = sBfsar.getEndian();
    mIsValid = false;
    mLabelsStartInfo.clear();
    mLabels.clear();
    mOffsetToLine.clear();

    SeqCompileState state;
    state.setCursorPos = setCursorPos;

    if (!parseLines_(state))
        return;

    if (!encodeCommands_(state))
        return;

    if (!resolveLabels_(state))
        return;

    storeCompiled_(state);
}

bool SequenceFile::recompileIfInvalid_()
{
    if (isValid())
        return true;

    if (!mSeqText)
        return false;

    compile_(false);

    return isValid();
}

void SequenceFile::syncRenamedLabels_()
{
    if (mCompiledLabels.size() != mLabels.size())
        return;

    for (u32 i = 0; i < mLabels.size(); i++)
    {
        const std::string& oldLabel = mCompiledLabels[i];
        const std::string& newLabel = mLabels[i];

        if (oldLabel == newLabel)
            continue;

        if (std::find(mLabels.begin(), mLabels.end(), oldLabel) != mLabels.end())
            continue;

        if (std::find(mCompiledLabels.begin(), mCompiledLabels.end(), newLabel) != mCompiledLabels.end())
            continue;

        for (Item* item : sBfsar.getSoundList())
        {
            Sound* sound = static_cast<Sound*>(item);
            if (sound->getSoundType() != Sound::SoundType::Seq)
                continue;

            Sound::SequenceSoundInfo& info = sound->getSequenceSoundInfo();
            if (info.getSequenceFileRef().getItem() != this)
                continue;

            if (info.getStartLabel() != sead::SafeString(oldLabel.c_str()))
                continue;

            info.getStartLabel() = newLabel.c_str();
            SetUnsavedChanges(true);
        }
    }
}
