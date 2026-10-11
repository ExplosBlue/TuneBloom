#include <ui/bank/BankFileEditor.h>

#include <ui/settings/Config.h>
#include <ui/Messages.h>
#include <ui/app/PopupMgr.h>
#include <ui/app/UI.h>

#include <midi/MidiInput.h>

#include <string>
#include <vector>

static bool sKeyPressed[128] = { false };
static s32 sContextKey = -1;

s32 TakeContextKey()
{
    const s32 key = sContextKey;
    sContextKey = -1;

    return key;
}

bool KeyboardFunc(void* UserData, s32 Msg, s32 Key, f32 Vel)
{
    if (Key < 0 || Key >= 128)
    {
        return false; // Midi max keys
    }

    if (Msg == NoteOff)
    {
        sKeyPressed[Key] = false;
        sSoundPlayer.stopBankNote(static_cast<u8>(Key));

        return false;
    }

    Item* item = static_cast<Item*>(UserData);
    if (!item || item->getItemType() != Item::ItemType::BankFileInstrument)
    {
        return false;
    }

    BankFile::Instrument* instrument = static_cast<BankFile::Instrument*>(item);

    switch (Msg)
    {
        case NoteGetStatus:
        {
            return sKeyPressed[Key];
        }

        case NoteOn:
        {
            sKeyPressed[Key] = true;

            const BankFile::KeyRegion* keyRegion = instrument->getKeyRegion(Key);
            if (!keyRegion)
            {
                return false;
            }

            u8 vel = static_cast<u8>(Vel * 127.0f + 0.5f);

            const BankFile::VelocityRegion* velocityRegion = keyRegion->getVelocityRegion(vel);
            if (!velocityRegion)
            {
                PopupMgr::instance()->addPopup({ sead::FormatFixedSafeString<32>(messages::bank::cInvalidVelocityRegionFormat, vel) });
                return false;
            }

            sSoundPlayer.playBankNotePoly(static_cast<u8>(Key), vel, *velocityRegion);

            break;
        }

        case NoteContext:
        {
            sContextKey = Key;

            break;
        }
    }

    return false;
}

struct MidiConn
{
    MidiInput input;
    std::string name;
};
static std::vector<MidiConn*> sMidiConns;

static void MidiInputCallback(void *userData, s32 msg, s32 key, f32 vel)
{
    if (msg == 3)
    {
        sSoundPlayer.setBankPitchBend(vel);
        return;
    }
    if (msg == 4)
    {
        switch (key)
        {
        case 1:
            sSoundPlayer.setBankModulation(vel);
            break;
        case 11:
            sSoundPlayer.setBankExpression(vel);
            break;
        case 64:
            sSoundPlayer.setBankSustain(vel >= 0.5f);
            break;
        default:
            break;
        }
        return;
    }

    if (key < 0 || key >= 128)
        return;

    if (msg == 2)
    {
        sKeyPressed[key] = false;
        sSoundPlayer.stopBankNote(static_cast<u8>(key));
        return;
    }

    if (msg == 1)
    {
        const MidiSettings& settings = GetMidiSettings();

        Item *selected = sSelectedItem;
        if (selected && selected->getItemType() == Item::ItemType::BankFileInstrument)
            KeyboardFunc(selected, NoteOn, key, settings.fixedVelocity ? settings.velocity / 127.0f : vel);
    }
}

void PollMidiInput()
{
    for (MidiConn* c : sMidiConns)
        c->input.poll();
}

bool MidiIsDeviceConnected(const char* name)
{
    if (!name)
        return false;

    for (MidiConn* c : sMidiConns)
        if (c->name == name)
            return true;

    return false;
}

bool MidiConnectDevice(s32 deviceIndex)
{
    if (deviceIndex < 0)
        return false;

    const char* name = MidiInput::getDeviceName(static_cast<u32>(deviceIndex));
    if (!name || !*name)
        return false;

    if (MidiIsDeviceConnected(name))
        return true;

    MidiConn* c = new MidiConn();
    c->name = name;
    c->input.start(&MidiInputCallback, nullptr, static_cast<u32>(deviceIndex));
    if (!c->input.isRunning())
    {
        delete c;
        return false;
    }

    sMidiConns.push_back(c);
    return true;
}

void MidiDisconnectDevice(const char* name)
{
    if (!name)
        return;

    for (size_t i = 0; i < sMidiConns.size(); i++)
    {
        if (sMidiConns[i]->name == name)
        {
            sMidiConns[i]->input.stop();
            delete sMidiConns[i];
            sMidiConns.erase(sMidiConns.begin() + i);
            return;
        }
    }
}

void MidiDisconnectAll()
{
    for (MidiConn* c : sMidiConns)
    {
        c->input.stop();
        delete c;
    }
    sMidiConns.clear();
}

u32 MidiConnectedCount()
{
    return static_cast<u32>(sMidiConns.size());
}
