#pragma once

#include <basis/seadTypes.h>

class MidiInput
{
public:
    using Callback = void (*)(void* userData, s32 msg, s32 key, f32 vel);

    MidiInput() = default;
    ~MidiInput() { stop(); }

    bool start(Callback callback, void* userData);
    void stop();
    void poll();
    bool isRunning() const;

private:
    Callback mCallback = nullptr;
    void* mUserData = nullptr;
#if defined(SEAD_PLATFORM_LINUX)
    void* mSeq = nullptr;
    int mPort = -1;
#elif defined(SEAD_PLATFORM_MACOSX)
    void* mClient = nullptr;
    void* mPort = nullptr;
    bool mConnected = false;
#elif defined(SEAD_PLATFORM_WINDOWS)
    void* mInHandle = nullptr;
#endif
};
