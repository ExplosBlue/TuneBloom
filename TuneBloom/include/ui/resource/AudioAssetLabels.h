#pragma once

#include <aal/Amta.h>
#include <ui/wave/LoopWaveformEditor.h>

#include <basis/seadTypes.h>
#include <prim/seadSafeString.h>

#include <vector>

class AudioAsset;

struct AssetLabelView
{
    const AudioAsset* asset = nullptr;
    std::vector<u8> meta;
    aal::AmtaLabels labels;
    const char* editReason = nullptr;
    const char* labelEditReason = nullptr;
    bool isReadable = false;
    bool hasTags = false;
    bool canAddKeys = false;
    bool isCueMoved = false;
    s32 selectedMarker = -1;
    s32 renameKey = -1;
    sead::FixedSafeString<256> tagsText;
    std::vector<sead::FixedSafeString<256>> keyTexts;
};

void RefreshAssetLabels(AssetLabelView* view, AudioAsset* asset);
void SetAssetLabelCues(const AssetLabelView& view, ImGui::LoopWaveformState* waveform);
void ApplyAssetLabelCues(AssetLabelView* view, AudioAsset* asset, const ImGui::LoopWaveformState& waveform);
void DrawAssetMarkersBox(AssetLabelView* view, AudioAsset* asset, u32 sampleCount, u32 sampleRate, u32 addFrame);
void DrawAssetTagsRow(AssetLabelView* view, AudioAsset* asset);
void DrawAssetMetadataTitle(AssetLabelView* view, AudioAsset* asset);
void DrawAssetKeyRows(AssetLabelView* view, AudioAsset* asset);
