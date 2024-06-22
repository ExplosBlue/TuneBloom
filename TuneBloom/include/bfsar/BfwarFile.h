#pragma once

#include <bfsar/InnerFile.h>

class BfwarFile : public InnerFile
{
    SEAD_RTTI_OVERRIDE(BfwarFile, InnerFile);

public:
    BfwarFile()
        : InnerFile()
    {
    }

private:
    void doRead(const void* fileAddr) override
    {
    }

    u32 doWrite(sead::FileHandle* handle, sead::WriteStream* stream, bool isLast) const override;
};
