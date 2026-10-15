#pragma once

#include <ui/app/UI.h>
#include <ui/settings/UiColors.h>

#include <vector>

inline constexpr s32 cTrackBypassIndex   = 3;
inline constexpr f32 cTrackCellPadXEm    = 0.4f;
inline constexpr f32 cTrackSilentAlpha   = 0.55f;

inline constexpr uicolors::Themed<f32> cTrackSelectedAlpha = { 0.10f, 0.06f };
inline constexpr uicolors::Themed<f32> cTrackIndexAlpha    = { 0.45f, 0.18f };

inline constexpr u32 cTrackMixLabelCount    = 4;
inline constexpr u32 cTrackSendLabelCount   = 4;
inline constexpr u32 cTrackFilterLabelCount = 3;

extern const char* const cTrackMixLabels[cTrackMixLabelCount];
extern const char* const cTrackSendLabels[cTrackSendLabelCount];
extern const char* const cTrackFilterLabels[cTrackFilterLabelCount];
extern const char* const cTrackWaveLabel;

enum class TrackParam
{
    Mute,
    Solo,
    WaveFile,
    Volume,
    Pan,
    SurroundPan,
    FrontBypass,
    MainSend,
    FxSend0,
    FxSend1,
    FxSend2,
    Lpf,
    BiquadType,
    BiquadValue
};

inline constexpr TrackParam cTrackMixParams[]    = { TrackParam::Volume, TrackParam::Pan, TrackParam::SurroundPan, TrackParam::FrontBypass };
inline constexpr TrackParam cTrackSendParams[]   = { TrackParam::MainSend, TrackParam::FxSend0, TrackParam::FxSend1, TrackParam::FxSend2 };
inline constexpr TrackParam cTrackFilterParams[] = { TrackParam::Lpf, TrackParam::BiquadType, TrackParam::BiquadValue };

struct TrackContext
{
    Sound* sound;
    std::vector<Sound::StreamSoundInfo::Track*> tracks;
    bool enableSend;
    bool enableFilter;
    bool canAdd;
    f32 biquadWidth;
};

struct TrackEdit
{
    bool add = false;
    bool toggleView = false;
    Sound::StreamSoundInfo::Track* insertAfter = nullptr;
    Sound::StreamSoundInfo::Track* deleteTrack = nullptr;
    Sound::StreamSoundInfo::Track* moveTrack = nullptr;
    Sound::StreamSoundInfo::Track* moveTarget = nullptr;
};

struct TrackSlots
{
    static constexpr s32 cSlotMax = 4;

    const char* const* labels;
    f32 widths[cSlotMax];
    s32 num;
    ImVec2 origin;
    f32 offset;
    s32 index;

    f32 calcWidth() const;
    void begin();
    const char* next(const ImVec2& itemSize, f32 rowHeight);
    void drawLabels() const;
};

struct TrackTableLayout
{
    static constexpr s32 cGroupMax = 4;

    TrackSlots muteSlots;
    TrackSlots soloSlots;
    TrackSlots mixSlots;
    TrackSlots bypassSlots;
    TrackSlots sendSlots;
    TrackSlots filterSlots;
    ImVec2 cellPadding;
    s32 columnNum;
    f32 indexWidth;
    f32 muteWidth;
    f32 soloWidth;
    f32 mixWidth;
    f32 bypassWidth;
    f32 sendWidth;
    f32 filterWidth;
    f32 columnExtra;
    f32 leadWidth;
    f32 groupWidths[cGroupMax];
    s32 groupNum;
    f32 fixedWidth;
};

Item* GetHighlightedStreamTrack();

f32 CalcTrackKnobSize();
TrackSlots MakeTrackSlots(const char* const* labels, s32 labelNum);
TrackTableLayout CalcTrackTableLayout(const TrackContext& ctx);

ImVec2 CalcTrackParamSize(const TrackContext& ctx, TrackParam param, f32 width);
void DrawTrackParam(const TrackContext& ctx, s32 index, TrackParam param, f32 width);
void DrawTrackSlotGroup(const TrackContext& ctx, s32 index, TrackSlots& slots, const TrackParam* params, f32 rowHeight);
bool DrawTrackToggle(const char* label, bool on, const ImVec4& color, const ImVec2& size);
void DrawTrackWaveSelector(Sound::StreamSoundInfo::Track* track, f32 width, bool compact);

bool DrawTrackHandle(const TrackContext& ctx, Sound::StreamSoundInfo::Track* track, const ImVec2& size, ImGuiSelectableFlags flags, TrackEdit* edit);
void DrawTrackListMenu(const TrackContext& ctx, TrackEdit* edit);

void DrawTrackTable(const TrackContext& ctx, TrackEdit* edit);
void DrawTrackStrips(const TrackContext& ctx, TrackEdit* edit);
