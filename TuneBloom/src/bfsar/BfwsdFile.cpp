#include <bfsar/BfwsdFile.h>

#include <snd/snd_WaveSoundFile.h>

u32 BfwsdFile::doWrite(sead::FileHandle* handle, sead::WriteStream* stream, bool isLast) const
{
    SEAD_ASSERT(mWaveArchive);

    FileWriter writer(handle, stream);
    writer.openFile("FWSD", 1, mVersion);

    //? Info Block
    {
        writer.openBlock(nw::snd::internal::ElementType_WaveSoundFile_InfoBlock, "INFO");

        writer.align(0x20);
        writer.closeBlock();
    }

    u32 fileSize = writer.getPosition();

    writer.closeFile();

    mWaveArchive = nullptr;

    return fileSize;
}
