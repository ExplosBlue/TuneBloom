#include <midi/MidiInput.h>

#if defined(SEAD_PLATFORM_LINUX)
#include <alsa/asoundlib.h>
#elif defined(SEAD_PLATFORM_MACOSX)
#include <CoreMIDI/CoreMIDI.h>
#elif defined(SEAD_PLATFORM_WINDOWS)
#include <windows.h>
#include <mmsystem.h>
#endif

#if defined(SEAD_PLATFORM_WINDOWS)
// WinMM callback — runs on a system thread, queues events for poll()
static void CALLBACK MidiInProc(HMIDIIN, UINT wMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR)
{
    if (wMsg != MIM_DATA)
        return;

    auto* self = reinterpret_cast<MidiInput*>(dwInstance);
    if (!self)
        return;

    u8 status = dwParam1 & 0xFF;
    u8 note = (dwParam1 >> 8) & 0xFF;
    u8 vel = (dwParam1 >> 16) & 0xFF;

    s32 msg;
    if ((status & 0xF0) == 0x90 && vel > 0)
        msg = 1;
    else if ((status & 0xF0) == 0x80 || ((status & 0xF0) == 0x90 && vel == 0))
        msg = 2;
    else
        return;

    s32 w = self->mQueueWrite;
    s32 n = (w + 1) % MidiInput::kQueueSize;
    if (n != self->mQueueRead)
    {
        self->mQueue[w].msg = msg;
        self->mQueue[w].key = note;
        self->mQueue[w].vel = vel / 127.0f;
        self->mQueueWrite = n;
    }
}
#endif

bool MidiInput::start(Callback callback, void* userData)
{
    stop();

    if (!callback)
        return false;

    mCallback = callback;
    mUserData = userData;

#if defined(SEAD_PLATFORM_LINUX)
    snd_seq_t* seq = nullptr;
    if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_INPUT, 0) < 0)
        return false;

    snd_seq_set_client_name(seq, "TuneBloom");

    int port = snd_seq_create_simple_port(seq, "MIDI Input",
        SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
        SND_SEQ_PORT_TYPE_APPLICATION);

    if (port < 0)
    {
        snd_seq_close(seq);
        return false;
    }

    mSeq = seq;
    mPort = port;
    return true;

#elif defined(SEAD_PLATFORM_WINDOWS)
    mQueueRead = 0;
    mQueueWrite = 0;

    HMIDIIN handle = nullptr;
    MMRESULT res = midiInOpen(&handle, MIDI_MAPPER,
        reinterpret_cast<DWORD_PTR>(&MidiInProc),
        reinterpret_cast<DWORD_PTR>(this),
        CALLBACK_FUNCTION);
    if (res != MMSYSERR_NOERROR)
        return false;

    mInHandle = handle;

    res = midiInStart(handle);
    if (res != MMSYSERR_NOERROR)
    {
        midiInClose(handle);
        mInHandle = nullptr;
        return false;
    }

    return true;

#elif defined(SEAD_PLATFORM_MACOSX)
    // macOS CoreMIDI implementation stub
    return false;

#else
    return false;
#endif
}

void MidiInput::stop()
{
#if defined(SEAD_PLATFORM_LINUX)
    if (mSeq)
    {
        snd_seq_close(static_cast<snd_seq_t*>(mSeq));
        mSeq = nullptr;
    }
    mPort = -1;
#elif defined(SEAD_PLATFORM_WINDOWS)
    if (mInHandle)
    {
        midiInStop(static_cast<HMIDIIN>(mInHandle));
        midiInReset(static_cast<HMIDIIN>(mInHandle));
        midiInClose(static_cast<HMIDIIN>(mInHandle));
        mInHandle = nullptr;
    }
    mQueueRead = 0;
    mQueueWrite = 0;
#endif
    mCallback = nullptr;
    mUserData = nullptr;
}

void MidiInput::poll()
{
    if (!mCallback)
        return;

#if defined(SEAD_PLATFORM_LINUX)
    if (!mSeq)
        return;

    snd_seq_t* seq = static_cast<snd_seq_t*>(mSeq);
    snd_seq_event_t* ev = nullptr;
    while (snd_seq_event_input_pending(seq, 1) > 0)
    {
        if (snd_seq_event_input(seq, &ev) < 0 || !ev)
            continue;

        switch (ev->type)
        {
        case SND_SEQ_EVENT_NOTEON:
        {
            u8 note = ev->data.note.note;
            u8 vel = ev->data.note.velocity;
            mCallback(mUserData, 1, note, vel / 127.0f);
            break;
        }
        case SND_SEQ_EVENT_NOTEOFF:
        {
            u8 note = ev->data.note.note;
            mCallback(mUserData, 2, note, 0.0f);
            break;
        }
        default:
            break;
        }
    }

#elif defined(SEAD_PLATFORM_WINDOWS)
    s32 r = mQueueRead;
    s32 w = mQueueWrite;
    while (r != w)
    {
        mCallback(mUserData, mQueue[r].msg, mQueue[r].key, mQueue[r].vel);
        r = (r + 1) % kQueueSize;
    }
    mQueueRead = r;
#endif
}

bool MidiInput::isRunning() const
{
#if defined(SEAD_PLATFORM_LINUX)
    return mSeq != nullptr;
#elif defined(SEAD_PLATFORM_WINDOWS)
    return mInHandle != nullptr;
#else
    return false;
#endif
}
