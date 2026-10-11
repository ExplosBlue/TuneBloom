#include <bfsar/archive/Bfsar.h>

#include <bfsar/io/FileIo.h>

#include <core/Debug.h>
#include <core/Json.h>

#include <ui/Messages.h>
#include <ui/app/Notifications.h>
#include <ui/app/PopupMgr.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

static constexpr s64 cMetadataVersion = 2;
static constexpr s64 cLegacyVersion   = 1;

static constexpr const char* cGuessPrefix           = "GUESS_";
static constexpr const char* cDefaultWaveName       = "Wave";
static constexpr const char* cDefaultSequenceName   = "Sequence";
static constexpr const char* cDefaultBankFileName   = "Bank";
static constexpr const char* cDefaultInstrumentName = "Instrument";

namespace
{

struct HashEntry
{
    std::string hash;
    std::string name;
    s64 index = -1;
    bool matched = false;
};

struct HashTarget
{
    Item* item = nullptr;
    std::string hash;
    bool used = false;
};

struct InstrumentEntry
{
    s64 program = 0;
    std::string name;
};

struct BankFileEntry
{
    s64 bank = -1;
    s64 bankFile = -1;
    std::string bankName;
    std::string name;
    std::vector<InstrumentEntry> instruments;
};

struct MetadataNames
{
    std::vector<BankFileEntry> bankFiles;
    std::vector<HashEntry> waveFiles;
    std::vector<HashEntry> sequenceFiles;
};

}

static void GetMetadataPath_(sead::BufferedSafeString* out, const sead::SafeString& archivePath)
{
    out->format("%s.metadata.json", archivePath.cstr());
}

static bool IsStoredName_(const Item& item, const char* defaultName)
{
    if (!item.isNameValid())
        return false;

    const sead::SafeString& name = item.getName();

    return name != sead::SafeString(defaultName) && !name.startsWith(cGuessPrefix);
}

static bool IsStoredWave_(const WaveFile& wave)
{
    return !wave.getIsFromStreamFile() && IsStoredName_(wave, cDefaultWaveName);
}

static bool HasStoredBankFileNames_(const BankFile& bankFile)
{
    if (IsStoredName_(bankFile, cDefaultBankFileName))
        return true;

    for (const Item* item : bankFile.getInstrumentList())
    {
        if (IsStoredName_(*item, cDefaultInstrumentName))
            return true;
    }

    return false;
}

static const BankFile* GetBankFile_(const Bank& bank)
{
    const Item* item = bank.getFileRef().getItem();

    if (!item || item->getItemType() != Item::ItemType::BankFile)
        return nullptr;

    return static_cast<const BankFile*>(item);
}

static bool ReadTextFile_(const char* path, std::string* out)
{
    FILE* f = fopen(path, "rb");

    if (!f)
        return false;

    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    out->clear();

    if (size > 0)
    {
        out->resize(static_cast<size_t>(size));
        out->resize(fread(out->data(), 1, out->size(), f));
    }

    fclose(f);

    return true;
}

static bool ParseInteger_(const std::string& text, s64* out)
{
    if (text.empty())
        return false;

    char* end = nullptr;
    const long long value = std::strtoll(text.c_str(), &end, 10);

    if (end != text.c_str() + text.size())
        return false;

    *out = value;

    return true;
}

static std::string NormalizeHash_(const std::string& hash)
{
    std::string out = hash;

    for (char& c : out)
        c = static_cast<char>(std::toupper(static_cast<u8>(c)));

    return out;
}

static void ReadHashEntries_(const json::Value* list, std::vector<HashEntry>* out)
{
    if (!list)
        return;

    for (const json::Value& value : list->items)
    {
        const std::string* hash = value.findString("hash");
        const std::string* name = value.findString("name");

        if (!hash || !name)
            continue;

        HashEntry& entry = out->emplace_back();
        entry.hash = NormalizeHash_(*hash);
        entry.name = *name;
        value.findInteger("index", &entry.index);
    }
}

static void ReadNames_(const json::Value& root, MetadataNames* out)
{
    if (const json::Value* bankFiles = root.findArray("bankFiles"))
    {
        for (const json::Value& value : bankFiles->items)
        {
            if (!value.isObject())
                continue;

            BankFileEntry& entry = out->bankFiles.emplace_back();
            value.findInteger("bank", &entry.bank);

            if (const std::string* bankName = value.findString("bankName"))
                entry.bankName = *bankName;

            if (const std::string* name = value.findString("name"))
                entry.name = *name;

            const json::Value* instruments = value.findArray("instruments");

            if (!instruments)
                continue;

            for (const json::Value& instrument : instruments->items)
            {
                const std::string* name = instrument.findString("name");
                s64 program;

                if (!name || !instrument.findInteger("program", &program))
                    continue;

                entry.instruments.push_back({ program, *name });
            }
        }
    }

    ReadHashEntries_(root.findArray("waveFiles"), &out->waveFiles);
    ReadHashEntries_(root.findArray("sequenceFiles"), &out->sequenceFiles);
}

static void ReadLegacyHashEntries_(const json::Value& object, std::vector<HashEntry>* out, std::unordered_set<std::string>* seen)
{
    for (const json::Member& member : object.members)
    {
        if (!member.value.isString())
            continue;

        const std::string hash = NormalizeHash_(member.key);

        if (!seen->insert(hash + ':' + member.value.string).second)
            continue;

        HashEntry& entry = out->emplace_back();
        entry.hash = hash;
        entry.name = member.value.string;
    }
}

static void ReadLegacyNames_(const json::Value& root, MetadataNames* out)
{
    if (const json::Value* banks = root.findObject("banks"))
    {
        for (const json::Member& member : banks->members)
        {
            const json::Value* instruments = member.value.findObject("instruments");
            s64 bankFile;

            if (!instruments || !ParseInteger_(member.key, &bankFile))
                continue;

            BankFileEntry& entry = out->bankFiles.emplace_back();
            entry.bankFile = bankFile;

            for (const json::Member& instrument : instruments->members)
            {
                s64 program;

                if (!instrument.value.isString() || !ParseInteger_(instrument.key, &program))
                    continue;

                entry.instruments.push_back({ program, instrument.value.string });
            }
        }
    }

    if (const json::Value* waveArchives = root.findObject("waveArchives"))
    {
        std::unordered_set<std::string> seen;

        for (const json::Member& member : waveArchives->members)
        {
            if (const json::Value* waves = member.value.findObject("waves"))
                ReadLegacyHashEntries_(*waves, &out->waveFiles, &seen);
        }
    }

    if (const json::Value* sequenceFiles = root.findObject("sequenceFiles"))
    {
        std::unordered_set<std::string> seen;
        ReadLegacyHashEntries_(*sequenceFiles, &out->sequenceFiles, &seen);
    }
}

template <typename ApplyName>
static u32 ApplyHashEntries_(std::vector<HashTarget>& targets, std::vector<HashEntry>& entries, ApplyName&& applyName)
{
    std::unordered_map<std::string, std::vector<u32>> targetsByHash;
    std::unordered_map<s64, u32> targetsById;

    for (u32 i = 0; i < targets.size(); i++)
    {
        targetsByHash[targets[i].hash].push_back(i);
        targetsById[targets[i].item->getId()] = i;
    }

    // Same hash and same index

    for (HashEntry& entry : entries)
    {
        const auto it = targetsById.find(entry.index);

        if (it == targetsById.end())
            continue;

        HashTarget& target = targets[it->second];

        if (target.used || target.hash != entry.hash)
            continue;

        target.used = true;
        entry.matched = true;
        applyName(target.item, entry.name);
    }

    u32 unmatched = 0;

    for (HashEntry& entry : entries)
    {
        if (entry.matched)
            continue;

        const auto it = targetsByHash.find(entry.hash);

        if (it != targetsByHash.end())
        {
            for (u32 index : it->second)
            {
                HashTarget& target = targets[index];

                if (target.used)
                    continue;

                target.used = true;
                entry.matched = true;
                applyName(target.item, entry.name);
                break;
            }
        }

        if (!entry.matched)
            unmatched++;
    }

    return unmatched;
}

static void AppendHashEntry_(std::vector<std::string>* lines, const std::string& hash, const Item& item)
{
    std::string line = "    { \"hash\": ";
    json::AppendString(&line, hash.c_str());
    line += ", \"index\": " + std::to_string(item.getId()) + ", \"name\": ";
    json::AppendString(&line, item.getName().cstr());
    line += " }";

    lines->push_back(std::move(line));
}

static void AppendSection_(std::string* out, const char* key, const std::vector<std::string>& lines, bool isLast)
{
    *out += "  \"";
    *out += key;
    *out += "\": [";

    for (size_t i = 0; i < lines.size(); i++)
    {
        *out += i == 0 ? "\n" : ",\n";
        *out += lines[i];
    }

    if (!lines.empty())
        *out += "\n  ";

    *out += isLast ? "]\n" : "],\n";
}

bool Bfsar::hasMetadataNames() const
{
    for (const Item* item : mWaveFileList)
    {
        if (IsStoredWave_(*static_cast<const WaveFile*>(item)))
            return true;
    }

    for (const Item* item : mSequenceFileList)
    {
        if (IsStoredName_(*item, cDefaultSequenceName))
            return true;
    }

    for (const Item* item : mBankList)
    {
        const BankFile* bankFile = GetBankFile_(*static_cast<const Bank*>(item));

        if (bankFile && HasStoredBankFileNames_(*bankFile))
            return true;
    }

    return false;
}

bool Bfsar::buildMetadata_(std::string* out)
{
    std::vector<std::string> bankLines;
    std::vector<std::string> waveLines;
    std::vector<std::string> sequenceLines;

    // Bank files

    std::unordered_set<const BankFile*> writtenBankFiles;

    for (const Item* item : mBankList)
    {
        const Bank* bank = static_cast<const Bank*>(item);
        const BankFile* bankFile = GetBankFile_(*bank);

        if (!bankFile || !HasStoredBankFileNames_(*bankFile) || !writtenBankFiles.insert(bankFile).second)
            continue;

        std::string line = "    {\n      \"bank\": " + std::to_string(bank->getId());

        if (bank->isNameValid())
        {
            line += ",\n      \"bankName\": ";
            json::AppendString(&line, bank->getName().cstr());
        }

        if (IsStoredName_(*bankFile, cDefaultBankFileName))
        {
            line += ",\n      \"name\": ";
            json::AppendString(&line, bankFile->getName().cstr());
        }

        bool firstInstrument = true;

        for (const Item* instrumentItem : bankFile->getInstrumentList())
        {
            const BankFile::Instrument* instrument = static_cast<const BankFile::Instrument*>(instrumentItem);

            if (!IsStoredName_(*instrument, cDefaultInstrumentName))
                continue;

            line += firstInstrument ? ",\n      \"instruments\": [\n" : ",\n";
            line += "        { \"program\": " + std::to_string(instrument->getProgramNo()) + ", \"name\": ";
            json::AppendString(&line, instrument->getName().cstr());
            line += " }";

            firstInstrument = false;
        }

        if (!firstInstrument)
            line += "\n      ]";

        line += "\n    }";

        bankLines.push_back(std::move(line));
    }

    // Wave files

    for (const Item* item : mWaveFileList)
    {
        const WaveFile* wave = static_cast<const WaveFile*>(item);

        if (IsStoredWave_(*wave))
            AppendHashEntry_(&waveLines, wave->getMd5Hash(), *wave);
    }

    // Sequence files

    for (Item* item : mSequenceFileList)
    {
        SequenceFile* sequence = static_cast<SequenceFile*>(item);

        if (IsStoredName_(*sequence, cDefaultSequenceName))
            AppendHashEntry_(&sequenceLines, sequence->getMd5Hash(), *sequence);
    }

    if (bankLines.empty() && waveLines.empty() && sequenceLines.empty())
        return false;

    *out = "{\n  \"version\": " + std::to_string(cMetadataVersion) + ",\n";

    AppendSection_(out, "bankFiles", bankLines, false);
    AppendSection_(out, "waveFiles", waveLines, false);
    AppendSection_(out, "sequenceFiles", sequenceLines, true);

    *out += "}\n";

    return true;
}

bool Bfsar::saveMetadata_(const sead::SafeString& archivePath, bool report)
{
    sead::FixedSafeString<1100> path;
    GetMetadataPath_(&path, archivePath);

    const bool isArchivePath = mFilePath && archivePath == *mFilePath;

    std::string text;

    if (!mSaveMetadata || !buildMetadata_(&text))
    {
        if (!isArchivePath || !mMetadataFileUnread)
            fileio::RemoveFile(path.cstr());

        if (isArchivePath)
            mMetadataUpgradePending = false;

        return true;
    }

    sead::FixedSafeString<1110> tmpPath;
    tmpPath.format("%s.tmp", path.cstr());

    bool written = fileio::WriteWholeFile(tmpPath.cstr(), text.data(), text.size()) == fileio::WriteResult::Ok;

    if (written && isArchivePath && mMetadataFileUnread)
    {
        sead::FixedSafeString<1110> copyPath;
        copyPath.format("%s.bak", path.cstr());

        written = fileio::RenameOver(path.cstr(), copyPath.cstr());
    }

    if (written)
        written = fileio::RenameOver(tmpPath.cstr(), path.cstr());

    if (written)
    {
        if (isArchivePath)
        {
            mMetadataUpgradePending = false;
            mMetadataFileUnread = false;
        }

        return true;
    }

    fileio::RemoveFile(tmpPath.cstr());

    LOG_FMT("Metadata file not written: %s", path.cstr());

    if (report)
        notify::Add(notify::Kind::Error, messages::archive::cMetadataWriteFailedTitle, messages::archive::cMetadataWriteFailedText);

    return false;
}

bool Bfsar::upgradeMetadata()
{
    if (!mOpen || !mFilePath)
        return false;

    return saveMetadata_(*mFilePath, false);
}

bool Bfsar::loadMetadata_(const sead::SafeString& archivePath)
{
    mMetadataUpgradePending = false;
    mMetadataFileUnread = false;

    sead::FixedSafeString<1100> path;
    GetMetadataPath_(&path, archivePath);

    std::string text;

    if (!ReadTextFile_(path.cstr(), &text))
    {
        std::error_code ec;
        mMetadataFileUnread = std::filesystem::exists(path.cstr(), ec);

        return false;
    }

    json::Value root;
    s64 version = cLegacyVersion;

    bool valid = json::Parse(text.data(), text.size(), &root) && root.isObject();

    if (valid && root.find("version"))
        valid = root.findInteger("version", &version) && version >= cLegacyVersion && version <= cMetadataVersion;

    if (!valid)
    {
        sead::FixedSafeString<1110> copyPath;
        copyPath.format("%s.bak", path.cstr());

        std::error_code ec;
        std::filesystem::copy_file(path.cstr(), copyPath.cstr(), std::filesystem::copy_options::overwrite_existing, ec);

        LOG_FMT("Metadata file not valid: %s", path.cstr());

        sead::FormatFixedSafeString<1024> msg(messages::archive::cMetadataUnreadableFormat, path.cstr());
        PopupMgr::instance()->addPopup({ msg });

        return true;
    }

    MetadataNames names;

    if (version == cLegacyVersion)
        ReadLegacyNames_(root, &names);
    else
        ReadNames_(root, &names);

    u32 applied = 0;
    u32 skipped = 0;

    auto applyName = [&](Item* item, const std::string& name)
    {
        if (name.starts_with(cGuessPrefix))
            return;

        if (name.empty() || name.size() >= 256)
        {
            skipped++;
            return;
        }

        item->getName() = name.c_str();
        item->setEnableName(true);
        applied++;
    };

    // Bank files

    for (const BankFileEntry& entry : names.bankFiles)
    {
        BankFile* bankFile = nullptr;

        if (entry.bankFile >= 0)
        {
            for (Item* item : mBankFileList)
            {
                if (static_cast<s64>(item->getId()) == entry.bankFile)
                    bankFile = static_cast<BankFile*>(item);
            }
        }
        else
        {
            const Bank* bankById = nullptr;
            const Bank* bankByName = nullptr;

            for (const Item* item : mBankList)
            {
                const Bank* bank = static_cast<const Bank*>(item);

                if (static_cast<s64>(bank->getId()) == entry.bank)
                    bankById = bank;

                if (!bankByName && !entry.bankName.empty() && bank->isNameValid() && bank->getName() == sead::SafeString(entry.bankName.c_str()))
                    bankByName = bank;
            }

            const Bank* bank = bankByName ? bankByName : bankById;

            if (bank)
                bankFile = const_cast<BankFile*>(GetBankFile_(*bank));
        }

        if (!bankFile)
        {
            skipped += static_cast<u32>(entry.instruments.size()) + (entry.name.empty() ? 0 : 1);
            continue;
        }

        if (!entry.name.empty())
            applyName(bankFile, entry.name);

        for (const InstrumentEntry& instrumentEntry : entry.instruments)
        {
            Item* instrument = nullptr;

            for (Item* item : bankFile->getInstrumentList())
            {
                if (static_cast<BankFile::Instrument*>(item)->getProgramNo() == instrumentEntry.program)
                {
                    instrument = item;
                    break;
                }
            }

            if (instrument)
                applyName(instrument, instrumentEntry.name);
            else
                skipped++;
        }
    }

    // Wave files

    std::vector<HashTarget> targets;

    for (Item* item : mWaveFileList)
    {
        const WaveFile* wave = static_cast<const WaveFile*>(item);

        if (!wave->getIsFromStreamFile())
            targets.push_back({ item, wave->getMd5Hash() });
    }

    skipped += ApplyHashEntries_(targets, names.waveFiles, applyName);

    // Sequence files

    targets.clear();

    for (Item* item : mSequenceFileList)
        targets.push_back({ item, static_cast<SequenceFile*>(item)->getMd5Hash() });

    skipped += ApplyHashEntries_(targets, names.sequenceFiles, applyName);

    LOG_FMT("Metadata version %d: %u names applied, %u skipped", static_cast<s32>(version), applied, skipped);

    if (skipped > 0 && version != cLegacyVersion)
    {
        sead::FixedSafeString<1110> copyPath;
        copyPath.format("%s.bak", path.cstr());

        std::error_code ec;
        std::filesystem::copy_file(path.cstr(), copyPath.cstr(), std::filesystem::copy_options::overwrite_existing, ec);
    }

    if (version == cLegacyVersion)
    {
        sead::FixedSafeString<1110> copyPath;
        copyPath.format("%s.v1.bak", path.cstr());

        std::error_code ec;
        std::filesystem::copy_file(path.cstr(), copyPath.cstr(), std::filesystem::copy_options::skip_existing, ec);

        mMetadataUpgradePending = true;
        mMetadataUpgradeLoaded = applied;
        mMetadataUpgradeMissing = skipped;
    }

    return true;
}
