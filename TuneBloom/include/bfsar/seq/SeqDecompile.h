#pragma once

#include <basis/seadTypes.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace nw { namespace snd { namespace internal {

class SequenceSoundFileReader;

} } } // namespace nw::snd::internal

class MmlCommandBase;

struct SeqFileLayout
{
    const u8* seqData = nullptr;
    u32 seqDataSize = 0;
};

bool CheckSequenceFileLayout(SeqFileLayout& layout, const nw::snd::internal::SequenceSoundFileReader& reader);
bool DecompileSequenceFile(std::vector<std::string>& outLines, std::unordered_map<u32, u32>& offsetToLine, nw::snd::internal::SequenceSoundFileReader& reader, const SeqFileLayout& layout);

MmlCommandBase* nw__snd__internal__driver__MmlParser__Parse(const u8*& trackData, nw::snd::internal::SequenceSoundFileReader& reader);
