#include <midi/SeqSoundFontExporter.h>

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
};

struct ExportRegion
{
    u8 keyMin, keyMax, velMin, velMax;
    u8 rootKey, volume, pan;
    u8 attack, decay, sustain, hold, release;
    bool loop;
    u32 sampleIndex;
};

struct ExportInstrument
{
    u16 bankNo;
    u16 programNo;
    std::vector<ExportRegion> regions;
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

static double indexToSeconds_(int v)
{
    if (v < 0)
        v = 0;
    if (v > 127)
        v = 127;
    if (v >= 127)
        return 0.001;
    double frac = v / 127.0;
    return 10.0 * std::pow(0.0001, frac);
}

struct Sf2Env
{
    s16 attack, hold, decay, sustain, release;
};

static Sf2Env adshrToSf2Env_(u8 attack, u8 decay, u8 sustain, u8 hold, u8 release)
{
    Sf2Env e;
    e.attack = tsFromSeconds_(indexToSeconds_(attack));
    e.decay = tsFromSeconds_(indexToSeconds_(decay));
    e.release = tsFromSeconds_(indexToSeconds_(release));
    e.hold = (hold == 0) ? (s16)-12000 : tsFromSeconds_(indexToSeconds_(hold));
    int cb = (int)std::lround((1.0 - (sustain > 127 ? 127 : sustain) / 127.0) * 1440.0);
    if (cb < 0)
        cb = 0;
    if (cb > 1440)
        cb = 1440;
    e.sustain = (s16)cb;
    return e;
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
    size_t n = name.size() < 20 ? name.size() : 20;
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
    GEN_SAMPLEMODES = 54,
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
        putU16_(shdr, 0);
        putU16_(shdr, 1);
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
        std::string iname = "INST_" + std::to_string(in.programNo);
        putName20_(inst, iname);
        putU16_(inst, ibagIndex);

        for (const ExportRegion &r : in.regions)
        {
            putU16_(ibag, (u16)(igen.size() / 4));
            putU16_(ibag, 0);
            ibagIndex++;

            putGen_(igen, GEN_KEYRANGE, (u16)((r.keyMax << 8) | r.keyMin));
            putGen_(igen, GEN_VELRANGE, (u16)((r.velMax << 8) | r.velMin));
            int pan = (int)std::lround((r.pan - 64) / 64.0 * 500.0);
            if (pan < -500)
                pan = -500;
            if (pan > 500)
                pan = 500;
            putGen_(igen, GEN_PAN, (u16)(s16)pan);
            int atten = (int)std::lround((1.0 - r.volume / 127.0) * 1440.0);
            if (atten < 0)
                atten = 0;
            if (atten > 1440)
                atten = 1440;
            putGen_(igen, GEN_INITATTEN, (u16)atten);
            Sf2Env env = adshrToSf2Env_(r.attack, r.decay, r.sustain, r.hold, r.release);
            putGen_(igen, GEN_ATTACKVOLENV, (u16)env.attack);
            putGen_(igen, GEN_HOLDVOLENV, (u16)env.hold);
            putGen_(igen, GEN_DECAYVOLENV, (u16)env.decay);
            putGen_(igen, GEN_SUSTAINVOLENV, (u16)env.sustain);
            putGen_(igen, GEN_RELEASEVOLENV, (u16)env.release);
            putGen_(igen, GEN_ROOTKEY, r.rootKey);
            putGen_(igen, GEN_SAMPLEMODES, r.loop ? 1 : 0);
            putGen_(igen, GEN_SAMPLEID, (u16)r.sampleIndex);
        }
    }
    putName20_(inst, "EOI");
    putU16_(inst, ibagIndex);
    putU16_(ibag, (u16)(igen.size() / 4));
    putU16_(ibag, 0);
    putGen_(igen, 0, 0);

    std::vector<u8> imod;
    putU16_(imod, 0);
    putU16_(imod, 0);
    putU16_(imod, 0);
    putU16_(imod, 0);
    putU16_(imod, 0);

    u16 pbagIndex = 0;
    for (size_t i = 0; i < model.instruments.size(); i++)
    {
        const ExportInstrument &in = model.instruments[i];
        std::string pname = "PRG_" + std::to_string(in.programNo);
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
    putU16_(pmod, 0);
    putU16_(pmod, 0);
    putU16_(pmod, 0);
    putU16_(pmod, 0);
    putU16_(pmod, 0);

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

    FILE *fp = std::fopen(path.cstr(), "wb");
    if (!fp)
        return false;
    bool ok = std::fwrite(file.data(), 1, file.size(), fp) == file.size();
    std::fclose(fp);
    return ok;
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
    putU32_(o, (u32)scale);
}

static s32 dlsTimeCents_(u8 idx)
{
    return (s32)tsFromSeconds_(indexToSeconds_(idx));
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
            putU16_(rgnh, 0);
            appendChunk_(rgnPayload, "rgnh", rgnh);

            putWsmp_(rgnPayload, r.rootKey, model.samples[r.sampleIndex]);

            std::vector<u8> wlnk;
            putU16_(wlnk, 0);
            putU16_(wlnk, 0);
            putU32_(wlnk, 1);
            putU32_(wlnk, r.sampleIndex);
            appendChunk_(rgnPayload, "wlnk", wlnk);

            int pan = (int)std::lround((r.pan - 64) / 64.0 * 500.0);
            if (pan < -500)
                pan = -500;
            if (pan > 500)
                pan = 500;
            int atten = (int)std::lround((1.0 - r.volume / 127.0) * 1440.0);
            if (atten < 0)
                atten = 0;
            if (atten > 1440)
                atten = 1440;
            int sustain = (int)std::lround((r.sustain > 127 ? 127 : r.sustain) / 127.0 * 1000.0);
            if (sustain < 0)
                sustain = 0;
            if (sustain > 1000)
                sustain = 1000;

            std::vector<u8> conns;
            putConnection_(conns, DLS_CONN_DST_ATTENUATION, atten);
            putConnection_(conns, DLS_CONN_DST_PAN, pan);
            putConnection_(conns, DLS_CONN_DST_EG1_ATTACKTIME, dlsTimeCents_(r.attack));
            putConnection_(conns, DLS_CONN_DST_EG1_HOLDTIME, r.hold == 0 ? -12000 : dlsTimeCents_(r.hold));
            putConnection_(conns, DLS_CONN_DST_EG1_DECAYTIME, dlsTimeCents_(r.decay));
            putConnection_(conns, DLS_CONN_DST_EG1_SUSTAINLEVEL, sustain);
            putConnection_(conns, DLS_CONN_DST_EG1_RELEASETIME, dlsTimeCents_(r.release));

            std::vector<u8> art2;
            putU32_(art2, 8);
            putU32_(art2, 7);
            art2.insert(art2.end(), conns.begin(), conns.end());

            std::vector<u8> lar2Payload;
            appendChunk_(lar2Payload, "art2", art2);
            appendListChunk_(rgnPayload, "lar2", lar2Payload);

            appendListChunk_(lrgnPayload, "rgn2", rgnPayload);
        }
        appendListChunk_(insPayload, "lrgn", lrgnPayload);

        std::vector<u8> instInfo;
        std::string iname = "INST_" + std::to_string(in.programNo);
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

    FILE *fp = std::fopen(path.cstr(), "wb");
    if (!fp)
        return false;
    bool ok = std::fwrite(file.data(), 1, file.size(), fp) == file.size();
    std::fclose(fp);
    return ok;
}

static void addBankToModel_(ExportModel &model,
                            std::unordered_map<const WaveFile *, s32> &sampleCache,
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

                s32 sidx;
                auto it = sampleCache.find(wave);
                if (it != sampleCache.end())
                {
                    sidx = it->second;
                }
                else
                {
                    DecodedPcm pcm = decodeWaveFileForEditing(*wave);
                    if (!pcm.isValid())
                    {
                        sampleCache[wave] = -1;
                        PopupMgr::instance()->addPopup({"A wave file could not be decoded for SF2 export", nullptr});
                        continue;
                    }

                    std::vector<float> mono = pcm.monoMix();
                    ExportSample es;
                    es.pcm.resize(mono.size());
                    for (size_t i = 0; i < mono.size(); i++)
                    {
                        float f = mono[i] * 32767.0f;
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
                    es.name = wave->getName().cstr();

                    sidx = (s32)model.samples.size();
                    model.samples.push_back(std::move(es));
                    sampleCache[wave] = sidx;
                }

                if (sidx < 0)
                    continue;

                ExportRegion r;
                r.keyMin = kr->getKeyMin();
                r.keyMax = kr->getKeyMax();
                r.velMin = vr->getVelocityMin();
                r.velMax = vr->getVelocityMax();
                r.rootKey = vr->getRootKey();
                r.volume = vr->getVolume();
                r.pan = vr->getPan();
                const snd::AdshrCurve &env = vr->getAdshrCurve();
                r.attack = env.attack;
                r.decay = env.decay;
                r.sustain = env.sustain;
                r.hold = env.hold;
                r.release = env.release;
                r.loop = wave->getIsLoop();
                r.sampleIndex = (u32)sidx;
                einst.regions.push_back(r);
            }
        }

        if (!einst.regions.empty())
            model.instruments.push_back(std::move(einst));
    }
}

bool exportBankToSf2(const sead::SafeString &path, const BankFile &bank)
{
    ExportModel model;
    std::unordered_map<const WaveFile *, s32> sampleCache;
    addBankToModel_(model, sampleCache, bank, 0);
    if (model.instruments.empty())
        return false;
    return writeSf2_(path, model, bank.getNameOrNull().cstr());
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

    std::unordered_map<const WaveFile *, s32> sampleCache;
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
    sead::FixedSafeString<256> name(sound.getNameOrNull());
    return writeSf2_(path, model, name.cstr());
}

bool exportBankToDls(const sead::SafeString &path, const BankFile &bank)
{
    ExportModel model;
    std::unordered_map<const WaveFile *, s32> sampleCache;
    addBankToModel_(model, sampleCache, bank, 0);
    if (model.instruments.empty())
        return false;
    return writeDls_(path, model, bank.getNameOrNull().cstr());
}

bool exportSeqSoundToDls(const sead::SafeString &path, const Sound &sound)
{
    ExportModel model;
    if (!buildSeqSoundModel_(sound, model))
        return false;
    sead::FixedSafeString<256> name(sound.getNameOrNull());
    return writeDls_(path, model, name.cstr());
}

static bool exportSeqSoundSetToDir_(const sead::SafeString &dirPath, const SoundSet &soundSet,
                                    const char *ext, bool useDls)
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

        sead::FixedSafeString<256> name(sound->getNameOrNull());
        if (name.isEmpty())
            name = "Sequence";

        sead::FormatFixedSafeString<512> outPath("%s/%s.%s", dirPath.cstr(), name.cstr(), ext);
        bool ok = useDls ? exportSeqSoundToDls(outPath, *sound) : exportSeqSoundToSf2(outPath, *sound);
        if (ok)
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
