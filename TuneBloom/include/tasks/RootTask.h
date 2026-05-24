#pragma once

#include <filedevice/seadFileDevice.h>
#include <framework/seadTask.h>

class RootTask : public sead::Task
{
    SEAD_RTTI_OVERRIDE(RootTask, sead::Task);

public:
    explicit RootTask(const sead::TaskConstructArg& arg);

    void prepare() override;
    void calc() override;
    void draw() override;

protected:
    sead::FileDevice* mNativeFileDevice;
};
