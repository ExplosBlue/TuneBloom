#include <bfsar/archive/Bfsar.h>

#include <core/Debug.h>

#include <bfsar/archive/BfsarOpenContext.h>

#include <ui/file/LoadProgress.h>
#include <ui/Messages.h>
#include <ui/app/PopupMgr.h>

#include <snd/snd_BankFileReader.h>
#include <snd/snd_WaveArchiveFileReader.h>
#include <snd/snd_WaveSoundFileReader.h>

#include <libs/md5.h>

#include <core/ParallelFor.h>

#include <algorithm>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using nw::snd::internal::SoundArchiveFile;
using nw::snd::internal::Util;

namespace
{

struct WaveHashJob
{
    const void* fileData;
    u32 fileDataSize;
    std::string* hash;
};

struct BankFileEntry
{
    u32 id;
    ByteRange file;
};

struct WaveIdVariation
{
    u32 warcId = nw::snd::SoundArchive::INVALID_ID;
    u32 warcIdx = nw::snd::SoundArchive::INVALID_ID;
    std::vector<u32> globalWaveIds;
};

using WaveIdTableFinder = const Util::WaveIdTable* (*)(const ByteRange& file);

}

template <typename File>
static void ReadInnerFileItem(File* item, const ByteRange& file, bool isLayoutValid)
{
    PopupMgr::instance()->setCurrentProcessItem(item);

    if (!isLayoutValid || !item->read(file.data()))
        PopupMgr::instance()->pushCurrentItemError(messages::archive::cInnerFileUnreadable);
}

void Bfsar::readSequenceFiles_(BfsarOpenContext& ctx)
{
    loadprogress::BeginStage(loadprogress::Stage::SequenceFiles);

    for (u32 i = 0; i < ctx.soundArchive.detail_GetFileCount(); i++)
    {
        loadprogress::Step();

        const ByteRange seqFile = FindArchiveFile(ctx, i);

        if (!IsInnerFileKind(seqFile, InnerFileKind::Sequence))
            continue;

        const u32 globalId = mSequenceFileList.size();

        SequenceFile* sequence = new(ctx.heap) SequenceFile();
        sequence->mId = globalId;

        sequence->mEnableName = true;
        sequence->mName = "Sequence";

        sequence->setFormat(mFormat);

        ReadInnerFileItem(sequence, seqFile, CheckInnerFileBlocks(seqFile));

        sequence->setSerializedHash(md5(seqFile.data(), static_cast<u32>(seqFile.size())));
        mSequenceFileList.pushBack(sequence);
        ctx.seqFileIdxMap.try_emplace(i, globalId);
    }

    LOG_FMT("SequenceFiles: %d", mSequenceFileList.size());
}

static bool OpenWaveArchiveFile(const BfsarOpenContext& ctx, u32 fileId, nw::snd::internal::WaveArchiveFileReader* reader)
{
    const ByteRange file = FindArchiveFile(ctx, fileId);

    if (IsInnerFileKind(file, InnerFileKind::WaveArchive) && !CheckWaveArchiveLayout(file))
    {
        PopupMgr::instance()->pushCurrentItemError(messages::archive::cWaveArchiveFileDamaged);
        return false;
    }

    reader->Initialize(file.size() >= cInnerFileMagicLength ? file.data() : nullptr);

    return reader->IsAvailable();
}

void Bfsar::hashWaveArchiveFiles_(BfsarOpenContext& ctx)
{
    const u32 warcNum = ctx.soundArchive.GetWaveArchiveCount();

    LOG_U32("warcNum", warcNum);
    LOG_U32("fileCount", ctx.soundArchive.detail_GetFileCount());
    LOG_U32("bankCount", ctx.soundArchive.GetBankCount());
    LOG_U32("soundCount", ctx.soundArchive.GetSoundCount());
    LOG_U32("soundGroupCount", ctx.soundArchive.GetSoundGroupCount());

    ctx.warcFileCache.resize(warcNum);
    ctx.waveIdMapSet.resize(warcNum);
    ctx.warcWaveHashes.resize(warcNum);

    loadprogress::BeginStage(loadprogress::Stage::WaveFiles);

    PopupMgr::instance()->setCurrentProcessItem(nullptr);

    std::vector<WaveHashJob> jobs;

    for (u32 i = 0; i < warcNum; i++)
    {
        const SoundArchiveFile::WaveArchiveInfo* warcInfo = ctx.soundArchive.GetWaveArchiveInfo(ctx.soundArchive.GetWaveArchiveIdFromIndex(i));

        if (!warcInfo)
            continue;

        nw::snd::internal::WaveArchiveFileReader reader;

        if (!OpenWaveArchiveFile(ctx, warcInfo->fileId, &reader))
            continue;

        const u32 waveFileCount = reader.GetWaveFileCount();
        ctx.warcWaveHashes[i].resize(waveFileCount);

        for (u32 j = 0; j < waveFileCount; j++)
            jobs.push_back({ reader.GetWaveFile(j), reader.GetWaveFileSize(j), &ctx.warcWaveHashes[i][j] });
    }

    ParallelFor(static_cast<u32>(jobs.size()), [&jobs](u32 index)
    {
        const WaveHashJob& job = jobs[index];
        *job.hash = md5(job.fileData, job.fileDataSize);
    });
}

Item* Bfsar::addGenWaveArchive_(BfsarOpenContext& ctx, u32 index, const SoundArchiveFile::WaveArchiveInfo& warcInfo)
{
    Item* warc = new(ctx.heap) Item();
    warc->mId = index;
    warc->mItemType = Item::ItemType::WaveArchive;

    PopupMgr::instance()->setCurrentProcessItem(warc);
    mGenWaveArchiveList.pushBack(warc);

    warc->mEnableName = true;

    const bool isGenerated = warcInfo.optionParameter.GetTrueCount(nw::snd::internal::WAVE_ARCHIVE_INFO_STRING_ID) == 0;
    readItemName_(ctx, warc, warcInfo.GetStringId(), isGenerated ? "@AutoGen_WARC_%u" : "WARC_%u");

    if (warcInfo.isLoadIndividual && warcInfo.optionParameter.GetTrueCount(nw::snd::internal::WAVE_ARCHIVE_INFO_WAVE_COUNT) == 0)
        PopupMgr::instance()->pushCurrentItemError(messages::archive::cWaveArchiveNoWaveCount);

    return warc;
}

static const WaveCacheEntry* FindSameWaveInEarlierArchives(const BfsarOpenContext& ctx, u32 warcIndex, const std::string& hash, const void* waveFile, u32 waveFileSize)
{
    for (u32 otherIndex = 0; otherIndex < warcIndex; otherIndex++)
    {
        for (const WaveCacheEntry& other : ctx.warcFileCache[otherIndex])
        {
            if (ctx.waveIdMapSet[warcIndex].contains(other.id))
                continue;

            if (other.hash == hash && other.fileDataSize == waveFileSize && sead::MemUtil::compare(other.fileData, waveFile, waveFileSize) == 0)
                return &other;
        }
    }

    return nullptr;
}

void Bfsar::readWaveArchiveWaves_(BfsarOpenContext& ctx, u32 warcIndex, u32 waveArchiveId, const nw::snd::internal::WaveArchiveFileReader& reader)
{
    const std::vector<std::string>& hashes = ctx.warcWaveHashes[warcIndex];
    const u32 waveFileCount = reader.GetWaveFileCount();

    SEAD_ASSERT(hashes.size() == waveFileCount);

    for (u32 i = 0; i < waveFileCount; i++)
    {
        const void* waveFile = reader.GetWaveFile(i);
        const u32 waveFileSize = reader.GetWaveFileSize(i);
        const std::string& hash = hashes[i];

        if (const WaveCacheEntry* sameWave = FindSameWaveInEarlierArchives(ctx, warcIndex, hash, waveFile, waveFileSize))
        {
            const WaveCacheEntry shared = *sameWave;

            ctx.warcFileCache[warcIndex].push_back(shared);
            ctx.waveIdMapSet[warcIndex].insert(shared.id);
            continue;
        }

        const u32 globalId = mWaveFileList.size();
        const ByteRange waveBytes = FindLoadedBytes(ctx, waveFile, waveFileSize);

        WaveFile* wave = new(ctx.heap) WaveFile();
        wave->mId = globalId;

        wave->mEnableName = true;
        wave->mName = "Wave";

        ReadInnerFileItem(wave, waveBytes, CheckInnerFileBlocks(waveBytes));

        mWaveFileList.pushBack(wave);
        wave->setSerializedHash(hash);
        wave->mWaveArchiveId = waveArchiveId;
        ctx.warcFileCache[warcIndex].emplace_back(hash, globalId, waveFile, waveFileSize);
        ctx.waveIdMapSet[warcIndex].insert(globalId);
    }
}

bool Bfsar::readWaveArchiveFiles_(BfsarOpenContext& ctx)
{
    const u32 warcNum = ctx.soundArchive.GetWaveArchiveCount();

    for (u32 i = 0; i < warcNum; i++)
    {
        loadprogress::Step();

        const u32 waveArchiveId = ctx.soundArchive.GetWaveArchiveIdFromIndex(i);
        const SoundArchiveFile::WaveArchiveInfo* warcInfo = ctx.soundArchive.GetWaveArchiveInfo(waveArchiveId);

        if (!warcInfo)
        {
            PopupMgr::instance()->setCorruptInfo(messages::archive::cInvalidWaveArchiveInfo);
            return false;
        }

        addGenWaveArchive_(ctx, i, *warcInfo);

        nw::snd::internal::WaveArchiveFileReader reader;

        if (OpenWaveArchiveFile(ctx, warcInfo->fileId, &reader))
            readWaveArchiveWaves_(ctx, i, waveArchiveId, reader);
    }

    return true;
}

bool Bfsar::readWaveArchives_(BfsarOpenContext& ctx)
{
    const u32 warcNum = ctx.soundArchive.GetWaveArchiveCount();

    for (u32 i = 0; i < warcNum; i++)
    {
        const SoundArchiveFile::WaveArchiveInfo* warcInfo = ctx.soundArchive.GetWaveArchiveInfo(ctx.soundArchive.GetWaveArchiveIdFromIndex(i));

        if (!warcInfo)
        {
            PopupMgr::instance()->setCorruptInfo(messages::archive::cInvalidWaveArchiveInfo);
            return false;
        }

        if (warcInfo->optionParameter.GetTrueCount(nw::snd::internal::WAVE_ARCHIVE_INFO_STRING_ID) == 0)
            continue;

        WaveArchive* warc = new(ctx.heap) WaveArchive();
        warc->mId = i;
        PopupMgr::instance()->setCurrentProcessItem(warc);

        warc->mEnableName = true;
        readItemName_(ctx, warc, warcInfo->GetStringId(), "WARC_%u");

        warc->mIsLoadIndividual = warcInfo->isLoadIndividual;

        mWaveArchiveList.pushBack(warc);
    }

    LOG_FMT("WaveFiles: %d, WaveArchives: %d, GenWaveArchives: %d", mWaveFileList.size(), mWaveArchiveList.size(), mGenWaveArchiveList.size());

    return true;
}

void Bfsar::readFileGroupTables_(BfsarOpenContext& ctx)
{
    const u32 fileCount = ctx.soundArchive.detail_GetFileCount();

    mFileAttachedGroups.assign(fileCount, {});
    mFileOriginalIncludeInBfsar.assign(fileCount, false);
    mFileOriginalHasData.assign(fileCount, false);
    mStreamFileOrder = StreamFileOrder::ByVersion;

    bool hasInnerFile = false;

    for (u32 i = 0; i < fileCount; i++)
    {
        const SoundArchiveFile::FileInfo* fileInfo = ctx.soundArchive.detail_GetFileInfo(i);

        if (!fileInfo)
            continue;

        if (fileInfo->GetFileLocationType() == SoundArchiveFile::FILE_LOCATION_TYPE_EXTERNAL)
        {
            if (mStreamFileOrder == StreamFileOrder::ByVersion)
                mStreamFileOrder = hasInnerFile ? StreamFileOrder::Last : StreamFileOrder::First;

            continue;
        }

        hasInnerFile = true;

        const SoundArchiveFile::InternalFileInfo* internalInfo = fileInfo->GetInternalFileInfo();

        if (!internalInfo)
            continue;

        mFileOriginalIncludeInBfsar[i] = true;
        mFileOriginalHasData[i] = internalInfo->GetOffsetFromFileBlockHead() != SoundArchiveFile::InternalFileInfo::INVALID_OFFSET;

        if (internalInfo->toAttachedGroupIdTable.typeId != nw::snd::internal::ElementType_Category_Tables ||
            !internalInfo->toAttachedGroupIdTable.IsValidOffset())
            continue;

        const Util::Table<nw::ut::ResU32>* groupTable = internalInfo->GetAttachedGroupTable();

        std::vector<u32>& ids = mFileAttachedGroups[i];
        ids.reserve(groupTable->count);

        for (u32 j = 0; j < groupTable->count; j++)
            ids.push_back(groupTable->item[j]);
    }

    LOG_FMT("Preserved attached group tables for %zu files, originalIncludeInBfsar for %zu files",
        mFileAttachedGroups.size(), std::count(mFileOriginalIncludeInBfsar.begin(), mFileOriginalIncludeInBfsar.end(), true));
}

static const Util::WaveIdTable* FindReadableBankWaveIdTable(const ByteRange& file)
{
    const Util::WaveIdTable* waveIdTable = FindBankWaveIdTable(file);

    if (!waveIdTable)
        return nullptr;

    nw::snd::internal::BankFileReader reader(file.data());

    return reader.IsInitialized() ? waveIdTable : nullptr;
}

static const Util::WaveIdTable* FindReadableWaveSoundWaveIdTable(const ByteRange& file)
{
    const Util::WaveIdTable* waveIdTable = FindWaveSoundWaveIdTable(file);

    if (!waveIdTable)
        return nullptr;

    nw::snd::internal::WaveSoundFileReader reader(file.data());

    return reader.IsAvailable() ? waveIdTable : nullptr;
}

static bool ReadWaveIdVariation(const BfsarOpenContext& ctx, const Util::WaveIdTable& waveIdTable, WaveIdVariation* out)
{
    const u32 waveCount = waveIdTable.GetCount();
    const Util::WaveId* waveIds = waveIdTable.table.item;

    for (u32 i = 1; i < waveCount; i++)
    {
        if (waveIds[i].waveArchiveId != waveIds[0].waveArchiveId)
            return false;
    }

    if (waveCount > 0)
    {
        out->warcId = waveIds[0].waveArchiveId;

        if (Util::GetItemType(out->warcId) != nw::snd::internal::ItemType_WaveArchive)
            out->warcId = Util::GetMaskedItemId(out->warcId, nw::snd::internal::ItemType_WaveArchive);

        out->warcIdx = Util::GetItemIndex(out->warcId);
    }

    out->globalWaveIds.assign(waveCount, 0);

    if (out->warcIdx >= ctx.warcFileCache.size())
        return true;

    const std::vector<WaveCacheEntry>& warcWaves = ctx.warcFileCache[out->warcIdx];

    for (u32 i = 0; i < waveCount; i++)
    {
        const u32 waveIndex = waveIds[i].waveIndex;

        if (waveIndex < warcWaves.size())
            out->globalWaveIds[i] = warcWaves[waveIndex].id;
    }

    return true;
}

static bool IsWaveIdOfOtherVariation(const BfsarOpenContext& ctx, const std::vector<WaveIdVariation>& variations, size_t variationIndex, u32 globalWaveId)
{
    for (size_t i = 0; i < variations.size(); i++)
    {
        if (i == variationIndex)
            continue;

        const u32 warcIdx = variations[i].warcIdx;

        if (warcIdx < ctx.waveIdMapSet.size() && ctx.waveIdMapSet[warcIdx].contains(globalWaveId))
            return true;
    }

    return false;
}

static std::vector<u32> PickBaseWaveIds(const BfsarOpenContext& ctx, const std::vector<WaveIdVariation>& variations)
{
    std::vector<u32> baseWaveIds = variations[0].globalWaveIds;

    for (size_t i = 0; i < baseWaveIds.size(); i++)
    {
        for (size_t j = 0; j < variations.size(); j++)
        {
            const u32 trialId = variations[j].globalWaveIds[i];

            if (IsWaveIdOfOtherVariation(ctx, variations, j, trialId))
                continue;

            baseWaveIds[i] = trialId;
            break;
        }
    }

    return baseWaveIds;
}

static void PatchFileWaveIds(BfsarOpenContext& ctx, u32 fileId, WaveIdTableFinder findWaveIdTable, std::unordered_map<u64, u32>& fileWarcIds)
{
    std::vector<WaveIdVariation> variations;
    std::unordered_map<const void*, u32> readFiles;
    const Util::WaveIdTable* baseWaveIdTable = nullptr;

    for (u32 variationId = 0; ; variationId++)
    {
        u32 groupId = nw::snd::SoundArchive::INVALID_ID;
        const ByteRange file = FindArchiveFile(ctx, fileId, variationId, &groupId);

        if (!file.data())
            break;

        const u64 fileKey = MakeGroupFileKey(fileId, groupId);

        if (const auto it = readFiles.find(file.data()); it != readFiles.end())
        {
            fileWarcIds[fileKey] = it->second;
            continue;
        }

        const Util::WaveIdTable* waveIdTable = findWaveIdTable(file);
        WaveIdVariation variation;

        const bool isReadable = waveIdTable && ReadWaveIdVariation(ctx, *waveIdTable, &variation) &&
                                (variations.empty() || variation.globalWaveIds.size() == variations[0].globalWaveIds.size());

        if (!isReadable && variationId == 0)
            return;

        if (!isReadable)
            continue;

        if (variationId == 0)
            baseWaveIdTable = waveIdTable;

        fileWarcIds[fileKey] = variation.warcId;
        readFiles[file.data()] = variation.warcId;
        variations.push_back(std::move(variation));
    }

    if (!baseWaveIdTable)
        return;

    const std::vector<u32> baseWaveIds = PickBaseWaveIds(ctx, variations);
    Util::WaveId* waveIds = const_cast<Util::WaveId*>(baseWaveIdTable->table.item);

    for (size_t i = 0; i < baseWaveIds.size(); i++)
    {
        waveIds[i].waveArchiveId = 0;
        waveIds[i].waveIndex = baseWaveIds[i];
    }
}

bool Bfsar::patchBankFileWaveIds_(BfsarOpenContext& ctx)
{
    PopupMgr::instance()->setCurrentProcessItem(nullptr);

    for (u32 i = 0; i < ctx.soundArchive.GetBankCount(); i++)
    {
        const SoundArchiveFile::BankInfo* bankInfo = ctx.soundArchive.GetBankInfo(ctx.soundArchive.GetBankIdFromIndex(i));

        if (!bankInfo)
        {
            PopupMgr::instance()->setCorruptInfo(messages::archive::cInvalidBankInfo);
            return false;
        }

        PatchFileWaveIds(ctx, bankInfo->fileId, FindReadableBankWaveIdTable, ctx.bankFileWarcId);
    }

    return true;
}

bool Bfsar::patchWaveSoundFileWaveIds_(BfsarOpenContext& ctx)
{
    PopupMgr::instance()->setCurrentProcessItem(nullptr);

    for (u32 i = 0; i < ctx.soundArchive.GetSoundGroupCount(); i++)
    {
        const SoundArchiveFile::SoundGroupInfo* soundSetInfo = ctx.soundArchive.detail_GetSoundGroupInfo(ctx.soundArchive.GetSoundGroupIdFromIndex(i));

        if (!soundSetInfo)
        {
            PopupMgr::instance()->setCorruptInfo(messages::archive::cInvalidSoundGroupInfo);
            return false;
        }

        if (!soundSetInfo->GetWaveSoundGroupInfo())
            continue;

        const Util::Table<nw::ut::ResU32>* fileIdTable = soundSetInfo->GetFileIdTable();

        if (fileIdTable->count == 0)
            continue;

        PatchFileWaveIds(ctx, fileIdTable->item[0], FindReadableWaveSoundWaveIdTable, ctx.wsdFileWarcId);
    }

    return true;
}

static const BankFileEntry* FindSameBankFile(const std::unordered_multimap<std::string, BankFileEntry>& readBankFiles, const std::string& hash, const ByteRange& bankFile)
{
    const auto [first, last] = readBankFiles.equal_range(hash);

    for (auto it = first; it != last; ++it)
    {
        const ByteRange& other = it->second.file;

        if (other.size() == bankFile.size() && sead::MemUtil::compare(other.data(), bankFile.data(), bankFile.size()) == 0)
            return &it->second;
    }

    return nullptr;
}

void Bfsar::readBankFiles_(BfsarOpenContext& ctx)
{
    std::unordered_set<u32> bankFileIndices;

    for (u32 i = 0; i < ctx.soundArchive.GetBankCount(); i++)
    {
        const SoundArchiveFile::BankInfo* bankInfo = ctx.soundArchive.GetBankInfo(ctx.soundArchive.GetBankIdFromIndex(i));

        if (bankInfo)
            bankFileIndices.insert(Util::GetItemIndex(bankInfo->fileId));
    }

    LOG_FMT("ReferencedFileIndices: %zu", bankFileIndices.size());

    std::unordered_multimap<std::string, BankFileEntry> readBankFiles;

    loadprogress::BeginStage(loadprogress::Stage::BankFiles);

    for (u32 i = 0; i < ctx.soundArchive.detail_GetFileCount(); i++)
    {
        loadprogress::Step();

        if (!bankFileIndices.contains(i))
            continue;

        const ByteRange bankFile = FindArchiveFile(ctx, i);

        if (!IsInnerFileKind(bankFile, InnerFileKind::Bank))
            continue;

        const std::string hash = md5(bankFile.data(), static_cast<u32>(bankFile.size()));

        if (const BankFileEntry* sameBankFile = FindSameBankFile(readBankFiles, hash, bankFile))
        {
            ctx.bankFileIdxMap.try_emplace(i, sameBankFile->id);
            continue;
        }

        const u32 globalId = mBankFileList.size();

        BankFile* bank = new(ctx.heap) BankFile();
        bank->mId = globalId;

        bank->mEnableName = true;
        bank->mName = "Bank";

        ReadInnerFileItem(bank, bankFile, FindBankWaveIdTable(bankFile) != nullptr);

        mBankFileList.pushBack(bank);

        readBankFiles.emplace(hash, BankFileEntry{ globalId, bankFile });
        ctx.bankFileIdxMap.try_emplace(i, globalId);
    }
}
