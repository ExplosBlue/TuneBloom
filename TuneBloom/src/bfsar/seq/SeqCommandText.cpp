#include <bfsar/seq/SeqParseUtil.h>

#include <bfsar/seq/SeqCommand.h>

#include <ui/Messages.h>
#include <ui/app/UI.h>

#include <core/Debug.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <format>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

std::vector<std::string> SplitString(const std::string& text, const std::string& delimiter)
{
    if (text.empty())
        return { };

    std::string::size_type tokenStart = 0;
    std::string::size_type tokenEnd;

    std::vector<std::string> tokens;

    while ((tokenEnd = text.find(delimiter, tokenStart)) != std::string::npos)
    {
        tokens.push_back(text.substr(tokenStart, tokenEnd - tokenStart));
        tokenStart = tokenEnd + delimiter.length();
    }

    tokens.push_back(text.substr(tokenStart));
    return tokens;
}

static std::string PopFront(std::vector<std::string>& values)
{
    if (values.empty())
        return { };

    std::string value = values.front();
    values.erase(values.begin());
    return value;
}

bool ParseSeqInt(int* out, std::string str)
{
    SEAD_ASSERT(out);

    bool negative = false;
    if (str.starts_with('-'))
    {
        negative = true;
        str = str.substr(1);
    }
    else if (str.starts_with('+'))
    {
        str = str.substr(1);
    }

    s32 base = 10;
    if (str.starts_with("0x"))
    {
        base = 16;

        if (!std::all_of(str.begin() + 2, str.end(), ::isxdigit))
        {
            return false;
        }
    }
    else if (str.starts_with("0b"))
    {
        base = 2;

        auto isbdigit = [](int c) -> int
        {
            return c == '0' || c == '1';
        };

        if (!std::all_of(str.begin() + 2, str.end(), isbdigit))
        {
            return false;
        }
    }
    else
    {
        if (!std::all_of(str.begin(), str.end(), ::isdigit))
        {
            return false;
        }
    }

    const char* startPtr = str.data() + (base != 10 ? 2 : 0);
    char* endPtr = nullptr;
    unsigned long long value = std::strtoull(startPtr, &endPtr, base);

    if (endPtr == startPtr) // No characters consumed = parsing failed
    {
        return false;
    }

    if (value > 0x7FFFFFFF)
    {
        return false;
    }

    *out = negative ? -static_cast<int>(value) : static_cast<int>(value);
    return true;
}

bool IsSeqLabelChar(int c)
{
    return ::isalnum(c) || c == '_';
}

static bool CheckArgRange(int value, s32 min, s32 max, u32 curArg, std::string& errorMsg)
{
    if (min <= value && value <= max)
        return true;

    errorMsg = std::format(messages::sequence::cErrorArgRangeFormat, curArg, min, max);
    return false;
}

static bool CheckArg16Range(int value, u32 curArg, std::string& errorMsg)
{
    return CheckArgRange(value, -32768, 65535, curArg, errorMsg);
}

static bool ParseIntArg(int* out, std::vector<std::string>& args, u32 curArg, std::string& errorMsg)
{
    SEAD_ASSERT(out);

    std::string arg = PopFront(args);
    if (arg.empty())
    {
        errorMsg = std::format(messages::sequence::cErrorArgEmptyFormat, curArg);
        return false;
    }

    int argInt;
    if (!ParseSeqInt(&argInt, arg))
    {
        errorMsg = std::format(messages::sequence::cErrorArgNumberFormat, curArg);
        return false;
    }

    *out = argInt;
    return true;
}

static bool ParseListArg(int* out, std::vector<std::string>& args, u32 curArg, const char* const* list, u32 listSize, const std::string& type, std::string& errorMsg)
{
    SEAD_ASSERT(out);

    std::string arg = PopFront(args);
    if (arg.empty())
    {
        errorMsg = std::format(messages::sequence::cErrorArgEmptyFormat, curArg);
        return false;
    }

    int argInt = -1;
    for (u32 i = 0; i < listSize; i++)
    {
        if (arg == list[i])
        {
            argInt = i;
            break;
        }
    }

    if (argInt < 0 && !ParseSeqInt(&argInt, arg))
    {
        errorMsg = std::format(messages::sequence::cErrorArgNumberOrFormat, curArg, type);
        return false;
    }

    *out = argInt;
    return true;
}

static constexpr u32 cVariableNum = 16 * 3;
static constexpr const char* cVariables[cVariableNum] = {
        "VAR_0",
        "VAR_1",
        "VAR_2",
        "VAR_3",
        "VAR_4",
        "VAR_5",
        "VAR_6",
        "VAR_7",
        "VAR_8",
        "VAR_9",
        "VAR_10",
        "VAR_11",
        "VAR_12",
        "VAR_13",
        "VAR_14",
        "VAR_15",

        "GVAR_0",
        "GVAR_1",
        "GVAR_2",
        "GVAR_3",
        "GVAR_4",
        "GVAR_5",
        "GVAR_6",
        "GVAR_7",
        "GVAR_8",
        "GVAR_9",
        "GVAR_10",
        "GVAR_11",
        "GVAR_12",
        "GVAR_13",
        "GVAR_14",
        "GVAR_15",

        "TVAR_0",
        "TVAR_1",
        "TVAR_2",
        "TVAR_3",
        "TVAR_4",
        "TVAR_5",
        "TVAR_6",
        "TVAR_7",
        "TVAR_8",
        "TVAR_9",
        "TVAR_10",
        "TVAR_11",
        "TVAR_12",
        "TVAR_13",
        "TVAR_14",
        "TVAR_15"
};

const char* GetSeqVariableName(u32 varNo)
{
    SEAD_ASSERT(varNo < cVariableNum);

    return cVariables[varNo];
}

static bool ParseVariableArg(int* out, std::vector<std::string>& args, u32 curArg, std::string& errorMsg)
{
    SEAD_ASSERT(out);

    int argInt;
    if (!ParseListArg(&argInt, args, curArg, cVariables, cVariableNum, messages::sequence::cVariableIdentifier, errorMsg))
    {
        return false;
    }

    if (!(0 <= argInt && argInt < cVariableNum))
    {
        errorMsg = std::format(messages::sequence::cErrorArgRangeOpenFormat, curArg, cVariableNum);
        return false;
    }

    *out = argInt;
    return true;
}

struct CommandInfo;

using CommandFactory = MmlCommandBase* (*)();
using ArgCallback = bool (*)(MmlCommandBase* base, const std::string& cmdName, const CommandInfo& info, const std::vector<std::string>& extensions, const std::vector<std::string>& args, std::string& errorMsg);

template <typename T>
static MmlCommandBase* TCommandFactory()
{
    return new T();
}

enum class SeqArgType
{
    None,
    ArgU8,
    ArgS8,
    ArgU16,
    ArgS16,
    ArgVMIDI,
    ArgBool
};

struct CommandInfo
{
    CommandFactory factory;
    SeqArgType defaultArgType;
    u32 baseArgCount;
    bool allowVariableAndRandom;
    bool allowTimeChange;
    bool allowIf;
    ArgCallback argCallback;
    u32 version = 0x00010000;
};

struct CommandExtensions
{
    u32 argNum = 0;
    bool isConditional = false;
    bool foundVariableAndRandomExt = false;
    bool foundTimeChangeExt = false;
    bool foundIfExt = false;
};

enum class ExtensionKind
{
    VariableAndRandom,
    TimeChange,
    If
};

struct ExtensionRule
{
    const char* name;
    ExtensionKind kind;
    u32 argNum;
};

static constexpr ExtensionRule cExtensionRules[] = {
    { "r",  ExtensionKind::VariableAndRandom, 1 },
    { "v",  ExtensionKind::VariableAndRandom, 0 },
    { "t",  ExtensionKind::TimeChange,        1 },
    { "tr", ExtensionKind::TimeChange,        2 },
    { "tv", ExtensionKind::TimeChange,        1 },
    { "if", ExtensionKind::If,                0 },
};

static const ExtensionRule* FindExtensionRule(const std::string& name)
{
    for (const ExtensionRule& rule : cExtensionRules)
    {
        if (name == rule.name)
            return &rule;
    }

    return nullptr;
}

static bool ApplyExtension(CommandExtensions& extInfo, const ExtensionRule& rule, bool isFirst, bool isLast, const std::string& cmdName, const CommandInfo& info, std::string& errorMsg)
{
    bool* found = &extInfo.foundIfExt;
    bool allowed = info.allowIf;

    if (rule.kind == ExtensionKind::VariableAndRandom)
    {
        found = &extInfo.foundVariableAndRandomExt;
        allowed = info.allowVariableAndRandom;
    }
    else if (rule.kind == ExtensionKind::TimeChange)
    {
        found = &extInfo.foundTimeChangeExt;
        allowed = info.allowTimeChange;
    }

    if (*found)
    {
        errorMsg = std::format(messages::sequence::cErrorExtensionTwiceFormat, rule.name);
        return false;
    }

    *found = true;

    if (rule.kind == ExtensionKind::VariableAndRandom && !isFirst)
    {
        errorMsg = std::format(messages::sequence::cErrorExtensionFirstFormat, rule.name);
        return false;
    }

    if (rule.kind == ExtensionKind::If && !isLast)
    {
        errorMsg = std::format(messages::sequence::cErrorExtensionLastFormat, rule.name);
        return false;
    }

    if (!allowed)
    {
        errorMsg = std::format(messages::sequence::cErrorExtensionNotAllowedFormat, cmdName, rule.name);
        return false;
    }

    extInfo.argNum += rule.argNum;

    if (rule.kind == ExtensionKind::If)
        extInfo.isConditional = true;

    return true;
}

static bool ParseCommandExtensions(CommandExtensions& extInfo, const std::string& cmdName, const CommandInfo& info, const std::vector<std::string>& extensions, const std::vector<std::string>& args, std::string& errorMsg)
{
    u32 maxExtNum = 0;
    maxExtNum += info.allowVariableAndRandom;
    maxExtNum += info.allowTimeChange;
    maxExtNum += info.allowIf;

    extInfo = CommandExtensions();
    extInfo.argNum = info.baseArgCount;

    if (extensions.size() > maxExtNum)
    {
        errorMsg = messages::sequence::cErrorTooManyExtensions;
        return false;
    }

    for (u32 i = 0; i < extensions.size(); i++)
    {
        const ExtensionRule* rule = FindExtensionRule(extensions[i]);
        if (!rule)
        {
            errorMsg = std::format(messages::sequence::cErrorExtensionUnknownFormat, extensions[i]);
            return false;
        }

        if (!ApplyExtension(extInfo, *rule, i == 0, i == extensions.size() - 1, cmdName, info, errorMsg))
            return false;
    }

    if (args.size() > extInfo.argNum)
    {
        errorMsg = std::format(messages::sequence::cErrorTooManyArgsFormat, extInfo.argNum, args.size());
        return false;
    }

    if (args.size() < extInfo.argNum)
    {
        errorMsg = std::format(messages::sequence::cErrorMissingArgsFormat, extInfo.argNum, args.size());
        return false;
    }

    return true;
}

static constexpr u32 cTimeChangeArgSlot = 1;

static bool SetNormalArg(MmlCommandBase* base, const CommandInfo& info, std::vector<std::string>& args, std::string& errorMsg, u32 curArg, u32 innerArg)
{
    int argInt;
    if (!ParseIntArg(&argInt, args, innerArg, errorMsg))
    {
        return false;
    }

    const bool isTimeArg = curArg == cTimeChangeArgSlot;

    bool inRange = true;
    if (isTimeArg)
        inRange = CheckArg16Range(argInt, innerArg, errorMsg);
    else if (info.defaultArgType == SeqArgType::ArgU8 || info.defaultArgType == SeqArgType::ArgS8)
        inRange = CheckArgRange(argInt, -128, 255, innerArg, errorMsg);
    else if (info.defaultArgType == SeqArgType::ArgU16 || info.defaultArgType == SeqArgType::ArgS16)
        inRange = CheckArg16Range(argInt, innerArg, errorMsg);
    else if (info.defaultArgType == SeqArgType::ArgVMIDI)
        inRange = CheckArgRange(argInt, 0, 0x0FFFFFFF, innerArg, errorMsg);

    if (!inRange)
        return false;

    SeqArgBase* seqArg = nullptr;
    if (isTimeArg)
    {
        seqArg = new SeqArg16(argInt, true);
    }
    else
    {
        switch (info.defaultArgType)
        {
            case SeqArgType::ArgU8:
                seqArg = new SeqArg8(argInt, false);
                break;
            case SeqArgType::ArgS8:
                seqArg = new SeqArg8(argInt, true);
                break;
            case SeqArgType::ArgU16:
                seqArg = new SeqArg16(argInt, false);
                break;
            case SeqArgType::ArgS16:
                seqArg = new SeqArg16(argInt, true);
                break;
            case SeqArgType::ArgVMIDI:
                seqArg = new SeqArgVMIDI(argInt);
                break;
            default:
                break;
        }
    }

    if (!seqArg || !base->setArg(curArg, seqArg))
    {
        delete seqArg;
        errorMsg = std::format(messages::sequence::cErrorArgInternalFormat, innerArg);
        return false;
    }

    return true;
}

static SeqArgBase* NewRandomArg(std::vector<std::string>& args, u32& curArg, std::string& errorMsg)
{
    int minInt;
    if (!ParseIntArg(&minInt, args, curArg, errorMsg))
        return nullptr;

    curArg++;

    int maxInt;
    if (!ParseIntArg(&maxInt, args, curArg, errorMsg))
        return nullptr;

    if (!CheckArg16Range(minInt, curArg - 1, errorMsg) || !CheckArg16Range(maxInt, curArg, errorMsg))
        return nullptr;

    return new SeqArgRandom(minInt, maxInt);
}

static bool SetRandomArg(MmlCommandBase* base, std::vector<std::string>& args, std::string& errorMsg, u32 curArg, u32 innerArg)
{
    SeqArgBase* seqArg = NewRandomArg(args, innerArg, errorMsg);
    if (!seqArg)
        return false;

    if (!base->setArg(curArg, seqArg))
    {
        delete seqArg;
        errorMsg = std::format(messages::sequence::cErrorArgPairInternalFormat, innerArg - 1, innerArg);
        return false;
    }

    return true;
}

static SeqArgBase* NewVariableArg(std::vector<std::string>& args, u32 curArg, std::string& errorMsg)
{
    int argInt;
    if (!ParseVariableArg(&argInt, args, curArg, errorMsg))
        return nullptr;

    return new SeqArgVariable(argInt);
}

static bool SetVariableArg(MmlCommandBase* base, std::vector<std::string>& args, std::string& errorMsg, u32 curArg, u32 innerArg)
{
    SeqArgBase* seqArg = NewVariableArg(args, innerArg, errorMsg);
    if (!seqArg)
        return false;

    if (!base->setArg(curArg, seqArg))
    {
        delete seqArg;
        errorMsg = std::format(messages::sequence::cErrorArgInternalFormat, innerArg);
        return false;
    }

    return true;
}

static bool SetBoolArg(MmlCommandBase* base, const std::string& cmdName, std::string& errorMsg)
{
    u8 arg = 0;
    if (cmdName.ends_with("on"))
    {
        arg = cmdName.starts_with("damper") ? 127 : 1;
    }
    else if (!cmdName.ends_with("off"))
    {
        errorMsg = messages::sequence::cErrorInternal;
        return false;
    }

    SeqArgBase* seqArg = new SeqArg8(arg, false);
    if (!base->setArg1(seqArg))
    {
        delete seqArg;
        errorMsg = messages::sequence::cErrorInternal;
        return false;
    }

    return true;
}

static bool SetDefaultArgs(MmlCommandBase* base, const CommandInfo& info, const CommandExtensions& extInfo, std::vector<std::string>& extensions, std::vector<std::string>& args, std::string& errorMsg)
{
    bool foundExt[] = { extInfo.foundVariableAndRandomExt, extInfo.foundTimeChangeExt };

    u32 curArg = 0;
    for (u32 i = 0; i < extInfo.argNum; i++)
    {
        std::string ext;
        if (foundExt[curArg])
        {
            ext = PopFront(extensions);
        }

        if (ext == "r" || ext == "tr")
        {
            if (!SetRandomArg(base, args, errorMsg, curArg, i + 1))
                return false;

            i++;
        }
        else if (ext == "v" || ext == "tv")
        {
            if (!SetVariableArg(base, args, errorMsg, curArg, i + 1))
                return false;
        }
        else
        {
            if (!SetNormalArg(base, info, args, errorMsg, curArg, i + 1))
                return false;
        }

        curArg++;
    }

    return true;
}

static MmlCommandBase* BuildCommand(const std::string& cmdName, const CommandInfo& info, std::vector<std::string> extensions, std::vector<std::string> args, std::string& errorMsg)
{
    CommandExtensions extInfo;
    if (!ParseCommandExtensions(extInfo, cmdName, info, extensions, args, errorMsg))
        return nullptr;

    std::unique_ptr<MmlCommandBase> base(info.factory());
    base->mConditional = extInfo.isConditional;

    if (info.argCallback)
    {
        if (!info.argCallback(base.get(), cmdName, info, extensions, args, errorMsg))
        {
            if (errorMsg.empty())
                errorMsg = messages::sequence::cErrorInternal;

            return nullptr;
        }
    }
    else if (info.defaultArgType == SeqArgType::ArgBool)
    {
        if (!SetBoolArg(base.get(), cmdName, errorMsg))
            return nullptr;
    }
    else if (!SetDefaultArgs(base.get(), info, extInfo, extensions, args, errorMsg))
    {
        return nullptr;
    }

    return base.release();
}

static bool CmdNoteArgCallback(MmlCommandBase* base, const std::string& cmdName, const CommandInfo& info, const std::vector<std::string>& extensions_, const std::vector<std::string>& args_, std::string& errorMsg)
{
    SEAD_ASSERT(base);

    std::vector<std::string> extensions = extensions_;
    std::vector<std::string> args = args_;

    MmlCommandNote* cmd = static_cast<MmlCommandNote*>(base);

    bool foundKey = false;
    for (u32 key = 0; key < MmlCommandNote::sKeysNum; key++)
    {
        if (cmdName == MmlCommandNote::sKeys[key])
        {
            cmd->mKey = key;

            foundKey = true;
            break;
        }
    }

    if (!foundKey)
    {
        return false;
    }

    u32 curArg = 1;

    int velocityInt;
    if (!ParseIntArg(&velocityInt, args, curArg, errorMsg))
    {
        return false;
    }

    if (!CheckArgRange(velocityInt, 0, 255, curArg, errorMsg))
    {
        return false;
    }

    cmd->mVelocity = velocityInt;

    curArg++;

    SeqArgBase* length = nullptr;

    std::string ext = PopFront(extensions);
    if (ext == "r")
    {
        length = NewRandomArg(args, curArg, errorMsg);
    }
    else if (ext == "v")
    {
        length = NewVariableArg(args, curArg, errorMsg);
    }
    else
    {
        int lenInt;
        if (ParseIntArg(&lenInt, args, curArg, errorMsg) && CheckArgRange(lenInt, 0, 0x0FFFFFFF, curArg, errorMsg))
            length = new SeqArgVMIDI(lenInt);
    }

    if (!length)
        return false;

    if (!cmd->setArg2(length))
    {
        delete length;
        return false;
    }

    return true;
}

static bool ParseLabelArg(std::string* out, std::vector<std::string>& args, u32 curArg, std::string& errorMsg)
{
    SEAD_ASSERT(out);

    std::string arg = PopFront(args);
    if (arg.empty())
    {
        errorMsg = std::format(messages::sequence::cErrorArgEmptyFormat, curArg);
        return false;
    }

    if (!arg.starts_with('_') && !::isalpha(arg[0]) || !std::all_of(arg.begin(), arg.end(), IsSeqLabelChar))
    {
        errorMsg = std::format(messages::sequence::cErrorArgLabelFormat, curArg);
        return false;
    }

    *out = arg;
    return true;
}

static bool CmdOpenTrackArgCallback(MmlCommandBase* base, const std::string& cmdName, const CommandInfo& info, const std::vector<std::string>& extensions_, const std::vector<std::string>& args_, std::string& errorMsg)
{
    SEAD_ASSERT(base);

    std::vector<std::string> args = args_;

    MmlCommandOpenTrack* cmd = static_cast<MmlCommandOpenTrack*>(base);

    int trackNo;
    if (!ParseIntArg(&trackNo, args, 1, errorMsg))
    {
        return false;
    }

    if (!(0 <= trackNo && trackNo < 16))
    {
        errorMsg = std::format(messages::sequence::cErrorArgRangeOpenFormat, 1, 16);
        return false;
    }

    cmd->mTrackNo = trackNo;

    if (!ParseLabelArg(&cmd->mLabelObj.mLabel, args, 2, errorMsg))
    {
        return false;
    }

    return true;
}

static bool CmdJumpAndCallArgCallback(MmlCommandBase* base, const std::string& cmdName, const CommandInfo& info, const std::vector<std::string>& extensions_, const std::vector<std::string>& args_, std::string& errorMsg)
{
    SEAD_ASSERT(base);

    std::vector<std::string> args = args_;

    std::string label;
    if (!ParseLabelArg(&label, args, 1, errorMsg))
    {
        return false;
    }

    MmlCommandJump* jump = sead::DynamicCast<MmlCommandJump>(base);
    MmlCommandCall* call = sead::DynamicCast<MmlCommandCall>(base);
    if (jump)
    {
        jump->mLabelObj.mLabel = label;
    }
    else if (call)
    {
        call->mLabelObj.mLabel = label;
    }
    else
    {
        return false;
    }

    return true;
}

static bool CmdAllocTrackArgCallback(MmlCommandBase* base, const std::string& cmdName, const CommandInfo& info, const std::vector<std::string>& extensions_, const std::vector<std::string>& args_, std::string& errorMsg)
{
    SEAD_ASSERT(base);

    std::vector<std::string> args = args_;

    MmlCommandAllocTrack* cmd = static_cast<MmlCommandAllocTrack*>(base);

    std::string arg = PopFront(args);
    if (arg.empty())
    {
        errorMsg = std::format(messages::sequence::cErrorArgEmptyFormat, 1);
        return false;
    }

    bool validArg = true;
    int argInt = 0;

    std::vector<std::string> tokens = SplitString(arg, "|");
    for (auto token : tokens)
    {
        trim(token);

        int track = -1;
        for (u32 i = 0; i < 16; i++)
        {
            if (token == "TRACK_" + std::to_string(i))
            {
                track = 1 << i;
                break;
            }
        }

        if (track < 0 && !ParseSeqInt(&track, token))
        {
            validArg = false;
            break;
        }

        argInt |= track;
    }

    if (!validArg && !ParseSeqInt(&argInt, arg))
    {
        errorMsg = std::format(messages::sequence::cErrorArgNumberOrFormat, 1, messages::sequence::cTrackIdentifier);
        return false;
    }

    if (!CheckArgRange(argInt, 0, 65535, 1, errorMsg))
    {
        return false;
    }

    SeqArgBase* seqArg = new SeqArg16(argInt, false);
    if (!cmd->setArg1(seqArg))
    {
        delete seqArg;
        return false;
    }

    return true;
}

static bool CmdPortaArgCallback(MmlCommandBase* base, const std::string& cmdName, const CommandInfo& info, const std::vector<std::string>& extensions_, const std::vector<std::string>& args_, std::string& errorMsg)
{
    SEAD_ASSERT(base);

    std::vector<std::string> args = args_;

    MmlCommandPorta* cmd = static_cast<MmlCommandPorta*>(base);

    int argInt;
    if (!ParseListArg(&argInt, args, 1, MmlCommandNote::sKeys, MmlCommandNote::sKeysNum, messages::sequence::cNoteIdentifier, errorMsg))
    {
        return false;
    }

    if (!(0 <= argInt && argInt < MmlCommandNote::sKeysNum))
    {
        errorMsg = std::format(messages::sequence::cErrorArgRangeOpenFormat, 1, MmlCommandNote::sKeysNum);
        return false;
    }

    SeqArgBase* seqArg = new SeqArg8(argInt, false);
    if (!cmd->setArg1(seqArg))
    {
        delete seqArg;
        return false;
    }

    return true;
}

static bool CmdModTypeArgCallback(MmlCommandBase* base, const std::string& cmdName, const CommandInfo& info, const std::vector<std::string>& extensions_, const std::vector<std::string>& args_, std::string& errorMsg)
{
    SEAD_ASSERT(base);

    std::vector<std::string> args = args_;

    int argInt;
    if (!ParseListArg(&argInt, args, 1, MmlCommandModType::cTypes, MmlCommandModType::cTypeNum, messages::sequence::cModTypeIdentifier, errorMsg))
    {
        return false;
    }

    if (!(0 <= argInt && argInt < MmlCommandModType::cTypeNum))
    {
        errorMsg = std::format(messages::sequence::cErrorArgRangeOpenFormat, 1, MmlCommandModType::cTypeNum);
        return false;
    }

    SeqArgBase* seqArg = new SeqArg8(argInt, false);
    if (!base->setArg1(seqArg))
    {
        delete seqArg;
        return false;
    }

    return true;
}

static bool CmdPrintVarArgCallback(MmlCommandBase* base, const std::string& cmdName, const CommandInfo& info, const std::vector<std::string>& extensions_, const std::vector<std::string>& args_, std::string& errorMsg)
{
    SEAD_ASSERT(base);

    std::vector<std::string> args = args_;

    MmlCommandPrintVar* cmd = static_cast<MmlCommandPrintVar*>(base);

    int argInt;
    if (!ParseVariableArg(&argInt, args, 1, errorMsg))
    {
        return false;
    }

    SeqArgBase* seqArg = new SeqArg8(argInt);
    if (!cmd->setArg1(seqArg))
    {
        delete seqArg;
        errorMsg = std::format(messages::sequence::cErrorArgInternalFormat, 1);
        return false;
    }

    return true;
}

template <typename T>
static bool CmdVarArgCallback(MmlCommandBase* base, const std::string& cmdName, const CommandInfo& info, const std::vector<std::string>& extensions_, const std::vector<std::string>& args_, std::string& errorMsg)
{
    SEAD_ASSERT(base);

    std::vector<std::string> extensions = extensions_;
    std::vector<std::string> args = args_;

    T* cmd = static_cast<T*>(base);

    u32 curArg = 1;

    SeqArgBase* seqArg1 = NewVariableArg(args, curArg, errorMsg);
    if (!seqArg1)
        return false;

    if (!cmd->setArg1(seqArg1))
    {
        delete seqArg1;
        errorMsg = std::format(messages::sequence::cErrorArgInternalFormat, curArg);
        return false;
    }

    curArg++;

    SeqArgBase* seqArg2 = nullptr;

    std::string ext = PopFront(extensions);
    if (ext == "r")
    {
        seqArg2 = NewRandomArg(args, curArg, errorMsg);
    }
    else if (ext == "v")
    {
        seqArg2 = NewVariableArg(args, curArg, errorMsg);
    }
    else
    {
        int valueInt;
        if (ParseIntArg(&valueInt, args, curArg, errorMsg) && CheckArg16Range(valueInt, curArg, errorMsg))
            seqArg2 = new SeqArg16(valueInt, true);
    }

    if (!seqArg2)
        return false;

    if (!cmd->setArg2(seqArg2))
    {
        delete seqArg2;
        return false;
    }

    return true;
}

using NamedCommandInfo = std::pair<const std::string, CommandInfo>;

static const NamedCommandInfo* FindCommandInfo(const std::string& cmd)
{
    static std::unordered_map<std::string, CommandInfo> sCommandInfo = {
        { "wait",            { &TCommandFactory<MmlCommandWait>,          SeqArgType::ArgVMIDI, 1, true,  false, true, nullptr } },
        { "prg",             { &TCommandFactory<MmlCommandPrg>,           SeqArgType::ArgVMIDI, 1, true,  false, true, nullptr } },

        { "opentrack",       { &TCommandFactory<MmlCommandOpenTrack>,     SeqArgType::None,     2, false, false, true, &CmdOpenTrackArgCallback } },
        { "jump",            { &TCommandFactory<MmlCommandJump>,          SeqArgType::None,     1, false, false, true, &CmdJumpAndCallArgCallback } },
        { "call",            { &TCommandFactory<MmlCommandCall>,          SeqArgType::None,     1, false, false, true, &CmdJumpAndCallArgCallback } },

        { "timebase",        { &TCommandFactory<MmlCommandTimebase>,      SeqArgType::ArgU8,    1, false, false, true, nullptr } },
        { "env_hold",        { &TCommandFactory<MmlCommandEnvHold>,       SeqArgType::ArgU8,    1, true,  false, true, nullptr } },
        { "monophonic_on",   { &TCommandFactory<MmlCommandMonophonic>,    SeqArgType::ArgBool,  0, false, false, true, nullptr } },
        { "monophonic_off",  { &TCommandFactory<MmlCommandMonophonic>,    SeqArgType::ArgBool,  0, false, false, true, nullptr } },
        { "velocity_range",  { &TCommandFactory<MmlCommandVelocityRange>, SeqArgType::ArgU8,    1, true,  false, true, nullptr } },
        { "biquad_type",     { &TCommandFactory<MmlCommandBiquadType>,    SeqArgType::ArgU8,    1, true,  false, true, nullptr } },
        { "biquad_value",    { &TCommandFactory<MmlCommandBiquadValue>,   SeqArgType::ArgU8,    1, true,  false, true, nullptr } },
        { "bank_select",     { &TCommandFactory<MmlCommandBankSelect>,    SeqArgType::ArgU8,    1, true,  false, true, nullptr } },
        { "mod_phase",       { &TCommandFactory<MmlCommandModPhase>,      SeqArgType::ArgU8,    1, true,  false, true, nullptr, 0x00020000 } },
        { "mod_curve",       { &TCommandFactory<MmlCommandModCurve>,      SeqArgType::ArgU8,    1, true,  false, true, nullptr, 0x00020000 } },
        { "frontbypass_on",  { &TCommandFactory<MmlCommandFrontBypass>,   SeqArgType::ArgBool,  0, false, false, true, nullptr } },
        { "frontbypass_off", { &TCommandFactory<MmlCommandFrontBypass>,   SeqArgType::ArgBool,  0, false, false, true, nullptr } },
        { "pan",             { &TCommandFactory<MmlCommandPan>,           SeqArgType::ArgU8,    1, true,  true,  true, nullptr } },
        { "volume",          { &TCommandFactory<MmlCommandVolume>,        SeqArgType::ArgU8,    1, true,  true,  true, nullptr } },
        { "main_volume",     { &TCommandFactory<MmlCommandMainVolume>,    SeqArgType::ArgU8,    1, true,  true,  true, nullptr } },
        { "transpose",       { &TCommandFactory<MmlCommandTranspose>,     SeqArgType::ArgS8,    1, true,  false, true, nullptr } },
        { "pitchbend",       { &TCommandFactory<MmlCommandPitchBend>,     SeqArgType::ArgS8,    1, true,  true,  true, nullptr } },
        { "bendrange",       { &TCommandFactory<MmlCommandBendRange>,     SeqArgType::ArgU8,    1, false, false, true, nullptr } },
        { "prio",            { &TCommandFactory<MmlCommandPrio>,          SeqArgType::ArgU8,    1, false, false, true, nullptr } },
        { "notewait_on",     { &TCommandFactory<MmlCommandNoteWait>,      SeqArgType::ArgBool,  0, false, false, true, nullptr } },
        { "notewait_off",    { &TCommandFactory<MmlCommandNoteWait>,      SeqArgType::ArgBool,  0, false, false, true, nullptr } },
        { "tieon",           { &TCommandFactory<MmlCommandTie>,           SeqArgType::ArgBool,  0, false, false, true, nullptr } },
        { "tieoff",          { &TCommandFactory<MmlCommandTie>,           SeqArgType::ArgBool,  0, false, false, true, nullptr } },
        { "porta",           { &TCommandFactory<MmlCommandPorta>,         SeqArgType::ArgU8,    1, false, false, true, &CmdPortaArgCallback } },
        { "mod_depth",       { &TCommandFactory<MmlCommandModDepth>,      SeqArgType::ArgU8,    1, true,  false, true, nullptr } },
        { "mod_speed",       { &TCommandFactory<MmlCommandModSpeed>,      SeqArgType::ArgU8,    1, true,  false, true, nullptr } },
        { "mod_type",        { &TCommandFactory<MmlCommandModType>,       SeqArgType::ArgU8,    1, false, false, true, &CmdModTypeArgCallback } },
        { "mod_range",       { &TCommandFactory<MmlCommandModRange>,      SeqArgType::ArgU8,    1, false, false, true, nullptr } },
        { "porta_on",        { &TCommandFactory<MmlCommandPortaSw>,       SeqArgType::ArgBool,  0, false, false, true, nullptr } },
        { "porta_off",       { &TCommandFactory<MmlCommandPortaSw>,       SeqArgType::ArgBool,  0, false, false, true, nullptr } },
        { "porta_time",      { &TCommandFactory<MmlCommandPortaTime>,     SeqArgType::ArgU8,    1, true,  false, true, nullptr } },
        { "attack",          { &TCommandFactory<MmlCommandAttack>,        SeqArgType::ArgU8,    1, true,  false, true, nullptr } },
        { "decay",           { &TCommandFactory<MmlCommandDecay>,         SeqArgType::ArgU8,    1, true,  false, true, nullptr } },
        { "sustain",         { &TCommandFactory<MmlCommandSustain>,       SeqArgType::ArgU8,    1, true,  false, true, nullptr } },
        { "release",         { &TCommandFactory<MmlCommandRelease>,       SeqArgType::ArgU8,    1, true,  false, true, nullptr } },
        { "loop_start",      { &TCommandFactory<MmlCommandLoopStart>,     SeqArgType::ArgU8,    1, true,  false, true, nullptr } },
        { "volume2",         { &TCommandFactory<MmlCommandVolume2>,       SeqArgType::ArgU8,    1, true,  true,  true, nullptr } },
        { "printvar",        { &TCommandFactory<MmlCommandPrintVar>,      SeqArgType::ArgU8,    1, false, false, true, &CmdPrintVarArgCallback } },
        { "span",            { &TCommandFactory<MmlCommandSurroundPan>,   SeqArgType::ArgU8,    1, true,  true,  true, nullptr } },
        { "lpf_cutoff",      { &TCommandFactory<MmlCommandLPFCutoff>,     SeqArgType::ArgU8,    1, true,  false, true, nullptr } },
        { "fxsend_a",        { &TCommandFactory<MmlCommandFxSendA>,       SeqArgType::ArgU8,    1, true,  false, true, nullptr } },
        { "fxsend_b",        { &TCommandFactory<MmlCommandFxSendB>,       SeqArgType::ArgU8,    1, true,  false, true, nullptr } },
        { "mainsend",        { &TCommandFactory<MmlCommandMainSend>,      SeqArgType::ArgU8,    1, true,  false, true, nullptr } },
        { "init_pan",        { &TCommandFactory<MmlCommandInitPan>,       SeqArgType::ArgU8,    1, true,  false, true, nullptr } },
        { "mute",            { &TCommandFactory<MmlCommandMute>,          SeqArgType::ArgU8,    1, true,  false, true, nullptr } },
        { "fxsend_c",        { &TCommandFactory<MmlCommandFxSendC>,       SeqArgType::ArgU8,    1, true,  false, true, nullptr } },
        { "damper_on",       { &TCommandFactory<MmlCommandDamper>,        SeqArgType::ArgBool,  0, false, false, true, nullptr } },
        { "damper_off",      { &TCommandFactory<MmlCommandDamper>,        SeqArgType::ArgBool,  0, false, false, true, nullptr } },
        { "userproc",        { &TCommandFactory<MmlCommandUserproc>,      SeqArgType::ArgU16,   1, false, false, true, nullptr } },

        { "mod_delay",       { &TCommandFactory<MmlCommandModDelay>,      SeqArgType::ArgS16,   1, true,  false, true, nullptr } },
        { "tempo",           { &TCommandFactory<MmlCommandTempo>,         SeqArgType::ArgS16,   1, true,  false, true, nullptr } },
        { "sweep_pitch",     { &TCommandFactory<MmlCommandSweepPitch>,    SeqArgType::ArgS16,   1, true,  false, true, nullptr } },
        { "mod_period",      { &TCommandFactory<MmlCommandModPeriod>,     SeqArgType::ArgS16,   1, true,  false, true, nullptr } },

        { "env_reset",       { &TCommandFactory<MmlCommandEnvReset>,      SeqArgType::None,     0, false, false, true, nullptr } },
        { "loop_end",        { &TCommandFactory<MmlCommandLoopEnd>,       SeqArgType::None,     0, false, false, true, nullptr } },
        { "ret",             { &TCommandFactory<MmlCommandRet>,           SeqArgType::None,     0, false, false, true, nullptr } },
        { "alloctrack",      { &TCommandFactory<MmlCommandAllocTrack>,    SeqArgType::ArgU16,   1, false, false, true, &CmdAllocTrackArgCallback } },
        { "fin",             { &TCommandFactory<MmlCommandFin>,           SeqArgType::None,     0, false, false, true, nullptr } },

        { "setvar",          { &TCommandFactory<MmlCommandSetVar>,        SeqArgType::ArgU8,    2, true,  false, true, &CmdVarArgCallback<MmlCommandSetVar> } },
        { "addvar",          { &TCommandFactory<MmlCommandAddVar>,        SeqArgType::ArgU8,    2, true,  false, true, &CmdVarArgCallback<MmlCommandAddVar> } },
        { "subvar",          { &TCommandFactory<MmlCommandSubVar>,        SeqArgType::ArgU8,    2, true,  false, true, &CmdVarArgCallback<MmlCommandSubVar> } },
        { "mulvar",          { &TCommandFactory<MmlCommandMulVar>,        SeqArgType::ArgU8,    2, true,  false, true, &CmdVarArgCallback<MmlCommandMulVar> } },
        { "divvar",          { &TCommandFactory<MmlCommandDivVar>,        SeqArgType::ArgU8,    2, true,  false, true, &CmdVarArgCallback<MmlCommandDivVar> } },
        { "shiftvar",        { &TCommandFactory<MmlCommandShiftVar>,      SeqArgType::ArgU8,    2, true,  false, true, &CmdVarArgCallback<MmlCommandShiftVar> } },
        { "randvar",         { &TCommandFactory<MmlCommandRandVar>,       SeqArgType::ArgU8,    2, true,  false, true, &CmdVarArgCallback<MmlCommandRandVar> } },
        { "andvar",          { &TCommandFactory<MmlCommandAndVar>,        SeqArgType::ArgU8,    2, true,  false, true, &CmdVarArgCallback<MmlCommandAndVar> } },
        { "orvar",           { &TCommandFactory<MmlCommandOrVar>,         SeqArgType::ArgU8,    2, true,  false, true, &CmdVarArgCallback<MmlCommandOrVar> } },
        { "xorvar",          { &TCommandFactory<MmlCommandXorVar>,        SeqArgType::ArgU8,    2, true,  false, true, &CmdVarArgCallback<MmlCommandXorVar> } },
        { "notvar",          { &TCommandFactory<MmlCommandNotVar>,        SeqArgType::ArgU8,    2, true,  false, true, &CmdVarArgCallback<MmlCommandNotVar> } },
        { "modvar",          { &TCommandFactory<MmlCommandModVar>,        SeqArgType::ArgU8,    2, true,  false, true, &CmdVarArgCallback<MmlCommandModVar> } },

        { "cmp_eq",          { &TCommandFactory<MmlCommandCmpEq>,         SeqArgType::ArgU8,    2, true,  false, true, &CmdVarArgCallback<MmlCommandCmpEq> } },
        { "cmp_ge",          { &TCommandFactory<MmlCommandCmpGe>,         SeqArgType::ArgU8,    2, true,  false, true, &CmdVarArgCallback<MmlCommandCmpGe> } },
        { "cmp_gt",          { &TCommandFactory<MmlCommandCmpGt>,         SeqArgType::ArgU8,    2, true,  false, true, &CmdVarArgCallback<MmlCommandCmpGt> } },
        { "cmp_le",          { &TCommandFactory<MmlCommandCmpLe>,         SeqArgType::ArgU8,    2, true,  false, true, &CmdVarArgCallback<MmlCommandCmpLe> } },
        { "cmp_lt",          { &TCommandFactory<MmlCommandCmpLt>,         SeqArgType::ArgU8,    2, true,  false, true, &CmdVarArgCallback<MmlCommandCmpLt> } },
        { "cmp_ne",          { &TCommandFactory<MmlCommandCmpNe>,         SeqArgType::ArgU8,    2, true,  false, true, &CmdVarArgCallback<MmlCommandCmpNe> } },

        { "mod2_curve",      { &TCommandFactory<MmlCommandMod2Curve>,     SeqArgType::ArgU8,    1, true,  false, true, nullptr, 0x00020000 } },
        { "mod2_phase",      { &TCommandFactory<MmlCommandMod2Phase>,     SeqArgType::ArgU8,    1, true,  false, true, nullptr, 0x00020000 } },
        { "mod2_depth",      { &TCommandFactory<MmlCommandMod2Depth>,     SeqArgType::ArgU8,    1, true,  false, true, nullptr, 0x00020000 } },
        { "mod2_speed",      { &TCommandFactory<MmlCommandMod2Speed>,     SeqArgType::ArgU8,    1, true,  false, true, nullptr, 0x00020000 } },
        { "mod2_range",      { &TCommandFactory<MmlCommandMod2Range>,     SeqArgType::ArgU8,    1, false, false, true, nullptr, 0x00020000 } },
        { "mod2_type",       { &TCommandFactory<MmlCommandMod2Type>,      SeqArgType::ArgU8,    1, false, false, true, &CmdModTypeArgCallback, 0x00020000 } },
        { "mod2_delay",      { &TCommandFactory<MmlCommandMod2Delay>,     SeqArgType::ArgS16,   1, true,  false, true, nullptr, 0x00020000 } },
        { "mod2_period",     { &TCommandFactory<MmlCommandMod2Period>,    SeqArgType::ArgS16,   1, true,  false, true, nullptr, 0x00020000 } },

        { "mod3_curve",      { &TCommandFactory<MmlCommandMod3Curve>,     SeqArgType::ArgU8,    1, true,  false, true, nullptr, 0x00020000 } },
        { "mod3_phase",      { &TCommandFactory<MmlCommandMod3Phase>,     SeqArgType::ArgU8,    1, true,  false, true, nullptr, 0x00020000 } },
        { "mod3_depth",      { &TCommandFactory<MmlCommandMod3Depth>,     SeqArgType::ArgU8,    1, true,  false, true, nullptr, 0x00020000 } },
        { "mod3_speed",      { &TCommandFactory<MmlCommandMod3Speed>,     SeqArgType::ArgU8,    1, true,  false, true, nullptr, 0x00020000 } },
        { "mod3_range",      { &TCommandFactory<MmlCommandMod3Range>,     SeqArgType::ArgU8,    1, false, false, true, nullptr, 0x00020000 } },
        { "mod3_type",       { &TCommandFactory<MmlCommandMod3Type>,      SeqArgType::ArgU8,    1, false, false, true, &CmdModTypeArgCallback, 0x00020000 } },
        { "mod3_delay",      { &TCommandFactory<MmlCommandMod3Delay>,     SeqArgType::ArgS16,   1, true,  false, true, nullptr, 0x00020000 } },
        { "mod3_period",     { &TCommandFactory<MmlCommandMod3Period>,    SeqArgType::ArgS16,   1, true,  false, true, nullptr, 0x00020000 } },

        { "mod4_curve",      { &TCommandFactory<MmlCommandMod4Curve>,     SeqArgType::ArgU8,    1, true,  false, true, nullptr, 0x00020000 } },
        { "mod4_phase",      { &TCommandFactory<MmlCommandMod4Phase>,     SeqArgType::ArgU8,    1, true,  false, true, nullptr, 0x00020000 } },
        { "mod4_depth",      { &TCommandFactory<MmlCommandMod4Depth>,     SeqArgType::ArgU8,    1, true,  false, true, nullptr, 0x00020000 } },
        { "mod4_speed",      { &TCommandFactory<MmlCommandMod4Speed>,     SeqArgType::ArgU8,    1, true,  false, true, nullptr, 0x00020000 } },
        { "mod4_range",      { &TCommandFactory<MmlCommandMod4Range>,     SeqArgType::ArgU8,    1, false, false, true, nullptr, 0x00020000 } },
        { "mod4_type",       { &TCommandFactory<MmlCommandMod4Type>,      SeqArgType::ArgU8,    1, false, false, true, &CmdModTypeArgCallback, 0x00020000 } },
        { "mod4_delay",      { &TCommandFactory<MmlCommandMod4Delay>,     SeqArgType::ArgS16,   1, true,  false, true, nullptr, 0x00020000 } },
        { "mod4_period",     { &TCommandFactory<MmlCommandMod4Period>,    SeqArgType::ArgS16,   1, true,  false, true, nullptr, 0x00020000 } },
    };

    static bool sInitedCmdInfo = false;
    if (!sInitedCmdInfo)
    {
        sInitedCmdInfo = true;

        for (u32 key = 0; key < MmlCommandNote::sKeysNum; key++)
        {
            sCommandInfo[MmlCommandNote::sKeys[key]] = { &TCommandFactory<MmlCommandNote>, SeqArgType::ArgVMIDI, 2, true, false, true, &CmdNoteArgCallback };
        }
    }

    auto it = sCommandInfo.find(cmd);
    if (it != sCommandInfo.end())
        return &*it;

    std::vector<std::string> parts = SplitString(cmd, "_");

    for (size_t i = 0; i < parts.size(); i++)
    {
        std::string baseCommand = parts[0];
        for (size_t j = 1; j < parts.size() - i; j++)
        {
            baseCommand += "_" + parts[j];
        }

        it = sCommandInfo.find(baseCommand);
        if (it != sCommandInfo.end())
            return &*it;
    }

    return nullptr;
}

MmlCommandBase* ParseSeqCommand(const std::string& str, const std::vector<std::string>& args, std::string& errorMsg)
{
    LOG_FUNC();

    const NamedCommandInfo* named = FindCommandInfo(str);
    if (!named)
    {
        errorMsg = std::format(messages::sequence::cErrorUnknownCommandFormat, str);
        return nullptr;
    }

    const std::string& cmdName = named->first;
    const CommandInfo& cmdInfo = named->second;

    if (cmdInfo.version > sBfsar.getVersionForBfseq())
    {
        errorMsg = std::format(messages::sequence::cCommandVersionFormat, cmdName, GetInnerFileDisplayName(sBfsar.getFormat(), InnerFileKind::Sequence), cmdInfo.version);
        errorMsg += std::format(messages::sequence::cErrorCurrentVersionFormat, sBfsar.getVersionForBfseq());
        return nullptr;
    }

    std::vector<std::string> extensions = SplitString(str.substr(cmdName.size()), "_");
    if (!extensions.empty())
        PopFront(extensions);

    return BuildCommand(cmdName, cmdInfo, extensions, args, errorMsg);
}

