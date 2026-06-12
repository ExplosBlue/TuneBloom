#include <midi/MidiInput.h>

#include <string>
#include <vector>

#if defined(SEAD_PLATFORM_LINUX)
#include <alsa/asoundlib.h>
#elif defined(SEAD_PLATFORM_MACOSX)
#include <CoreMIDI/CoreMIDI.h>
#elif defined(SEAD_PLATFORM_WINDOWS)
#include <windows.h>
#include <mmsystem.h>
#endif

struct DeviceEntry
{
    std::string name;
#if defined(SEAD_PLATFORM_LINUX)
    int client;
    int port;
#endif
};

static std::vector<DeviceEntry> sDeviceList;

#if defined(SEAD_PLATFORM_WINDOWS)
static std::string WideToUtf8(const wchar_t* str)
{
    if (!str || !*str) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, str, -1, nullptr, 0, nullptr, nullptr);
    std::string ret(static_cast<size_t>(len) - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, str, -1, &ret[0], len, nullptr, nullptr);
    return ret;
}
#endif

static void PopulateDeviceList()
{
    if (!sDeviceList.empty())
        return;

#if defined(SEAD_PLATFORM_WINDOWS)
    UINT numDevs = midiInGetNumDevs();
    sDeviceList.reserve(numDevs);
    for (UINT i = 0; i < numDevs; i++)
    {
        MIDIINCAPSW caps;
        if (midiInGetDevCapsW(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR)
            sDeviceList.push_back({ WideToUtf8(caps.szPname) });
    }

#elif defined(SEAD_PLATFORM_MACOSX)
    ItemCount count = MIDIGetNumberOfSources();
    sDeviceList.reserve(count);
    for (ItemCount i = 0; i < count; i++)
    {
        MIDIEndpointRef src = MIDIGetSource(i);
        if (!src)
            continue;

        CFStringRef str = nullptr;
        if (MIDIObjectGetStringProperty(src, kMIDIPropertyName, &str) == noErr && str)
        {
            char buf[256];
            if (CFStringGetCString(str, buf, sizeof(buf), kCFStringEncodingUTF8))
                sDeviceList.push_back({ buf });
            CFRelease(str);
        }
    }

#elif defined(SEAD_PLATFORM_LINUX)
    snd_seq_t* seq = nullptr;
    if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_INPUT, 0) < 0)
        return;

    snd_seq_client_info_t* cinfo;
    snd_seq_port_info_t* pinfo;
    snd_seq_client_info_alloca(&cinfo);
    snd_seq_port_info_alloca(&pinfo);

    snd_seq_client_info_set_client(cinfo, -1);
    while (snd_seq_query_next_client(seq, cinfo) >= 0)
    {
        int client = snd_seq_client_info_get_client(cinfo);
        const char* cname = snd_seq_client_info_get_name(cinfo);

        snd_seq_port_info_set_client(pinfo, client);
        snd_seq_port_info_set_port(pinfo, -1);
        while (snd_seq_query_next_port(seq, pinfo) >= 0)
        {
            unsigned int caps = snd_seq_port_info_get_capability(pinfo);
            if (caps & SND_SEQ_PORT_CAP_READ)
            {
                int port = snd_seq_port_info_get_port(pinfo);
                const char* pname = snd_seq_port_info_get_name(pinfo);
                sDeviceList.push_back({ std::string(cname) + ":" + std::string(pname), client, port });
            }
        }
    }

    snd_seq_close(seq);
#endif
}

#if defined(SEAD_PLATFORM_MACOSX)
// CoreMIDI callback — runs on a system thread, queues events for poll()
static void MidiReadProc(const MIDIPacketList* pktList, void* refCon, void*)
{
    auto* self = static_cast<MidiInput*>(refCon);
    if (!self)
        return;

    const MIDIPacket* pkt = &pktList->packet[0];
    for (UInt32 i = 0; i < pktList->numPackets; i++)
    {
        if (pkt->length >= 3)
        {
            u8 status = pkt->data[0];
            u8 note = pkt->data[1];
            u8 vel = pkt->data[2];

            s32 msg;
            if ((status & 0xF0) == 0x90 && vel > 0)
                msg = 1;
            else if ((status & 0xF0) == 0x80 || ((status & 0xF0) == 0x90 && vel == 0))
                msg = 2;
            else
                continue;

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
        pkt = MIDIPacketNext(pkt);
    }
}
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

bool MidiInput::start(Callback callback, void* userData, u32 deviceIndex)
{
    stop();

    if (!callback)
        return false;

    mCallback = callback;
    mUserData = userData;
    mDeviceIndex = static_cast<s32>(deviceIndex);

    PopulateDeviceList();

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

    if (deviceIndex < sDeviceList.size())
    {
        const auto& entry = sDeviceList[deviceIndex];
        snd_seq_connect_from(seq, port, entry.client, entry.port);
    }

    mSeq = seq;
    mPort = port;
    return true;

#elif defined(SEAD_PLATFORM_MACOSX)
    mQueueRead = 0;
    mQueueWrite = 0;

    MIDIClientRef client = nullptr;
    OSStatus err = MIDIClientCreate(CFSTR("TuneBloom"), nullptr, nullptr, &client);
    if (err != noErr)
        return false;
    mClient = client;

    MIDIPortRef port = nullptr;
    err = MIDIInputPortCreate(client, CFSTR("MIDI Input"), MidiReadProc, this, &port);
    if (err != noErr)
    {
        MIDIClientDispose(client);
        mClient = nullptr;
        return false;
    }
    mPort = port;

    if (deviceIndex < MIDIGetNumberOfSources())
    {
        MIDIEndpointRef src = MIDIGetSource(deviceIndex);
        if (src)
            MIDIPortConnectSource(port, src, nullptr);
    }

    return true;

#elif defined(SEAD_PLATFORM_WINDOWS)
    mQueueRead = 0;
    mQueueWrite = 0;

    HMIDIIN handle = nullptr;
    MMRESULT res = midiInOpen(&handle, deviceIndex,
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
#elif defined(SEAD_PLATFORM_MACOSX)
    if (mPort)
    {
        MIDIPortDispose(static_cast<MIDIPortRef>(mPort));
        mPort = nullptr;
    }
    if (mClient)
    {
        MIDIClientDispose(static_cast<MIDIClientRef>(mClient));
        mClient = nullptr;
    }
    mQueueRead = 0;
    mQueueWrite = 0;
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
    mDeviceIndex = -1;
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

#elif defined(SEAD_PLATFORM_MACOSX) || defined(SEAD_PLATFORM_WINDOWS)
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

u32 MidiInput::getDeviceCount()
{
    PopulateDeviceList();
    return static_cast<u32>(sDeviceList.size());
}

const char* MidiInput::getDeviceName(u32 index)
{
    PopulateDeviceList();
    if (index >= sDeviceList.size())
        return "";
    return sDeviceList[index].name.c_str();
}

bool MidiInput::isRunning() const
{
#if defined(SEAD_PLATFORM_LINUX)
    return mSeq != nullptr;
#elif defined(SEAD_PLATFORM_MACOSX)
    return mClient != nullptr;
#elif defined(SEAD_PLATFORM_WINDOWS)
    return mInHandle != nullptr;
#else
    return false;
#endif
}
