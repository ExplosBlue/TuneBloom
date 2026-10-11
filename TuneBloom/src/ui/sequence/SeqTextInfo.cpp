#include <ui/sequence/SeqTextInfo.h>

#include <snd/SoundThread.h>

#include <ui/Messages.h>

#include <imgui/imgui.h>

#include <format>

const u32 SeqTextInfo::Track::HistoryLine::cMaxLife = 40;

void SeqTextInfo::reset()
{
    for (u32 i = 0; i < SequenceSoundPlayer::cTrackNumPerPlayer; i++)
    {
        mTracks[i].active = false;
        mTracks[i].updated = false;
        mTracks[i].line = 0;
        mTracks[i].offset = 0;
        mTracks[i].historyLines.clear();
    }
}

void SeqTextInfo::update(const SequenceSoundPlayer& player, TextEditor& editor, const std::unordered_map<u32, u32>& offsetToLine, bool showTrail)
{
    snd::internal::driver::SoundThreadLock lock;

    auto findLine = [&](u32 offset) -> s32
    {
        auto it = offsetToLine.find(offset);
        return it != offsetToLine.end() ? static_cast<s32>(it->second) : 0;
    };

    for (u32 i = 0; i < SequenceSoundPlayer::cTrackNumPerPlayer; i++)
    {
        const SequenceTrack& track = player.getTrack_(i);
        const SequenceTrack::ParserTrackParam& param = track.getParserTrackParam();

        mTracks[i].active = track.isOpened() && param.baseAddr && param.currentCmdAddr;

        for (Track::HistoryLine& historyLine : mTracks[i].historyLines)
        {
            if (historyLine.life > 0)
                historyLine.line = findLine(historyLine.offset);
        }

        if (!mTracks[i].active)
        {
            mTracks[i].updated = false;
            continue;
        }

        mTracks[i].offset = static_cast<u32>(param.currentCmdAddr - param.baseAddr);

        s32 line = findLine(mTracks[i].offset);
        mTracks[i].updated = line != mTracks[i].line;
        mTracks[i].line = line;
    }

    static const ImU32 sTrackColors[SequenceSoundPlayer::cTrackNumPerPlayer] = {
        IM_COL32(7,   83, 175, 0.8f * 255),
        IM_COL32(67,  78, 184, 0.8f * 255),
        IM_COL32(88,  71, 190, 0.8f * 255),
        IM_COL32(103, 65, 195, 0.8f * 255),
        IM_COL32(116, 58, 201, 0.8f * 255),
        IM_COL32(125, 51, 205, 0.8f * 255),
        IM_COL32(143, 37, 214, 0.8f * 255),
        IM_COL32(153, 23, 219, 0.8f * 255),
        IM_COL32(166, 7,  221, 0.8f * 255),
        IM_COL32(180, 14, 210, 0.8f * 255),
        IM_COL32(189, 18, 197, 0.8f * 255),
        IM_COL32(197, 21, 188, 0.8f * 255),
        IM_COL32(206, 24, 179, 0.8f * 255),
        IM_COL32(214, 25, 168, 0.8f * 255),
        IM_COL32(230, 28, 142, 0.8f * 255),
        IM_COL32(246, 31, 111, 0.8f * 255),
    };
    
    auto mixColor = [](ImU32 color1, ImU32 color2)
    {
        ImVec4 c1 = ImGui::ColorConvertU32ToFloat4(color1);
        ImVec4 c2 = ImGui::ColorConvertU32ToFloat4(color2);
        ImVec4 mixed = {
            (c1.x + c2.x) * 0.5f,
            (c1.y + c2.y) * 0.5f,
            (c1.z + c2.z) * 0.5f,
            (c1.w + c2.w) * 0.5f,
        };
        return ImGui::ColorConvertFloat4ToU32(mixed);
    };

    TextEditor::Markers markers;
    for (u32 i = 0; i < SequenceSoundPlayer::cTrackNumPerPlayer; i++)
    {
        Track& track = mTracks[i];
        if (!track.active || track.line <= 0)
        {
            continue;
        }

        auto it = markers.find(track.line);
        if (it == markers.end())
        {
            markers[track.line] = { std::format(messages::sequence::cTrackMarkerFormat, i), sTrackColors[i], false, true };
        }
        else
        {
            it->second.mDetail += std::format(messages::sequence::cTrackMarkerNextFormat, i);
            it->second.mColor = mixColor(it->second.mColor, sTrackColors[i]);
        }
    }

    for (u32 i = 0; i < SequenceSoundPlayer::cTrackNumPerPlayer; i++)
    {
        Track& track = mTracks[i];

        for (Track::HistoryLine& historyLine : track.historyLines)
        {
            if (historyLine.life > 0)
            {
                if (showTrail && historyLine.line > 0)
                {
                    auto it = markers.find(historyLine.line);
                    if (it == markers.end())
                    {
                        ImVec4 color = ImGui::ColorConvertU32ToFloat4(sTrackColors[i]);
                        color.w *= f32(historyLine.life) / Track::HistoryLine::cMaxLife;

                        markers[historyLine.line] = { "", ImGui::ColorConvertFloat4ToU32(color), false, false };
                    }
                    else if (!track.active || historyLine.line != track.line)
                    {
                        ImVec4 color = ImGui::ColorConvertU32ToFloat4(sTrackColors[i]);
                        color.w *= f32(historyLine.life) / Track::HistoryLine::cMaxLife;

                        it->second.mColor = mixColor(it->second.mColor, ImGui::ColorConvertFloat4ToU32(color));
                    }
                }

                historyLine.life--;
            }
        }
    }

    editor.SetMarkers(markers);
}

void SeqTextInfo::pushHistory(u32 track, u32 offset)
{
    Track::HistoryLine historyLine;
    historyLine.life = Track::HistoryLine::cMaxLife;
    historyLine.offset = offset;

    mTracks[track].historyLines.forcePushFront(historyLine);
}
