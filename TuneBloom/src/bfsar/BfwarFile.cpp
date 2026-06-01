#include <bfsar/BfwarFile.h>

#include <bfsar/WaveFile.h>
#include <ui/UI.h>

#include <snd/snd_WaveArchiveFile.h>

u32 BfwarFile::doWrite(sead::FileHandle* handle, sead::WriteStream* stream, bool isLast) const
{
    FileWriter writer(handle, stream);
    writer.openFile(mFormat == ArchiveFormat::BCSAR ? "CWAR" : "FWAR", nw::snd::internal::WaveArchiveFile::BLOCK_SIZE, mVersion);

    //? Info Block
    {
        writer.openBlock(nw::snd::internal::ElementType_WaveArchiveFile_InfoBlock, "INFO");

        writer.openSizedReferenceTable("WaveTable", mWaveFiles.size());

        writer.align(0x20);
        writer.closeBlock();
    }

    //? File Block
    {
        writer.openBlock(nw::snd::internal::ElementType_WaveArchiveFile_FileBlock, "FILE");

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

            writer.setSizedReferenceTableReferenceSize("WaveTable", size);

            i++;
        }

        writer.closeSizedReferenceTable("WaveTable");

        writer.closeBlock();
    }

    u32 fileSize = writer.getPosition();

    writer.closeFile();

    return fileSize;
}
