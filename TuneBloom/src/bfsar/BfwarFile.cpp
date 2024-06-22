#include <bfsar/BfwarFile.h>

#include <snd/snd_WaveArchiveFile.h>

u32 BfwarFile::doWrite(sead::FileHandle* handle, sead::WriteStream* stream, bool isLast) const
{
    FileWriter writer(handle, stream);
    writer.openFile("FWAR", nw::snd::internal::WaveArchiveFile::BLOCK_SIZE, mVersion);

    //? Info Block
    {
        writer.openBlock(nw::snd::internal::ElementType_WaveArchiveFile_InfoBlock, "INFO");

        // writer.openSizedReferenceTable("WaveTable", mWaveList.size());

        writer.align(0x20);
        writer.closeBlock();
    }

    //? File Block
    {
        writer.openBlock(nw::snd::internal::ElementType_WaveArchiveFile_FileBlock, "FILE");

        writer.align(0x20);

        // for (const Item* item : mWaveList)
        // {
        //     SEAD_ASSERT(item->getItemType() == Item::ItemType::WaveFile);
        //     const WaveFile* wave = static_cast<const WaveFile*>(item);

        //     writer.align(0x20);

        //     writer.addSizedReferenceTableReference("WaveTable", nw::snd::internal::ElementType_General_ByteStream, 0);

        //     u32 pos = writer.getPosition();
        //     {
        //         wave->write(handle, stream, mEndian, wave->getId() == mWaveList.size() - 1);
        //     }
        //     u32 size = writer.getPosition() - pos;

        //     writer.setSizedReferenceTableReferenceSize("WaveTable", size);
        // }

        // writer.closeSizedReferenceTable("WaveTable");

        writer.closeBlock();
    }

    u32 fileSize = writer.getPosition();

    writer.closeFile();

    return fileSize;
}
