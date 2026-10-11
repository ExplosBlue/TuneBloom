#include <midi/InstBankExporter.h>

#include <bfsar/model/Bank.h>
#include <bfsar/files/BankFile.h>
#include <bfsar/audio/DecodedPcm.h>
#include <bfsar/files/SequenceFile.h>
#include <bfsar/model/Sound.h>
#include <bfsar/files/WaveFile.h>
#include <bfsar/audio/WaveFileEditDecode.h>

#include <midi/ExportCommon.h>
#include <midi/SeqMidiExporter.h>

#include <ui/Messages.h>
#include <ui/app/PopupMgr.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

static constexpr const char* cDefaultBankName      = "TuneBloom Bank";
static constexpr const char* cDefaultSequenceName  = "Sequence";
static constexpr const char* cSoftwareName         = "TuneBloom";
static constexpr const char* cSoundEngineName      = "EMU8000";
static constexpr const char* cInstrumentNamePrefix = "INST_";
static constexpr const char* cPresetNamePrefix     = "PRG_";
static constexpr const char* cWaveNamePrefix       = "WAVE_";

static constexpr u32 cDefaultSampleRate = 22050;
static constexpr u16 cSf2DrumBank       = 128;
static constexpr u32 cDlsDrumBank       = 0x80000000;

struct ExportSample
{
    std::vector<s16> pcm;
    u32 sampleRate;
    s32 loopStart;
    s32 loopEnd;
    u8 rootKey;
    std::string name;
    s8 stereoSide = 0;
    s32 linkedSample = -1;
};

struct ExportRegion
{
    u8 keyMin, keyMax, velMin, velMax;
    u8 rootKey, volume, pan;
    u8 attack, decay, sustain, hold, release;
    bool loop;
    u32 sampleIndex;
    s16 coarseTune = 0;
    s16 fineTune = 0;
    bool hardPanOverride = false;
    s16 hardPanValue = 0;
    u8 keyGroup = 0;
};

struct ExportInstrument
{
    u16 bankNo;
    u16 programNo;
    bool drum = false;
    std::string name;
    std::vector<ExportRegion> regions;
};

struct CachedSampleIndices
{
    s32 mono = -1;
    s32 right = -1;
};

struct ExportModel
{
    std::vector<ExportSample> samples;
    std::vector<ExportInstrument> instruments;
    u8 volumeReference = 127;
};

struct ExportModelBuilder
{
    ExportModel model;
    std::unordered_map<const WaveFile*, CachedSampleIndices> sampleCache;
};

using ExportModelWriter = bool (*)(const sead::SafeString& path, const ExportModel& model, const char* bankName);

static s16 TimecentsFromSeconds(double sec)
{
    if (sec <= 0.0)
        return -32768;

    double tc = std::round(1200.0 * std::log2(sec));

    if (tc < -32768.0)
        tc = -32768.0;

    if (tc > 32767.0)
        tc = 32767.0;

    return (s16)tc;
}

constexpr s16 cEnvelopeTimecentsMin = -12000;
constexpr s16 cEnvelopeTimecentsMax = 8000;
constexpr s16 cHoldTimecentsMax = 5000;

static s16 ClampEnvelopeTimecents(s16 timecents, s16 maxTimecents = cEnvelopeTimecentsMax)
{
    if (timecents < cEnvelopeTimecentsMin)
        return cEnvelopeTimecentsMin;

    if (timecents > maxTimecents)
        return maxTimecents;

    return timecents;
}

static const u8 cAttackTailTable[127 - 109 + 1] = {
    0, 1, 5, 14, 26, 38, 51, 63, 73, 84,
    92, 100, 109, 116, 123, 127, 132, 137, 143};

static f32 AttackCoefficient(u8 attack)
{
    double x = (attack < 109) ? (255.0 - attack) : (double)cAttackTailTable[127 - attack];
    return (f32)std::pow(x / 256.0, 1.0 / 5.0);
}

static const s16 cSustainDbX10Table[128] = {
    -723, -722, -721, -651, -601, -562, -530, -503,
    -480, -460, -442, -425, -410, -396, -383, -371,
    -360, -349, -339, -330, -321, -313, -305, -297,
    -289, -282, -276, -269, -263, -257, -251, -245,
    -239, -234, -229, -224, -219, -214, -210, -205,
    -201, -196, -192, -188, -184, -180, -176, -173,
    -169, -165, -162, -158, -155, -152, -149, -145,
    -142, -139, -136, -133, -130, -127, -125, -122,
    -119, -116, -114, -111, -109, -106, -103, -101,
    -99, -96, -94, -91, -89, -87, -85, -82,
    -80, -78, -76, -74, -72, -70, -68, -66,
    -64, -62, -60, -58, -56, -54, -52, -50,
    -49, -47, -45, -43, -42, -40, -38, -36,
    -35, -33, -31, -30, -28, -27, -25, -23,
    -22, -20, -19, -17, -16, -14, -13, -11,
    -10, -8, -7, -6, -4, -3, -1, 0};

static u8 ClampAdshrIndex(u8 idx)
{
    return idx > 127 ? 127 : idx;
}

static double CalcEngineRate(u8 idx)
{
    idx = ClampAdshrIndex(idx);

    if (idx == 127)
        return 65535.0;

    if (idx == 126)
        return 120.0 / 5.0;

    if (idx < 50)
        return ((idx << 1) + 1) / 128.0 / 5.0;

    return (60.0 / (126 - idx)) / 5.0;
}

static double EngineDecayReleaseSeconds(u8 idx)
{
    return 1.0 / CalcEngineRate(idx);
}

static double EngineAttackSeconds(u8 idx)
{
    f32 attack = AttackCoefficient(ClampAdshrIndex(idx));

    if (attack <= 0.0f)
        return 0.0;

    const double cInitValueX10 = -904.0;
    const double cThresholdX10 = -1.0 / 32.0;
    double n = std::log(cThresholdX10 / cInitValueX10) / std::log(static_cast<double>(attack));

    return n > 0.0 ? n / 1000.0 : 0.0;
}

static double EngineHoldSeconds(u8 idx)
{
    idx = ClampAdshrIndex(idx);
    return ((double)(idx + 1) * (double)(idx + 1) / 4.0) / 1000.0;
}

constexpr double cDlsEnvelopeRangeDbX10 = 960.0;

static s16 EngineSustainCb(u8 idx)
{
    return (s16)(-cSustainDbX10Table[ClampAdshrIndex(idx)]);
}

static s32 EngineSustainPermille(u8 idx)
{
    double dbX10 = (double)cSustainDbX10Table[ClampAdshrIndex(idx)];
    s32 permille = (s32)std::lround(1000.0 * (1.0 + dbX10 / cDlsEnvelopeRangeDbX10));

    if (permille < 0)
        permille = 0;

    if (permille > 1000)
        permille = 1000;

    return permille;
}

struct RegionGenSet
{
    s16 attackTc, holdTc, decayTc, releaseTc;
    s16 sustainCb;
    s32 sustainPermille;
    s16 pan;
    s16 initAttenCb;
    s16 coarseTune;
    s16 fineTune;
    s32 pitchCentsDls;
};

static RegionGenSet ComputeRegionGenSet(const ExportRegion& r, u8 volumeReference)
{
    RegionGenSet g;

    g.attackTc = TimecentsFromSeconds(EngineAttackSeconds(r.attack));
    g.decayTc = TimecentsFromSeconds(EngineDecayReleaseSeconds(r.decay));
    g.releaseTc = TimecentsFromSeconds(EngineDecayReleaseSeconds(r.release));
    g.holdTc = (r.hold == 0) ? cEnvelopeTimecentsMin : TimecentsFromSeconds(EngineHoldSeconds(r.hold));

    g.sustainCb = EngineSustainCb(r.sustain);
    g.sustainPermille = EngineSustainPermille(r.sustain);

    if (r.hardPanOverride)
    {
        g.pan = r.hardPanValue;
    }
    else
    {
        int pan = (int)std::lround((r.pan - 64) / 63.0 * 500.0);

        if (pan < -500)
            pan = -500;

        if (pan > 500)
            pan = 500;

        g.pan = (s16)pan;
    }

    double linearVol = static_cast<double>(r.volume) / volumeReference;
    double attenDb = linearVol > 0.0 ? -20.0 * std::log10(linearVol) : 144.0;
    int atten = (int)std::lround(attenDb * 10.0);

    if (atten < 0)
        atten = 0;

    if (atten > 1440)
        atten = 1440;

    g.initAttenCb = (s16)atten;

    g.coarseTune = r.coarseTune;
    g.fineTune = r.fineTune;
    g.pitchCentsDls = (s32)r.coarseTune * 100 + (s32)r.fineTune;

    return g;
}

static void PutU16(std::vector<u8>& o, u16 v)
{
    o.push_back(v & 0xFF);
    o.push_back((v >> 8) & 0xFF);
}

static void PutU32(std::vector<u8>& o, u32 v)
{
    o.push_back(v & 0xFF);
    o.push_back((v >> 8) & 0xFF);
    o.push_back((v >> 16) & 0xFF);
    o.push_back((v >> 24) & 0xFF);
}

static void PutBytes(std::vector<u8>& o, const void* p, u32 n)
{
    const u8* b = static_cast<const u8*>(p);
    o.insert(o.end(), b, b + n);
}

static void AppendChunk(std::vector<u8>& out, const char* tag, const std::vector<u8>& data)
{
    PutBytes(out, tag, 4);
    PutU32(out, (u32)data.size());

    out.insert(out.end(), data.begin(), data.end());

    if (data.size() & 1)
        out.push_back(0);
}

static void AppendListChunk(std::vector<u8>& out, const char* listType, const std::vector<u8>& payload)
{
    std::vector<u8> body;
    PutBytes(body, listType, 4);
    body.insert(body.end(), payload.begin(), payload.end());
    AppendChunk(out, "LIST", body);
}

static void PutName20(std::vector<u8>& o, const std::string& name)
{
    char buf[20];

    std::memset(buf, 0, sizeof(buf));
    size_t n = name.size() < 19 ? name.size() : 19;
    std::memcpy(buf, name.data(), n);

    PutBytes(o, buf, 20);
}

static std::vector<u8> ZeroTerminatedEven(const std::string& s)
{
    std::vector<u8> b(s.begin(), s.end());
    b.push_back(0);

    if (b.size() & 1)
        b.push_back(0);

    return b;
}

static const char* BankNameOrDefault(const char* bankName)
{
    return bankName && *bankName ? bankName : cDefaultBankName;
}

static std::string InstrumentDisplayName(const ExportInstrument& in, const char* fallbackPrefix)
{
    if (!in.name.empty())
        return in.name;

    return fallbackPrefix + std::to_string(in.programNo);
}

static std::string ResolveItemName(const Item& item, const char* fallback)
{
    sead::FixedSafeString<256> name(item.isNameValid() ? item.getName().cstr() : fallback);
    SanitizeFileName(&name);

    return name.cstr();
}

enum
{
    GEN_PAN = 17,
    GEN_INSTRUMENT = 41,
    GEN_KEYRANGE = 43,
    GEN_VELRANGE = 44,
    GEN_ATTACKVOLENV = 34,
    GEN_HOLDVOLENV = 35,
    GEN_DECAYVOLENV = 36,
    GEN_SUSTAINVOLENV = 37,
    GEN_RELEASEVOLENV = 38,
    GEN_INITATTEN = 48,
    GEN_COARSETUNE = 51,
    GEN_FINETUNE = 52,
    GEN_SAMPLEMODES = 54,
    GEN_EXCLUSIVECLASS = 57,
    GEN_ROOTKEY = 58,
    GEN_SAMPLEID = 53,
};

constexpr u32 cSf2LoopTailFrames  = 8;
constexpr u32 cSf2SamplePadFrames = 46;
constexpr u32 cSf2RegionGenCount  = 15;
constexpr u32 cSf2IndexMax        = 0xFFFF;

struct Sf2PresetData
{
    std::vector<u8> phdr, pbag, pmod, pgen;
    std::vector<u8> inst, ibag, imod, igen;
    std::vector<u8> shdr;
};

static void PutGen(std::vector<u8>& igen, u16 op, u16 value)
{
    PutU16(igen, op);
    PutU16(igen, value);
}

static void PutEmptyModTerminator(std::vector<u8>& mod)
{
    for (int i = 0; i < 5; i++)
        PutU16(mod, 0);
}

static bool IsSf2IndexRangeValid(const ExportModel& model)
{
    size_t regionCount = 0;
    for (const ExportInstrument& in : model.instruments)
        regionCount += in.regions.size();

    return regionCount * cSf2RegionGenCount < cSf2IndexMax && model.samples.size() < cSf2IndexMax;
}

static std::vector<u8> BuildSf2Info(const char* bankName)
{
    std::vector<u8> info;
    std::vector<u8> ifil;

    PutU16(ifil, 2);
    PutU16(ifil, 1);

    AppendChunk(info, "ifil", ifil);
    AppendChunk(info, "isng", ZeroTerminatedEven(cSoundEngineName));
    AppendChunk(info, "INAM", ZeroTerminatedEven(BankNameOrDefault(bankName)));
    AppendChunk(info, "ISFT", ZeroTerminatedEven(cSoftwareName));

    return info;
}

static std::vector<u8> BuildSf2SampleData(const ExportModel& model, std::vector<std::pair<u32, u32>>& sampleOffsets)
{
    std::vector<u8> smpl;

    for (const ExportSample& s : model.samples)
    {
        u32 start = (u32)(smpl.size() / 2);
        u32 frameCount = (u32)s.pcm.size();
        bool hasLoop = s.loopStart >= 0 && s.loopEnd > s.loopStart && (u32)s.loopEnd <= frameCount;

        if (hasLoop)
            frameCount = (u32)s.loopEnd;

        for (u32 i = 0; i < frameCount; i++)
            PutU16(smpl, (u16)s.pcm[i]);

        // Loop tail
        if (hasLoop)
        {
            u32 loopLength = (u32)(s.loopEnd - s.loopStart);

            for (u32 i = 0; i < cSf2LoopTailFrames; i++)
                PutU16(smpl, (u16)s.pcm[s.loopStart + i % loopLength]);

            frameCount += cSf2LoopTailFrames;
        }

        for (u32 i = 0; i < cSf2SamplePadFrames; i++)
            PutU16(smpl, 0);

        sampleOffsets.push_back({start, start + frameCount});
    }

    std::vector<u8> sdta;
    AppendChunk(sdta, "smpl", smpl);

    return sdta;
}

static void PutSf2SampleHeaders(std::vector<u8>& shdr, const ExportModel& model, const std::vector<std::pair<u32, u32>>& sampleOffsets)
{
    for (size_t i = 0; i < model.samples.size(); i++)
    {
        const ExportSample& s = model.samples[i];
        u32 start = sampleOffsets[i].first;
        u32 end = sampleOffsets[i].second;
        u32 ls, le;

        if (s.loopStart >= 0 && s.loopEnd > s.loopStart && start + (u32)s.loopEnd <= end)
        {
            ls = start + (u32)s.loopStart;
            le = start + (u32)s.loopEnd;
        }
        else
        {
            ls = start;
            le = end > start ? end - 1 : start;
        }

        PutName20(shdr, s.name);

        PutU32(shdr, start);
        PutU32(shdr, end);
        PutU32(shdr, ls);
        PutU32(shdr, le);
        PutU32(shdr, s.sampleRate);
        shdr.push_back(s.rootKey & 0x7F);

        shdr.push_back(0);

        u16 sampleType = (s.stereoSide == 0) ? 1 : ((s.stereoSide < 0) ? 4 : 2);
        u16 sampleLink = (s.linkedSample < 0) ? 0 : (u16)s.linkedSample;

        PutU16(shdr, sampleLink);
        PutU16(shdr, sampleType);
    }

    PutName20(shdr, "EOS");

    PutU32(shdr, 0);
    PutU32(shdr, 0);
    PutU32(shdr, 0);
    PutU32(shdr, 0);
    PutU32(shdr, 0);

    shdr.push_back(0);
    shdr.push_back(0);

    PutU16(shdr, 0);
    PutU16(shdr, 0);
}

static void PutSf2Instruments(Sf2PresetData& pdta, const ExportModel& model)
{
    std::vector<u8>& inst = pdta.inst;
    std::vector<u8>& ibag = pdta.ibag;
    std::vector<u8>& igen = pdta.igen;

    u16 ibagIndex = 0;
    for (const ExportInstrument& in : model.instruments)
    {
        std::string iname = InstrumentDisplayName(in, cInstrumentNamePrefix);
        PutName20(inst, iname);
        PutU16(inst, ibagIndex);

        for (const ExportRegion& r : in.regions)
        {
            PutU16(ibag, (u16)(igen.size() / 4));
            PutU16(ibag, 0);
            ibagIndex++;

            RegionGenSet g = ComputeRegionGenSet(r, model.volumeReference);

            PutGen(igen, GEN_KEYRANGE, (u16)((r.keyMax << 8) | r.keyMin));
            PutGen(igen, GEN_VELRANGE, (u16)((r.velMax << 8) | r.velMin));
            PutGen(igen, GEN_PAN, (u16)g.pan);
            PutGen(igen, GEN_INITATTEN, (u16)g.initAttenCb);
            PutGen(igen, GEN_ATTACKVOLENV, (u16)ClampEnvelopeTimecents(g.attackTc));
            PutGen(igen, GEN_HOLDVOLENV, (u16)ClampEnvelopeTimecents(g.holdTc, cHoldTimecentsMax));
            PutGen(igen, GEN_DECAYVOLENV, (u16)ClampEnvelopeTimecents(g.decayTc));
            PutGen(igen, GEN_SUSTAINVOLENV, (u16)g.sustainCb);
            PutGen(igen, GEN_RELEASEVOLENV, (u16)ClampEnvelopeTimecents(g.releaseTc));
            PutGen(igen, GEN_COARSETUNE, (u16)g.coarseTune);
            PutGen(igen, GEN_FINETUNE, (u16)g.fineTune);
            PutGen(igen, GEN_ROOTKEY, r.rootKey);
            PutGen(igen, GEN_SAMPLEMODES, r.loop ? 1 : 0);
            PutGen(igen, GEN_EXCLUSIVECLASS, r.keyGroup);
            PutGen(igen, GEN_SAMPLEID, (u16)r.sampleIndex);
        }
    }

    PutName20(inst, "EOI");

    PutU16(inst, ibagIndex);
    PutU16(ibag, (u16)(igen.size() / 4));
    PutU16(ibag, 0);

    PutGen(igen, 0, 0);

    PutEmptyModTerminator(pdta.imod);
}

static void PutSf2Presets(Sf2PresetData& pdta, const ExportModel& model)
{
    std::vector<u8>& phdr = pdta.phdr;
    std::vector<u8>& pbag = pdta.pbag;
    std::vector<u8>& pgen = pdta.pgen;

    u16 pbagIndex = 0;

    for (size_t i = 0; i < model.instruments.size(); i++)
    {
        const ExportInstrument& in = model.instruments[i];
        std::string pname = InstrumentDisplayName(in, cPresetNamePrefix);
        PutName20(phdr, pname);
        PutU16(phdr, getMidiProgram(in.programNo));
        PutU16(phdr, in.drum ? cSf2DrumBank : getMidiBank(in.bankNo, in.programNo));
        PutU16(phdr, pbagIndex);
        PutU32(phdr, 0);
        PutU32(phdr, 0);
        PutU32(phdr, 0);

        PutU16(pbag, (u16)(pgen.size() / 4));
        PutU16(pbag, 0);
        pbagIndex++;

        PutGen(pgen, GEN_INSTRUMENT, (u16)i);
    }

    PutName20(phdr, "EOP");

    PutU16(phdr, 0);
    PutU16(phdr, 0);
    PutU16(phdr, pbagIndex);

    PutU32(phdr, 0);
    PutU32(phdr, 0);
    PutU32(phdr, 0);

    PutU16(pbag, (u16)(pgen.size() / 4));
    PutU16(pbag, 0);

    PutGen(pgen, 0, 0);

    PutEmptyModTerminator(pdta.pmod);
}

static std::vector<u8> BuildSf2PresetData(const ExportModel& model, const std::vector<std::pair<u32, u32>>& sampleOffsets)
{
    Sf2PresetData chunks;

    PutSf2SampleHeaders(chunks.shdr, model, sampleOffsets);
    PutSf2Instruments(chunks, model);
    PutSf2Presets(chunks, model);

    std::vector<u8> pdta;

    AppendChunk(pdta, "phdr", chunks.phdr);
    AppendChunk(pdta, "pbag", chunks.pbag);
    AppendChunk(pdta, "pmod", chunks.pmod);
    AppendChunk(pdta, "pgen", chunks.pgen);
    AppendChunk(pdta, "inst", chunks.inst);
    AppendChunk(pdta, "ibag", chunks.ibag);
    AppendChunk(pdta, "imod", chunks.imod);
    AppendChunk(pdta, "igen", chunks.igen);
    AppendChunk(pdta, "shdr", chunks.shdr);

    return pdta;
}

static bool WriteSf2(const sead::SafeString& path, const ExportModel& model, const char* bankName)
{
    if (model.instruments.empty())
        return false;

    if (!IsSf2IndexRangeValid(model))
        return false;

    std::vector<std::pair<u32, u32>> sampleOffsets;

    std::vector<u8> info = BuildSf2Info(bankName);
    std::vector<u8> sdta = BuildSf2SampleData(model, sampleOffsets);
    std::vector<u8> pdta = BuildSf2PresetData(model, sampleOffsets);

    std::vector<u8> body;

    PutBytes(body, "sfbk", 4);

    AppendListChunk(body, "INFO", info);
    AppendListChunk(body, "sdta", sdta);
    AppendListChunk(body, "pdta", pdta);

    std::vector<u8> file;

    AppendChunk(file, "RIFF", body);

    return WriteExportFile(path.cstr(), file);
}

enum
{
    DLS_CONN_SRC_NONE = 0x0000,
    DLS_CONN_TRN_NONE = 0x0000,
    DLS_CONN_DST_ATTENUATION = 0x0001,
    DLS_CONN_DST_PITCH = 0x0003,
    DLS_CONN_DST_PAN = 0x0004,
    DLS_CONN_DST_EG1_ATTACKTIME = 0x0206,
    DLS_CONN_DST_EG1_DECAYTIME = 0x0207,
    DLS_CONN_DST_EG1_RELEASETIME = 0x0209,
    DLS_CONN_DST_EG1_SUSTAINLEVEL = 0x020A,
    DLS_CONN_DST_EG1_HOLDTIME = 0x020C,
};

static void PutConnection(std::vector<u8>& o, u16 dst, s32 scale)
{
    PutU16(o, DLS_CONN_SRC_NONE);
    PutU16(o, DLS_CONN_SRC_NONE);
    PutU16(o, dst);
    PutU16(o, DLS_CONN_TRN_NONE);
    PutU32(o, (u32)(s32)((s64)scale << 16));
}

static void PutWsmp(std::vector<u8>& out, u16 unityNote, const ExportSample& s)
{
    bool hasLoop = s.loopStart >= 0 && s.loopEnd > s.loopStart && static_cast<size_t>(s.loopEnd) <= s.pcm.size();
    std::vector<u8> wsmp;

    PutU32(wsmp, 20);

    PutU16(wsmp, unityNote);
    PutU16(wsmp, 0);

    PutU32(wsmp, 0);
    PutU32(wsmp, 0);
    PutU32(wsmp, hasLoop ? 1 : 0);

    if (hasLoop)
    {
        u32 loopStart = (u32)s.loopStart;
        u32 loopLen = (s.loopEnd > s.loopStart) ? (u32)(s.loopEnd - s.loopStart) : 0;

        PutU32(wsmp, 16);
        PutU32(wsmp, 0);
        PutU32(wsmp, loopStart);
        PutU32(wsmp, loopLen);
    }

    AppendChunk(out, "wsmp", wsmp);
}

static std::vector<u8> BuildDlsWavePool(const ExportModel& model, std::vector<u32>& cueOffsets)
{
    std::vector<u8> wvplPayload;

    for (const ExportSample& s : model.samples)
    {
        cueOffsets.push_back((u32)wvplPayload.size());

        std::vector<u8> fmt;

        PutU16(fmt, 1);
        PutU16(fmt, 1);
        PutU32(fmt, s.sampleRate);
        PutU32(fmt, s.sampleRate * 2);
        PutU16(fmt, 2);
        PutU16(fmt, 16);

        std::vector<u8> data;
        data.reserve(s.pcm.size() * 2);

        for (s16 v : s.pcm)
            PutU16(data, (u16)v);

        std::vector<u8> wave;

        AppendChunk(wave, "fmt ", fmt);
        AppendChunk(wave, "data", data);

        PutWsmp(wave, s.rootKey, s);
        AppendListChunk(wvplPayload, "wave", wave);
    }

    return wvplPayload;
}

static std::vector<u8> BuildDlsPoolTable(const std::vector<u32>& cueOffsets)
{
    std::vector<u8> ptbl;

    PutU32(ptbl, 8);
    PutU32(ptbl, (u32)cueOffsets.size());

    for (u32 off : cueOffsets)
        PutU32(ptbl, off);

    return ptbl;
}

static void PutDlsArticulation(std::vector<u8>& rgnPayload, const ExportRegion& r, u8 volumeReference)
{
    RegionGenSet g = ComputeRegionGenSet(r, volumeReference);

    std::vector<u8> conns;

    PutConnection(conns, DLS_CONN_DST_ATTENUATION, -g.initAttenCb);
    PutConnection(conns, DLS_CONN_DST_PAN, g.pan);
    PutConnection(conns, DLS_CONN_DST_PITCH, g.pitchCentsDls);
    PutConnection(conns, DLS_CONN_DST_EG1_ATTACKTIME, g.attackTc);
    PutConnection(conns, DLS_CONN_DST_EG1_HOLDTIME, g.holdTc);
    PutConnection(conns, DLS_CONN_DST_EG1_DECAYTIME, g.decayTc);
    PutConnection(conns, DLS_CONN_DST_EG1_SUSTAINLEVEL, g.sustainPermille);
    PutConnection(conns, DLS_CONN_DST_EG1_RELEASETIME, g.releaseTc);

    std::vector<u8> artChunkBody;
    PutU32(artChunkBody, 8);
    PutU32(artChunkBody, 8);

    artChunkBody.insert(artChunkBody.end(), conns.begin(), conns.end());

    std::vector<u8> lartPayload;
    AppendChunk(lartPayload, "art1", artChunkBody);
    AppendListChunk(rgnPayload, "lart", lartPayload);

    std::vector<u8> lar2Payload;
    AppendChunk(lar2Payload, "art2", artChunkBody);
    AppendListChunk(rgnPayload, "lar2", lar2Payload);
}

static void PutDlsRegion(std::vector<u8>& lrgnPayload, const ExportModel& model, const ExportRegion& r)
{
    std::vector<u8> rgnPayload;
    std::vector<u8> rgnh;

    PutU16(rgnh, r.keyMin);
    PutU16(rgnh, r.keyMax);
    PutU16(rgnh, r.velMin);
    PutU16(rgnh, r.velMax);
    PutU16(rgnh, 0);
    PutU16(rgnh, r.keyGroup);

    AppendChunk(rgnPayload, "rgnh", rgnh);

    const ExportSample& sample = model.samples[r.sampleIndex];
    PutWsmp(rgnPayload, r.rootKey, sample);

    bool isStereo = sample.stereoSide != 0;
    u32 channelMask = isStereo ? ((sample.stereoSide < 0) ? 1u : 2u) : 1u;
    u16 phaseGroup = isStereo ? (u16)std::min<s32>((s32)r.sampleIndex, sample.linkedSample) : 0;
    u16 fusOptions = isStereo ? 1 : 0;

    std::vector<u8> wlnk;

    PutU16(wlnk, fusOptions);
    PutU16(wlnk, phaseGroup);
    PutU32(wlnk, channelMask);
    PutU32(wlnk, r.sampleIndex);

    AppendChunk(rgnPayload, "wlnk", wlnk);

    PutDlsArticulation(rgnPayload, r, model.volumeReference);

    AppendListChunk(lrgnPayload, "rgn2", rgnPayload);
}

static void PutDlsInstrument(std::vector<u8>& linsPayload, const ExportModel& model, const ExportInstrument& in)
{
    std::vector<u8> insPayload;
    std::vector<u8> insh;

    PutU32(insh, (u32)in.regions.size());
    PutU32(insh, in.drum ? cDlsDrumBank : static_cast<u32>(getMidiBank(in.bankNo, in.programNo)) << 8);
    PutU32(insh, getMidiProgram(in.programNo));

    AppendChunk(insPayload, "insh", insh);

    std::vector<u8> lrgnPayload;

    for (const ExportRegion& r : in.regions)
        PutDlsRegion(lrgnPayload, model, r);

    AppendListChunk(insPayload, "lrgn", lrgnPayload);

    std::vector<u8> instInfo;
    std::string iname = InstrumentDisplayName(in, cInstrumentNamePrefix);

    AppendChunk(instInfo, "INAM", ZeroTerminatedEven(iname));
    AppendListChunk(insPayload, "INFO", instInfo);
    AppendListChunk(linsPayload, "ins ", insPayload);
}

static bool WriteDls(const sead::SafeString& path, const ExportModel& model, const char* bankName)
{
    if (model.instruments.empty())
        return false;

    std::vector<u32> cueOffsets;
    std::vector<u8> wvplPayload = BuildDlsWavePool(model, cueOffsets);
    std::vector<u8> ptbl = BuildDlsPoolTable(cueOffsets);

    std::vector<u8> linsPayload;
    for (const ExportInstrument& in : model.instruments)
        PutDlsInstrument(linsPayload, model, in);

    std::vector<u8> colh;
    PutU32(colh, (u32)model.instruments.size());

    std::vector<u8> info;
    AppendChunk(info, "INAM", ZeroTerminatedEven(BankNameOrDefault(bankName)));
    AppendChunk(info, "ISFT", ZeroTerminatedEven(cSoftwareName));

    std::vector<u8> body;
    PutBytes(body, "DLS ", 4);
    AppendChunk(body, "colh", colh);
    AppendListChunk(body, "lins", linsPayload);
    AppendChunk(body, "ptbl", ptbl);
    AppendListChunk(body, "wvpl", wvplPayload);
    AppendListChunk(body, "INFO", info);

    std::vector<u8> file;
    AppendChunk(file, "RIFF", body);

    return WriteExportFile(path.cstr(), file);
}

static ExportSample BuildExportSample(const std::vector<float>& src, s8 stereoSide, const DecodedPcm& pcm, const BankFile::VelocityRegion* vr, const WaveFile* wave)
{
    ExportSample es;
    es.pcm.resize(src.size());

    for (size_t i = 0; i < src.size(); i++)
    {
        float f = src[i] * 32767.0f;

        if (f > 32767.0f)
            f = 32767.0f;

        if (f < -32768.0f)
            f = -32768.0f;

        es.pcm[i] = (s16)std::lround(f);
    }

    es.sampleRate = pcm.sampleRate ? pcm.sampleRate : cDefaultSampleRate;
    es.loopStart = pcm.isLoop ? (s32)pcm.loopStartFrame : -1;
    es.loopEnd = pcm.isLoop ? (s32)pcm.loopEndFrame : (s32)pcm.sampleCount;
    es.rootKey = vr->getRootKey();
    es.name = wave->isNameValid() ? wave->getName().cstr() : cWaveNamePrefix + std::to_string(wave->getId());
    es.stereoSide = stereoSide;

    return es;
}

static CachedSampleIndices AddSamples(ExportModel& model, const DecodedPcm& pcm, const BankFile::VelocityRegion* vr, const WaveFile* wave)
{
    CachedSampleIndices idx;

    if (pcm.channels.size() == 2)
    {
        ExportSample left = BuildExportSample(pcm.channels[0], -1, pcm, vr, wave);
        ExportSample right = BuildExportSample(pcm.channels[1], 1, pcm, vr, wave);

        s32 leftIdx = (s32)model.samples.size();
        s32 rightIdx = leftIdx + 1;

        left.linkedSample = rightIdx;
        right.linkedSample = leftIdx;

        model.samples.push_back(std::move(left));
        model.samples.push_back(std::move(right));

        idx.mono = leftIdx;
        idx.right = rightIdx;
    }
    else
    {
        ExportSample mono = BuildExportSample(pcm.monoMix(), 0, pcm, vr, wave);
        idx.mono = (s32)model.samples.size();
        idx.right = -1;
        model.samples.push_back(std::move(mono));
    }

    return idx;
}

static CachedSampleIndices FindOrAddSamples(ExportModelBuilder& builder, const BankFile::VelocityRegion* vr, const WaveFile* wave)
{
    auto it = builder.sampleCache.find(wave);
    if (it != builder.sampleCache.end())
        return it->second;

    DecodedPcm pcm = decodeWaveFileForEditing(*wave);

    if (!pcm.isValid())
    {
        builder.sampleCache[wave] = CachedSampleIndices();
        PopupMgr::instance()->addPopup({messages::exporting::cWaveDecodeFailed, nullptr});
        return CachedSampleIndices();
    }

    CachedSampleIndices idx = AddSamples(builder.model, pcm, vr, wave);
    builder.sampleCache[wave] = idx;

    return idx;
}

static void SetRegionTune(ExportRegion& region, f32 pitch)
{
    double semitones = pitch > 0.0f ? 12.0 * std::log2((double)pitch) : 0.0;
    s32 coarse = (s32)std::lround(semitones);

    if (coarse < -120)
        coarse = -120;

    if (coarse > 120)
        coarse = 120;

    s32 fine = (s32)std::lround((semitones - coarse) * 100.0);

    if (fine > 99)
    {
        fine -= 100;
        coarse = coarse < 120 ? coarse + 1 : coarse;
    }

    if (fine < -99)
    {
        fine += 100;
        coarse = coarse > -120 ? coarse - 1 : coarse;
    }

    region.coarseTune = (s16)coarse;
    region.fineTune = (s16)fine;
}

static ExportRegion BuildExportRegion(const BankFile::KeyRegion* kr, const BankFile::VelocityRegion* vr, const WaveFile* wave)
{
    ExportRegion base;
    base.keyMin = kr->getKeyMin();
    base.keyMax = kr->getKeyMax();
    base.velMin = vr->getVelocityMin();
    base.velMax = vr->getVelocityMax();
    base.rootKey = vr->getRootKey();
    base.volume = vr->getVolume();
    base.pan = vr->getPan();
    base.keyGroup = vr->getKeyGroup() > 127 ? 127 : vr->getKeyGroup();

    const snd::AdshrCurve& env = vr->getAdshrCurve();

    base.attack = env.attack;
    base.decay = env.decay;
    base.sustain = env.sustain;
    base.hold = env.hold;
    base.release = env.release;
    base.loop = wave->getIsLoop();

    SetRegionTune(base, vr->getPitch());

    return base;
}

static void AddRegions(ExportInstrument& einst, const ExportRegion& base, const CachedSampleIndices& idx)
{
    if (idx.right >= 0)
    {
        ExportRegion left = base;
        left.sampleIndex = (u32)idx.mono;
        left.hardPanOverride = true;
        left.hardPanValue = -500;
        einst.regions.push_back(left);

        ExportRegion right = base;
        right.sampleIndex = (u32)idx.right;
        right.hardPanOverride = true;
        right.hardPanValue = 500;
        einst.regions.push_back(right);
    }
    else
    {
        ExportRegion r = base;
        r.sampleIndex = (u32)idx.mono;
        einst.regions.push_back(r);
    }
}

static void AddInstrumentRegions(ExportModelBuilder& builder, ExportInstrument& einst, const BankFile::Instrument* instr)
{
    ForEachVelocityRegion(*instr, [&](const BankFile::KeyRegion& kr, const BankFile::VelocityRegion& vr)
    {
        const Item* waveItem = vr.getWaveFileRef().getItem();

        if (!waveItem)
            return;

        const WaveFile* wave = static_cast<const WaveFile*>(waveItem);

        CachedSampleIndices idx = FindOrAddSamples(builder, &vr, wave);

        if (idx.mono < 0)
            return;

        ExportRegion base = BuildExportRegion(&kr, &vr, wave);

        AddRegions(einst, base, idx);
    });
}

static void AddBankToModel(ExportModelBuilder& builder, const BankFile& bank, u16 bankNo, const std::set<std::pair<u16, u16>>* usedPrograms = nullptr)
{
    for (const Item* instrItem : bank.getInstrumentList())
    {
        const auto* instr = static_cast<const BankFile::Instrument*>(instrItem);
        u16 programNo = (u16)instr->getProgramNo();

        if (usedPrograms && usedPrograms->count({bankNo, programNo}) == 0)
            continue;

        ExportInstrument einst;
        einst.bankNo = bankNo;
        einst.programNo = programNo;

        if (instr->isNameValid())
            einst.name = instr->getName().cstr();

        AddInstrumentRegions(builder, einst, instr);

        if (einst.regions.empty())
            continue;

        for (const ExportRegion& region : einst.regions)
            builder.model.volumeReference = std::max(builder.model.volumeReference, region.volume);

        builder.model.instruments.push_back(std::move(einst));
    }
}

static void AddDrumCopies(ExportModel& model, const std::set<SeqProgram>& drumPrograms)
{
    std::set<u8> drumMidiPrograms;
    const size_t instrumentNum = model.instruments.size();

    for (size_t i = 0; i < instrumentNum; i++)
    {
        const ExportInstrument& instrument = model.instruments[i];

        if (drumPrograms.count({instrument.bankNo, instrument.programNo}) == 0)
            continue;

        if (!drumMidiPrograms.insert(getMidiProgram(instrument.programNo)).second)
            continue;

        ExportInstrument drum = instrument;
        drum.drum = true;
        model.instruments.push_back(std::move(drum));
    }
}

static bool BuildSeqSoundModel(const Sound& sound, const SeqMidiFilter* filter, ExportModelBuilder& builder)
{
    if (sound.getSoundType() != Sound::SoundType::Seq)
        return false;

    const SeqMidiContents contents = collectSeqMidiContents(sound);

    std::set<SeqProgram> used;
    std::set<SeqProgram> drumPrograms;

    for (const SeqMidiPart& part : contents.parts)
    {
        if (filter && !filter->isEnabled(part.trackNo, part.program))
            continue;

        used.insert(part.program);

        if (getSeqMidiChannel(part.trackNo, contents.trackFlags) == cMidiDrumChannel)
            drumPrograms.insert(part.program);
    }

    if (used.empty() && !contents.parts.empty())
        return false;

    const std::set<SeqProgram>* usedPrograms = used.empty() ? nullptr : &used;

    for (u32 i = 0; i < cSeqSoundBankNum; i++)
    {
        const BankFile* bankFile = GetSeqSoundBankFile(sound, i);

        if (bankFile)
            AddBankToModel(builder, *bankFile, static_cast<u16>(i), usedPrograms);
    }

    AddDrumCopies(builder.model, drumPrograms);

    return !builder.model.instruments.empty();
}

static bool ExportBank(const sead::SafeString& path, const BankFile& bank, ExportModelWriter write)
{
    ExportModelBuilder builder;

    AddBankToModel(builder, bank, 0);

    if (builder.model.instruments.empty())
        return false;

    std::string name = ResolveItemName(bank, cDefaultBankName);
    return write(path, builder.model, name.c_str());
}

static bool ExportSeqSound(const sead::SafeString& path, const Sound& sound, const SeqMidiFilter* filter, ExportModelWriter write)
{
    ExportModelBuilder builder;

    if (!BuildSeqSoundModel(sound, filter, builder))
        return false;

    std::string name = ResolveItemName(sound, cDefaultSequenceName);

    return write(path, builder.model, name.c_str());
}

bool exportBankToSf2(const sead::SafeString& path, const BankFile& bank)
{
    return ExportBank(path, bank, &WriteSf2);
}

bool exportSeqSoundToSf2(const sead::SafeString& path, const Sound& sound, const SeqMidiFilter* filter)
{
    return ExportSeqSound(path, sound, filter, &WriteSf2);
}

bool exportBankToDls(const sead::SafeString& path, const BankFile& bank)
{
    return ExportBank(path, bank, &WriteDls);
}

bool exportSeqSoundToDls(const sead::SafeString& path, const Sound& sound, const SeqMidiFilter* filter)
{
    return ExportSeqSound(path, sound, filter, &WriteDls);
}
