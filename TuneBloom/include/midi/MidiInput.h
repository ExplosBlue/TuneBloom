#pragma once

#include <basis/seadTypes.h>

class MidiInput
{
public:
    using Callback = void (*)(void* userData, s32 msg, s32 key, f32 vel);

    MidiInput() = default;
    ~MidiInput() { stop(); }

    bool start(Callback callback, void* userData, u32 deviceIndex = 0);
    void stop();
    void poll();
    bool isRunning() const;

    static u32 getDeviceCount();
    static const char* getDeviceName(u32 index);
    static void refreshDevices();
    s32 getCurrentDeviceIndex() const { return mDeviceIndex; }

    struct QueuedEvent { s32 msg; s32 key; f32 vel; };
    static const s32 kQueueSize = 256;
    volatile s32 mQueueRead = 0;
    volatile s32 mQueueWrite = 0;
    QueuedEvent mQueue[kQueueSize];

private:
    Callback mCallback = nullptr;
    void* mUserData = nullptr;
    s32 mDeviceIndex = -1;
#if defined(SEAD_PLATFORM_LINUX)
    void* mSeq = nullptr;
    int mPort = -1;
#elif defined(SEAD_PLATFORM_MACOSX)
    void* mClient = nullptr;
    void* mPort = nullptr;
#elif defined(SEAD_PLATFORM_WINDOWS)
    void* mInHandle = nullptr;
#endif
};
