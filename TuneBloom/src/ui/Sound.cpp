#include <ui/PopupMgr.h>
#include <ui/UI.h>
#include <ui/Messages.h>

#include <imgui/imgui_custom.h>

#include <bfsar/BfwsdFile.h>

#include <filedevice/seadPath.h>

#include <filesystem>

static constexpr ImS8 cStepS8 = 1;
static constexpr ImU8 cStepU8 = 1;
static constexpr ImU16 cStepU16 = 1;
static constexpr ImU32 cStepU32 = 1;
static constexpr ImU8 cVolumeMin = 0;
static constexpr ImU8 cVolumeMax = 255;
static constexpr ImU8 cPanMin = 0;
static constexpr ImU8 cPanMax = 127;

template <typename GetMain, typename SetMain, typename GetFx, typename SetFx>

static void DrawMainAndFxSendUI(GetMain getMain, SetMain setMain, GetFx getFx, SetFx setFx)
{
    u8 mainSend = getMain();
    
    if (ImGui::SliderScalar("Main Send", ImGuiDataType_U8, &mainSend, &cPanMin, &cPanMax))
    {
        setMain(mainSend);
        SetUnsavedChanges(true);
    }

    for (u32 i = 0; i < 3; i++)
    {
        u8 fxSend = getFx(i);
        if (ImGui::SliderScalar(sead::FormatFixedSafeString<16>("Fx Send %u", i).cstr(), ImGuiDataType_U8, &fxSend, &cPanMin, &cPanMax))
        {
            setFx(i, fxSend);
            SetUnsavedChanges(true);
        }
    }
}


namespace
{

class StateHash
{
public:
    void mixBytes(const void* data, size_t size)
    {
        const u8* bytes = static_cast<const u8*>(data);

        for (size_t i = 0; i < size; i++)
        {
            mValue ^= bytes[i];
            mValue *= 0x100000001B3ULL;
        }
    }

    template <typename T>
    void mixValue(const T& value)
    {
        mixBytes(&value, sizeof(value));
    }

    void mixText(const sead::SafeString& text)
    {
        mixBytes(text.cstr(), text.calcLength() + 1);
    }

    void mixRef(const ItemReference& reference)
    {
        if (!reference.isAttached())
        {
            mixValue(static_cast<u8>(0));
            return;
        }

        mixValue(static_cast<u8>(1));
        mixValue(reference.getItem());
    }

    u64 value() const
    {
        return mValue;
    }

private:
    u64 mValue = 0xCBF29CE484222325ULL;
};

}

void Sound::captureBaseline()
{
    mBaselineSignature = computeStateSignature();
    mHasBaseline = true;

    mCachedSignature = mBaselineSignature;
    mCachedSignatureGeneration = GetEditGeneration();
}

bool Sound::isModifiedSinceBaseline() const
{
    if (!mHasBaseline)
        return true;

    const u64 generation = GetEditGeneration();

    if (mCachedSignatureGeneration != generation)
    {
        mCachedSignature = computeStateSignature();
        mCachedSignatureGeneration = generation;
    }

    return mCachedSignature != mBaselineSignature;
}

u64 Sound::computeStateSignature() const
{
    StateHash hash;

    hash.mixValue(isEnableName());
    hash.mixText(getName());
    hash.mixRef(getPlayerRef());

    hash.mixValue(getVolume());
    hash.mixValue(getRemoteFilter());
    hash.mixValue(getSoundType());

    hash.mixValue(isEnablePanParam());
    hash.mixValue(getPanMode());
    hash.mixValue(getPanCurve());

    hash.mixValue(isEnablePlayerParam());
    hash.mixValue(getPlayerPriority());
    hash.mixValue(getActorPlayerId());

    for (u32 i = 0; i < 4; i++)
    {
        hash.mixValue(isEnableUserParam(i));
        hash.mixValue(getUserParam(i));
    }

    hash.mixValue(isEnableIsFrontBypass());
    hash.mixValue(getIsFrontBypass());

    hash.mixValue(isEnableSound3DInfo());

    {
        const Sound3DInfo& info3D = getSound3DInfo();

        hash.mixValue(info3D.getFlags());
        hash.mixValue(info3D.getDecayRatio());
        hash.mixValue(info3D.getDecayCurve());
        hash.mixValue(info3D.getDopplerFactor());
    }

    hash.mixBytes(mV3NameField.data(), mV3NameField.size());

    for (const std::pair<u32, u32>& option : mExtraSoundInfoOptions)
    {
        hash.mixValue(option.first);
        hash.mixValue(option.second);
    }

    {
        const SequenceSoundInfo& seqInfo = getSequenceSoundInfo();

        hash.mixRef(seqInfo.getSequenceFileRef());

        for (u32 i = 0; i < 4; i++)
            hash.mixRef(seqInfo.getBankRef(i));

        hash.mixValue(seqInfo.isEnableStartOffset());
        hash.mixText(seqInfo.getStartLabel());
        hash.mixValue(seqInfo.isEnablePriority());
        hash.mixValue(seqInfo.getChannelPriority());
        hash.mixValue(seqInfo.getIsReleasePriorityFix());
    }

    {
        const StreamSoundInfo& strmInfo = getStreamSoundInfo();

        hash.mixText(strmInfo.getPath());
        hash.mixValue(strmInfo.getAllocateTrackFlags());
        hash.mixValue(strmInfo.getAllocateChannelCount());
        hash.mixValue(strmInfo.getPitch());
        hash.mixValue(strmInfo.getMainSend());

        for (u32 i = 0; i < 3; i++)
            hash.mixValue(strmInfo.getFxSend(i));

        hash.mixValue(strmInfo.isEnableStreamSoundExtension());
        hash.mixValue(strmInfo.getStreamType());
        hash.mixValue(strmInfo.getIsLoop());
        hash.mixValue(strmInfo.getLoopStartFrame());
        hash.mixValue(strmInfo.getLoopEndFrame());
        hash.mixValue(strmInfo.getStreamTypeInfoUpper());
        hash.mixRef(strmInfo.getPrefetchFileRef());

        const StreamSoundInfo::Track::List& tracks = strmInfo.getTrackList();

        for (s32 i = 0; i < tracks.size(); i++)
        {
            const StreamSoundInfo::Track& track = *static_cast<const StreamSoundInfo::Track*>(tracks.nth(i)->val());

            hash.mixValue(track.isEnableName());
            hash.mixText(track.getName());
            hash.mixRef(track.getWaveFileRef());

            hash.mixValue(track.getVolume());
            hash.mixValue(track.getPan());
            hash.mixValue(track.getSPan());
            hash.mixValue(track.getFlags());
            hash.mixValue(track.getMainSend());

            for (u32 j = 0; j < 3; j++)
                hash.mixValue(track.getFxSend(j));

            hash.mixValue(track.getLpfFreq());
            hash.mixValue(track.getBiquadType());
            hash.mixValue(track.getBiquadValue());

            const sead::ObjList<u8>& channels = track.getChannels_();

            for (s32 j = 0; j < channels.size(); j++)
                hash.mixValue(*channels.nth(j));
        }
    }

    {
        const WaveSoundInfo& waveInfo = getWaveSoundInfo();

        hash.mixRef(waveInfo.getWaveFileRef());
        hash.mixValue(waveInfo.getAllocateTrackCount());

        hash.mixValue(waveInfo.isEnablePriority());
        hash.mixValue(waveInfo.getChannelPriority());
        hash.mixValue(waveInfo.getIsReleasePriorityFix());

        hash.mixValue(waveInfo.isEnablePan());
        hash.mixValue(waveInfo.getPan());
        hash.mixValue(waveInfo.getSurroundPan());

        hash.mixValue(waveInfo.isEnablePitch());
        hash.mixValue(waveInfo.getPitch());

        hash.mixValue(waveInfo.isEnableSend());
        hash.mixValue(waveInfo.getMainSend());
        hash.mixValue(waveInfo.getFxSendCount());

        for (u32 i = 0; i < 3; i++)
            hash.mixValue(waveInfo.getFxSend(i));

        hash.mixValue(waveInfo.isEnableEnvelope());

        {
            const snd::AdshrCurve& adshrCurve = waveInfo.getAdshrCurve();

            hash.mixValue(adshrCurve.attack);
            hash.mixValue(adshrCurve.decay);
            hash.mixValue(adshrCurve.sustain);
            hash.mixValue(adshrCurve.hold);
            hash.mixValue(adshrCurve.release);
        }

        hash.mixValue(waveInfo.isEnableFilter());
        hash.mixValue(waveInfo.getLpfFreq());
        hash.mixValue(waveInfo.getBiquadType());
        hash.mixValue(waveInfo.getBiquadValue());
    }

    return hash.value();
}

Sound::~Sound()
{
    if (this == sSoundPlayer.getPlayingSound())
    {
        sSoundPlayer.resetPlayingSound();
    }

    if (this == sSoundPlayer.getLastPlayedSound())
    {
        sSoundPlayer.resetLastPlayedSound();
    }
}

const Item* Sound::validate(sead::BufferedSafeString& error) const
{
    if (!Item::validateName(error))
    {
        return this;
    }

    if (!getPlayerRef().isAttached())
    {
        error = messages::validation::cInvalidPlayer;
        return this;
    }

    switch (mSoundType)
    {
        case SoundType::Seq:
        {
            const Sound::SequenceSoundInfo& seqInfo = getSequenceSoundInfo();
            if (!seqInfo.getSequenceFileRef().isAttached())
            {
                error = messages::validation::cInvalidSequenceFile;
                return this;
            }

            const SequenceFile& seqFile = *static_cast<const SequenceFile*>(seqInfo.getSequenceFileRef().getItem());
            if (!seqFile.isValid())
            {
                error = messages::sequence::cNotCompiled;
                return this;
            }

            if (seqFile.getLabelOffset(seqInfo.getStartLabel()) == SequenceFile::cInvaldOffset)
            {
                error = messages::sequence::cInvalidStartLabel;
                return this;
            }

            break;
        }

        case SoundType::Strm:
        {
            const Sound::StreamSoundInfo& strmInfo = getStreamSoundInfo();
            if (strmInfo.getPath().isEmpty())
            {
                error = messages::stream::cNoPath.text;
                return this;
            }

            if (strmInfo.getStreamType() == Sound::StreamSoundInfo::StreamType::NwStreamBinary)
            {
                const Sound::StreamSoundInfo::Track::List& tracks = strmInfo.getTrackList();

                if (tracks.size() > cStrmTrackNum)
                {
                    error.format(messages::stream::cTrackLimitFormat, cStrmTrackNum);
                    return this;
                }

                if (tracks.isEmpty() && strmInfo.getAllocateChannelCount() == 0)
                {
                    error = messages::stream::cNoTracksOrChannels;
                    return this;
                }

                const WaveFile* mainWaveFile = nullptr;

                for (s32 i = 0; i < tracks.size(); i++)
                {
                    const Sound::StreamSoundInfo::Track& track = *static_cast<const Sound::StreamSoundInfo::Track*>(tracks.nth(i)->val());

                    if (!track.getWaveFileRef().isAttached())
                    {
                        if (track.getChannels_().isEmpty())
                        {
                            error.format(messages::stream::cTrackNoWaveFileFormat, i);
                            return this;
                        }

                        continue;
                    }

                    const WaveFile& waveFile = *static_cast<const WaveFile*>(track.getWaveFileRef().getItem());

                    if (!mainWaveFile)
                    {
                        mainWaveFile = &waveFile;
                        continue;
                    }

                    if (mainWaveFile->getEncoding() != waveFile.getEncoding())
                    {
                        error = messages::stream::cTracksSameEncoding;
                        return this;
                    }

                    if (mainWaveFile->getSampleRate() != waveFile.getSampleRate())
                    {
                        error = messages::stream::cTracksSameSampleRate;
                        return this;
                    }
                }
            }
            else
            {
                //? Dont scream as we simply dont save
                // error = "Only BFSTM streams are supported";
                // return this;
            }

            break;
        }

        case SoundType::Wave:
        {
            const Sound::WaveSoundInfo& waveInfo = getWaveSoundInfo();
            if (!waveInfo.getWaveFileRef().isAttached())
            {
                error = messages::validation::cInvalidWaveFile;
                return this;
            }

            break;
        }

        default:
            error = messages::validation::cInvalidSoundType;
            return this;
    }

    return nullptr;
}

static Sound* sStreamReloadSound = nullptr;
static Bfsar::StreamReloadResult sStreamReloadResult;

bool ReloadStreamFile(Sound* sound)
{
    sStreamReloadSound = sound;
    sStreamReloadResult = sBfsar.reloadStreamSound(sound);

    if (sStreamReloadResult.succeeded)
        SetUnsavedChanges(true);

    return sStreamReloadResult.succeeded;
}

static void ReplaceStreamFile(Sound* sound, Sound::StreamSoundInfo& strmSoundInfo)
{
    const InnerFileFormatInfo& streamFormat = GetInnerFileFormat(sBfsar.getFormat(), InnerFileKind::Stream);

    sead::FixedSafeString<64> filterName;
    filterName.format("%s file (*.%s)", streamFormat.displayName, streamFormat.extension);

    sead::FixedSafeString<32> filterPattern;
    filterPattern.format("*.%s", streamFormat.extension);

    FileFilter filters[1] = {
        { filterName.cstr(), filterPattern.cstr() }
    };

    sead::FixedSafeString<1024> pickedPath;

    if (!OpenFileDialog(&pickedPath, nullptr, 1, filters))
        return;

    sead::FixedSafeString<512> archiveDir;

    if (!sead::Path::getDirectoryName(&archiveDir, sBfsar.getFilePath()) &&
        !sead::Path::getDirectoryName(&archiveDir, sBfsar.getLoadedArchivePath()))
    {
        PopupMgr::instance()->addPopup({messages::stream::cReloadNoArchiveFolder, sound});
        return;
    }

    std::error_code error;
    std::filesystem::path relativePath = std::filesystem::relative(pickedPath.cstr(), archiveDir.cstr(), error);

    if (error || relativePath.empty())
    {
        sead::FormatFixedSafeString<1024> msg(messages::stream::cReplaceOutsideArchiveFolderFormat, pickedPath.cstr());
        PopupMgr::instance()->addPopup({msg, sound});
        return;
    }

    std::string newPath = relativePath.generic_string();

    if (newPath.rfind("..", 0) == 0)
    {
        sead::FormatFixedSafeString<1024> msg(messages::stream::cReplaceOutsideArchiveFolderFormat, newPath.c_str());
        PopupMgr::instance()->addPopup({msg, sound});
    }

    const sead::FixedSafeString<512> previousPath(strmSoundInfo.getPath());
    strmSoundInfo.getPath() = newPath.c_str();

    if (!ReloadStreamFile(sound))
        strmSoundInfo.getPath() = previousPath;
}

static void SetStreamReloadTooltip(const Sound* sound, const Sound::StreamSoundInfo& strmSoundInfo, bool hasPath, bool hasArchiveDir)
{
    if (!IsItemHoveredAllowDisabled())
        return;

    if (!hasPath)
    {
        ImGui::SetTooltip("%s", messages::stream::cNoPath.text);
        return;
    }

    if (!hasArchiveDir)
    {
        ImGui::SetTooltip("%s", messages::stream::cReloadNoArchiveFolder);
        return;
    }

    sead::FixedSafeString<1024> resolvedPath;

    if (!sBfsar.resolveStreamFilePath(*sound, &resolvedPath))
    {
        ImGui::SetTooltip(messages::stream::cReloadNotFoundFormat, strmSoundInfo.getPath().cstr());
        return;
    }

    ImGui::SetTooltip("%s", messages::stream::cReloadTooltip);
}

static void DrawStreamReloadSummary(const Sound* sound, const Sound::StreamSoundInfo& strmSoundInfo)
{
    if (sStreamReloadSound != sound || !sStreamReloadResult.succeeded)
        return;

    ImGui::TextDisabled(messages::stream::cReloadedSummaryFormat,
                        sStreamReloadResult.trackCount, sStreamReloadResult.trackCount == 1 ? "" : "s",
                        sStreamReloadResult.channelCount, sStreamReloadResult.channelCount == 1 ? "" : "s",
                        sStreamReloadResult.sampleRate);

    if (sStreamReloadResult.layoutChanged)
        ImGui::TextDisabled("%s", messages::stream::cReloadLayoutChanged);

    if (sStreamReloadResult.tracksReplaced)
        ImGui::TextDisabled("%s", messages::stream::cReloadTracksReplaced);

    if (sStreamReloadResult.streamTypeChanged)
    {
        const char* typeName = strmSoundInfo.getStreamType() == Sound::StreamSoundInfo::StreamType::Opus
                                   ? messages::stream::cTypeOpus
                                   : GetInnerFileDisplayName(sBfsar.getFormat(), InnerFileKind::Stream);

        ImGui::TextDisabled(messages::stream::cReloadTypeChangedFormat, typeName);
    }

    if (sStreamReloadResult.loopChanged)
        ImGui::TextDisabled("%s", messages::stream::cReloadLoopChanged);

    if (sStreamReloadResult.sharedSoundCount != 0)
    {
        ImGui::TextDisabled(messages::stream::cReloadSharedFormat, sStreamReloadResult.sharedSoundCount,
                            sStreamReloadResult.sharedSoundCount == 1 ? "" : "s");
    }
}

static void DrawStreamPathUI(Sound* sound, Sound::StreamSoundInfo& strmSoundInfo)
{
    const bool hasArchiveDir = sBfsar.hasArchiveDirectory();
    const bool hasPath = !strmSoundInfo.getPath().isEmpty();
    const bool canReload = hasPath && hasArchiveDir;

    const ImGuiStyle& style = ImGui::GetStyle();
    const f32 buttonWidth = ImGui::GetFrameHeight();

    ImGui::SetNextItemWidth(ImGui::CalcItemWidth() - (buttonWidth + style.ItemInnerSpacing.x) * 2.0f);

    sead::FixedSafeString<512> path(strmSoundInfo.getPath());

    if (ImGui::InputText("###Path", path.getBuffer(), path.getBufferSize(), ImGuiInputTextFlags_EnterReturnsTrue) || ImGui::IsItemDeactivatedAfterEdit())
    {
        if (path != strmSoundInfo.getPath())
        {
            strmSoundInfo.getPath() = path;
            SetUnsavedChanges(true);
        }
    }

    ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);

    if (!canReload)
        ImGui::BeginDisabled();

    if (ImGui::Button(ICON_LC_REFRESH_CW "###ReloadStream", ImVec2(buttonWidth, buttonWidth)))
        ReloadStreamFile(sound);

    if (!canReload)
        ImGui::EndDisabled();

    SetStreamReloadTooltip(sound, strmSoundInfo, hasPath, hasArchiveDir);

    ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);

    if (!hasArchiveDir)
        ImGui::BeginDisabled();

    if (ImGui::Button(ICON_LC_FOLDER_OPEN "###ReplaceStream", ImVec2(buttonWidth, buttonWidth)))
        ReplaceStreamFile(sound, strmSoundInfo);

    if (!hasArchiveDir)
        ImGui::EndDisabled();

    SetDisabledTooltip(hasArchiveDir ? messages::stream::cReplaceTooltip : messages::stream::cReloadNoArchiveFolder);

    ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
    ImGui::TextUnformatted(messages::stream::cPathLabel);

    ImGui::SameLine();
    HelpMarker(sead::FormatFixedSafeString<64>(messages::stream::cPathRelativeFormat, GetInnerFileExtension(sBfsar.getFormat(), InnerFileKind::SoundArchive)).cstr());

    DrawStreamReloadSummary(sound, strmSoundInfo);
}

void DrawSoundPropertiesUI()
{
    Sound* sound = static_cast<Sound*>(sSelectedItem);

    {
        Item* player = sound->getPlayerRef().getItem();
        if (ItemSelector("Player", sBfsar.getPlayerList(), &player))
        {
            sound->getPlayerRef().attach(player);
            SetUnsavedChanges(true);
        }
    }

    {
        u8 volume = sound->getVolume();
        if (ImGui::SliderScalar(sead::FormatFixedSafeString<32>("Volume (%.3f)###vol", static_cast<f32>(volume) / 127.0f).cstr(), ImGuiDataType_U8, &volume, &cVolumeMin, &cVolumeMax))
        {
            sound->setVolume(volume);
            sSoundPlayer.refreshSoundVolume(*sound);
            SetUnsavedChanges(true);
        }
    }

    {
        u8 remoteFilter = sound->getRemoteFilter();

        if (ImGui::SliderScalar("Remote Filter", ImGuiDataType_U8, &remoteFilter, &cPanMin, &cPanMax))
        {
            sound->setRemoteFilter(remoteFilter);
            SetUnsavedChanges(true);
        }
    }

    if (sBfsar.isV3Bfsar())
    {
        char tag[0x20];
        sound->getV3Tag(tag, sizeof(tag));

        if (ImGui::InputText("Tag", tag, sizeof(tag)))
        {
            sound->setV3Tag(tag);
            SetUnsavedChanges(true);
        }
    }

    {
        bool enablePanParam = sound->isEnablePanParam();
        if (ImGui::Checkbox("Enable Pan Param", &enablePanParam))
        {
            sound->setEnablePanParam(enablePanParam);
            SetUnsavedChanges(true);
        }

        if (!enablePanParam)
            ImGui::BeginDisabled();

        {
            static const char *sPanModes[] = {
                "Dual",
                "Balance",
                //"Invalid"
            };

            snd::PanMode panMode = sound->getPanMode();
            if (ComboScroll("Pan Mode", (s32 *)&panMode, sPanModes, IM_ARRAYSIZE(sPanModes)))
            {
                sound->setPanMode(panMode);
                SetUnsavedChanges(true);
            }
        }

        {
            static const char* sPanCurves[] = { 
                "Sqrt",
                "Sqrt0Db",
                "Sqrt0DbClamp",
                "SinCos",
                "SinCos0Db",
                "SinCos0DbClamp",
                "Linear",
                "Linear0Db",
                "Linear0DbClamp",
                //"Invalid"
            };

            snd::PanCurve panCurve = sound->getPanCurve();
            if (ComboScroll("Pan Curve", (s32*)&panCurve, sPanCurves, IM_ARRAYSIZE(sPanCurves)))
            {
                sound->setPanCurve(panCurve);
                SetUnsavedChanges(true);
            }
        }

        if (!enablePanParam)
            ImGui::EndDisabled();
    }

    {
        bool enablePlayerParam = sound->isEnablePlayerParam();
        if (ImGui::Checkbox("Enable Player Param", &enablePlayerParam))
        {
            sound->setEnablePlayerParam(enablePlayerParam);
            SetUnsavedChanges(true);
        }

        if (!enablePlayerParam)
            ImGui::BeginDisabled();

        {
            u8 playerPriority = sound->getPlayerPriority();
            if (ImGui::InputScalar("Player Priority", ImGuiDataType_U8, &playerPriority, &cStepU8))
            {
                sound->setPlayerPriority(playerPriority);
                SetUnsavedChanges(true);
            }
        }

        {
            u8 actorPlayerId = sound->getActorPlayerId();
            if (ImGui::InputScalar("Actor Player Id", ImGuiDataType_U8, &actorPlayerId, &cStepU8))
            {
                sound->setActorPlayerId(actorPlayerId);
                SetUnsavedChanges(true);
            }
        }

        if (!enablePlayerParam)
            ImGui::EndDisabled();
    }

    {
        if (ImGui::CollapsingHeader("User Param"))
        {
            for (u32 i = 0; i < 4; i++)
            {
                bool enableUserParam = sound->isEnableUserParam(i);
                if (ImGui::Checkbox(sead::FormatFixedSafeString<64>("Enable User Param %u", i).cstr(), &enableUserParam))
                {
                    sound->setEnableUserParam(i, enableUserParam);
                    SetUnsavedChanges(true);
                }

                if (!enableUserParam)
                    ImGui::BeginDisabled();

                {
                    u32 userParam = sound->getUserParam(i);
                    if (ImGui::InputScalar(sead::FormatFixedSafeString<64>("User Param %u", i).cstr(), ImGuiDataType_U32, &userParam, &cStepU32))
                    {
                        sound->setUserParam(i, userParam);
                        SetUnsavedChanges(true);
                    }
                }

                if (!enableUserParam)
                    ImGui::EndDisabled();
            }

            ImGui::Separator();
        }
    }

    {
        bool isStrm = sound->getSoundType() == Sound::SoundType::Strm;

        if (isStrm)
            ImGui::BeginDisabled();

        bool enableIsFrontBypass = !isStrm ? sound->isEnableIsFrontBypass() : false;
        if (ImGui::Checkbox("Enable Front Bypass", &enableIsFrontBypass))
        {
            sound->setEnableIsFrontBypass(enableIsFrontBypass);
            SetUnsavedChanges(true);
        }

        if (isStrm)
            ImGui::EndDisabled();

        if (!enableIsFrontBypass)
            ImGui::BeginDisabled();

        {
            bool isFrontBypass = !isStrm ? sound->getIsFrontBypass() : false;
            if (ImGui::Checkbox("Front Bypass", &isFrontBypass))
            {
                sound->setIsFrontBypass(isFrontBypass);
                SetUnsavedChanges(true);
            }
        }

        if (!enableIsFrontBypass)
            ImGui::EndDisabled();

        if (isStrm)
        {
            ImGui::SameLine();
            HelpMarker("For Stream Sounds this field is specified in each Track");
        }
    }

    {
        bool enableSound3DInfo = sound->isEnableSound3DInfo();
        if (ImGui::Checkbox("Enable Sound 3D Info", &enableSound3DInfo))
        {
            sound->setEnableSound3DInfo(enableSound3DInfo);
            SetUnsavedChanges(true);
        }

        if (ImGui::CollapsingHeader("Sound 3D Info"))
        {
            if (!enableSound3DInfo)
                ImGui::BeginDisabled();

            Sound::Sound3DInfo& sound3DInfo = sound->getSound3DInfo();

            {
                u32 flags = enableSound3DInfo ? sound3DInfo.getFlags() : 0;
                //if (ImGui::InputScalar("Flags", ImGuiDataType_U32, &flags, &cStepU32))
                //{
                //    sound3DInfo.setFlags(flags);
                //}

                CenteredTextX("Flags");

                if (ImGui::CheckboxFlags("Volume", &flags, Sound::Sound3DInfo::Flags::Volume))
                {
                    sound3DInfo.setFlags(flags);
                    SetUnsavedChanges(true);
                }

                ImGui::SameLine();

                if (ImGui::CheckboxFlags("Priority", &flags, Sound::Sound3DInfo::Flags::Priority))
                {
                    sound3DInfo.setFlags(flags);
                    SetUnsavedChanges(true);
                }

                ImGui::SameLine();

                if (ImGui::CheckboxFlags("Pan", &flags, Sound::Sound3DInfo::Flags::Pan))
                {
                    sound3DInfo.setFlags(flags);
                    SetUnsavedChanges(true);
                }

                ImGui::SameLine();

                if (ImGui::CheckboxFlags("Surround Pan", &flags, Sound::Sound3DInfo::Flags::SPan))
                {
                    sound3DInfo.setFlags(flags);
                    SetUnsavedChanges(true);
                }

                ImGui::SameLine();

                if (ImGui::CheckboxFlags("Filter", &flags, Sound::Sound3DInfo::Flags::Filter))
                {
                    sound3DInfo.setFlags(flags);
                    SetUnsavedChanges(true);
                }
            }

            {
                f32 decayRatio = enableSound3DInfo ? sound3DInfo.getDecayRatio() : 0.5f;
                if (ImGui::SliderFloat("Decay Ratio", &decayRatio, 0.0f, 1.0f))
                {
                    sound3DInfo.setDecayRatio(decayRatio);
                    SetUnsavedChanges(true);
                }
            }

            {
                static const char *sCurveTypes[] = {"Logarithmic", "Linear"};

                u32 decayCurve = (enableSound3DInfo ? sound3DInfo.getDecayCurve() : Sound::Sound3DInfo::DecayCurve::Logarithmic) - 1;
                if (ComboScroll("Decay Curve", (s32 *)&decayCurve, sCurveTypes, IM_ARRAYSIZE(sCurveTypes)))
                {
                    sound3DInfo.setDecayCurve(decayCurve + 1);
                    SetUnsavedChanges(true);
                }
            }

            {
                u8 dopplerFactor = enableSound3DInfo ? sound3DInfo.getDopplerFactor() : 0;
                if (ImGui::InputScalar("Doppler Factor", ImGuiDataType_U8, &dopplerFactor, &cStepU8))
                {
                    sound3DInfo.setDopplerFactor(dopplerFactor);
                    SetUnsavedChanges(true);
                }
            }

            if (!enableSound3DInfo)
                ImGui::EndDisabled();

            ImGui::Separator();
        }
    }

    {
        static const char *sSoundTypes[] = {
            "Sequence",
            "Stream",
            "Wave"
        };

        u32 soundType = (u32)sound->getSoundType() - 1;
        if (ComboScroll("Sound Type", (s32 *)&soundType, sSoundTypes, IM_ARRAYSIZE(sSoundTypes)))
        {
            sound->setSoundType(static_cast<Sound::SoundType>(soundType + 1));
            SetUnsavedChanges(true);
        }
    }

    if (ImGui::BeginTabBar("SoundTabBar"))
    {
        bool isSeq = sound->getSoundType() == Sound::SoundType::Seq;
        bool isStrm = sound->getSoundType() == Sound::SoundType::Strm;
        bool isWave = sound->getSoundType() == Sound::SoundType::Wave;

        if (!isSeq)
            ImGui::BeginDisabled();

        if (ImGui::BeginTabItem("Sequence", nullptr, isSeq ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None))
        {
            Sound::SequenceSoundInfo& seqSoundInfo = sound->getSequenceSoundInfo();

            {
                Item* seqFile = seqSoundInfo.getSequenceFileRef().getItem();
                if (ItemSelector("Sequence File", sBfsar.getSequenceFileList(), &seqFile))
                {
                    seqSoundInfo.getSequenceFileRef().attach(seqFile);
                    SetUnsavedChanges(true);
                }

                if (!seqFile)
                {
                    ImGui::BeginDisabled();
                }

                ImGui::SameLine();
                if (ImGui::Button(ICON_LC_FILE_PEN "###EditSeq"))
                {
                    OpenFileWindow(seqFile);
                }

                if (!seqFile)
                {
                    ImGui::EndDisabled();

                    SetDisabledTooltip(messages::reference::cNoSequenceFile);
                }
            }

            //if (ImGui::CollapsingHeader("Banks"))
            {
                for (u32 i = 0; i < 4; i++)
                {
                    Item* bank = seqSoundInfo.getBankRef(i).getItem();
                    if (ItemSelector(sead::FormatFixedSafeString<16>("Bank %u", i).cstr(), sBfsar.getBankList(), &bank, true))
                    {
                        seqSoundInfo.getBankRef(i).attach(bank);
                        SetUnsavedChanges(true);
                    }

                    Item* bankFile = nullptr;
                    if (bank)
                    {
                        Bank* bankItem = static_cast<Bank*>(bank);
                        bankFile = bankItem->getFileRef().getItem();
                    }

                    if (!bankFile)
                    {
                        ImGui::BeginDisabled();
                    }

                    ImGui::SameLine();
                    if (ImGui::Button(sead::FormatFixedSafeString<32>(ICON_LC_FILE_PEN "###EditBank%u", i).cstr()))
                    {
                        OpenFileWindow(bankFile);
                    }

                    if (!bankFile)
                    {
                        ImGui::EndDisabled();

                        SetDisabledTooltip(bank ? messages::bank::cNoFileAttached : messages::reference::cNoBank);
                    }
                }

                //ImGui::Separator();
            }

            // {
            //     u32 allocateTrackFlags = seqSoundInfo.getAllocateTrackFlags();
            //     //if (ImGui::InputScalar("Allocate Track Flags", ImGuiDataType_U32, &allocateTrackFlags, &cStepU32))
            //     //{
            //     //    seqSoundInfo.setAllocateTrackFlags(allocateTrackFlags);
            //     //}

            //     CenteredTextX("Allocate Tracks");

            //     for (u32 i = 0; i < 16; i++)
            //     {
            //         if (ImGui::CheckboxFlagsT<u32>(sead::FormatFixedSafeString<8>("###%u", i).cstr(), &allocateTrackFlags, 1 << i))
            //         {
            //             seqSoundInfo.setAllocateTrackFlags(allocateTrackFlags);
            //         }

            //         ImGui::SameLine();
            //         f32 cursorPos = ImGui::GetCursorPosX();
            //         ImGui::Text("%u", i);

            //         if ((i + 1) % 8 != 0)
            //         {
            //             ImGui::SameLine();
            //             ImGui::SetCursorPosX(cursorPos + 30.0f);
            //         }
            //     }
            // }

            {
                bool enableStartOffset = seqSoundInfo.isEnableStartOffset();
                if (ImGui::Checkbox("Enable Start Offset", &enableStartOffset))
                {
                    seqSoundInfo.setEnableStartOffset(enableStartOffset);
                    SetUnsavedChanges(true);
                }

                if (!enableStartOffset)
                    ImGui::BeginDisabled();

                {
                    static const char *cStartOfFileLabel = "(Start of File)";

                    const char **labels = nullptr;
                    u32 labelCount = 0;

                    Item *seqFile = seqSoundInfo.getSequenceFileRef().getItem();
                    if (seqFile)
                    {
                        SequenceFile *seq = static_cast<SequenceFile *>(seqFile);
                        const std::vector<std::string> &labelsVec = seq->getLabels();

                        labelCount = labelsVec.size() + 1;
                        labels = new const char *[labelCount];
                        labels[0] = cStartOfFileLabel;
                        for (u32 i = 0; i < labelsVec.size(); i++)
                        {
                            labels[i + 1] = labelsVec[i].c_str();
                        }
                    }

                    sead::FixedSafeString<128> startLabel = enableStartOffset ? seqSoundInfo.getStartLabel() : sead::FixedSafeString<128>();
                    if (ImGui::InputTextCombo("Start Label", startLabel.getBuffer(), startLabel.getBufferSize(), labels, labelCount))
                    {
                        if (startLabel == cStartOfFileLabel)
                            startLabel.clear();

                        seqSoundInfo.getStartLabel() = startLabel;
                        SetUnsavedChanges(true);
                    }

                    delete[] labels;
                }

                if (!enableStartOffset)
                    ImGui::EndDisabled();

                {
                    Item* seqFile = seqSoundInfo.getSequenceFileRef().getItem();
                    sead::FixedSafeString<128> startLabel = enableStartOffset ? seqSoundInfo.getStartLabel() : sead::FixedSafeString<128>();

                    if (!seqFile)
                    {
                        ImGui::BeginDisabled();
                    }

                    ImGui::SameLine();
                    if (ImGui::Button(ICON_LC_EXTERNAL_LINK "###GoSeq"))
                    {
                        SequenceFile* seq = static_cast<SequenceFile*>(seqFile);

                        if (seq->getLabelOffset(startLabel) != SequenceFile::cInvaldOffset)
                        {
                            for (u32 i = 0; i < 4; i++)
                            {
                                seq->getBankRef_(i)->attach(seqSoundInfo.getBankRef(i).getItem());
                                SetUnsavedChanges(true);
                            }

                            seq->getStartLabel_() = startLabel;

                            OpenFileWindow(seqFile);

                            seq->setCursorToLabel_(startLabel);
                        }
                        else
                        {
                            PopupMgr::instance()->addPopup({ sead::FormatFixedSafeString<128>("Couldn't find start label in Sequence File\n'%s'", startLabel.cstr()).cstr(), nullptr });
                        }

                    }

                    if (!seqFile)
                    {
                        ImGui::EndDisabled();

                        SetDisabledTooltip(messages::reference::cNoSequenceFile);
                    }
                    else if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNone))
                    {
                        ImGui::SetTooltip("%s", messages::sequence::cGoToLabelHint);
                    }
                }
            }

            {
                bool enablePriority = seqSoundInfo.isEnablePriority();
                if (ImGui::Checkbox("Enable Priority", &enablePriority))
                {
                    seqSoundInfo.setEnablePriority(enablePriority);
                    SetUnsavedChanges(true);
                }

                if (!enablePriority)
                    ImGui::BeginDisabled();

                {
                    u8 channelPriority = seqSoundInfo.getChannelPriority();
                    if (ImGui::InputScalar("Channel Priority", ImGuiDataType_U8, &channelPriority, &cStepU8))
                    {
                        seqSoundInfo.setChannelPriority(channelPriority);
                        SetUnsavedChanges(true);
                    }
                }

                {
                    bool isReleasePriorityFix = seqSoundInfo.getIsReleasePriorityFix();
                    if (ImGui::Checkbox("Fix Priority At Release", &isReleasePriorityFix))
                    {
                        seqSoundInfo.setIsReleasePriorityFix(isReleasePriorityFix);
                        SetUnsavedChanges(true);
                    }
                }

                if (!enablePriority)
                    ImGui::EndDisabled();
            }

            ImGui::EndTabItem();
        }

        if (!isSeq)
        {
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayNone) && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                sound->setSoundType(Sound::SoundType::Seq);
                SetUnsavedChanges(true);
            }
        }

        if (!isStrm)
            ImGui::BeginDisabled();

        if (ImGui::BeginTabItem("Stream", nullptr, isStrm ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None))
        {
            Sound::StreamSoundInfo& strmSoundInfo = sound->getStreamSoundInfo();

            DrawStreamPathUI(sound, strmSoundInfo);

            // {
            //     u32 allocateTrackFlags = strmSoundInfo.getAllocateTrackFlags();
            //     //if (ImGui::InputScalar("Allocate Track Flags", ImGuiDataType_U16, &allocateTrackFlags, &cStepU16))
            //     //{
            //     //    strmSoundInfo.setAllocateTrackFlags(allocateTrackFlags);
            //     //}

            //     CenteredTextX("Allocate Tracks");

            //     for (u32 i = 0; i < 8; i++)
            //     {
            //         if (ImGui::CheckboxFlagsT<u32>(sead::FormatFixedSafeString<8>("###%u", i).cstr(), &allocateTrackFlags, 1 << i))
            //         {
            //             strmSoundInfo.setAllocateTrackFlags(allocateTrackFlags);
            //         }

            //         ImGui::SameLine();
            //         f32 cursorPos = ImGui::GetCursorPosX();
            //         ImGui::Text("%u", i);

            //         if ((i + 1) % 8 != 0)
            //         {
            //             ImGui::SameLine();
            //             ImGui::SetCursorPosX(cursorPos + 30.0f);
            //         }
            //     }
            // }

            // {
            //     u16 allocateChannelCount = strmSoundInfo.getAllocateChannelCount();
            //     if (ImGui::InputScalar("Allocate Channel Count", ImGuiDataType_U16, &allocateChannelCount, &cStepU16))
            //     {
            //         strmSoundInfo.setAllocateChannelCount(allocateChannelCount);
            //     }
            // }

            {
                u32 sampleRate = 0;

                const Sound::StreamSoundInfo::Track::List& tracks = strmSoundInfo.getTrackList();
                
                for (s32 i = 0; i < tracks.size(); i++)
                {
                    const auto& track = *static_cast<const Sound::StreamSoundInfo::Track*>(tracks.nth(i)->val());
                    if (track.getWaveFileRef().isAttached())
                    {
                        const WaveFile& wf = *static_cast<const WaveFile*>(track.getWaveFileRef().getItem());
                        sampleRate = wf.getSampleRate();
                        break;
                    }
                }

                if (sampleRate != 0)
                    ImGui::Text("Sample Rate: %u Hz", sampleRate);
            }

            {
                bool enableSend = sBfsar.isStreamSendAvailable();

                if (!enableSend)
                    ImGui::BeginDisabled();

                {
                    f32 pitch = enableSend ? strmSoundInfo.getPitch() : 1.0f;

                    if (ImGui::SliderFloat("Pitch", &pitch, 0.0f, 8.0f))
                    {
                        strmSoundInfo.setPitch(pitch);
                        SetUnsavedChanges(true);
                    }
                }

                DrawMainAndFxSendUI(
                    [&] { return enableSend ? strmSoundInfo.getMainSend() : (u8)127; },
                    [&](u8 v) { strmSoundInfo.setMainSend(v); },
                    [&](u32 i) { return enableSend ? strmSoundInfo.getFxSend(i) : (u8)0; },
                    [&](u32 i, u8 v) { strmSoundInfo.setFxSend(i, v); }
                );

                bool enableStreamSoundExtension = enableSend ? strmSoundInfo.isEnableStreamSoundExtension() : false;

                if (ImGui::Checkbox("Enable Stream Sound Extension", &enableStreamSoundExtension))
                {
                    strmSoundInfo.setEnableStreamSoundExtension(enableStreamSoundExtension);
                    SetUnsavedChanges(true);
                }

                if (!enableSend)
                    ImGui::EndDisabled();

                if (ImGui::CollapsingHeader("Stream Sound Extension"))
                {
                    bool enableSoundExt = enableSend && enableStreamSoundExtension;

                    if (!enableSoundExt)
                        ImGui::BeginDisabled();

                    {
                        const char *streamFmt = GetInnerFileDisplayName(sBfsar.getFormat(), InnerFileKind::Stream);
                        const char *streamTypeLabels[] = {streamFmt, messages::stream::cTypeAdts, messages::stream::cTypeOpus};

                        u32 streamType = (enableSend ? strmSoundInfo.getStreamType() : Sound::StreamSoundInfo::StreamType::NwStreamBinary) - 1;
                        u32 streamTypeCount = sBfsar.isV3Bfsar() || streamType + 1 == Sound::StreamSoundInfo::StreamType::Opus ? IM_ARRAYSIZE(streamTypeLabels) : IM_ARRAYSIZE(streamTypeLabels) - 1;

                        if (ComboScroll("Stream Type", (s32 *)&streamType, streamTypeLabels, streamTypeCount))
                        {
                            strmSoundInfo.setStreamType(static_cast<Sound::StreamSoundInfo::StreamType>(streamType + 1));
                            SetUnsavedChanges(true);
                        }
                    }

                    {
                        bool isLoop = enableSend ? strmSoundInfo.getIsLoop() : false;
                        if (ImGui::Checkbox("Is Loop", &isLoop))
                        {
                            strmSoundInfo.setIsLoop(isLoop);
                            SetUnsavedChanges(true);
                        }

                        if (strmSoundInfo.getStreamType() == Sound::StreamSoundInfo::StreamType::NwStreamBinary)
                        {
                            ImGui::SameLine();

                            if (!enableSoundExt)
                            {
                                ImGui::EndDisabled();
                            }

                            const char* loopHelpFmt = GetInnerFileDisplayName(sBfsar.getFormat(), InnerFileKind::Stream);
                            sead::FormatFixedSafeString<128> helpMsg(
                                "Note: For %s Streams this looping info is ignored\n"
                                "and instead is taken from the first Track attached Wave File",
                                loopHelpFmt
                            );
                            HelpMarker(helpMsg.cstr());

                            if (!enableSoundExt)
                            {
                                ImGui::BeginDisabled();
                            }
                        }
                    }

                    {
                        u32 loopStartFrame = enableSend ? strmSoundInfo.getLoopStartFrame() : 0;
                        if (ImGui::InputScalar("Loop Start Sample", ImGuiDataType_U32, &loopStartFrame, &cStepU32))
                        {
                            strmSoundInfo.setLoopStartFrame(loopStartFrame);
                            SetUnsavedChanges(true);
                        }
                    }

                    {
                        u32 loopEndFrame = enableSend ? strmSoundInfo.getLoopEndFrame() : 0;
                        if (ImGui::InputScalar("Loop End Sample", ImGuiDataType_U32, &loopEndFrame, &cStepU32))
                        {
                            strmSoundInfo.setLoopEndFrame(loopEndFrame);
                            SetUnsavedChanges(true);
                        }
                    }

                    if (sBfsar.isV3Bfsar())
                    {
                        u16 typeInfoUpper = enableSend ? strmSoundInfo.getStreamTypeInfoUpper() : (u16)0;

                        if (ImGui::InputScalar("Unknown (Type Info)", ImGuiDataType_U16, &typeInfoUpper, nullptr, nullptr, "%04X", ImGuiInputTextFlags_CharsHexadecimal))
                        {
                            strmSoundInfo.setStreamTypeInfoUpper(typeInfoUpper);
                            SetUnsavedChanges(true);
                        }
                    }

                    if (!enableSoundExt)
                        ImGui::EndDisabled();

                    ImGui::Separator();
                }
            }

            {
                bool enablePrefetch = sBfsar.isStreamPrefetchAvailable();

                if (!enablePrefetch)
                    ImGui::BeginDisabled();

                {
                    // Item* prefetchFile = enablePrefetch ? strmSoundInfo.getPrefetchFileRef().getItem() : nullptr;
                    // if (ItemSelector("Prefetch File", sBfsar.getFileList(), &prefetchFile, true))
                    // {
                    //     strmSoundInfo.getPrefetchFileRef().attach(prefetchFile);
                    // }
                }

                if (!enablePrefetch)
                    ImGui::EndDisabled();
            }

            ImGui::EndTabItem();
        }

        if (!isStrm)
        {
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayNone) && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                sound->setSoundType(Sound::SoundType::Strm);
                SetUnsavedChanges(true);
            }
        }

        if (!isWave)
            ImGui::BeginDisabled();

        if (ImGui::BeginTabItem("Wave", nullptr, isWave ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None))
        {
            Sound::WaveSoundInfo& waveSoundInfo = sound->getWaveSoundInfo();

            {
                // u32 index = waveSoundInfo.getIndex();
                // if (ImGui::InputScalar("Index", ImGuiDataType_U32, &index, &cStepU32))
                // {
                //     waveSoundInfo.setIndex(index);
                // }
            }

            {
                Item* waveFile = waveSoundInfo.getWaveFileRef().getItem();
                if (ItemSelector("Wave File", sBfsar.getWaveFileList(), &waveFile))
                {
                    waveSoundInfo.getWaveFileRef().attach(waveFile);
                    SetUnsavedChanges(true);
                }

                if (!waveFile)
                {
                    ImGui::BeginDisabled();
                }

                ImGui::SameLine();
                if (ImGui::Button(ICON_LC_EXTERNAL_LINK "###GoWave"))
                {
                    SelectItem(waveFile);
                }

                if (!waveFile)
                {
                    ImGui::EndDisabled();

                    SetDisabledTooltip(messages::reference::cNoWaveFile);
                }
            }

            {
                // u32 allocateTrackCount = waveSoundInfo.getAllocateTrackCount();
                // if (ImGui::InputScalar("Allocate Track Count", ImGuiDataType_U32, &allocateTrackCount, &cStepU32))
                // {
                //     waveSoundInfo.setAllocateTrackCount(allocateTrackCount);
                // }
            }

            {
                bool enablePriority = waveSoundInfo.isEnablePriority();
                if (ImGui::Checkbox("Enable Priority", &enablePriority))
                {
                    waveSoundInfo.setEnablePriority(enablePriority);
                    SetUnsavedChanges(true);
                }

                if (!enablePriority)
                    ImGui::BeginDisabled();

                {
                    u8 channelPriority = waveSoundInfo.getChannelPriority();
                    if (ImGui::InputScalar("Channel Priority", ImGuiDataType_U8, &channelPriority, &cStepU8))
                    {
                        waveSoundInfo.setChannelPriority(channelPriority);
                        SetUnsavedChanges(true);
                    }
                }

                {
                    bool isReleasePriorityFix = waveSoundInfo.getIsReleasePriorityFix();
                    if (ImGui::Checkbox("Fix Priority At Release", &isReleasePriorityFix))
                    {
                        waveSoundInfo.setIsReleasePriorityFix(isReleasePriorityFix);
                        SetUnsavedChanges(true);
                    }
                }

                if (!enablePriority)
                    ImGui::EndDisabled();
            }

            {
                bool enablePan = waveSoundInfo.isEnablePan();
                if (ImGui::Checkbox("Enable Pan", &enablePan))
                {
                    waveSoundInfo.setEnablePan(enablePan);
                    SetUnsavedChanges(true);
                }

                if (!enablePan)
                    ImGui::BeginDisabled();

                {
                    u8 pan = waveSoundInfo.getPan();
                    if (ImGui::SliderScalar(sead::FormatFixedSafeString<32>("Pan (%s)###pan", FormatPanLabel(pan, cPanMin, 64, cPanMax).cstr()).cstr(), ImGuiDataType_U8, &pan, &cPanMin, &cPanMax))
                    {
                        waveSoundInfo.setPan(pan);
                        SetUnsavedChanges(true);
                    }
                }

                {
                    s8 surroundPan = waveSoundInfo.getSurroundPan();
                    if (ImGui::InputScalar("Surround Pan", ImGuiDataType_S8, &surroundPan, &cStepS8))
                    {
                        waveSoundInfo.setSurroundPan(surroundPan);
                        SetUnsavedChanges(true);
                    }
                }

                if (!enablePan)
                    ImGui::EndDisabled();
            }

            {
                bool enablePitch = waveSoundInfo.isEnablePitch();
                if (ImGui::Checkbox("Enable Pitch", &enablePitch))
                {
                    waveSoundInfo.setEnablePitch(enablePitch);
                    SetUnsavedChanges(true);
                }

                if (!enablePitch)
                    ImGui::BeginDisabled();

                {
                    f32 pitch = waveSoundInfo.getPitch();
                    if (ImGui::SliderFloat("Pitch", &pitch, 0.0f, 8.0f))
                    {
                        waveSoundInfo.setPitch(pitch);
                        SetUnsavedChanges(true);
                    }
                }

                if (!enablePitch)
                    ImGui::EndDisabled();
            }

            {
                bool enableSend = waveSoundInfo.isEnableSend();

                if (ImGui::Checkbox("Enable Send", &enableSend))
                {
                    waveSoundInfo.setEnableSend(enableSend);
                    SetUnsavedChanges(true);
                }

                if (!enableSend)
                    ImGui::BeginDisabled();

                DrawMainAndFxSendUI(
                    [&] { return waveSoundInfo.getMainSend(); },
                    [&](u8 v) { waveSoundInfo.setMainSend(v); },
                    [&](u32 i) { return waveSoundInfo.getFxSend(i); },
                    [&](u32 i, u8 v) { waveSoundInfo.setFxSend(i, v); }
                );

                if (!enableSend)
                    ImGui::EndDisabled();
            }

            {
                bool enableEnvelope = waveSoundInfo.isEnableEnvelope();
                if (ImGui::Checkbox("Enable Envelope", &enableEnvelope))
                {
                    waveSoundInfo.setEnableEnvelope(enableEnvelope);
                    SetUnsavedChanges(true);
                }

                if (!enableEnvelope)
                    ImGui::BeginDisabled();

                {
                    snd::AdshrCurve adshrCurve = waveSoundInfo.getAdshrCurve();
                    static const ImU8 cAdsrMin = 0;
                    static const ImU8 cAdsrMax = 127;

                    bool edited = false;
                    if (ImGui::SliderScalar("Attack", ImGuiDataType_U8, &adshrCurve.attack, &cAdsrMin, &cAdsrMax))
                    {
                        edited = true;
                    }

                    if (ImGui::SliderScalar("Decay", ImGuiDataType_U8, &adshrCurve.decay, &cAdsrMin, &cAdsrMax))
                    {
                        edited = true;
                    }

                    if (ImGui::SliderScalar("Sustain", ImGuiDataType_U8, &adshrCurve.sustain, &cAdsrMin, &cAdsrMax))
                    {
                        edited = true;
                    }

                    if (ImGui::SliderScalar("Hold", ImGuiDataType_U8, &adshrCurve.hold, &cAdsrMin, &cAdsrMax))
                    {
                        edited = true;
                    }

                    if (ImGui::SliderScalar("Release", ImGuiDataType_U8, &adshrCurve.release, &cAdsrMin, &cAdsrMax))
                    {
                        edited = true;
                    }

                    if (edited)
                    {
                        waveSoundInfo.setAdshrCurve(adshrCurve);
                        SetUnsavedChanges(true);
                    }
                }

                if (!enableEnvelope)
                    ImGui::EndDisabled();
            }

            {
                bool filterVersionEnable = BfwsdFile::isFilterSupportedVersion(sBfsar.getVersionForBfwsd(), sBfsar.getFormat());
                if (!filterVersionEnable)
                    ImGui::BeginDisabled();

                bool enableFilter = filterVersionEnable ? waveSoundInfo.isEnableFilter() : false;
                if (ImGui::Checkbox("Enable Filter", &enableFilter))
                {
                    waveSoundInfo.setEnableFilter(enableFilter);
                    SetUnsavedChanges(true);
                }

                if (!enableFilter)
                    ImGui::BeginDisabled();

                {
                    u8 lpfFreq = filterVersionEnable ? waveSoundInfo.getLpfFreq() : 64;
                    if (ImGui::InputScalar(sead::FormatFixedSafeString<64>("LPF Frequency (%.3f)###lfreq", (static_cast<f32>(lpfFreq) / 64.0f) - 1.0f).cstr(), ImGuiDataType_U8, &lpfFreq, &cStepU8))
                    {
                        waveSoundInfo.setLpfFreq(lpfFreq);
                        SetUnsavedChanges(true);
                    }
                }

                {
                    u8 biquadType = filterVersionEnable ? waveSoundInfo.getBiquadType() : 0;
                    if (ImGui::InputScalar("Biquad Type", ImGuiDataType_U8, &biquadType, &cStepU8)) // TODO: Combo ?
                    {
                        waveSoundInfo.setBiquadType(biquadType);
                        SetUnsavedChanges(true);
                    }
                }

                {
                    u8 biquadValue = filterVersionEnable ? waveSoundInfo.getBiquadValue() : 0;
                    if (ImGui::InputScalar(sead::FormatFixedSafeString<64>("Biquad Value (%.3f)###bival", static_cast<f32>(biquadValue) / 127.0f).cstr(), ImGuiDataType_U8, &biquadValue, &cStepU8))
                    {
                        waveSoundInfo.setBiquadValue(biquadValue);
                        SetUnsavedChanges(true);
                    }
                }

                if (!enableFilter)
                    ImGui::EndDisabled();

                if (!filterVersionEnable)
                    ImGui::EndDisabled();
            }

            {
                std::vector<std::pair<u32, u32>> &extraOptions = waveSoundInfo.getExtraOptions();

                if (!extraOptions.empty() && ImGui::CollapsingHeader("Unknown Options"))
                {
                    for (std::pair<u32, u32> &option : extraOptions)
                    {
                        u32 value = option.second;
                        if (ImGui::InputScalar(sead::FormatFixedSafeString<64>("Option Bit %u", option.first).cstr(), ImGuiDataType_U32, &value, nullptr, nullptr, "%08X", ImGuiInputTextFlags_CharsHexadecimal))
                        {
                            option.second = value;
                            SetUnsavedChanges(true);
                        }
                    }

                    ImGui::Separator();
                }
            }

            ImGui::EndTabItem();
        }

        if (!isWave)
        {
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayNone) && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                sound->setSoundType(Sound::SoundType::Wave);
                SetUnsavedChanges(true);
            }
        }

        ImGui::EndTabBar();
    }
}

void Sound::StreamSoundInfo::Track::drawUI()
{
    {
        Item* waveFile = getWaveFileRef().getItem();
        if (ItemSelector("Wave File", sBfsar.getWaveFileList(), &waveFile))
        {
            getWaveFileRef().attach(waveFile);
            SetUnsavedChanges(true);
        }

        if (!waveFile)
        {
            ImGui::BeginDisabled();
        }

        ImGui::SameLine();
        if (ImGui::Button(ICON_LC_EXTERNAL_LINK "###GoWave"))
        {
            SelectItem(waveFile);
        }

        if (!waveFile)
        {
            ImGui::EndDisabled();

            SetDisabledTooltip(messages::reference::cNoWaveFile);
        }
    }

    {
        u8 volume = getVolume();
        if (ImGui::SliderScalar(sead::FormatFixedSafeString<32>("Volume (%.3f)###vol", static_cast<f32>(volume) / 127.0f).cstr(), ImGuiDataType_U8, &volume, &cVolumeMin, &cVolumeMax))
        {
            setVolume(volume);
            sSoundPlayer.refreshStreamTrackVolume(*this);
            SetUnsavedChanges(true);
        }
    }

    {
        u8 pan = getPan();
        if (ImGui::SliderScalar(sead::FormatFixedSafeString<32>("Pan (%s)###pan", FormatPanLabel(pan, cPanMin, 64, cPanMax).cstr()).cstr(), ImGuiDataType_U8, &pan, &cPanMin, &cPanMax))
        {
            setPan(pan);
            SetUnsavedChanges(true);
        }
    }

    {
        u8 span = getSPan();
        if (ImGui::InputScalar("Surround Pan", ImGuiDataType_U8, &span, &cStepU8))
        {
            setSPan(span);
            SetUnsavedChanges(true);
        }
    }

    {
        bool flags = getFlags();
        if (ImGui::Checkbox("Front Bypass", &flags))
        {
            setFlags(flags);
            SetUnsavedChanges(true);
        }
    }

    {
        bool enableSend = sBfsar.isStreamSendAvailable();

        if (!enableSend)
            ImGui::BeginDisabled();

        DrawMainAndFxSendUI(
            [&] { return enableSend ? getMainSend() : (u8)127; },
            [&](u8 v) { setMainSend(v); },
            [&](u32 i) { return enableSend ? getFxSend(i) : (u8)0; },
            [&](u32 i, u8 v) { setFxSend(i, v); }
        );

        if (!enableSend)
            ImGui::EndDisabled();
    }

    {
        bool enableFilter = sBfsar.isFilterSupportedVersion();
        if (!enableFilter)
        {
            ImGui::BeginDisabled();
        }

        {
            u8 lpfFreq = enableFilter ? getLpfFreq() : 64;
            if (ImGui::InputScalar(sead::FormatFixedSafeString<64>("LPF Frequency (%.3f)###lfreq", (static_cast<f32>(lpfFreq) / 64.0f) - 1.0f).cstr(), ImGuiDataType_U8, &lpfFreq, &cStepU8))
            {
                setLpfFreq(lpfFreq);
                SetUnsavedChanges(true);
            }
        }

        {
            u8 biquadType = enableFilter ? getBiquadType() : 0;
            if (ImGui::InputScalar("Biquad Type", ImGuiDataType_U8, &biquadType, &cStepU8)) // TODO: Combo ?
            {
                setBiquadType(biquadType);
                SetUnsavedChanges(true);
            }
        }

        {
            u8 biquadValue = enableFilter ? getBiquadValue() : 0;
            if (ImGui::InputScalar(sead::FormatFixedSafeString<64>("Biquad Value (%.3f)###bival", static_cast<f32>(biquadValue) / 127.0f).cstr(), ImGuiDataType_U8, &biquadValue, &cStepU8))
            {
                setBiquadValue(biquadValue);
                SetUnsavedChanges(true);
            }
        }

        if (!enableFilter)
        {
            ImGui::EndDisabled();
        }
    }
}
