#include <midi/InstBankExporter.h>

#include <bfsar/Bank.h>
#include <bfsar/BankFile.h>
#include <bfsar/Bfsar.h>
#include <bfsar/DecodedPcm.h>
#include <bfsar/SequenceFile.h>
#include <bfsar/Sound.h>
#include <bfsar/SoundSet.h>
#include <bfsar/WaveFile.h>
#include <bfsar/WaveFileEditDecode.h>

#include <midi/SeqMidiExporter.h>

#include <ui/PopupMgr.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

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
};

static s16 tsFromSeconds_(double sec)
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

static const u8 kAttackTailTable_[127 - 109 + 1] = {
    0, 1, 5, 14, 26, 38, 51, 63, 73, 84,
    92, 100, 109, 116, 123, 127, 132, 137, 143};

static f32 attackCoefficient_(u8 attack)
{
    double x = (attack < 109) ? (255.0 - attack) : (double)kAttackTailTable_[127 - attack];
    return (f32)std::pow(x / 256.0, 1.0 / 5.0);
}

static const s16 kSustainDbX10Table_[128] = {
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

static u8 clampAdshrIndex_(u8 idx)
{
    return idx > 127 ? 127 : idx;
}

static double calcEngineRate_(u8 idx)
{
    idx = clampAdshrIndex_(idx);

    if (idx == 127)
        return 65535.0;

    if (idx == 126)
        return 120.0 / 5.0;

    if (idx < 50)
        return ((idx << 1) + 1) / 128.0 / 5.0;

    return (60.0 / (126 - idx)) / 5.0;
}

static double engineDecayReleaseSeconds_(u8 idx)
{
    return 1.0 / calcEngineRate_(idx);
}

static double engineAttackSeconds_(u8 idx)
{
    f32 mAttack = attackCoefficient_(clampAdshrIndex_(idx));

    if (mAttack <= 0.0f)
        return 0.0;

    const double kInitValueX10 = -904.0;
    const double kThresholdX10 = -1.0 / 32.0;
    double n = std::log(kThresholdX10 / kInitValueX10) / std::log((double)mAttack);

    return n > 0.0 ? n / 1000.0 : 0.0;
}

static double engineHoldSeconds_(u8 idx)
{
    idx = clampAdshrIndex_(idx);
    return ((double)(idx + 1) * (double)(idx + 1) / 4.0) / 1000.0;
}

static s16 engineSustainCb_(u8 idx)
{
    return (s16)(-kSustainDbX10Table_[clampAdshrIndex_(idx)]);
}

static s32 engineSustainPermille_(u8 idx)
{
    double dbX10 = (double)kSustainDbX10Table_[clampAdshrIndex_(idx)];
    double linear = std::pow(10.0, dbX10 / 10.0 / 20.0);
    s32 permille = (s32)std::lround(linear * 1000.0);

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

static RegionGenSet computeRegionGenSet_(const ExportRegion &r)
{
    RegionGenSet g;

    g.attackTc = tsFromSeconds_(engineAttackSeconds_(r.attack));
    g.decayTc = tsFromSeconds_(engineDecayReleaseSeconds_(r.decay));
    g.releaseTc = tsFromSeconds_(engineDecayReleaseSeconds_(r.release));
    g.holdTc = (r.hold == 0) ? (s16)-12000 : tsFromSeconds_(engineHoldSeconds_(r.hold));

    g.sustainCb = engineSustainCb_(r.sustain);
    g.sustainPermille = engineSustainPermille_(r.sustain);

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

    double linearVol = r.volume / 127.0;
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

static void putU16_(std::vector<u8> &o, u16 v)
{
    o.push_back(v & 0xFF);
    o.push_back((v >> 8) & 0xFF);
}

static void putU32_(std::vector<u8> &o, u32 v)
{
    o.push_back(v & 0xFF);
    o.push_back((v >> 8) & 0xFF);
    o.push_back((v >> 16) & 0xFF);
    o.push_back((v >> 24) & 0xFF);
}

static void putBytes_(std::vector<u8> &o, const void *p, u32 n)
{
    const u8 *b = static_cast<const u8 *>(p);
    o.insert(o.end(), b, b + n);
}

static void appendChunk_(std::vector<u8> &out, const char *tag, const std::vector<u8> &data)
{
    putBytes_(out, tag, 4);
    putU32_(out, (u32)data.size());

    out.insert(out.end(), data.begin(), data.end());

    if (data.size() & 1)
        out.push_back(0);
}

static void putName20_(std::vector<u8> &o, const std::string &name)
{
    char buf[20];

    std::memset(buf, 0, sizeof(buf));
    size_t n = name.size() < 19 ? name.size() : 19;
    std::memcpy(buf, name.data(), n);

    putBytes_(o, buf, 20);
}

static std::vector<u8> zstrEven_(const std::string &s)
{
    std::vector<u8> b(s.begin(), s.end());
    b.push_back(0);

    if (b.size() & 1)
        b.push_back(0);
    
    return b;
}

static std::string instrumentDisplayName_(const ExportInstrument &in, const char *fallbackPrefix)
{
    if (!in.name.empty())
        return in.name;
    return fallbackPrefix + std::to_string(in.programNo);
}

static std::string resolveItemName_(const Item &item, const char *fallback)
{
    return item.isNameValid() ? item.getName().cstr() : fallback;
}

static bool writeFileBytes_(const sead::SafeString &path, const std::vector<u8> &data)
{
    FILE *fp = std::fopen(path.cstr(), "wb");

    if (!fp)
        return false;

    bool ok = std::fwrite(data.data(), 1, data.size(), fp) == data.size();
    std::fclose(fp);

    return ok;
}

static void putEmptyModTerminator_(std::vector<u8> &mod)
{
    for (int i = 0; i < 5; i++)
        putU16_(mod, 0);
}

enum
{
    GEN_STARTLOOP = 2,
    GEN_ENDLOOP = 3,
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

static void putGen_(std::vector<u8> &igen, u16 op, u16 value)
{
    putU16_(igen, op);
    putU16_(igen, value);
}

static bool writeSf2_(const sead::SafeString &path, const ExportModel &model, const char *bankName)
{
    if (model.instruments.empty())
        return false;

    std::vector<u8> info;
    {
        std::vector<u8> ifil;

        putU16_(ifil, 2);
        putU16_(ifil, 1);

        appendChunk_(info, "ifil", ifil);
        appendChunk_(info, "isng", zstrEven_("EMU8000"));
        appendChunk_(info, "INAM", zstrEven_(bankName && *bankName ? bankName : "TuneBloom Bank"));
        appendChunk_(info, "ISFT", zstrEven_("TuneBloom"));
    }

    std::vector<u8> infoList;
    putBytes_(infoList, "INFO", 4);
    infoList.insert(infoList.end(), info.begin(), info.end());

    std::vector<u8> smpl;
    std::vector<std::pair<u32, u32>> sampleOffsets;

    for (const ExportSample &s : model.samples)
    {
        u32 start = (u32)(smpl.size() / 2);
        for (s16 v : s.pcm)
            putU16_(smpl, (u16)v);
        for (int g = 0; g < 46; g++)
            putU16_(smpl, 0);
        u32 end = start + (u32)s.pcm.size();
        sampleOffsets.push_back({start, end});
    }

    std::vector<u8> sdta;
    appendChunk_(sdta, "smpl", smpl);
    std::vector<u8> sdtaList;
    putBytes_(sdtaList, "sdta", 4);
    sdtaList.insert(sdtaList.end(), sdta.begin(), sdta.end());

    std::vector<u8> shdr, inst, ibag, igen, phdr, pbag, pgen;

    for (size_t i = 0; i < model.samples.size(); i++)
    {
        const ExportSample &s = model.samples[i];
        u32 start = sampleOffsets[i].first;
        u32 end = sampleOffsets[i].second;
        u32 ls, le;

        if (s.loopStart >= 0)
        {
            ls = start + (u32)s.loopStart;
            le = start + (u32)s.loopEnd;
        }
        else
        {
            ls = start;
            le = end > start ? end - 1 : start;
        }

        putName20_(shdr, s.name);

        putU32_(shdr, start);
        putU32_(shdr, end);
        putU32_(shdr, ls);
        putU32_(shdr, le);
        putU32_(shdr, s.sampleRate);
        shdr.push_back(s.rootKey & 0x7F);

        shdr.push_back(0);

        u16 sampleType = (s.stereoSide == 0) ? 1 : ((s.stereoSide < 0) ? 4 : 2);
        u16 sampleLink = (s.linkedSample < 0) ? 0 : (u16)s.linkedSample;

        putU16_(shdr, sampleLink);
        putU16_(shdr, sampleType);
    }
    putName20_(shdr, "EOS");

    putU32_(shdr, 0);
    putU32_(shdr, 0);
    putU32_(shdr, 0);
    putU32_(shdr, 0);
    putU32_(shdr, 0);

    shdr.push_back(0);
    shdr.push_back(0);

    putU16_(shdr, 0);
    putU16_(shdr, 0);

    u16 ibagIndex = 0;
    for (const ExportInstrument &in : model.instruments)
    {
        std::string iname = instrumentDisplayName_(in, "INST_");
        putName20_(inst, iname);
        putU16_(inst, ibagIndex);

        for (const ExportRegion &r : in.regions)
        {
            putU16_(ibag, (u16)(igen.size() / 4));
            putU16_(ibag, 0);
            ibagIndex++;

            RegionGenSet g = computeRegionGenSet_(r);

            putGen_(igen, GEN_KEYRANGE, (u16)((r.keyMax << 8) | r.keyMin));
            putGen_(igen, GEN_VELRANGE, (u16)((r.velMax << 8) | r.velMin));
            putGen_(igen, GEN_PAN, (u16)g.pan);
            putGen_(igen, GEN_INITATTEN, (u16)g.initAttenCb);
            putGen_(igen, GEN_ATTACKVOLENV, (u16)g.attackTc);
            putGen_(igen, GEN_HOLDVOLENV, (u16)g.holdTc);
            putGen_(igen, GEN_DECAYVOLENV, (u16)g.decayTc);
            putGen_(igen, GEN_SUSTAINVOLENV, (u16)g.sustainCb);
            putGen_(igen, GEN_RELEASEVOLENV, (u16)g.releaseTc);
            putGen_(igen, GEN_COARSETUNE, (u16)g.coarseTune);
            putGen_(igen, GEN_FINETUNE, (u16)g.fineTune);
            putGen_(igen, GEN_ROOTKEY, r.rootKey);
            putGen_(igen, GEN_SAMPLEMODES, r.loop ? 1 : 0);
            putGen_(igen, GEN_EXCLUSIVECLASS, r.keyGroup);
            putGen_(igen, GEN_SAMPLEID, (u16)r.sampleIndex);
        }
    }

    putName20_(inst, "EOI");

    putU16_(inst, ibagIndex);
    putU16_(ibag, (u16)(igen.size() / 4));
    putU16_(ibag, 0);

    putGen_(igen, 0, 0);

    std::vector<u8> imod;
    putEmptyModTerminator_(imod);
    u16 pbagIndex = 0;

    for (size_t i = 0; i < model.instruments.size(); i++)
    {
        const ExportInstrument &in = model.instruments[i];
        std::string pname = instrumentDisplayName_(in, "PRG_");
        putName20_(phdr, pname);
        putU16_(phdr, in.programNo);
        putU16_(phdr, in.bankNo);
        putU16_(phdr, pbagIndex);
        putU32_(phdr, 0);
        putU32_(phdr, 0);
        putU32_(phdr, 0);

        putU16_(pbag, (u16)(pgen.size() / 4));
        putU16_(pbag, 0);
        pbagIndex++;

        putGen_(pgen, GEN_INSTRUMENT, (u16)i);
    }

    putName20_(phdr, "EOP");

    putU16_(phdr, 0);
    putU16_(phdr, 0);
    putU16_(phdr, pbagIndex);

    putU32_(phdr, 0);
    putU32_(phdr, 0);
    putU32_(phdr, 0);

    putU16_(pbag, (u16)(pgen.size() / 4));
    putU16_(pbag, 0);

    putGen_(pgen, 0, 0);

    std::vector<u8> pmod;
    putEmptyModTerminator_(pmod);
    std::vector<u8> pdta;

    appendChunk_(pdta, "phdr", phdr);
    appendChunk_(pdta, "pbag", pbag);
    appendChunk_(pdta, "pmod", pmod);
    appendChunk_(pdta, "pgen", pgen);
    appendChunk_(pdta, "inst", inst);
    appendChunk_(pdta, "ibag", ibag);
    appendChunk_(pdta, "imod", imod);
    appendChunk_(pdta, "igen", igen);
    appendChunk_(pdta, "shdr", shdr);

    std::vector<u8> pdtaList;

    putBytes_(pdtaList, "pdta", 4);

    pdtaList.insert(pdtaList.end(), pdta.begin(), pdta.end());

    std::vector<u8> body;

    putBytes_(body, "sfbk", 4);

    appendChunk_(body, "LIST", infoList);
    appendChunk_(body, "LIST", sdtaList);
    appendChunk_(body, "LIST", pdtaList);

    std::vector<u8> file;

    appendChunk_(file, "RIFF", body);

    return writeFileBytes_(path, file);
}

static void appendListChunk_(std::vector<u8> &out, const char *listType, const std::vector<u8> &payload)
{
    std::vector<u8> body;
    putBytes_(body, listType, 4);
    body.insert(body.end(), payload.begin(), payload.end());
    appendChunk_(out, "LIST", body);
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

static void putConnection_(std::vector<u8> &o, u16 dst, s32 scale)
{
    putU16_(o, DLS_CONN_SRC_NONE);
    putU16_(o, DLS_CONN_SRC_NONE);
    putU16_(o, dst);
    putU16_(o, DLS_CONN_TRN_NONE);
    putU32_(o, (u32)(s32)((s64)scale << 16));
}

static void putWsmp_(std::vector<u8> &out, u16 unityNote, const ExportSample &s)
{
    bool hasLoop = s.loopStart >= 0;
    std::vector<u8> wsmp;

    putU32_(wsmp, 20);

    putU16_(wsmp, unityNote);
    putU16_(wsmp, 0);

    putU32_(wsmp, 0);
    putU32_(wsmp, 0);
    putU32_(wsmp, hasLoop ? 1 : 0);

    if (hasLoop)
    {
        u32 loopStart = (u32)s.loopStart;
        u32 loopLen = (s.loopEnd > s.loopStart) ? (u32)(s.loopEnd - s.loopStart) : 0;

        putU32_(wsmp, 16);
        putU32_(wsmp, 0);
        putU32_(wsmp, loopStart);
        putU32_(wsmp, loopLen);
    }

    appendChunk_(out, "wsmp", wsmp);
}

static bool writeDls_(const sead::SafeString &path, const ExportModel &model, const char *bankName)
{
    if (model.instruments.empty())
        return false;

    std::vector<u8> wvplPayload;
    std::vector<u32> cueOffsets;

    for (const ExportSample &s : model.samples)
    {
        cueOffsets.push_back((u32)wvplPayload.size());

        std::vector<u8> fmt;

        putU16_(fmt, 1);
        putU16_(fmt, 1);
        putU32_(fmt, s.sampleRate);
        putU32_(fmt, s.sampleRate * 2);
        putU16_(fmt, 2);
        putU16_(fmt, 16);

        std::vector<u8> data;
        data.reserve(s.pcm.size() * 2);

        for (s16 v : s.pcm)
            putU16_(data, (u16)v);

        std::vector<u8> wave;

        appendChunk_(wave, "fmt ", fmt);
        appendChunk_(wave, "data", data);

        putWsmp_(wave, s.rootKey, s);
        appendListChunk_(wvplPayload, "wave", wave);
    }

    std::vector<u8> ptbl;

    putU32_(ptbl, 8);
    putU32_(ptbl, (u32)cueOffsets.size());

    for (u32 off : cueOffsets)
        putU32_(ptbl, off);


    std::vector<u8> linsPayload;
    for (const ExportInstrument &in : model.instruments)
    {
        std::vector<u8> insPayload;
        std::vector<u8> insh;

        putU32_(insh, (u32)in.regions.size());
        putU32_(insh, in.bankNo);
        putU32_(insh, in.programNo);

        appendChunk_(insPayload, "insh", insh);

        std::vector<u8> lrgnPayload;

        for (const ExportRegion &r : in.regions)
        {
            std::vector<u8> rgnPayload;
            std::vector<u8> rgnh;

            putU16_(rgnh, r.keyMin);
            putU16_(rgnh, r.keyMax);
            putU16_(rgnh, r.velMin);
            putU16_(rgnh, r.velMax);
            putU16_(rgnh, 0);
            putU16_(rgnh, r.keyGroup);

            appendChunk_(rgnPayload, "rgnh", rgnh);

            const ExportSample &sample = model.samples[r.sampleIndex];
            putWsmp_(rgnPayload, r.rootKey, sample);

            bool isStereo = sample.stereoSide != 0;
            u32 channelMask = isStereo ? ((sample.stereoSide < 0) ? 1u : 2u) : 1u;
            u16 phaseGroup = isStereo ? (u16)std::min<s32>((s32)r.sampleIndex, sample.linkedSample) : 0;
            u16 fusOptions = isStereo ? 1 : 0;

            std::vector<u8> wlnk;

            putU16_(wlnk, fusOptions);
            putU16_(wlnk, phaseGroup);
            putU32_(wlnk, channelMask);
            putU32_(wlnk, r.sampleIndex);

            appendChunk_(rgnPayload, "wlnk", wlnk);

            RegionGenSet g = computeRegionGenSet_(r);

            std::vector<u8> conns;

            putConnection_(conns, DLS_CONN_DST_ATTENUATION, g.initAttenCb);
            putConnection_(conns, DLS_CONN_DST_PAN, g.pan);
            putConnection_(conns, DLS_CONN_DST_PITCH, g.pitchCentsDls);
            putConnection_(conns, DLS_CONN_DST_EG1_ATTACKTIME, g.attackTc);
            putConnection_(conns, DLS_CONN_DST_EG1_HOLDTIME, g.holdTc);
            putConnection_(conns, DLS_CONN_DST_EG1_DECAYTIME, g.decayTc);
            putConnection_(conns, DLS_CONN_DST_EG1_SUSTAINLEVEL, g.sustainPermille);
            putConnection_(conns, DLS_CONN_DST_EG1_RELEASETIME, g.releaseTc);

            std::vector<u8> artChunkBody;
            putU32_(artChunkBody, 8);
            putU32_(artChunkBody, 8);

            artChunkBody.insert(artChunkBody.end(), conns.begin(), conns.end());

            std::vector<u8> lartPayload;
            appendChunk_(lartPayload, "art1", artChunkBody);
            appendListChunk_(rgnPayload, "lart", lartPayload);

            std::vector<u8> lar2Payload;
            appendChunk_(lar2Payload, "art2", artChunkBody);
            appendListChunk_(rgnPayload, "lar2", lar2Payload);

            appendListChunk_(lrgnPayload, "rgn2", rgnPayload);
        }

        appendListChunk_(insPayload, "lrgn", lrgnPayload);

        std::vector<u8> instInfo;
        std::string iname = instrumentDisplayName_(in, "INST_");

        appendChunk_(instInfo, "INAM", zstrEven_(iname));
        appendListChunk_(insPayload, "INFO", instInfo);
        appendListChunk_(linsPayload, "ins ", insPayload);
    }

    std::vector<u8> colh;
    putU32_(colh, (u32)model.instruments.size());

    std::vector<u8> info;
    appendChunk_(info, "INAM", zstrEven_(bankName && *bankName ? bankName : "TuneBloom Bank"));
    appendChunk_(info, "ISFT", zstrEven_("TuneBloom"));

    std::vector<u8> body;
    putBytes_(body, "DLS ", 4);
    appendChunk_(body, "colh", colh);
    appendListChunk_(body, "lins", linsPayload);
    appendChunk_(body, "ptbl", ptbl);
    appendListChunk_(body, "wvpl", wvplPayload);
    appendListChunk_(body, "INFO", info);

    std::vector<u8> file;
    appendChunk_(file, "RIFF", body);

    return writeFileBytes_(path, file);
}

static void addBankToModel_(ExportModel &model,
                            std::unordered_map<const WaveFile *, CachedSampleIndices> &sampleCache,
                            const BankFile &bank, u16 bankNo,
                            const std::set<std::pair<u16, u16>> *usedPrograms = nullptr)
{
    for (const Item *instrItem : bank.getInstrumentList())
    {
        const auto *instr = static_cast<const BankFile::Instrument *>(instrItem);
        u16 programNo = (u16)instr->getProgramNo();

        if (usedPrograms && usedPrograms->count({bankNo, programNo}) == 0)
            continue;

        ExportInstrument einst;
        einst.bankNo = bankNo;
        einst.programNo = programNo;
        if (instr->isNameValid())
            einst.name = instr->getName().cstr();

        for (const Item *krItem : instr->getKeyRegionList())
        {
            const auto *kr = static_cast<const BankFile::KeyRegion *>(krItem);

            for (const Item *vrItem : kr->getVelocityRegionList())
            {
                const auto *vr = static_cast<const BankFile::VelocityRegion *>(vrItem);
                const Item *waveItem = vr->getWaveFileRef().getItem();
                
                if (!waveItem)
                    continue;

                const WaveFile *wave = static_cast<const WaveFile *>(waveItem);

                CachedSampleIndices idx;
                auto it = sampleCache.find(wave);
                if (it != sampleCache.end())
                {
                    idx = it->second;
                }
                else
                {
                    DecodedPcm pcm = decodeWaveFileForEditing(*wave);

                    if (!pcm.isValid())
                    {
                        sampleCache[wave] = CachedSampleIndices();
                        PopupMgr::instance()->addPopup({"A wave file could not be decoded for SF2 export", nullptr});
                        continue;
                    }

                    auto buildSample = [&](const std::vector<float> &src, s8 stereoSide)
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

                        es.sampleRate = pcm.sampleRate ? pcm.sampleRate : 22050;
                        es.loopStart = wave->getIsLoop() ? (s32)wave->getOriginalLoopStartFrame() : -1;
                        es.loopEnd = (s32)pcm.sampleCount;
                        es.rootKey = vr->getRootKey();
                        es.name = wave->isNameValid() ? wave->getName().cstr() : "WAVE_" + std::to_string(wave->getId());
                        es.stereoSide = stereoSide;

                        return es;
                    };

                    if (pcm.channels.size() == 2)
                    {
                        ExportSample left = buildSample(pcm.channels[0], -1);
                        ExportSample right = buildSample(pcm.channels[1], 1);

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
                        ExportSample mono = buildSample(pcm.monoMix(), 0);
                        idx.mono = (s32)model.samples.size();
                        idx.right = -1;
                        model.samples.push_back(std::move(mono));
                    }

                    sampleCache[wave] = idx;
                }

                if (idx.mono < 0)
                    continue;

                ExportRegion base;
                base.keyMin = kr->getKeyMin();
                base.keyMax = kr->getKeyMax();
                base.velMin = vr->getVelocityMin();
                base.velMax = vr->getVelocityMax();
                base.rootKey = vr->getRootKey();
                base.volume = vr->getVolume();
                base.pan = vr->getPan();
                base.keyGroup = vr->getKeyGroup() > 127 ? 127 : vr->getKeyGroup();

                const snd::AdshrCurve &env = vr->getAdshrCurve();

                base.attack = env.attack;
                base.decay = env.decay;
                base.sustain = env.sustain;
                base.hold = env.hold;
                base.release = env.release;
                base.loop = wave->getIsLoop();

                f32 pitch = vr->getPitch();
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

                base.coarseTune = (s16)coarse;
                base.fineTune = (s16)fine;

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
        }

        if (!einst.regions.empty())
            model.instruments.push_back(std::move(einst));
    }
}

bool exportBankToSf2(const sead::SafeString &path, const BankFile &bank)
{
    ExportModel model;
    std::unordered_map<const WaveFile *, CachedSampleIndices> sampleCache;

    addBankToModel_(model, sampleCache, bank, 0);
    
    if (model.instruments.empty())
        return false;
    
    std::string name = resolveItemName_(bank, "TuneBloom Bank");
    return writeSf2_(path, model, name.c_str());
}

static bool buildSeqSoundModel_(const Sound &sound, ExportModel &model)
{
    if (sound.getSoundType() != Sound::SoundType::Seq)
        return false;

    const Sound::SequenceSoundInfo &info = sound.getSequenceSoundInfo();

    std::set<std::pair<u16, u16>> used;
    const Item *seqItem = info.getSequenceFileRef().getItem();

    if (seqItem && seqItem->getItemType() == Item::ItemType::SequenceFile)
    {
        const SequenceFile *seqFile = static_cast<const SequenceFile *>(seqItem);
        used = collectUsedPrograms(*seqFile, info.getStartOffset());
    }

    const std::set<std::pair<u16, u16>> *filter = used.empty() ? nullptr : &used;

    std::unordered_map<const WaveFile *, CachedSampleIndices> sampleCache;
    for (u32 i = 0; i < 4; i++)
    {
        const Item *bankItem = info.getBankRef(i).getItem();

        if (!bankItem || bankItem->getItemType() != Item::ItemType::Bank)
            continue;

        const Bank *bank = static_cast<const Bank *>(bankItem);
        const Item *fileItem = bank->getFileRef().getItem();

        if (!fileItem || fileItem->getItemType() != Item::ItemType::BankFile)
            continue;

        const BankFile *bankFile = static_cast<const BankFile *>(fileItem);
        
        addBankToModel_(model, sampleCache, *bankFile, (u16)i, filter);
    }

    return !model.instruments.empty();
}

bool exportSeqSoundToSf2(const sead::SafeString &path, const Sound &sound)
{
    ExportModel model;

    if (!buildSeqSoundModel_(sound, model))
        return false;
    
    std::string name = resolveItemName_(sound, "Sequence");

    return writeSf2_(path, model, name.c_str());
}

bool exportBankToDls(const sead::SafeString &path, const BankFile &bank)
{
    ExportModel model;
    std::unordered_map<const WaveFile *, CachedSampleIndices> sampleCache;

    addBankToModel_(model, sampleCache, bank, 0);

    if (model.instruments.empty())
        return false;
    
    std::string name = resolveItemName_(bank, "TuneBloom Bank");
    return writeDls_(path, model, name.c_str());
}

bool exportSeqSoundToDls(const sead::SafeString &path, const Sound &sound)
{
    ExportModel model;

    if (!buildSeqSoundModel_(sound, model))
        return false;
    
    std::string name = resolveItemName_(sound, "Sequence");

    return writeDls_(path, model, name.c_str());
}

static bool exportSeqSoundSetToDir_(const sead::SafeString &dirPath, const SoundSet &soundSet, const char *ext, bool useDls)
{
    if (soundSet.getIsEmpty() || soundSet.getSoundSetType() != SoundSet::SoundSetType::Seq)
        return false;

    u32 startId = soundSet.getStartId();
    u32 endId = soundSet.getEndId();

    extern Bfsar sBfsar;
    auto &soundList = sBfsar.getSoundList();

    bool anyOk = false;

    for (auto it = soundList.begin(); it != soundList.end(); ++it)
    {
        Sound *sound = static_cast<Sound *>(*it);
        u32 id = sound->getId();
        
        if (id < startId || id > endId)
            continue;
        
        if (sound->getSoundType() != Sound::SoundType::Seq)
            continue;

        std::string name = resolveItemName_(*sound, "Sequence");

        sead::FormatFixedSafeString<512> outPath("%s/%s.%s", dirPath.cstr(), name.c_str(), ext);
        
        if (useDls ? exportSeqSoundToDls(outPath, *sound) : exportSeqSoundToSf2(outPath, *sound))
            anyOk = true;
    }

    return anyOk;
}

bool exportSeqSoundSetToSf2Dir(const sead::SafeString &dirPath, const SoundSet &soundSet)
{
    return exportSeqSoundSetToDir_(dirPath, soundSet, "sf2", false);
}

bool exportSeqSoundSetToDlsDir(const sead::SafeString &dirPath, const SoundSet &soundSet)
{
    return exportSeqSoundSetToDir_(dirPath, soundSet, "dls", true);
}