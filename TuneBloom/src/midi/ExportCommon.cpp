#include <midi/ExportCommon.h>

#include <bfsar/model/Bank.h>
#include <bfsar/files/BankFile.h>
#include <bfsar/archive/Bfsar.h>
#include <bfsar/io/FileIo.h>
#include <bfsar/model/Sound.h>
#include <bfsar/model/SoundSet.h>

#include <cstring>

extern Bfsar sBfsar;

bool WriteExportFile(const char* path, const std::vector<u8>& data)
{
    return fileio::WriteWholeFileAtomic(path, data.data(), data.size());
}

void SanitizeFileName(sead::BufferedSafeString* name)
{
    static constexpr char cInvalidChars[] = "/\\:*?\"<>|";

    for (s32 i = 0; i < name->calcLength(); i++)
    {
        char& c = name->getBuffer()[i];

        if (static_cast<u8>(c) < 0x20 || std::strchr(cInvalidChars, c))
            c = '_';
    }
}

std::vector<const Sound*> GetSeqSoundsOfSet(const SoundSet& soundSet)
{
    std::vector<const Sound*> sounds;

    if (soundSet.getIsEmpty() || soundSet.getSoundSetType() != SoundSet::SoundSetType::Seq)
        return sounds;

    u32 startId = soundSet.getStartId();
    u32 endId = soundSet.getEndId();

    auto& soundList = sBfsar.getSoundList();
    for (auto it = soundList.begin(); it != soundList.end(); ++it)
    {
        const Sound* sound = static_cast<const Sound*>(*it);
        u32 id = sound->getId();

        if (id < startId || id > endId)
            continue;

        if (sound->getSoundType() != Sound::SoundType::Seq)
            continue;

        sounds.push_back(sound);
    }

    return sounds;
}

const BankFile* GetSeqSoundBankFile(const Sound& sound, u32 bankIndex)
{
    if (sound.getSoundType() != Sound::SoundType::Seq || bankIndex >= cSeqSoundBankNum)
        return nullptr;

    const Item* bankItem = sound.getSequenceSoundInfo().getBankRef(bankIndex).getItem();
    if (!bankItem || bankItem->getItemType() != Item::ItemType::Bank)
        return nullptr;

    const Item* fileItem = static_cast<const Bank*>(bankItem)->getFileRef().getItem();
    if (!fileItem || fileItem->getItemType() != Item::ItemType::BankFile)
        return nullptr;

    return static_cast<const BankFile*>(fileItem);
}
