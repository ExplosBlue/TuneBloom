#pragma once

#include <ui/app/UI.h>

#include <vector>

inline constexpr const char* cTrackMenuId = "TrackMenu";

inline constexpr s32 cTrackBypassIndex = 3;
inline constexpr f32 cTrackCellPadXEm  = 0.4f;

inline constexpr u32 cTrackMixLabelCount    = 4;
inline constexpr u32 cTrackSendLabelCount   = 4;
inline constexpr u32 cTrackFilterLabelCount = 3;

extern const char* const cTrackMixLabels[cTrackMixLabelCount];
extern const char* const cTrackSendLabels[cTrackSendLabelCount];
extern const char* const cTrackFilterLabels[cTrackFilterLabelCount];
extern const char* const cTrackWaveLabel;

enum class TrackParam
{
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
    StreamSource source;
    std::vector<Sound::StreamSoundInfo::Track*> tracks;
    f32 biquadWidth;
};

struct TrackEdit
{
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

enum class TrackGroup : u8
{
    Mix,
    Bypass,
    Send,
    Filter
};

struct TrackTableLayout
{
    static constexpr s32 cGroupMax = 4;

    TrackSlots mixSlots;
    TrackSlots bypassSlots;
    TrackSlots sendSlots;
    TrackSlots filterSlots;
    ImVec2 cellPadding;
    s32 columnNum;
    s32 leadNum;
    f32 indexWidth;
    f32 channelWidth;
    f32 menuWidth;
    f32 columnExtra;
    f32 leadWidth;
    TrackGroup groups[cGroupMax];
    f32 groupWidths[cGroupMax];
    s32 groupNum;
    f32 fixedWidth;
};

Item* GetHighlightedStreamTrack();

TrackContext MakeTrackContext(const StreamSource& source);
void DrawTrackLane(const TrackContext& ctx, s32 index, f32 height);
void DrawTrackLaneRuler(const TrackContext& ctx);
f32 CalcStreamPlayRatio(const StreamSource& source, u32 sampleCount);

f32 CalcTrackKnobSize();
TrackTableLayout CalcTrackTableLayout(const TrackContext& ctx);

void DrawTrackSlotGroup(const TrackContext& ctx, s32 index, TrackSlots& slots, const TrackParam* params, f32 rowHeight);
void DrawTrackWaveSelector(const TrackContext& ctx, Sound::StreamSoundInfo::Track* track, f32 width);

bool DrawTrackHandle(const TrackContext& ctx, Sound::StreamSoundInfo::Track* track, f32 height, TrackEdit* edit);
void DrawTrackListMenu(const TrackContext& ctx);

void DrawTrackTable(const TrackContext& ctx, TrackEdit* edit);
f32 CalcTrackTableFitHeight(const TrackContext& ctx);
