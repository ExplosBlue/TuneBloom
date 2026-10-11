#include <bfsar/archive/Bfsar.h>

#include <core/Debug.h>

#include <bfsar/archive/BfsarOpenContext.h>
#include <bfsar/archive/GroupFilePaths.h>

#include <ui/file/LoadProgress.h>
#include <ui/Messages.h>
#include <ui/app/PopupMgr.h>

#include <filedevice/seadFileDeviceMgr.h>
#include <filedevice/seadPath.h>

#include <snd/snd_GroupFileReader.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#if defined(SEAD_PLATFORM_WINDOWS)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

using nw::snd::internal::GroupFile;
using nw::snd::internal::GroupFileReader;
using nw::snd::internal::SoundArchiveFile;
using nw::snd::internal::Util;

namespace
{

constexpr u32 cPathBufferSize       = 512;
constexpr u32 cGroupSearchEntryMax  = 20000;
constexpr s32 cGroupSearchDepthMax  = 8;
constexpr u64 cGroupOrderStepMax    = 100000;
constexpr const char* cPathBadChars = "\\/:*?\"<>|";

}

static bool IsPlainFileName(const sead::SafeString& name)
{
    if (name.isEmpty())
        return false;

    for (const char* c = name.cstr(); *c != '\0'; c++)
    {
        if (*c < 0x20 || *c > 0x7E || std::strchr(cPathBadChars, *c))
            return false;
    }

    return true;
}

static bool GetNarrowPath(const std::filesystem::path& path, sead::BufferedSafeString* outPath)
{
    char buffer[cPathBufferSize];

#if defined(SEAD_PLATFORM_WINDOWS)
    BOOL usedDefaultChar = FALSE;
    const int length = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, path.c_str(), -1, buffer, cPathBufferSize, nullptr, &usedDefaultChar);

    if (length <= 0 || usedDefaultChar)
        return false;
#else
    if (path.native().size() >= cPathBufferSize)
        return false;

    std::snprintf(buffer, cPathBufferSize, "%s", path.c_str());
#endif

    outPath->copy(buffer);

    return true;
}

static ByteRange LoadExtDataGroupFile(const sead::SafeString& archiveDir, const sead::SafeString& groupName, std::string* outPath)
{
    for (ArchiveFormat format : { ArchiveFormat::BCSAR, ArchiveFormat::BFSAR })
    {
        const char* extension = GetInnerFileExtension(format, InnerFileKind::Group);
        const s32 length = std::snprintf(nullptr, 0, "%s/extData/%s.%s", archiveDir.cstr(), groupName.cstr(), extension);

        if (length < 0 || length >= static_cast<s32>(cPathBufferSize))
            continue;

        const sead::FormatFixedSafeString<cPathBufferSize> path("%s/extData/%s.%s", archiveDir.cstr(), groupName.cstr(), extension);
        const ByteRange file = LoadFileBytes(path);

        if (file.data())
        {
            *outPath = path.cstr();
            return file;
        }
    }

    return ByteRange();
}

static bool IsGroupFileOfName(const std::filesystem::path& path, const std::filesystem::path& groupName)
{
    if (path.stem() != groupName)
        return false;

    for (ArchiveFormat format : { ArchiveFormat::BCSAR, ArchiveFormat::BFSAR })
    {
        if (path.extension() == std::string(".") + GetInnerFileExtension(format, InnerFileKind::Group))
            return true;
    }

    return false;
}

static ByteRange FindGroupFileInFolder(const sead::SafeString& folder, const sead::SafeString& groupName, std::string* outPath)
{
    namespace fs = std::filesystem;

    const fs::path wantedName(groupName.cstr());
    std::error_code error;
    u32 entryCount = 0;

    for (fs::recursive_directory_iterator it(fs::path(folder.cstr()), fs::directory_options::skip_permission_denied, error), end; !error && it != end; it.increment(error))
    {
        if (++entryCount > cGroupSearchEntryMax)
            break;

        loadprogress::Update();

        if (it.depth() >= cGroupSearchDepthMax)
            it.disable_recursion_pending();

        std::error_code entryError;

        if (!it->is_regular_file(entryError) || entryError || !IsGroupFileOfName(it->path(), wantedName))
            continue;

        sead::FixedSafeString<cPathBufferSize> filePath;

        if (!GetNarrowPath(it->path(), &filePath))
            continue;

        const ByteRange file = LoadFileBytes(filePath);

        if (file.data())
        {
            fprintf(stdout, "Found external group: %s\n", filePath.cstr());
            *outPath = filePath.cstr();
            return file;
        }
    }

    return ByteRange();
}

static ByteRange FindExternalGroupFile(const Group& group, const sead::SafeString& archivePath, std::string* outPath)
{
    sead::FixedSafeString<cPathBufferSize> archiveDir;

    if (!sead::Path::getDirectoryName(&archiveDir, archivePath) || !IsPlainFileName(group.getName()))
        return ByteRange();

    const ByteRange file = LoadExtDataGroupFile(archiveDir, group.getName(), outPath);

    return file.data() ? file : FindGroupFileInFolder(archiveDir, group.getName(), outPath);
}

static ByteRange FindGroupFile(const BfsarOpenContext& ctx, u32 fileId)
{
    const u32 fileCount = ctx.soundArchive.detail_GetFileCount();

    if (fileId >= fileCount)
    {
        const u32 externalIndex = fileId - fileCount;

        return externalIndex < ctx.externalGroupFiles.size() ? ctx.externalGroupFiles[externalIndex] : ByteRange();
    }

    const SoundArchiveFile::InternalFileInfo* internalInfo = ctx.soundArchive.detail_GetFileInfo(fileId)->GetInternalFileInfo();
    const void* file = ctx.soundArchive.detail_GetFileAddressSimple(fileId);

    if (!internalInfo || !file)
        return ByteRange();

    return FindLoadedBytes(ctx, file, internalInfo->GetFileSize());
}

static bool IsGroupFileId(const nw::snd::MemorySoundArchive& soundArchive, u32 fileId)
{
    for (u32 i = 0; i < soundArchive.GetGroupCount(); i++)
    {
        const SoundArchiveFile::GroupInfo* groupInfo = soundArchive.GetGroupInfo(soundArchive.GetGroupIdFromIndex(i));

        if (groupInfo && groupInfo->fileId == fileId)
            return true;
    }

    return false;
}

static void HideArchiveFilesOfGroup(const nw::snd::MemorySoundArchive& soundArchive, const GroupFileReader& reader)
{
    for (u32 i = 0; i < reader.GetGroupItemCount(); i++)
    {
        const u32 fileId = reader.GetGroupItemInfo(i)->fileId;
        const SoundArchiveFile::FileInfo* fileInfo = soundArchive.detail_GetFileInfo(fileId);

        if (!fileInfo || IsGroupFileId(soundArchive, fileId))
            continue;

        SoundArchiveFile::InternalFileInfo* internalInfo = const_cast<SoundArchiveFile::InternalFileInfo*>(fileInfo->GetInternalFileInfo());

        if (internalInfo)
            internalInfo->toFileImageFromFileBlockBody.offset = static_cast<s32>(SoundArchiveFile::InternalFileInfo::INVALID_OFFSET);
    }
}

void Bfsar::attachExternalGroupFile_(BfsarOpenContext& ctx, Group* group, SoundArchiveFile::GroupInfo* groupInfo, const ByteRange& file, const std::string& path)
{
    group->mOutputType = Group::OutputType::External;
    group->mExternalFilePath = path;
    group->mExternalFileState = Group::ExternalFileState::Loaded;

    groupInfo->fileId = ctx.soundArchive.detail_GetFileCount() + static_cast<u32>(ctx.soundArchive.mExternalGroups.size());

    ctx.soundArchive.mExternalGroups.push_back(file.data());
    ctx.externalGroupFiles.push_back(file);
    mExternalGroupBuffers.push_back(const_cast<u8*>(static_cast<const u8*>(file.data())));

    if (!(ctx.groupFileSource == getFilePath()))
        return;

    mLoadedGroupFilePaths.push_back(path);
    rememberGroupFilePath_(group);
}

bool Bfsar::loadExtDataGroupFile_(BfsarOpenContext& ctx, Group* group, SoundArchiveFile::GroupInfo* groupInfo)
{
    std::string path;
    const ByteRange file = loadExternalGroupFile_(ctx, *group, false, &path);

    if (!file.data())
        return false;

    HideArchiveFilesOfGroup(ctx.soundArchive, GroupFileReader(file.data()));
    attachExternalGroupFile_(ctx, group, groupInfo, file, path);

    return true;
}

static Bfsar::GroupFileCheck CheckGroupFile(const ByteRange& file, u32 archiveFileCount)
{
    if (!file.data())
        return Bfsar::GroupFileCheck::Unreadable;

    if (!IsInnerFileKind(file, InnerFileKind::Group) || !CheckGroupFileLayout(file))
        return Bfsar::GroupFileCheck::NotGroupFile;

    const GroupFileReader reader(file.data());

    if (!reader.IsInitialized())
        return Bfsar::GroupFileCheck::NotGroupFile;

    for (u32 i = 0; i < reader.GetGroupItemCount(); i++)
    {
        if (reader.GetGroupItemInfo(i)->fileId >= archiveFileCount)
            return Bfsar::GroupFileCheck::OtherArchive;
    }

    return Bfsar::GroupFileCheck::Ok;
}

Bfsar::GroupFileCheck Bfsar::checkGroupFile(const sead::SafeString& path) const
{
    const LoadedFile file(path);

    return CheckGroupFile(file.bytes, static_cast<u32>(mFileOriginalHasData.size()));
}

std::string Bfsar::findGroupFileInFolder(const sead::SafeString& groupName, const sead::SafeString& folder) const
{
    std::string path;

    if (IsPlainFileName(groupName))
        FreeFileBytes(FindGroupFileInFolder(folder, groupName, &path));

    return path;
}

ByteRange Bfsar::takeGroupFileOfArchive_(const BfsarOpenContext& ctx, const Group& group, const ByteRange& file) const
{
    if (!file.data() || CheckGroupFile(file, ctx.soundArchive.detail_GetFileCount()) == GroupFileCheck::Ok)
        return file;

    FreeFileBytes(file);
    PopupMgr::instance()->pushCurrentItemError(messages::archive::cGroupFileOfOtherArchive);

    if (mCliMode)
        fprintf(stderr, "The group file of '%s' is not of this archive\n", group.mName.cstr());

    return ByteRange();
}

ByteRange Bfsar::loadExternalGroupFile_(const BfsarOpenContext& ctx, const Group& group, bool searchFolders, std::string* outPath)
{
    const bool isOwnFolder = ctx.groupFileSource == getFilePath();

    if (isOwnFolder)
        *outPath = groupfilepaths::Find(getFilePath(), group.mName, usesStoredGroupFilePaths_());

    ByteRange file = takeGroupFileOfArchive_(ctx, group, outPath->empty() ? ByteRange() : LoadFileBytes(outPath->c_str()));

    if (file.data())
        return file;

    sead::FixedSafeString<cPathBufferSize> archiveDir;

    if (searchFolders)
        file = FindExternalGroupFile(group, ctx.groupFileSource, outPath);
    else if (sead::Path::getDirectoryName(&archiveDir, ctx.groupFileSource) && IsPlainFileName(group.mName))
        file = LoadExtDataGroupFile(archiveDir, group.mName, outPath);

    if (searchFolders && !file.data() && mCliMode)
        fprintf(stderr, "Failed to find external group file for '%s'\n", group.mName.cstr());

    return takeGroupFileOfArchive_(ctx, group, file);
}

void Bfsar::resolveGroupFile_(BfsarOpenContext& ctx, Group* group, SoundArchiveFile::GroupInfo* groupInfo)
{
    if (groupInfo->fileId == nw::snd::SoundArchive::INVALID_ID)
    {
        group->mOutputType = Group::OutputType::External;
        group->mExternalFileState = Group::ExternalFileState::Missing;

        std::string path;
        const ByteRange externalFile = loadExternalGroupFile_(ctx, *group, true, &path);

        if (externalFile.data())
            attachExternalGroupFile_(ctx, group, groupInfo, externalFile, path);
    }

    if (groupInfo->fileId == nw::snd::SoundArchive::INVALID_ID)
        return;

    const bool isExternalFile = groupInfo->fileId >= ctx.soundArchive.detail_GetFileCount();
    const ByteRange file = FindGroupFile(ctx, groupInfo->fileId);
    const bool hasGroupMagic = IsInnerFileKind(file, InnerFileKind::Group);

    if (!isExternalFile && !hasGroupMagic)
    {
        group->mHasFileSlot = true;
        group->mGroupInfoFileId = groupInfo->fileId;

        if (!loadExtDataGroupFile_(ctx, group, groupInfo))
        {
            group->mOutputType = Group::OutputType::External;
            group->mExternalFileState = Group::ExternalFileState::Missing;
            groupInfo->fileId = nw::snd::SoundArchive::INVALID_ID;
        }

        return;
    }

    if (hasGroupMagic && !CheckGroupFileLayout(file))
    {
        PopupMgr::instance()->pushCurrentItemError(messages::archive::cGroupFileDamaged);
        groupInfo->fileId = nw::snd::SoundArchive::INVALID_ID;

        return;
    }

    const GroupFileReader reader(file.size() >= cInnerFileMagicLength ? file.data() : nullptr);

    if (!reader.IsInitialized())
        groupInfo->fileId = nw::snd::SoundArchive::INVALID_ID;
}

bool Bfsar::readGroups_(BfsarOpenContext& ctx)
{
    loadprogress::BeginStage(loadprogress::Stage::Groups);

    for (u32 i = 0; i < ctx.soundArchive.GetGroupCount(); i++)
    {
        loadprogress::Step();

        SoundArchiveFile::GroupInfo* groupInfo = const_cast<SoundArchiveFile::GroupInfo*>(ctx.soundArchive.GetGroupInfo(ctx.soundArchive.GetGroupIdFromIndex(i)));

        if (!groupInfo)
        {
            PopupMgr::instance()->setCorruptInfo(messages::archive::cInvalidGroupInfo);
            return false;
        }

        Group* group = new(ctx.heap) Group();
        group->mId = i;
        PopupMgr::instance()->setCurrentProcessItem(group);

        group->mEnableName = groupInfo->optionParameter.GetTrueCount(nw::snd::internal::GROUP_INFO_STRING_ID) != 0;
        readItemName_(ctx, group, groupInfo->GetStringId(), "GROUP_%u");

        resolveGroupFile_(ctx, group, groupInfo);

        mGroupList.pushBack(group);
    }

    return true;
}

void Bfsar::addGroupItem_(BfsarOpenContext& ctx, const GroupFileReader& reader, Group* group, u32 itemIdx)
{
    GroupFile::GroupItemInfoEx itemInfoEx;

    if (!reader.ReadGroupItemInfoEx(&itemInfoEx, itemIdx))
        return;

    Group::ItemInfo* itemInfo = new(ctx.heap) Group::ItemInfo(group);
    itemInfo->mId = itemIdx;
    itemInfo->mIsDisabled = itemInfoEx.itemId == 0 || itemInfoEx.itemId == nw::snd::SoundArchive::INVALID_ID;

    if (!itemInfo->mIsDisabled)
    {
        switch (Util::GetItemType(itemInfoEx.itemId))
        {
            case nw::snd::internal::ItemType_Sound:
                itemInfo->mItemRefType = Item::ItemType::Sound;
                break;

            case nw::snd::internal::ItemType_SoundGroup:
                itemInfo->mItemRefType = Item::ItemType::SoundSet;
                break;

            case nw::snd::internal::ItemType_Bank:
                itemInfo->mItemRefType = Item::ItemType::Bank;
                break;

            case nw::snd::internal::ItemType_WaveArchive:
                itemInfo->mItemRefType = Item::ItemType::WaveArchive;
                break;

            default:
                itemInfo->mItemRefType = Item::ItemType::Invalid;
                break;
        }

        itemInfo->mOriginalItemId = itemInfoEx.itemId;
        itemInfo->mItemRef.attach(getItem(itemInfoEx.itemId, getItemList(itemInfo->mItemRefType)));

        if (itemInfo->mItemRef.isAttached())
            itemInfo->setLoadItems(itemInfoEx.loadFlag);
        else
            PopupMgr::instance()->pushCurrentItemError(sead::FormatFixedSafeString<64>(messages::archive::cGroupItemInvalidFormat, itemIdx));
    }

    group->mItemInfoList.pushBack(itemInfo);
}

static void AddGroupWaveArchiveFile(const BfsarOpenContext& ctx, const BfsarGroupItemFiles& item, u32 warcId)
{
    const SoundArchiveFile::WaveArchiveInfo* warcInfo = ctx.soundArchive.GetWaveArchiveInfo(warcId);

    if (warcInfo)
        item.fileIds->insert(warcInfo->fileId);
}

static void AddGroupSharedWaveArchiveFile(const BfsarOpenContext& ctx, const BfsarGroupItemFiles& item, u32 fileId, const std::unordered_map<u64, u32>& fileWarcIds)
{
    const u32 fileGroupId = item.group->getOutputType() == Group::OutputType::Link ? nw::snd::SoundArchive::INVALID_ID : item.groupId;
    const auto it = fileWarcIds.find(MakeGroupFileKey(fileId, fileGroupId));

    if (it != fileWarcIds.end() && it->second != nw::snd::SoundArchive::INVALID_ID)
        AddGroupWaveArchiveFile(ctx, item, it->second);
}

void Bfsar::addGroupBankFiles_(BfsarOpenContext& ctx, const BfsarGroupItemFiles& item, u32 bankId)
{
    const SoundArchiveFile::BankInfo* bankInfo = ctx.soundArchive.GetBankInfo(bankId);

    if (!bankInfo)
        return;

    if (item.loadFlag & Group::ItemInfo::LoadFlag::LoadBank)
        item.fileIds->insert(bankInfo->fileId);

    const Bank* bank = static_cast<const Bank*>(getItem(bankId, getBankList()));

    if (!bank || !(item.loadFlag & Group::ItemInfo::LoadFlag::LoadWarc))
        return;

    switch (bank->getWaveArchiveType())
    {
        case WaveArchiveType::AutomaticShared:
            AddGroupSharedWaveArchiveFile(ctx, item, bankInfo->fileId, ctx.bankFileWarcId);
            break;

        case WaveArchiveType::AutomaticIndividual:
        case WaveArchiveType::Explicit:
            if (bankInfo->GetWaveArchiveItemIdTable()->count > 0)
                AddGroupWaveArchiveFile(ctx, item, bankInfo->GetWaveArchiveItemIdTable()->item[0]);
            break;

        default:
            break;
    }
}

void Bfsar::addGroupSoundFiles_(BfsarOpenContext& ctx, const BfsarGroupItemFiles& item, u32 soundId, bool isGroupItem)
{
    const SoundArchiveFile::SoundInfo* soundInfo = ctx.soundArchive.GetSoundInfo(soundId);

    if (!soundInfo)
        return;

    const Sound* sound = static_cast<const Sound*>(getItem(soundId, getSoundList()));

    if (sound && sound->getSoundType() == Sound::SoundType::Strm && sound->getStreamSoundInfo().hasPrefetch())
    {
        item.fileIds->insert(soundInfo->GetStreamSoundInfo().prefetchFileId);
        return;
    }

    if (soundInfo->GetSoundType() != nw::snd::SoundArchive::SOUND_TYPE_SEQ)
    {
        if (isGroupItem && soundInfo->GetSoundType() == nw::snd::SoundArchive::SOUND_TYPE_INVALID)
            PopupMgr::instance()->pushCurrentItemError(sead::FormatFixedSafeString<64>(messages::archive::cGroupItemInvalidFormat, item.itemIdx));

        return;
    }

    if (item.loadFlag & Group::ItemInfo::LoadFlag::LoadSeq)
        item.fileIds->insert(soundInfo->fileId);

    u32 bankIds[nw::snd::SoundArchive::SEQ_BANK_MAX];
    soundInfo->GetSequenceSoundInfo().GetBankIds(bankIds);

    for (u32 bankId : bankIds)
    {
        if (bankId != nw::snd::SoundArchive::INVALID_ID)
            addGroupBankFiles_(ctx, item, bankId);
    }
}

void Bfsar::addGroupWaveSoundSetFiles_(BfsarOpenContext& ctx, const BfsarGroupItemFiles& item, const SoundArchiveFile::SoundGroupInfo& soundSetInfo)
{
    const Util::Table<nw::ut::ResU32>* fileIdTable = soundSetInfo.GetFileIdTable();
    const Util::Table<nw::ut::ResU32>* warcIdTable = soundSetInfo.GetWaveSoundGroupInfo()->GetWaveArchiveItemIdTable();

    if (fileIdTable->count == 0)
        return;

    const u32 waveSoundFileId = fileIdTable->item[0];

    if (item.loadFlag & Group::ItemInfo::LoadFlag::LoadWsd)
        item.fileIds->insert(waveSoundFileId);

    const SoundSet* soundSet = static_cast<const SoundSet*>(getItem(item.itemId, getSoundSetList()));

    if (!soundSet || !(item.loadFlag & Group::ItemInfo::LoadFlag::LoadWarc))
        return;

    switch (soundSet->getWaveArchiveType())
    {
        case WaveArchiveType::AutomaticShared:
            AddGroupSharedWaveArchiveFile(ctx, item, waveSoundFileId, ctx.wsdFileWarcId);
            break;

        case WaveArchiveType::AutomaticIndividual:
        case WaveArchiveType::Explicit:
            if (warcIdTable->count > 0)
                AddGroupWaveArchiveFile(ctx, item, warcIdTable->item[0]);
            break;

        default:
            break;
    }
}

void Bfsar::addGroupSoundSetFiles_(BfsarOpenContext& ctx, const BfsarGroupItemFiles& item)
{
    const SoundArchiveFile::SoundGroupInfo* soundSetInfo = ctx.soundArchive.detail_GetSoundGroupInfo(item.itemId);

    if (!soundSetInfo)
        return;

    if (soundSetInfo->GetWaveSoundGroupInfo())
    {
        addGroupWaveSoundSetFiles_(ctx, item, *soundSetInfo);
        return;
    }

    const u32 endIndex = Util::GetItemIndex(soundSetInfo->endId);

    for (const Item::ListNode* node = getItem(soundSetInfo->startId, getSoundList());
         node && node->val()->getId() <= endIndex;
         node = getSoundList().next(node))
    {
        addGroupSoundFiles_(ctx, item, node->val()->getIdWithType(), false);
    }
}

void Bfsar::addGroupItemFiles_(BfsarOpenContext& ctx, const BfsarGroupItemFiles& item)
{
    switch (Util::GetItemType(item.itemId))
    {
        case nw::snd::internal::ItemType_Sound:
            addGroupSoundFiles_(ctx, item, item.itemId, true);
            break;

        case nw::snd::internal::ItemType_SoundGroup:
            addGroupSoundSetFiles_(ctx, item);
            break;

        case nw::snd::internal::ItemType_Bank:
            addGroupBankFiles_(ctx, item, item.itemId);
            break;

        case nw::snd::internal::ItemType_WaveArchive:
            AddGroupWaveArchiveFile(ctx, item, item.itemId);
            break;

        default:
            break;
    }
}

static bool IsAnonymousWaveArchiveFile(const nw::snd::MemorySoundArchive& soundArchive, u32 fileId)
{
    for (u32 i = 0; i < soundArchive.GetWaveArchiveCount(); i++)
    {
        const SoundArchiveFile::WaveArchiveInfo* warcInfo = soundArchive.GetWaveArchiveInfo(soundArchive.GetWaveArchiveIdFromIndex(i));

        if (warcInfo && warcInfo->fileId == fileId)
            return warcInfo->optionParameter.GetTrueCount(nw::snd::internal::WAVE_ARCHIVE_INFO_STRING_ID) == 0;
    }

    return false;
}

static void InsertAnonymousWaveArchiveFiles(const nw::snd::MemorySoundArchive& soundArchive, const std::vector<u32>& groupFileIds, std::vector<VectorSet<u32>>& itemFileMap)
{
    for (size_t i = 1; i < groupFileIds.size(); i++)
    {
        if (!IsAnonymousWaveArchiveFile(soundArchive, groupFileIds[i]))
            continue;

        for (VectorSet<u32>& itemFiles : itemFileMap)
        {
            const auto it = std::find(itemFiles.begin(), itemFiles.end(), groupFileIds[i - 1]);

            if (it != itemFiles.end())
                itemFiles.insert(std::next(it), groupFileIds[i]);
        }
    }
}

static bool IsStartOf(const VectorSet<u32>& start, const std::vector<u32>& all)
{
    if (start.size() > all.size())
        return false;

    for (u32 i = 0; i < start.size(); i++)
    {
        if (start[i] != all[i])
            return false;
    }

    return true;
}

static bool FindGroupItemOrder(const std::vector<VectorSet<u32>>& itemFileMap, const std::vector<u32>& groupFileIds, std::vector<u32>& outOrder)
{
    const u32 itemCount = static_cast<u32>(itemFileMap.size());

    std::vector<bool> used(itemCount, false);
    std::vector<u32> nextItem(1, 0);
    std::vector<u32> fileCountBefore;
    VectorSet<u32> orderedFileIds;
    u64 stepCount = 1;

    outOrder.reserve(itemCount);
    orderedFileIds.reserve(static_cast<u32>(groupFileIds.size()));

    if (itemCount == 0)
        return orderedFileIds == groupFileIds;

    while (!nextItem.empty())
    {
        bool isItemAdded = false;

        for (u32& i = nextItem.back(); i < itemCount && !isItemAdded; i++)
        {
            if (used[i])
                continue;

            const u32 fileCount = static_cast<u32>(orderedFileIds.size());

            for (u32 fileId : itemFileMap[i])
                orderedFileIds.insert(fileId);

            if (!IsStartOf(orderedFileIds, groupFileIds))
            {
                orderedFileIds.resize(fileCount);
                continue;
            }

            if (++stepCount > cGroupOrderStepMax)
                return false;

            if (outOrder.size() + 1 == itemCount && !(orderedFileIds == groupFileIds))
            {
                orderedFileIds.resize(fileCount);
                continue;
            }

            used[i] = true;
            outOrder.push_back(i);
            fileCountBefore.push_back(fileCount);
            isItemAdded = true;
        }

        if (isItemAdded && outOrder.size() == itemCount)
            return true;

        if (isItemAdded)
        {
            nextItem.push_back(0);
            continue;
        }

        nextItem.pop_back();

        if (outOrder.empty())
            continue;

        used[outOrder.back()] = false;
        orderedFileIds.resize(fileCountBefore.back());
        outOrder.pop_back();
        fileCountBefore.pop_back();
    }

    return false;
}

void Bfsar::addGroupItemsByFile_(BfsarOpenContext& ctx, const GroupFileReader& reader, Group* group)
{
    std::unordered_map<u32, Item*> fileToItem;

    for (Item* sound : mSoundList)
    {
        if (const SoundArchiveFile::SoundInfo* info = ctx.soundArchive.GetSoundInfo(sound->getIdWithType()))
            fileToItem[info->fileId] = sound;
    }

    for (Item* bank : mBankList)
    {
        if (const SoundArchiveFile::BankInfo* info = ctx.soundArchive.GetBankInfo(bank->getIdWithType()))
            fileToItem[info->fileId] = bank;
    }

    for (Item* warc : mWaveArchiveList)
    {
        if (const SoundArchiveFile::WaveArchiveInfo* info = ctx.soundArchive.GetWaveArchiveInfo(warc->getIdWithType()))
            fileToItem[info->fileId] = warc;
    }

    for (u32 i = 0; i < reader.GetGroupItemCount(); i++)
    {
        const auto it = fileToItem.find(reader.GetGroupItemInfo(i)->fileId);

        Group::ItemInfo* itemInfo = new(ctx.heap) Group::ItemInfo(group);
        itemInfo->mId = group->getItemInfoList().size();
        itemInfo->mIsDisabled = it == fileToItem.end();

        if (!itemInfo->mIsDisabled)
        {
            Item* item = it->second;

            itemInfo->mItemRefType = item->getItemType();
            itemInfo->mItemRef.attach(item);
            itemInfo->mOriginalItemId = item->getIdWithType();
            itemInfo->setLoadItems(Group::ItemInfo::LoadFlag::LoadAll);
        }

        group->mItemInfoList.pushBack(itemInfo);
    }
}

static Group::OutputType GetGroupOutputType(const GroupFileReader& reader, bool isExternalFile)
{
    if (isExternalFile)
        return Group::OutputType::External;

    nw::snd::internal::GroupItemLocationInfo locationInfo;

    if (reader.ReadGroupItemLocationInfo(&locationInfo, 0) && !locationInfo.address)
        return Group::OutputType::Link;

    return Group::OutputType::Embed;
}

static std::vector<u32> ReadGroupFileIds(const GroupFileReader& reader)
{
    std::vector<u32> fileIds(reader.GetGroupItemCount());

    for (u32 i = 0; i < fileIds.size(); i++)
        fileIds[i] = reader.GetGroupItemInfo(i)->fileId;

    return fileIds;
}

static std::vector<u32> FindFilesOfNoItem(const std::vector<u32>& groupFileIds, const std::vector<VectorSet<u32>>& itemFileMap)
{
    std::unordered_set<u32> itemFileIds;

    for (const VectorSet<u32>& itemFiles : itemFileMap)
        itemFileIds.insert(itemFiles.begin(), itemFiles.end());

    std::vector<u32> extraFileIds;

    for (u32 fileId : groupFileIds)
    {
        if (!itemFileIds.contains(fileId))
            extraFileIds.push_back(fileId);
    }

    return extraFileIds;
}

void Bfsar::readGroupFile_(BfsarOpenContext& ctx, Group* group, u32 groupId, const SoundArchiveFile::GroupInfo* groupInfo)
{
    const ByteRange file = FindArchiveFile(ctx, groupInfo->fileId);

    if (!IsInnerFileKind(file, InnerFileKind::Group) || !CheckGroupFileLayout(file))
        return;

    const GroupFileReader reader(file.data());

    if (!reader.IsInitialized())
        return;

    group->mOutputType = GetGroupOutputType(reader, groupInfo->fileId >= ctx.soundArchive.detail_GetFileCount());

    const std::vector<u32> groupFileIds = ReadGroupFileIds(reader);
    std::vector<VectorSet<u32>> itemFileMap(reader.GetGroupItemExCount());

    for (u32 i = 0; i < itemFileMap.size(); i++)
    {
        GroupFile::GroupItemInfoEx itemInfoEx;

        if (reader.ReadGroupItemInfoEx(&itemInfoEx, i))
            addGroupItemFiles_(ctx, { group, groupId, i, itemInfoEx.itemId, itemInfoEx.loadFlag, &itemFileMap[i] });
    }

    InsertAnonymousWaveArchiveFiles(ctx.soundArchive, groupFileIds, itemFileMap);

    if (group->mOutputType == Group::OutputType::External)
    {
        group->mOriginalFileOrder = groupFileIds;
        group->mOriginalExtraFiles = FindFilesOfNoItem(groupFileIds, itemFileMap);
    }

    std::vector<u32> itemOrder;

    if (FindGroupItemOrder(itemFileMap, groupFileIds, itemOrder))
    {
        for (u32 itemIdx : itemOrder)
            addGroupItem_(ctx, reader, group, itemIdx);
    }
    else if (!itemFileMap.empty())
    {
        for (u32 i = 0; i < itemFileMap.size(); i++)
            addGroupItem_(ctx, reader, group, i);

        LOG_FMT("Group '%s' (id=%u) has items but the file order could not be determined", group->mName.cstr(), group->getId());
        PopupMgr::instance()->pushCurrentItemWarning(messages::archive::cGroupItemOrderNotFound);
    }
    else
    {
        addGroupItemsByFile_(ctx, reader, group);
    }

    updateList(group->getItemInfoList());
}

bool Bfsar::readGroupItems_(BfsarOpenContext& ctx)
{
    loadprogress::BeginStage(loadprogress::Stage::GroupFiles);

    for (u32 i = 0; i < ctx.soundArchive.GetGroupCount(); i++)
    {
        loadprogress::Step();

        const u32 groupId = ctx.soundArchive.GetGroupIdFromIndex(i);
        const SoundArchiveFile::GroupInfo* groupInfo = ctx.soundArchive.GetGroupInfo(groupId);
        Group* group = static_cast<Group*>(getItem(i, mGroupList));

        if (!groupInfo || !group)
        {
            PopupMgr::instance()->setCorruptInfo(messages::archive::cInvalidGroupInfo);
            return false;
        }

        PopupMgr::instance()->setCurrentProcessItem(group);

        if (groupInfo->fileId != nw::snd::SoundArchive::INVALID_ID)
            readGroupFile_(ctx, group, groupId, groupInfo);
    }

    return true;
}

static std::vector<bool> FindReferencedFiles(const nw::snd::MemorySoundArchive& soundArchive)
{
    std::vector<bool> referenced(soundArchive.detail_GetFileCount(), false);

    auto add = [&](u32 fileId)
    {
        if (fileId < referenced.size())
            referenced[fileId] = true;
    };

    for (u32 i = 0; i < soundArchive.GetSoundCount(); i++)
    {
        if (const SoundArchiveFile::SoundInfo* info = soundArchive.GetSoundInfo(soundArchive.GetSoundIdFromIndex(i)))
            add(info->fileId);
    }

    for (u32 i = 0; i < soundArchive.GetSoundGroupCount(); i++)
    {
        const SoundArchiveFile::SoundGroupInfo* info = soundArchive.detail_GetSoundGroupInfo(soundArchive.GetSoundGroupIdFromIndex(i));

        if (!info)
            continue;

        const Util::Table<nw::ut::ResU32>* fileIdTable = info->GetFileIdTable();

        for (u32 j = 0; j < fileIdTable->count; j++)
            add(fileIdTable->item[j]);
    }

    for (u32 i = 0; i < soundArchive.GetBankCount(); i++)
    {
        if (const SoundArchiveFile::BankInfo* info = soundArchive.GetBankInfo(soundArchive.GetBankIdFromIndex(i)))
            add(info->fileId);
    }

    for (u32 i = 0; i < soundArchive.GetWaveArchiveCount(); i++)
    {
        if (const SoundArchiveFile::WaveArchiveInfo* info = soundArchive.GetWaveArchiveInfo(soundArchive.GetWaveArchiveIdFromIndex(i)))
            add(info->fileId);
    }

    for (u32 i = 0; i < soundArchive.GetGroupCount(); i++)
    {
        if (const SoundArchiveFile::GroupInfo* info = soundArchive.GetGroupInfo(soundArchive.GetGroupIdFromIndex(i)))
            add(info->fileId);
    }

    return referenced;
}

void Bfsar::assignGroupFileSlots_(const BfsarOpenContext& ctx)
{
    std::vector<bool> referenced = FindReferencedFiles(ctx.soundArchive);
    std::vector<u32> slotFileIds;

    for (Item* item : mGroupList)
    {
        const Group* group = static_cast<const Group*>(item);

        if (group->mGroupInfoFileId >= referenced.size())
            continue;

        referenced[group->mGroupInfoFileId] = true;
        mItemOrigFileIds.emplace_back(group, group->mGroupInfoFileId);
    }

    for (u32 i = 0; i < referenced.size(); i++)
    {
        if (!referenced[i] && mFileOriginalIncludeInBfsar[i] && !mFileOriginalHasData[i] && mFileAttachedGroups[i].empty())
            slotFileIds.push_back(i);
    }

    std::vector<Group*> externalGroups;

    for (Item* item : mGroupList)
    {
        Group* group = static_cast<Group*>(item);

        if (group->mOutputType == Group::OutputType::External && !group->mHasFileSlot)
            externalGroups.push_back(group);
    }

    if (slotFileIds.empty() || slotFileIds.size() != externalGroups.size())
        return;

    for (u32 i = 0; i < externalGroups.size(); i++)
    {
        externalGroups[i]->mHasFileSlot = true;
        mItemOrigFileIds.emplace_back(externalGroups[i], slotFileIds[i]);
    }
}
