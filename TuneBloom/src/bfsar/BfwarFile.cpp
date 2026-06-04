#include <bfsar/BfwarFile.h>
#include <Debug.h>

#include <bfsar/WaveFile.h>
#include <ui/UI.h>

#include <snd/snd_WaveArchiveFile.h>

u32 BfwarFile::doWrite(sead::FileHandle* handle, sead::WriteStream* stream, bool isLast) const
{
    LOG_FUNC();
    LOG_FMT("format=%s, version=0x%04X, waveFiles=%d", mFormat == ArchiveFormat::BCSAR ? "CWAR" : "FWAR", mVersion, (s32)mWaveFiles.size());

    FileWriter writer(handle, stream);
    writer.openFile(mFormat == ArchiveFormat::BCSAR ? "CWAR" : "FWAR", nw::snd::internal::WaveArchiveFile::BLOCK_SIZE, mVersion);

    LOG_FMT("File opened: magic=%s, blockCount=%u, version=0x%04X", mFormat == ArchiveFormat::BCSAR ? "CWAR" : "FWAR", nw::snd::internal::WaveArchiveFile::BLOCK_SIZE, mVersion);

    //? Info Block
    {
        writer.openBlock(nw::snd::internal::ElementType_WaveArchiveFile_InfoBlock, "INFO");

        LOG_FMT("InfoBlock opened, waveTable size=%d", (s32)mWaveFiles.size());
        writer.openSizedReferenceTable("WaveTable", mWaveFiles.size());

        writer.align(0x20);
        writer.closeBlock();
    }

    //? File Block
    {
        writer.openBlock(nw::snd::internal::ElementType_WaveArchiveFile_FileBlock, "FILE");

        LOG_FMT("FileBlock opened");

        writer.align(0x20);

        u32 i = 0;
        for (const WaveFile* wave : mWaveFiles)
        {
            SEAD_ASSERT(wave->getItemType() == Item::ItemType::WaveFile);

            writer.align(0x20);

            writer.addSizedReferenceTableReference("WaveTable", nw::snd::internal::ElementType_General_ByteStream, 0);

            u32 pos = writer.getPosition();
            {
                wave->setup(mEndian);
                wave->setFormat(mFormat);
                wave->write(handle, stream, mEndian, i == mWaveFiles.size() - 1);
            }
            u32 size = writer.getPosition() - pos;

            LOG_U32("waveIndex", i);
            LOG_U32("waveDataSize", size);

            writer.setSizedReferenceTableReferenceSize("WaveTable", size);

            i++;
        }

        writer.closeSizedReferenceTable("WaveTable");

        LOG_FMT("FileBlock done, total files written=%d", (s32)mWaveFiles.size());

        writer.closeBlock();
    }

    u32 fileSize = writer.getPosition();

    LOG_U32("fileSize", fileSize);

    writer.closeFile();

    return fileSize;
}
