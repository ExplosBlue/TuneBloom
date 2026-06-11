#include <midi/MidiInput.h>

#if defined(SEAD_PLATFORM_LINUX)
#include <alsa/asoundlib.h>
#elif defined(SEAD_PLATFORM_MACOSX)
#include <CoreMIDI/CoreMIDI.h>
#elif defined(SEAD_PLATFORM_WINDOWS)
#include <windows.h>
#include <mmsystem.h>
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

#elif defined(SEAD_PLATFORM_MACOSX)
    // macOS CoreMIDI implementation stub
    return false;

#elif defined(SEAD_PLATFORM_WINDOWS)
    // Windows MM implementation stub
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
        midiInClose((HMIDI)mInHandle);
        mInHandle = nullptr;
    }
#endif
    mCallback = nullptr;
    mUserData = nullptr;
}

void MidiInput::poll()
{
    if (!mCallback || !mSeq)
        return;

#if defined(SEAD_PLATFORM_LINUX)
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
#endif
}

bool MidiInput::isRunning() const
{
#if defined(SEAD_PLATFORM_LINUX)
    return mSeq != nullptr;
#else
    return false;
#endif
}
