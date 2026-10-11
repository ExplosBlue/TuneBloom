#include <ui/bank/BankBundle.h>

#include <ui/bank/BankFileEditor.h>
#include <ui/widgets/Dialogs.h>
#include <ui/export/ExportInternal.h>
#include <ui/file/File.h>
#include <ui/file/FileDialogs.h>
#include <ui/Messages.h>
#include <ui/app/PopupMgr.h>
#include <ui/items/Selection.h>
#include <ui/file/UnsavedChanges.h>

#include <libs/IconsLucide.h>
#include <imgui/imgui.h>

#include <filedevice/seadFileDeviceMgr.h>
#include <prim/seadEndian.h>
#include <prim/seadMemUtil.h>
#include <stream/seadFileDeviceStream.h>

#include <bfsar/files/BankFile.h>
#include <bfsar/io/FileIo.h>

#include <cstring>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct BundleReader
{
    u8* data = nullptr;
    u32 size = 0;
    u32 offset = 0;
    bool oob = false;

    bool read(void* out, u32 length)
    {
        if (oob || offset + length > size)
        {
            oob = true;
            return false;
        }

        sead::MemUtil::copy(out, data + offset, length);
        offset += length;

        return true;
    }

    u32 readU32()
    {
        u32 value = 0;
        read(&value, sizeof(value));

        return sead::Endian::toHostU32(sead::Endian::eBig, value);
    }

    u16 readU16()
    {
        u16 value = 0;
        read(&value, sizeof(value));

        return sead::Endian::toHostU16(sead::Endian::eBig, value);
    }

    u8 readU8()
    {
        u8 value = 0;
        read(&value, sizeof(value));

        return value;
    }

    f32 readF32()
    {
        u32 bits = readU32();
        f32 value;
        sead::MemUtil::copy(&value, &bits, sizeof(value));

        return value;
    }

    std::string readString()
    {
        u32 length = readU32();
        if (oob || length > size - offset)
        {
            oob = true;
            return std::string();
        }

        std::string text(reinterpret_cast<const char*>(data + offset), length);
        offset += length;

        return text;
    }
};

namespace
{

struct InstrumentImport
{
    u8* fileData = nullptr;
    u32 fileSize = 0;
    u32 waveCount = 0;
    u32 dataOffset = 0;
    BankFile* targetBank = nullptr;
    BankFile::Instrument* replaceTarget = nullptr;
    sead::FixedSafeString<256> nameEdit;
    s16 programNo = 0;
};

struct BankBundleImport
{
    u8* fileData = nullptr;
    u32 fileSize = 0;
    u32 waveCount = 0;
    u32 dataOffset = 0;
    sead::FixedSafeString<256> nameEdit;
};

struct BundleFileType
{
    const char* extension;
    const char* filterNameFormat;
    u32 magic;
    const char* invalidMessage;
    const char* versionMessage;
};

enum class BundleLoadResult
{
    None,
    Loaded,
    Failed
};

}

static constexpr u32 cBundleVersion = 1;

static constexpr BundleFileType cInstrumentBundleType = { "ibnk", messages::filedialog::cFilterInstrumentBundleFormat, 0x49424E44, messages::import::cInstrumentBundleInvalid, messages::import::cInstrumentBundleVersion };
static constexpr BundleFileType cBankBundleType       = { "bbnk", messages::filedialog::cFilterBankBundleFormat, 0x42424E44, messages::import::cBankBundleInvalid, messages::import::cBankBundleVersion };

static std::vector<BankFile*> sPendingBankBundleExports;
static bool sPendingBankBundleImport = false;
static BankFile::Instrument* sPendingInstrumentExport = nullptr;
static BankFile* sPendingInstrumentImportBank = nullptr;

static InstrumentImport sInstrumentImport;
static BankBundleImport sBankBundleImport;

static void UnloadBundleFile(u8* fileData)
{
    if (!fileData)
        return;

    sead::FileDevice* device = sead::FileDeviceMgr::instance()->findDevice("native");
    if (device)
        device->unload(fileData);
}

static void CloseInstrumentImport()
{
    UnloadBundleFile(sInstrumentImport.fileData);

    sInstrumentImport.fileData = nullptr;
    sInstrumentImport.fileSize = 0;
    sInstrumentImport.waveCount = 0;
    sInstrumentImport.dataOffset = 0;
    sInstrumentImport.targetBank = nullptr;
    sInstrumentImport.replaceTarget = nullptr;
}

static void CloseBankBundleImport()
{
    UnloadBundleFile(sBankBundleImport.fileData);

    sBankBundleImport.fileData = nullptr;
    sBankBundleImport.fileSize = 0;
    sBankBundleImport.waveCount = 0;
    sBankBundleImport.dataOffset = 0;
}

void RequestExportBankBundles(const std::vector<BankFile*>& bankFiles)
{
    sPendingBankBundleExports = bankFiles;
}

void RequestImportBankBundle()
{
    sPendingBankBundleImport = true;
}

void RequestExportInstrument(Item* instrument)
{
    sPendingInstrumentExport = static_cast<BankFile::Instrument*>(instrument);
}

void RequestImportInstrument(Item* targetBank)
{
    sInstrumentImport.replaceTarget = nullptr;
    sPendingInstrumentImportBank = static_cast<BankFile*>(targetBank);
}

void RequestReplaceInstrument(Item* instrument, Item* bank)
{
    sInstrumentImport.replaceTarget = static_cast<BankFile::Instrument*>(instrument);
    sPendingInstrumentImportBank = static_cast<BankFile*>(bank);
}

void CancelBankBundleTransfers()
{
    sPendingBankBundleExports.clear();
    sPendingBankBundleImport = false;
    sPendingInstrumentExport = nullptr;
    sPendingInstrumentImportBank = nullptr;

    CloseInstrumentImport();
    CloseBankBundleImport();
}

static void CollectBundledWaves(const BankFile::Instrument* instr, std::vector<const WaveFile*>* waves, std::unordered_set<const WaveFile*>* seenWaves)
{
    ForEachVelocityRegion(*instr, [&](const BankFile::KeyRegion&, const BankFile::VelocityRegion& vr)
    {
        const Item* waveItem = vr.getWaveFileRef().getItem();
        if (!waveItem)
            return;

        const WaveFile* wave = static_cast<const WaveFile*>(waveItem);
        if (seenWaves->insert(wave).second)
            waves->push_back(wave);
    });
}

static void BuildWaveExportNames(const std::vector<const WaveFile*>& waves, u32* fallbackId, std::unordered_map<const WaveFile*, std::string>* exportNames)
{
    std::unordered_map<std::string, u32> nameCounts;
    for (const WaveFile* wave : waves)
        nameCounts[wave->getName().cstr()]++;

    std::unordered_set<std::string> usedNames;
    for (const WaveFile* wave : waves)
    {
        std::string name = wave->getName().cstr();

        if (!name.empty() && nameCounts[name] <= 1)
        {
            (*exportNames)[wave] = name;
            usedNames.insert(name);
            continue;
        }

        const std::string& hash = wave->getMd5Hash();
        if (!hash.empty())
        {
            std::string exportName = hash;
            u32 suffix = 0;
            while (nameCounts.find(exportName) != nameCounts.end() || usedNames.count(exportName))
                exportName = hash + "_" + std::to_string(suffix++);

            usedNames.insert(exportName);
            (*exportNames)[wave] = exportName;
            continue;
        }

        sead::FixedSafeString<256> fallbackName;
        do
        {
            fallbackName.format("_W%u", (*fallbackId)++);
        } while (nameCounts.find(fallbackName.cstr()) != nameCounts.end() || usedNames.count(fallbackName.cstr()));

        usedNames.insert(fallbackName.cstr());
        (*exportNames)[wave] = fallbackName.cstr();
    }
}

static void WriteBundledWave(sead::FileDeviceWriteStream* stream, sead::FileDevice* device, const WaveFile* wave, const char* waveName, const char* tempTag, u64* tempCounter)
{
    u32 waveNameLen = strlen(waveName);
    stream->writeU32(waveNameLen);
    stream->writeMemBlock(waveName, waveNameLen);

    std::string tmpDir = std::filesystem::temp_directory_path().string();
    sead::FixedSafeString<512> tempPath;
    tempPath.format("%s/tb_%s_%llu.tmp", tmpDir.c_str(), tempTag, (unsigned long long)(*tempCounter)++);

    sead::FileHandle tempHandle;
    if (!device->tryOpen(&tempHandle, tempPath, sead::FileDevice::FileOpenFlag::eWriteOnly, 0))
    {
        stream->writeU32(0);
        return;
    }

    sead::FileDeviceWriteStream tempStream(&tempHandle, sead::Stream::Modes::eBinary);
    wave->write(&tempHandle, &tempStream, sead::Endian::eBig, true);
    tempHandle.close();

    sead::FileDevice::LoadArg loadArg;
    loadArg.path = tempPath;
    u8* waveBuf = device->tryLoad(loadArg);
    if (waveBuf)
    {
        u32 waveSize = loadArg.read_size;
        stream->writeU32(waveSize);
        stream->writeMemBlock(waveBuf, waveSize);
        device->unload(waveBuf);
    }
    else
    {
        stream->writeU32(0);
    }

    ::remove(tempPath.cstr());
}

static void WriteBundledKeyRegions(sead::FileDeviceWriteStream* stream, const BankFile::Instrument* instr, const std::unordered_map<const WaveFile*, std::string>& waveExportNames)
{
    stream->writeU16(instr->getKeyRegionList().size());

    for (const Item* krItem : instr->getKeyRegionList())
    {
        const auto* kr = static_cast<const BankFile::KeyRegion*>(krItem);
        stream->writeU8(kr->getKeyMin());
        stream->writeU8(kr->getKeyMax());
        stream->writeU16(kr->getVelocityRegionList().size());

        for (const Item* vrItem : kr->getVelocityRegionList())
        {
            const auto* vr = static_cast<const BankFile::VelocityRegion*>(vrItem);

            const Item* refWaveItem = vr->getWaveFileRef().getItem();
            const char* refWaveName = "";
            if (refWaveItem)
            {
                auto nameIt = waveExportNames.find(static_cast<const WaveFile*>(refWaveItem));
                if (nameIt != waveExportNames.end())
                    refWaveName = nameIt->second.c_str();
            }
            u32 refNameLen = strlen(refWaveName);
            stream->writeU32(refNameLen);
            stream->writeMemBlock(refWaveName, refNameLen);

            stream->writeU8(vr->getVelocityMin());
            stream->writeU8(vr->getVelocityMax());
            stream->writeU8(vr->getRootKey());
            stream->writeU8(vr->getVolume());
            stream->writeU8(vr->getPan());
            stream->writeF32(vr->getPitch());
            stream->writeU8(vr->getIsIgnoreNoteOff() ? 1 : 0);
            stream->writeU8(vr->getKeyGroup());
            stream->writeU8(vr->getInterpolationType());
            const auto& adsr = vr->getAdshrCurve();
            stream->writeU8(adsr.attack);
            stream->writeU8(adsr.decay);
            stream->writeU8(adsr.sustain);
            stream->writeU8(adsr.hold);
            stream->writeU8(adsr.release);
        }
    }
}

static void ExportBankBundle(const BankFile* bank, sead::FileDevice* device, sead::FileHandle* handle)
{
    std::vector<const WaveFile*> referencedWaves;
    std::unordered_set<const WaveFile*> seenWaves;

    for (const Item* instrItem : bank->getInstrumentList())
        CollectBundledWaves(static_cast<const BankFile::Instrument*>(instrItem), &referencedWaves, &seenWaves);

    static u32 sWaveExportId = 0;
    std::unordered_map<const WaveFile*, std::string> waveExportNames;
    BuildWaveExportNames(referencedWaves, &sWaveExportId, &waveExportNames);

    sead::FileDeviceWriteStream stream(handle, sead::Stream::Modes::eBinary);
    stream.setBinaryEndian(sead::Endian::eBig);

    // Magic: "BBND"
    stream.writeU32(0x42424E44);
    // Version
    stream.writeU32(1);

    // Bank name
    const char* bankName = bank->getNameOrNull().cstr();
    u32 nameLen = strlen(bankName);
    stream.writeU32(nameLen);
    stream.writeMemBlock(bankName, nameLen);

    // Wave count
    stream.writeU32(referencedWaves.size());

    // Write each wave binary via temp file
    for (const WaveFile* wave : referencedWaves)
    {
        static u64 sTempCounter = 0;
        WriteBundledWave(&stream, device, wave, waveExportNames[wave].c_str(), "bbnk", &sTempCounter);
    }

    // Instrument count
    stream.writeU32(bank->getInstrumentList().size());
    for (const Item* instrItem : bank->getInstrumentList())
    {
        const auto* instr = static_cast<const BankFile::Instrument*>(instrItem);
        stream.writeU16(instr->getProgramNo());
        WriteBundledKeyRegions(&stream, instr, waveExportNames);
    }
}

static bool WriteBankBundleFile(const BankFile* bank, sead::FileDevice* device, const char* path)
{
    fileio::StagedFile staged;
    if (!staged.open(path))
        return false;

    ExportBankBundle(bank, device, staged.getHandle());

    return staged.commit();
}

static void ExportInstrumentBundle(const BankFile::Instrument* instr, sead::FileDevice* device, sead::FileHandle* handle)
{
    std::vector<const WaveFile*> referencedWaves;
    std::unordered_set<const WaveFile*> seenWaves;
    CollectBundledWaves(instr, &referencedWaves, &seenWaves);

    static u32 sWaveExportId = 0;
    std::unordered_map<const WaveFile*, std::string> waveExportNames;
    BuildWaveExportNames(referencedWaves, &sWaveExportId, &waveExportNames);

    sead::FileDeviceWriteStream stream(handle, sead::Stream::Modes::eBinary);
    stream.setBinaryEndian(sead::Endian::eBig);

    stream.writeU32(0x49424E44);
    stream.writeU32(1);

    const char* instrName = instr->getNameOrNull().cstr();
    u32 nameLen = strlen(instrName);
    stream.writeU32(nameLen);
    stream.writeMemBlock(instrName, nameLen);

    stream.writeU16(instr->getProgramNo());

    stream.writeU32(referencedWaves.size());
    for (const WaveFile* wave : referencedWaves)
    {
        static u64 sTempCounter = 0;
        WriteBundledWave(&stream, device, wave, waveExportNames[wave].c_str(), "ibnk", &sTempCounter);
    }

    WriteBundledKeyRegions(&stream, instr, waveExportNames);
}

static void ReportFailedBundledWaves(u32 failedWaveCount)
{
    if (failedWaveCount == 0)
        return;

    sead::FormatFixedSafeString<64> message(failedWaveCount == 1 ? messages::import::cBundledWavesFailedOneFormat : messages::import::cBundledWavesFailedFormat, failedWaveCount);

    PopupMgr::instance()->addPopup({message, nullptr});
}

static WaveFile* FindWaveFileByName(const char* name)
{
    for (const auto& node : sBfsar.getWaveFileList())
        if (node->getName() == name)
            return static_cast<WaveFile*>(node);

    return nullptr;
}

void PatchWaveFileHeader(u8* waveData)
{
    nw::ut::BinaryFileHeader* header = reinterpret_cast<nw::ut::BinaryFileHeader*>(waveData);
    const bool isLittle = waveData[4] == 0xFF;
    const u32 version = sBfsar.getVersionForBfwav();

    for (u32 i = 0; i < 4; i++)
        waveData[8 + i] = (version >> (isLittle ? i * 8 : 24 - i * 8)) & 0xFF;

    const char* magic = GetInnerFileMagic(sBfsar.getFormat(), InnerFileKind::Wave);

    for (u32 i = 0; i < 4; i++)
        header->signature[i] = magic[i];
}

static void ReadBundledWaves(BundleReader* reader, u32 waveCount, std::unordered_map<std::string, WaveFile*>* waveMap)
{
    u32 failedWaveCount = 0;

    for (u32 i = 0; !reader->oob && i < waveCount; i++)
    {
        std::string waveName = reader->readString();
        u32 waveSize = reader->readU32();

        if (reader->oob || waveSize == 0)
            continue;

        if (waveSize < 12 || waveSize > reader->size - reader->offset)
        {
            reader->oob = true;
            continue;
        }

        u8* waveData = reader->data + reader->offset;
        reader->offset += waveSize;

        if (WaveFile* existing = FindWaveFileByName(waveName.c_str()))
        {
            (*waveMap)[waveName.c_str()] = existing;
            continue;
        }

        PatchWaveFileHeader(waveData);

        WaveFile* newWave = new WaveFile();
        newWave->setEnableName(true);
        newWave->getName() = waveName.c_str();

        if (!newWave->read(waveData))
        {
            delete newWave;
            failedWaveCount++;
            continue;
        }

        newWave->setFormat(sBfsar.getFormat());
        newWave->setVersion(sBfsar.getVersionForBfwav());
        newWave->refreshSerializedHash();
        sBfsar.getWaveFileList().pushBack(newWave);
        sBfsar.updateList(sBfsar.getWaveFileList());
        SetUnsavedChanges(true);
        (*waveMap)[waveName.c_str()] = newWave;
    }

    ReportFailedBundledWaves(failedWaveCount);
}

static BankFile::KeyRegion* ReadBundledKeyRegion(BundleReader* reader, const std::unordered_map<std::string, WaveFile*>& waveMap)
{
    u8 keyMin = reader->readU8();
    u8 keyMax = reader->readU8();
    u16 vrCount = reader->readU16();

    BankFile::KeyRegion* kr = new BankFile::KeyRegion(keyMin, keyMax);

    for (u16 v = 0; !reader->oob && v < vrCount; v++)
    {
        std::string refWaveName = reader->readString();
        u8 velMin = reader->readU8();
        u8 velMax = reader->readU8();

        BankFile::VelocityRegion* vr = new BankFile::VelocityRegion(velMin, velMax);
        vr->setRootKey(reader->readU8());
        vr->setVolume(reader->readU8());
        vr->setPan(reader->readU8());
        vr->setPitch(reader->readF32());
        vr->setIsIgnoreNoteOff(reader->readU8() != 0);
        vr->setKeyGroup(reader->readU8());
        vr->setInterpolationType(reader->readU8());

        snd::AdshrCurve adsr;
        adsr.attack = reader->readU8();
        adsr.decay = reader->readU8();
        adsr.sustain = reader->readU8();
        adsr.hold = reader->readU8();
        adsr.release = reader->readU8();
        vr->setAdshrCurve(adsr);

        WaveFile* foundWave = nullptr;
        auto waveIt = waveMap.find(refWaveName.c_str());

        if (waveIt != waveMap.end())
            foundWave = waveIt->second;
        else if (!refWaveName.empty())
            foundWave = FindWaveFileByName(refWaveName.c_str());

        if (foundWave)
            vr->getWaveFileRef().attach(foundWave);

        kr->getVelocityRegionList().pushBack(vr);
    }

    return kr;
}

void ExportPendingBankBundles()
{
    const sead::FormatFixedSafeString<64> filterName(cBankBundleType.filterNameFormat, cBankBundleType.extension);

    ExportItemsToFiles(&sPendingBankBundleExports, { cBankBundleType.extension, filterName.cstr(), messages::common::cBank, messages::exporting::cSelectFolder },
        [](BankFile* bank, const sead::SafeString& path)
        {
            sead::FileDevice* device = sead::FileDeviceMgr::instance()->findDevice("native");
            return device && WriteBankBundleFile(bank, device, path.cstr());
        },
        [](bool exported)
        {
            if (exported)
                ShowExportResult(messages::exporting::cBankBundleExported);
            else
                ShowExportResult(sead::FormatFixedSafeString<128>(messages::exporting::cFileFailedFormat, messages::exporting::cFormatBankBundle).cstr());
        },
        [](s32 total, s32 failureCount)
        {
            ShowBatchExportResult(total, failureCount, messages::exporting::cFormatBankBundle);
        });
}

static const BankFile* FindInstrumentBank(const BankFile::Instrument* instr)
{
    for (const auto& node : sBfsar.getBankFileList())
    {
        const BankFile* bank = static_cast<const BankFile*>(node);

        for (const auto& instrNode : bank->getInstrumentList())
        {
            if (instrNode == instr)
                return bank;
        }
    }

    return nullptr;
}

static void BuildInstrumentExportPath(const BankFile::Instrument* instr, const char* ext, sead::BufferedSafeString* out)
{
    const BankFile* ownerBank = FindInstrumentBank(instr);

    sead::FixedSafeString<256> fileName;
    if (instr->isNameValid())
        fileName.format("%s", instr->getName().cstr());
    else if (ownerBank && ownerBank->isNameValid())
        fileName.format(messages::exporting::cInstrumentFileOfBankFormat, ownerBank->getName().cstr(), instr->getProgramNo());
    else
        fileName.format(messages::exporting::cInstrumentFileFormat, instr->getProgramNo());

    SanitizeFilenameInPlace(&fileName);

    const std::string cwd = std::filesystem::current_path().string();
    out->format("%s/%s.%s", cwd.c_str(), fileName.cstr(), ext);
}

static bool WriteInstrumentBundleFile(const BankFile::Instrument* instr, const sead::SafeString& path)
{
    sead::FileDevice* device = sead::FileDeviceMgr::instance()->findDevice("native");
    if (!device)
        return false;

    fileio::StagedFile staged;
    if (!staged.open(path))
        return false;

    ExportInstrumentBundle(instr, device, staged.getHandle());

    return staged.commit();
}

void ExportPendingInstrument()
{
    if (!sPendingInstrumentExport)
        return;

    BankFile::Instrument* instr = sPendingInstrumentExport;
    sPendingInstrumentExport = nullptr;

    const char* ext = cInstrumentBundleType.extension;
    const sead::FormatFixedSafeString<64> filterName(cInstrumentBundleType.filterNameFormat, ext);

    sead::FixedSafeString<512> defaultPath;
    BuildInstrumentExportPath(instr, ext, &defaultPath);

    sead::FixedSafeString<512> path;
    if (!SaveFileDialogWithExtension(&path, filterName.cstr(), ext, defaultPath.cstr()))
        return;

    if (WriteInstrumentBundleFile(instr, path))
        ShowExportResult(messages::exporting::cInstrumentExported);
    else
        ShowExportResult(sead::FormatFixedSafeString<128>(messages::exporting::cFileFailedFormat, messages::exporting::cFormatInstrument).cstr());
}

static BundleLoadResult LoadBundleFile(const BundleFileType& type, BundleReader* reader)
{
    const sead::FormatFixedSafeString<64> filterName(type.filterNameFormat, type.extension);

    sead::FixedSafeString<512> path;
    if (!OpenFileDialogWithExtension(&path, filterName.cstr(), type.extension))
        return BundleLoadResult::None;

    sead::FileDevice* device = sead::FileDeviceMgr::instance()->findDevice("native");
    if (!device)
        return BundleLoadResult::None;

    sead::FileDevice::LoadArg loadArg;
    loadArg.path = path;
    u8* fileData = device->tryLoad(loadArg);

    if (!fileData)
        return BundleLoadResult::None;

    *reader = { fileData, static_cast<u32>(loadArg.read_size) };

    const char* error = nullptr;

    if (reader->readU32() != type.magic)
        error = type.invalidMessage;
    else if (reader->readU32() != cBundleVersion)
        error = type.versionMessage;

    if (!error)
        return BundleLoadResult::Loaded;

    PopupMgr::instance()->addPopup({error, nullptr});
    device->unload(fileData);

    return BundleLoadResult::Failed;
}

bool BeginInstrumentImport()
{
    if (!sPendingInstrumentImportBank)
        return true;

    BankFile* targetBank = sPendingInstrumentImportBank;
    sPendingInstrumentImportBank = nullptr;

    BundleReader reader;
    const BundleLoadResult result = LoadBundleFile(cInstrumentBundleType, &reader);

    if (result != BundleLoadResult::Loaded)
        return result == BundleLoadResult::None;

    const std::string instrumentName = reader.readString();
    const s16 programNo = static_cast<s16>(reader.readU16());
    const u32 waveCount = reader.readU32();

    if (reader.oob)
    {
        PopupMgr::instance()->addPopup({messages::import::cInstrumentBundleCorrupt, nullptr});
        UnloadBundleFile(reader.data);
        return false;
    }

    const BankFile::Instrument* replaceTarget = sInstrumentImport.replaceTarget;

    sInstrumentImport.fileData = reader.data;
    sInstrumentImport.fileSize = reader.size;
    sInstrumentImport.targetBank = targetBank;
    sInstrumentImport.programNo = programNo;
    sInstrumentImport.waveCount = waveCount;
    sInstrumentImport.dataOffset = reader.offset;
    sInstrumentImport.nameEdit = !instrumentName.empty() ? instrumentName.c_str() : (replaceTarget ? replaceTarget->getName().cstr() : "Instrument");

    ImGui::OpenPopup(replaceTarget ? messages::import::cInstrumentReplaceTitle : messages::import::cInstrumentImportTitle);

    return true;
}

static BankFile::Instrument* ReadBundledInstrument(BundleReader* reader, const std::unordered_map<std::string, WaveFile*>& waveMap)
{
    BankFile::Instrument* instr = new BankFile::Instrument();

    const u16 keyRegionCount = reader->readU16();

    for (u16 k = 0; !reader->oob && k < keyRegionCount; k++)
        instr->getKeyRegionList().pushBack(ReadBundledKeyRegion(reader, waveMap));

    return instr;
}

static void ReplaceInstrument(BankFile::Instrument* target, BankFile::Instrument* source)
{
    DeselectVelocity();
    target->getKeyRegionList().clear();

    auto& src = source->getKeyRegionList();
    auto& dst = target->getKeyRegionList();

    for (auto it = src.robustBegin(); it != src.robustEnd(); ++it)
        dst.pushBack(&(*it));

    if (!sInstrumentImport.nameEdit.isEmpty())
    {
        target->setEnableName(true);
        target->getName() = sInstrumentImport.nameEdit.cstr();
    }

    target->setProgramNo(sInstrumentImport.programNo);

    delete source;
}

static void CommitInstrumentImport()
{
    BundleReader reader = { sInstrumentImport.fileData, sInstrumentImport.fileSize, sInstrumentImport.dataOffset };

    std::unordered_map<std::string, WaveFile*> waveMap;
    ReadBundledWaves(&reader, sInstrumentImport.waveCount, &waveMap);

    BankFile::Instrument* instr = ReadBundledInstrument(&reader, waveMap);

    instr->setEnableName(true);
    instr->getName() = sInstrumentImport.nameEdit.isEmpty() ? "Instrument" : sInstrumentImport.nameEdit.cstr();
    instr->setProgramNo(sInstrumentImport.programNo);

    if (reader.oob)
    {
        PopupMgr::instance()->addPopup({messages::import::cInstrumentBundleCorrupt, nullptr});
        delete instr;
        return;
    }

    if (sInstrumentImport.replaceTarget)
        ReplaceInstrument(sInstrumentImport.replaceTarget, instr);
    else
        sInstrumentImport.targetBank->getInstrumentList().pushBack(instr);

    sBfsar.updateList(sInstrumentImport.targetBank->getInstrumentList());
    SetUnsavedChanges(true);
}

void DrawInstrumentImportDialog()
{
    const BankFile::Instrument* replaceTarget = sInstrumentImport.replaceTarget;
    const char* popupName = replaceTarget ? messages::import::cInstrumentReplaceTitle : messages::import::cInstrumentImportTitle;

    if (!sInstrumentImport.fileData)
        return;

    if (!BeginDialog(popupName, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        if (!ImGui::IsPopupOpen(popupName))
            CloseInstrumentImport();

        return;
    }

    if (replaceTarget)
    {
        sead::FixedSafeString<256> replaceInfo;
        replaceInfo.format(messages::import::cInstrumentReplacingFormat, replaceTarget->getProgramNo());
        ImGui::TextUnformatted(replaceInfo.cstr());
    }

    ImGui::SetNextItemWidth(240.0f);
    ImGui::InputText(messages::common::cName, sInstrumentImport.nameEdit.getBuffer(), sInstrumentImport.nameEdit.getBufferSize());

    s16 progStep = 1;
    s16 progStepFast = 10;

    ImGui::SetNextItemWidth(120.0f);
    ImGui::InputScalar(messages::bank::cProgramNumber, ImGuiDataType_S16, &sInstrumentImport.programNo, &progStep, &progStepFast);

    if (DrawNextProgramButton("###NextAvailImport"))
        sInstrumentImport.programNo = FindNextAvailableProgramNo(sInstrumentImport.targetBank->getInstrumentList(), sInstrumentImport.replaceTarget);

    ImGui::Text(messages::import::cWavesToImportFormat, sInstrumentImport.waveCount);

    BeginDialogFooter({messages::common::cImport, messages::common::cCancel});

    if (DialogButton(messages::common::cImport))
    {
        CommitInstrumentImport();
        CloseInstrumentImport();
        ImGui::CloseCurrentPopup();
    }

    if (DialogButton(messages::common::cCancel))
    {
        CloseInstrumentImport();
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

bool BeginBankBundleImport()
{
    if (!sPendingBankBundleImport)
        return true;

    sPendingBankBundleImport = false;

    BundleReader reader;
    const BundleLoadResult result = LoadBundleFile(cBankBundleType, &reader);

    if (result != BundleLoadResult::Loaded)
        return result == BundleLoadResult::None;

    const std::string bankName = reader.readString();
    const u32 waveCount = reader.readU32();

    if (reader.oob)
    {
        PopupMgr::instance()->addPopup({messages::import::cBankBundleCorrupt, nullptr});
        UnloadBundleFile(reader.data);
        return false;
    }

    sBankBundleImport.fileData = reader.data;
    sBankBundleImport.fileSize = reader.size;
    sBankBundleImport.waveCount = waveCount;
    sBankBundleImport.dataOffset = reader.offset;
    sBankBundleImport.nameEdit = !bankName.empty() ? bankName.c_str() : "Bank";

    ImGui::OpenPopup(messages::menu::cImportBankBundle);

    return true;
}

static const char* CommitBankBundleImport()
{
    BundleReader reader = { sBankBundleImport.fileData, sBankBundleImport.fileSize, sBankBundleImport.dataOffset };

    std::unordered_map<std::string, WaveFile*> waveMap;
    ReadBundledWaves(&reader, sBankBundleImport.waveCount, &waveMap);

    if (reader.oob || reader.offset >= reader.size)
        return messages::import::cBankBundleTruncated;

    const u32 instrumentCount = reader.readU32();
    if (reader.oob || instrumentCount == 0)
        return reader.oob ? messages::import::cBankBundleCorrupt : messages::import::cBankBundleNoInstruments;

    BankFile* newBank = new BankFile();
    newBank->setEnableName(true);
    newBank->getName() = sBankBundleImport.nameEdit.isEmpty() ? "Bank" : sBankBundleImport.nameEdit.cstr();
    newBank->setup(sBfsar.getEndian(), sBfsar.getFormat(), sBfsar.getVersionForBfbnk());

    for (u32 i = 0; !reader.oob && i < instrumentCount; i++)
    {
        const s16 programNo = reader.readU16();

        BankFile::Instrument* instr = ReadBundledInstrument(&reader, waveMap);
        instr->setProgramNo(programNo);

        newBank->getInstrumentList().pushBack(instr);
    }

    if (reader.oob)
    {
        delete newBank;
        return messages::import::cBankBundleCorrupt;
    }

    sBfsar.getBankFileList().pushBack(newBank);
    sBfsar.updateList(sBfsar.getBankFileList());
    SetUnsavedChanges(true);
    SelectItem(newBank);

    return nullptr;
}

bool DrawBankBundleImportDialog()
{
    if (!sBankBundleImport.fileData)
        return true;

    if (!BeginDialog(messages::menu::cImportBankBundle, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        if (!ImGui::IsPopupOpen(messages::menu::cImportBankBundle))
            CloseBankBundleImport();

        return true;
    }

    bool failed = false;

    ImGui::SetNextItemWidth(240.0f);
    ImGui::InputText(messages::common::cName, sBankBundleImport.nameEdit.getBuffer(), sBankBundleImport.nameEdit.getBufferSize());

    ImGui::Text(messages::import::cWavesToImportFormat, sBankBundleImport.waveCount);

    BeginDialogFooter({messages::common::cImport, messages::common::cCancel});

    if (DialogButton(messages::common::cImport))
    {
        if (const char* error = CommitBankBundleImport())
        {
            PopupMgr::instance()->addPopup({error, nullptr});
            failed = true;
        }

        CloseBankBundleImport();
        ImGui::CloseCurrentPopup();
    }

    if (!failed && DialogButton(messages::common::cCancel))
    {
        CloseBankBundleImport();
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();

    return !failed;
}
