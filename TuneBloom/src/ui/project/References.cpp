#define IMGUI_DEFINE_MATH_OPERATORS
#include <ui/project/References.h>

#include <ui/file/File.h>
#include <ui/file/FileWindow.h>
#include <ui/items/ItemColumns.h>
#include <ui/Messages.h>
#include <ui/widgets/Properties.h>
#include <ui/items/Selection.h>
#include <ui/sound/SoundViews.h>
#include <ui/widgets/Widgets.h>
#include <ui/app/Workspace.h>

#include <libs/IconsLucide.h>
#include <imgui/imgui_internal.h>

#include <bfsar/files/BankFile.h>

#include <cstring>

const char* GetSoundIcon(const Sound* sound)
{
    switch (sound->getSoundType())
    {
        case Sound::SoundType::Seq:  return ICON_LC_MUSIC_3 " ";
        case Sound::SoundType::Strm: return ICON_LC_DISC_3 " ";
        case Sound::SoundType::Wave: return ICON_LC_AUDIO_LINES " ";
        default:                     return ICON_LC_FILE_QUESTION " ";
    }
}

const char* GetItemIcon(const Item* item)
{
    switch (item->getItemType())
    {
        case Item::ItemType::Sound:        return GetSoundIcon(static_cast<const Sound*>(item));
        case Item::ItemType::SoundSet:     return ICON_LC_MUSIC_4 " ";
        case Item::ItemType::Bank:         return ICON_LC_PIANO " ";
        case Item::ItemType::WaveArchive:  return ICON_LC_ARCHIVE " ";
        case Item::ItemType::Group:        return ICON_LC_FOLDERS " ";
        case Item::ItemType::Player:       return ICON_LC_VOLUME_2 " ";
        case Item::ItemType::WaveFile:     return ICON_LC_FILE_AUDIO " ";
        case Item::ItemType::SequenceFile: return ICON_LC_FILE_MUSIC " ";
        case Item::ItemType::BankFile:     return ICON_LC_KEYBOARD_MUSIC " ";
        case Item::ItemType::StreamTrack:  return ICON_LC_MUSIC_2 " ";
        default:                           return "";
    }
}

struct RefEntry
{
    const char* icon;
    sead::FixedSafeString<16> id;
    sead::FixedSafeString<512> name;
    Item* navigateItem;
    Item* selectSubItem;
};

static void SetRefText(RefEntry* entry, const Item* item, const char* separator = nullptr, const Item* part = nullptr)
{
    const sead::FixedSafeString<256> formatted = item->getFormattedName();
    const char* text = formatted.cstr();
    const char* idEnd = text[0] == '[' ? strstr(text, "] ") : nullptr;

    if (idEnd)
    {
        entry->id.format("%.*s", static_cast<s32>(idEnd - text - 1), text + 1);
        entry->name.copy(idEnd + 2);
    }
    else
    {
        entry->id.clear();
        entry->name.copy(text);
    }

    if (part)
        entry->name.appendWithFormat("%s%s", separator, part->getFormattedName().cstr());
}

static bool FindVelocityRegionOwner(const Item* region, Item** outBankFile, Item** outInstrument)
{
    for (Item* bfItem : sBfsar.getBankFileList())
    {
        BankFile* bankFile = static_cast<BankFile*>(bfItem);
        for (Item* instItem : bankFile->getInstrumentList())
        {
            BankFile::Instrument* inst = static_cast<BankFile::Instrument*>(instItem);
            for (Item* krItem : inst->getKeyRegionList())
            {
                BankFile::KeyRegion* kr = static_cast<BankFile::KeyRegion*>(krItem);
                for (Item* vrItem : kr->getVelocityRegionList())
                {
                    if (vrItem == region)
                    {
                        *outBankFile = bfItem;
                        *outInstrument = instItem;
                        return true;
                    }
                }
            }
        }
    }

    return false;
}

static Item* FindStreamTrackSound(const Item* track)
{
    for (Item* soundItem : sBfsar.getSoundList())
    {
        Sound* sound = static_cast<Sound*>(soundItem);
        if (sound->getSoundType() != Sound::SoundType::Strm)
            continue;

        for (Item* trackItem : sound->getStreamSoundInfo().getTrackList())
        {
            if (trackItem == track)
                return soundItem;
        }
    }

    return nullptr;
}

static void BuildRefEntry(RefEntry* entry, const Item* owner)
{
    entry->icon = GetItemIcon(owner);

    if (owner->getItemType() == Item::ItemType::BankFileVelocityRegion)
    {
        Item* bankFile = nullptr;
        Item* instrument = nullptr;

        if (FindVelocityRegionOwner(owner, &bankFile, &instrument))
        {
            SetRefText(entry, bankFile, " \xe2\x86\x92 ", instrument);
            return;
        }
    }

    if (owner->getItemType() == Item::ItemType::StreamTrack)
    {
        if (const Item* sound = FindStreamTrackSound(owner))
        {
            SetRefText(entry, sound, " - ", owner);
            return;
        }
    }

    SetRefText(entry, owner);
}

static constexpr f32 cReferenceRowsMin = 6.0f;
static constexpr s32 cReferenceMax     = 1024;
static constexpr f32 cReferenceGapEm   = 0.6f;

static bool IsNewRefEntry(const RefEntry* entries, s32 entryCount)
{
    const RefEntry& entry = entries[entryCount];

    for (s32 i = 0; i < entryCount; i++)
    {
        if (entries[i].id == entry.id && entries[i].name == entry.name && strcmp(entries[i].icon, entry.icon) == 0)
            return false;
    }

    return true;
}

static bool BuildBankRegionRefEntry(RefEntry* entry, const Item* region)
{
    Item* bankFile = nullptr;
    Item* instrument = nullptr;

    if (!FindVelocityRegionOwner(region, &bankFile, &instrument))
        return false;

    for (Item* bankItem : sBfsar.getBankList())
    {
        Bank* bank = static_cast<Bank*>(bankItem);
        if (bank->getFileRef().getItem() != bankFile)
            continue;

        entry->icon = ICON_LC_PIANO " ";
        entry->navigateItem = bankFile;
        entry->selectSubItem = instrument;
        SetRefText(entry, bankItem, " - ", instrument);

        return true;
    }

    return false;
}

static s32 CollectRefEntries(const Item* item, RefEntry* entries, bool* outFull)
{
    const ItemReference::List& refs = item->getReferences();

    s32 entryCount = 0;

    for (auto it = refs.robustBegin(); it != refs.robustEnd(); ++it)
    {
        ItemReference* ref = it->val();
        if (!ref)
            continue;

        Item* owner = ref->getOwner();
        if (!owner)
            continue;

        if (owner->isFileWindow())
            continue;

        if (entryCount >= cReferenceMax)
        {
            *outFull = true;
            break;
        }

        RefEntry& entry = entries[entryCount];

        if (item->getItemType() == Item::ItemType::WaveFile && owner->getItemType() == Item::ItemType::BankFileVelocityRegion)
        {
            if (!BuildBankRegionRefEntry(&entry, owner))
                continue;
        }
        else
        {
            Item* sound = owner->getItemType() == Item::ItemType::StreamTrack ? FindStreamTrackSound(owner) : nullptr;

            entry.navigateItem = sound ? sound : owner;
            entry.selectSubItem = sound ? owner : nullptr;
            BuildRefEntry(&entry, owner);
        }

        if (IsNewRefEntry(entries, entryCount))
            entryCount++;
    }

    return entryCount;
}

static void NavigateToRefEntry(RefEntry* entry)
{
    SelectItem(entry->navigateItem);

    if (entry->selectSubItem)
    {
        if (entry->selectSubItem->getItemType() == Item::ItemType::BankFileInstrument)
        {
            sSelectedItem = entry->selectSubItem;
            sSubSelectedItem = nullptr;
            OpenFileWindow(entry->navigateItem);
        }
        else if (entry->selectSubItem->getItemType() == Item::ItemType::StreamTrack)
        {
            HighlightStreamTrack(entry->selectSubItem);
            entry->selectSubItem = nullptr;
        }
        else
        {
            sSubSelectedItem = entry->selectSubItem;
            sSelectedItemIsSubWindow = true;
        }
    }

    if (entry->navigateItem->getItemType() == Item::ItemType::Sound)
    {
        const Sound* sound = static_cast<const Sound*>(entry->navigateItem);
        switch (sound->getSoundType())
        {
            case Sound::SoundType::Seq:  SetUITab(UIType::SequenceSounds); break;
            case Sound::SoundType::Strm: SetUITab(UIType::StreamSounds); break;
            case Sound::SoundType::Wave: SetUITab(UIType::WaveSounds); break;
            default: break;
        }

        size_t newIdx = (size_t)sSelectedUIType;
        sSelectedItemArr[newIdx] = entry->navigateItem;
        sSubSelectedItemArr[newIdx] = entry->selectSubItem;
        sSelectedItemIsSubWindowArr[newIdx] = entry->selectSubItem != nullptr;
    }
}

static bool DrawRefEntryRows(RefEntry* entries, s32 entryCount)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const f32 gap = ImFloor(ImGui::GetFontSize() * cReferenceGapEm);
    const ImU32 textColor = ImGui::GetColorU32(ImGuiCol_Text);
    const ImU32 valueColor = ImGui::GetColorU32(GetItemValueColor());

    f32 idWidth = 0.0f;
    f32 iconWidth = 0.0f;

    for (s32 i = 0; i < entryCount; i++)
    {
        idWidth = sead::Mathf::max(idWidth, ImGui::CalcTextSize(entries[i].id.cstr()).x);
        iconWidth = sead::Mathf::max(iconWidth, ImGui::CalcTextSize(entries[i].icon).x);
    }

    ImDrawList* draw = ImGui::GetWindowDrawList();
    bool clicked = false;

    for (s32 i = 0; i < entryCount; i++)
    {
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const f32 right = pos.x + ImGui::GetContentRegionAvail().x - style.FramePadding.x;
        const f32 iconX = pos.x + style.FramePadding.x + (idWidth > 0.0f ? idWidth + gap : 0.0f);
        const f32 nameX = iconX + iconWidth;

        ImGui::PushID(i);
        const bool pressed = ImGui::Selectable("##reference");
        ImGui::PopID();

        if (ImGui::IsItemVisible())
        {
            draw->AddText(ImVec2(pos.x + style.FramePadding.x, pos.y), valueColor, entries[i].id.cstr());
            draw->AddText(ImVec2(iconX, pos.y), textColor, entries[i].icon);
            ImGui::RenderTextEllipsis(draw, ImVec2(nameX, pos.y), ImVec2(right, pos.y + ImGui::GetFontSize()), right, right, entries[i].name.cstr(), nullptr, nullptr);
        }

        if (pressed)
        {
            NavigateToRefEntry(&entries[i]);
            clicked = true;
        }
    }

    return clicked;
}

bool DrawReferencesUI(Item* item, f32 reserveBelow)
{
    static RefEntry entries[cReferenceMax];

    bool full = false;
    const s32 entryCount = CollectRefEntries(item, entries, &full);

    if (!BeginPropertySection(sead::FormatFixedSafeString<64>("%s###References", messages::innerfile::cReferences).cstr(), nullptr, nullptr, true, 1, nullptr, sead::FormatFixedSafeString<16>(full ? "%d+" : "%d", entryCount).cstr()))
        return false;

    if (entryCount == 0)
    {
        ImGui::TextDisabled("%s", messages::innerfile::cNoReferences);
        EndPropertySection();
        return false;
    }

    const f32 rowHeight = ImGui::GetTextLineHeightWithSpacing();
    const f32 freeHeight = ImGui::GetContentRegionAvail().y - reserveBelow - ImGui::GetStyle().CellPadding.y;
    const f32 listHeight = sead::Mathf::min(rowHeight * entryCount, sead::Mathf::max(rowHeight * cReferenceRowsMin, ImFloor(freeHeight / rowHeight) * rowHeight)) - ImGui::GetStyle().ItemSpacing.y;

    bool clicked = false;
    if (ImGui::BeginChild("###ReferencesList", ImVec2(0.0f, listHeight), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground))
        clicked = DrawRefEntryRows(entries, entryCount);

    ImGui::EndChild();

    EndPropertySection();

    return clicked;
}

static void FormatInnerVersion(sead::BufferedSafeString* out, u32 version)
{
    if (GetArchiveVersionByteCount(sBfsar.getFormat()) == 4)
        out->format("%u.%u.%u.%u", (version >> 24) & 0xFF, (version >> 16) & 0xFF, (version >> 8) & 0xFF, version & 0xFF);
    else
        out->format("%u.%u.%u", (version >> 16) & 0xFF, (version >> 8) & 0xFF, version & 0xFF);
}

void DrawArchiveInfoUI(Item::ItemType itemType)
{
    if (!BeginPropertySection(sead::FormatFixedSafeString<64>("%s###Archive", messages::innerfile::cArchive).cstr(), nullptr, nullptr, false, 1, messages::innerfile::cArchiveDetail))
        return;

    u32 version = 0;

    switch (itemType)
    {
        case Item::ItemType::WaveFile:
            version = sBfsar.getVersionForBfwav();
            break;

        case Item::ItemType::SequenceFile:
            version = sBfsar.getVersionForBfseq();
            break;

        default:
            version = sBfsar.getVersionForBfbnk();
            break;
    }

    sead::FixedSafeString<24> text;

    PropertyRow(messages::innerfile::cByteOrder);
    ImGui::TextUnformatted(sBfsar.getEndian() == sead::Endian::eBig ? messages::innerfile::cBigEndian : messages::innerfile::cLittleEndian);

    FormatInnerVersion(&text, version);
    PropertyRow(messages::innerfile::cVersion);
    ImGui::TextUnformatted(text.cstr());

    if (itemType == Item::ItemType::WaveFile)
    {
        FormatInnerVersion(&text, sBfsar.getVersionForBfstm());
        PropertyRow(messages::innerfile::cStreamVersion);
        ImGui::TextUnformatted(text.cstr());
    }

    EndPropertySection();
}
