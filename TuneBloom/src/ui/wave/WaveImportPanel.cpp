#include <ui/wave/WaveImportPanel.h>

#include <ui/widgets/Dialogs.h>
#include <ui/Messages.h>
#include <ui/wave/WaveImportPanelInternal.h>

#include <libs/IconsLucide.h>
#include <imgui/imgui_internal.h>

#include <bfsar/model/ArchiveFormat.h>
#include <bfsar/audio/TempWavWriter.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

static constexpr f32 cPanelWidthEm   = 35.0f;
static constexpr f32 cPanelHeightEm  = 36.0f;
static constexpr f32 cPanelReserveEm = 6.0f;

static constexpr const char* cProcessedSuffix = ".loopbloom_processed.wav";

namespace waveimport
{

static bool IsPassThroughSource(const sead::SafeString& path)
{
    return MatchesInnerFileExtension(path.cstr(), InnerFileKind::Wave);
}

void WaveImportPanel::reset()
{
    stopPreview();

    mSettings.hasDecoded = false;
    mSettings.cachedPath.clear();
    mTouched = false;
}

void WaveImportPanel::tick()
{
    if (!mDrawnThisFrame)
    {
        stopPreview();
        mTouched = false;
    }

    mDrawnThisFrame = false;
}

bool WaveImportPanel::confirmCancel(bool cancelClicked)
{
    if (cancelClicked)
    {
        if (!isModified())
            return true;

        ImGui::OpenPopup("###WaveImportDiscard");
    }

    bool confirmed = false;

    if (BeginDialog(sead::FormatFixedSafeString<64>(ICON_LC_TRASH_2 " %s###WaveImportDiscard", messages::import::cWaveDiscardTitle).cstr(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted(messages::import::cWaveDiscardQuestion);

        BeginDialogFooter({messages::dialog::cYes, messages::dialog::cNo});

        if (DialogButton(messages::dialog::cYes))
        {
            confirmed = true;
            ImGui::CloseCurrentPopup();
        }

        if (DialogButton(messages::dialog::cNo))
            ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }

    return confirmed;
}

void WaveImportPanel::ensureDecoded(const WaveFile::RiffWaveInfo& info)
{
    if (mSettings.matchesLoadedPath(info))
        return;

    cancelJob();
    mShapedResult = {};
    mSettings.decode(info);

    mTouched = false;
    mPreviewBuilt = false;
    mPreviewStale = false;
    mLoopPlayback = true;
    mPeakStale = false;
    mPlayRequested = false;
    mWaveformState = ImGui::LoopWaveformState{};
}

void WaveImportPanel::rescaleWaveformView(u32 oldLength, u32 newLength)
{
    if (oldLength > 0 && newLength > 0 && oldLength != newLength && mWaveformState.viewInitialized)
    {
        const double ratio = static_cast<double>(newLength) / static_cast<double>(oldLength);
        u32 viewStart = static_cast<u32>(mWaveformState.viewStart * ratio);
        u32 viewEnd = static_cast<u32>(mWaveformState.viewEnd * ratio);

        if (viewEnd > newLength)
            viewEnd = newLength;

        if (viewStart >= viewEnd)
        {
            viewStart = 0;
            viewEnd = newLength;
        }

        mWaveformState.viewStart = viewStart;
        mWaveformState.viewEnd = viewEnd;
        mWaveformState.viewAnimInit = false;
    }
}

bool WaveImportPanel::beginBody()
{
    const f32 maxHeight = ImGui::GetMainViewport()->WorkSize.y - ImFloor(ImGui::GetFontSize() * cPanelReserveEm);

    mBodyScroll = mBodyHeight > maxHeight;

    if (mBodyScroll && !ImGui::BeginChild("##WaveImportBody", ImVec2(0.0f, maxHeight)))
    {
        ImGui::EndChild();
        return false;
    }

    mBodyTop = ImGui::GetCursorPosY();

    return true;
}

void WaveImportPanel::endBody()
{
    mBodyHeight = ImGui::GetCursorPosY() - mBodyTop - ImGui::GetStyle().ItemSpacing.y;

    if (mBodyScroll)
        ImGui::EndChild();
}

WaveImportPanel::FrameContext WaveImportPanel::beginFrame(WaveFile::Encoding* encoding, WaveFile::RiffWaveInfo* info, bool passThrough)
{
    FrameContext ctx;
    ctx.encoding = encoding;
    ctx.info = info;

    ctx.rebuiltThisFrame = updateDerived(passThrough);

    ctx.totalSamples = static_cast<u32>(mSettings.mono.size());
    ctx.manualLoop = (mSettings.loopMode == LoopMode::Manual);
    ctx.loopActive = mSettings.hasLoop();
    ctx.showLoop = ctx.loopActive;
    ctx.editable = ctx.manualLoop;
    ctx.loopStart = mSettings.workingLoopStart;
    ctx.loopEnd = mSettings.workingLoopEnd;
    ctx.cropEditable = !passThrough;
    ctx.cropStart = mSettings.workingCropStart;
    ctx.cropEnd = mSettings.workingCropEnd;

    return ctx;
}

void WaveImportPanel::constrainRanges(FrameContext& ctx)
{
    if (!ctx.loopActive || ctx.cropEnd <= ctx.cropStart)
        return;

    const u32 oldLoopStart = ctx.loopStart;
    const u32 oldLoopEnd = ctx.loopEnd;
    const u32 oldCropStart = ctx.cropStart;
    const u32 oldCropEnd = ctx.cropEnd;

    if (ctx.manualLoop)
    {
        ctx.loopStart = std::max(ctx.cropStart, std::min(ctx.loopStart, ctx.cropEnd - 1));
        ctx.loopEnd = std::max(ctx.loopStart + 1, std::min(ctx.loopEnd, ctx.cropEnd));
    }
    else
    {
        ctx.cropStart = std::min(ctx.cropStart, ctx.loopStart);
        ctx.cropEnd = std::max(ctx.cropEnd, ctx.loopEnd);
    }

    if (ctx.loopStart != oldLoopStart || ctx.loopEnd != oldLoopEnd || ctx.cropStart != oldCropStart || ctx.cropEnd != oldCropEnd)
        ctx.changed = true;
}

void WaveImportPanel::commitLoop(FrameContext& ctx)
{
    if (ctx.cropStart != mSettings.workingCropStart || ctx.cropEnd != mSettings.workingCropEnd)
        mPeakStale = true;

    mSettings.workingCropStart = ctx.cropStart;
    mSettings.workingCropEnd = ctx.cropEnd;

    if (ctx.totalSamples > 0)
    {
        mSettings.cropFracStart = static_cast<double>(ctx.cropStart) / ctx.totalSamples;
        mSettings.cropFracEnd = static_cast<double>(ctx.cropEnd) / ctx.totalSamples;
    }

    mSettings.workingLoopStart = ctx.loopStart;
    mSettings.workingLoopEnd = ctx.loopEnd;

    if (ctx.manualLoop && ctx.totalSamples > 0)
    {
        mSettings.manualLoopFracStart = std::min(1.0, std::max(0.0, static_cast<double>(ctx.loopStart) / ctx.totalSamples));
        mSettings.manualLoopFracEnd = std::min(1.0, std::max(0.0, static_cast<double>(ctx.loopEnd) / ctx.totalSamples));
    }

    ctx.info->isLoop = ctx.loopActive;

    if (!ctx.loopActive)
    {
        ctx.info->loopStartFrame = 0;
        ctx.info->loopEndFrame = ctx.cropEnd > ctx.cropStart ? ctx.cropEnd - ctx.cropStart : 0;
        return;
    }

    ctx.info->loopStartFrame = ctx.loopStart > ctx.cropStart ? ctx.loopStart - ctx.cropStart : 0;
    ctx.info->loopEndFrame = ctx.loopEnd > ctx.cropStart ? ctx.loopEnd - ctx.cropStart : 0;
}

void WaveImportPanel::draw(const char* header, WaveFile::Encoding* encoding, WaveFile::RiffWaveInfo* info)
{
    mDrawnThisFrame = true;

    const bool passThrough = IsPassThroughSource(info->path);

    ensureDecoded(*info);

    if (!beginBody())
        return;

    if (!mSettings.pcm.isValid())
    {
        drawUndecodableSource(header, encoding, info);
        endBody();
        return;
    }

    drawSourceHeader(header, mSettings.pcm.sampleRate, static_cast<u32>(mSettings.pcm.channels.size()), mSettings.pcm.sampleCount);

    FrameContext ctx = beginFrame(encoding, info, passThrough);

    drawWaveformBox(ctx);
    drawColumns(ctx);
    constrainRanges(ctx);

    if (ctx.changed || mSettings.derivedDirty)
        mTouched = true;

    commitLoop(ctx);
    refreshPreview(ctx);
    endBody();
}

std::string WaveImportPanel::finalizeForCommit(WaveFile::RiffWaveInfo* info)
{
    if (!info || !mSettings.hasDecoded)
        return {};

    if (IsPassThroughSource(info->path))
        return {};

    if (std::strcmp(mSettings.cachedPath.cstr(), info->path.cstr()) != 0)
        return {};

    cancelJob();

    if (mSettings.shapeDirty)
        rebuildShapedNow(false);
    else if (mSettings.derivedDirty || mPeakStale)
        mSettings.rebuildRanges();

    mPeakStale = false;

    if (mSettings.shapedChannels.empty())
        return {};

    const bool isLoop = info->isLoop;
    const u32 loopStart = mSettings.workingLoopStart;
    const u32 loopEnd = mSettings.workingLoopEnd;
    const u32 workingLength = mSettings.shapedLength();

    const bool manualLoop = (mSettings.loopMode == LoopMode::Manual);
    const bool bakeNeeded = isLoop && manualLoop && mSettings.crossfadeSamples(loopStart, loopEnd, mWaveformState.crossfadeMs) > 0;
    const u32 cropStart = mSettings.workingCropStart;
    const u32 cropEnd = isLoop ? loopEnd : mSettings.workingCropEnd;
    const bool cropNeeded = cropStart > 0 || cropEnd < workingLength;
    const bool curveNeeded = mSettings.curveEnabled && !AudioProcessing::isCurveFlat(mWaveformState.curve);

    if (mSettings.isUnmodified() && !bakeNeeded && !cropNeeded && !curveNeeded)
        return {};

    char tempPathBuf[600];
    snprintf(tempPathBuf, sizeof(tempPathBuf), "%s%s", mSettings.cachedPath.cstr(), cProcessedSuffix);

    std::vector<std::vector<float>> baked = BuildOutputChannels(mSettings.shapedChannels, makeOutputParams(manualLoop, loopStart, loopEnd, cropEnd, mSettings.normalizeGain()));

    const u32 keptLength = baked.empty() ? 0u : static_cast<u32>(baked[0].size());
    const u32 outLoopStart = !isLoop ? 0u : loopStart > cropStart ? loopStart - cropStart : 0u;
    const u32 outLoopEnd = !isLoop ? keptLength : loopEnd > cropStart ? loopEnd - cropStart : 0u;

    std::string written = TempWavWriter::write(baked, mSettings.workingSampleRate, isLoop, outLoopStart, outLoopEnd, tempPathBuf);

    if (written.empty())
    {
        info->isValid = false;
        return {};
    }

    info->path.copy(written.c_str());
    WaveFile::readRiffWavInfo(info);

    info->isLoop = isLoop;
    info->loopStartFrame = outLoopStart;
    info->loopEndFrame = outLoopEnd;

    return written;
}

static WaveImportPanel sPanel;

}

using waveimport::sPanel;

void DrawWaveImportInfo(WaveFile::Encoding* encoding, WaveFile::RiffWaveInfo* info, const char* header)
{
    sPanel.draw(header, encoding, info);
}

f32 CalcWaveImportWidth()
{
    return ImFloor(ImGui::GetFontSize() * cPanelWidthEm);
}

f32 CalcWaveImportHeight()
{
    const f32 height = sPanel.getBodyHeight();

    return height > 0.0f ? height : ImFloor(ImGui::GetFontSize() * cPanelHeightEm);
}

std::string FinalizeImportInfoForCommit(WaveFile::RiffWaveInfo* info)
{
    return sPanel.finalizeForCommit(info);
}

void ResetWaveImport()
{
    sPanel.reset();
}

void WaveImportTick()
{
    sPanel.tick();
}

bool WaveImportConfirmCancel(bool cancelClicked)
{
    return sPanel.confirmCancel(cancelClicked);
}
