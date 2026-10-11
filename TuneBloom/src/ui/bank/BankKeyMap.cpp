#include <ui/bank/BankFileEditor.h>

#include <ui/widgets/ContextMenu.h>
#include <ui/Messages.h>
#include <ui/bank/PianoRollPalette.h>
#include <ui/app/Shortcuts.h>
#include <ui/app/UI.h>
#include <ui/widgets/ZoomScrollBar.h>

#include <imgui/imgui_custom.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

static constexpr f32 cPianoMinNoteWidthEm = 0.35f;
static constexpr f32 cPianoMaxNoteWidthEm = 1.5f;
static constexpr f32 cPianoViewMoveTime   = 0.25f;

static constexpr f32 cVelocityTickWidth    = 8.0f;
static constexpr f32 cVelocityLabelGapEm   = 1.6f;
static constexpr s32 cVelocityLabelAlpha   = 170;

static constexpr f32 cRegionLightAlpha         = 0.22f;
static constexpr f32 cRegionLightSelectedAlpha = 0.45f;
static constexpr f32 cRegionLightBorderAlpha   = 0.55f;

static constexpr f32 cRegionEdgeSize      = 6.0f;
static constexpr f32 cVelocityScrollWidth = 9.0f;
static constexpr ImU32 cRegionGapColor    = IM_COL32(210, 100, 100, 150);

namespace {

enum class DragMode
{
    None,
    ResizeL,
    ResizeR,
    ResizeVTop
};

struct DragState
{
    DragMode mode = DragMode::None;
    BankFile::KeyRegion* region = nullptr;
    BankFile::KeyRegion* prev = nullptr;
    BankFile::KeyRegion* next = nullptr;

    BankFile::VelocityRegion* velRegion = nullptr;
    BankFile::VelocityRegion* vNext = nullptr;

    ImVec2 r0;
    ImVec2 r1;

    ImVec2 v0;
    ImVec2 v1;

    ImVec2 initialCanvasPos;
};

enum class AddMode
{
    None,
    Front,
    Back,
    After
};

struct KeyMapView
{
    f32 zoom = 1.0f;
    f32 zoomTarget = 1.0f;
    f32 scrollX = 0.0f;
    f32 scrollXTarget = 0.0f;
    f32 anchorNorm = 0.0f;
    f32 anchorScreenX = 0.0f;
    bool middlePan = false;

    f32 zoomY = 1.0f;
    f32 zoomTargetY = 1.0f;
    f32 scrollY = 0.0f;
    f32 scrollYTarget = 0.0f;
    f32 anchorNormY = 0.0f;
    f32 anchorScreenY = 0.0f;

    const BankFile::Instrument* lastInstrument = nullptr;
    u32 lastInstrumentId = Item::cInvalidId;
    bool centerView = true;
    bool snapView = true;
    f32 viewMoveFrom = 0.0f;
    f32 viewMoveTo = 0.0f;
    f32 viewMoveTime = cPianoViewMoveTime;

    bool vScrollDrag = false;
    f32 vScrollGrab = 0.0f;

    f32 edgeOffset = 0.0f;
    s32 mouseKey = 0;
    s32 prevNote = -1;
};

struct KeyMapFrame
{
    KeyMapView& view;
    BankFile::Instrument* instrument;
    ImDrawList* draw;

    f32 width = 0.0f;
    f32 keyboardHeight = 0.0f;
    f32 regionHeight = 0.0f;
    s32 beginNote = 0;
    s32 endNote = 0;
    s32 numKeys = 1;

    ImVec2 canvasPos;
    ImVec2 visibleCanvasPos;
    ImVec2 canvasSize;

    f32 minZoomedWidth = 0.0f;
    f32 zoomMin = 1.0f;
    f32 zoomMax = 1.0f;
    f32 zoomedWidth = 0.0f;
    f32 noteWidth = 0.0f;
    f32 zoomedHeight = 0.0f;
    f32 maxScrollY = 0.0f;

    ImVec2 mouse;
    bool mouseDown = false;
    bool mouseClicked = false;

    f32 vsbTrackX = 0.0f;
    f32 vsbTrackY0 = 0.0f;
    f32 vsbTrackY1 = 0.0f;
    f32 vsbThumbH = 0.0f;
    f32 vsbTravel = 0.0f;
    bool overVScrollbar = false;
    bool vScrollbarHot = false;

    ImDrawListSplitter splitter;
    AddMode addMode = AddMode::None;
    BankFile::KeyRegion* addNode = nullptr;
    bool drawGrabBar = false;
};

struct KeyRegionSpan
{
    BankFile::KeyRegion* keyRegion = nullptr;
    BankFile::KeyRegion* prev = nullptr;
    BankFile::KeyRegion* next = nullptr;

    f32 x0 = 0.0f;
    f32 x1 = 0.0f;
    ImVec2 r0;
    ImVec2 r1;

    bool hoveredRegionY = false;
    bool onLeftEdge = false;
    bool onRightEdge = false;
};

}

static DragState sDrag;

static BankFile::KeyRegion* sContextKeyRegion = nullptr;
static s32 sContextSplitKey = -1;
static s32 sContextSplitVel = -1;

static void SelectVelocity(BankFile::KeyRegion* keyRegion, BankFile::VelocityRegion* velocityRegion)
{
    sSubSelectedItem = velocityRegion;
    sSelectedItemIsSubWindow = true;
    sContextKeyRegion = keyRegion;
}

void DeselectVelocity()
{
    sSubSelectedItem = nullptr;
    sSelectedItemIsSubWindow = false;
    sContextKeyRegion = nullptr;
}

static void DeleteVelocityRegion(BankFile::KeyRegion* keyRegion, BankFile::VelocityRegion* velocityRegion)
{
    DeselectVelocity();

    snd::internal::driver::SoundThreadLock lock;

    if (keyRegion->getVelocityRegionList().size() == 1) //? Deleted last VelocityRegion, this KeyRegion can die now
    {
        delete keyRegion;
        SetUnsavedChanges(true);
    }
    else
    {
        //? VelocityRegions can't contain gaps, so handle that

        if (velocityRegion == keyRegion->getVelocityRegionList().front()) //? VelocityRegion is the bottom one, extend the one above to cover it
        {
            velocityRegion->getNext(*keyRegion)->setVelocityMin(0);
            SetUnsavedChanges(true);
        }
        else if (velocityRegion == keyRegion->getVelocityRegionList().back()) //? VelocityRegion is the top one, extend the one below to cover it
        {
            velocityRegion->getPrev(*keyRegion)->setVelocityMax(127);
            SetUnsavedChanges(true);
        }
        else //? VelocityRegion is sandwiched, extend the one below to cover it
        {
            velocityRegion->getPrev(*keyRegion)->setVelocityMax(velocityRegion->getVelocityMax());
            SetUnsavedChanges(true);
        }

        delete velocityRegion;
        SetUnsavedChanges(true);
    }
}

static void SplitKeyRegion(BankFile::Instrument* instrument, BankFile::KeyRegion* keyRegion)
{
    snd::internal::driver::SoundThreadLock lock;

    u8 newKeyMax = keyRegion->getKeyMax();
    s32 splitMin = sContextSplitKey;

    if (splitMin <= keyRegion->getKeyMin() || splitMin > keyRegion->getKeyMax())
        splitMin = (keyRegion->getKeyMax() + keyRegion->getKeyMin()) / 2 + 1;

    u8 newKeyMin = static_cast<u8>(splitMin);

    keyRegion->setKeyMax(newKeyMin - 1, *instrument);

    BankFile::KeyRegion* newKeyRegion = new BankFile::KeyRegion(newKeyMin, newKeyMax);
    newKeyRegion->setId(0);
    newKeyRegion->setEnableName(true);
    newKeyRegion->getName() = "KeyRegion";

    keyRegion->insertBack(newKeyRegion);

    sBfsar.updateList(instrument->getKeyRegionList());

    for (Item* item : keyRegion->getVelocityRegionList())
    {
        BankFile::VelocityRegion* srcVelRegion = static_cast<BankFile::VelocityRegion*>(item);
        BankFile::VelocityRegion* newVelRegion = new BankFile::VelocityRegion(srcVelRegion->getVelocityMin(), srcVelRegion->getVelocityMax());

        newVelRegion->setId(0);
        newVelRegion->setEnableName(true);
        newVelRegion->getName() = "VelocityRegion";

        CopyVelocityRegion(newVelRegion, srcVelRegion);

        newKeyRegion->getVelocityRegionList().pushBack(newVelRegion);
    }

    sBfsar.updateList(newKeyRegion->getVelocityRegionList());
    SetUnsavedChanges(true);
}

static void SplitVelocityRegion(BankFile::KeyRegion* keyRegion, BankFile::VelocityRegion* velocityRegion)
{
    snd::internal::driver::SoundThreadLock lock;

    u8 newVelMax = velocityRegion->getVelocityMax();
    s32 splitVelMin = sContextSplitVel;

    if (splitVelMin <= velocityRegion->getVelocityMin() || splitVelMin > velocityRegion->getVelocityMax())
        splitVelMin = (velocityRegion->getVelocityMax() + velocityRegion->getVelocityMin()) / 2 + 1;

    u8 newVelMin = static_cast<u8>(splitVelMin);

    velocityRegion->setVelocityMax(newVelMin - 1);

    BankFile::VelocityRegion* newVelRegion = new BankFile::VelocityRegion(newVelMin, newVelMax);
    newVelRegion->setId(0);
    newVelRegion->setEnableName(true);
    newVelRegion->getName() = "VelocityRegion";
    CopyVelocityRegion(newVelRegion, velocityRegion);

    velocityRegion->insertBack(newVelRegion);

    sBfsar.updateList(keyRegion->getVelocityRegionList());
    SetUnsavedChanges(true);
}

static void VelocityContextMenu(BankFile::Instrument* instrument, BankFile::KeyRegion* keyRegion, BankFile::VelocityRegion* velocityRegion)
{
    if (!BeginContextMenu("VelocityRegionContextMenu"))
        return;

    if (ContextMenuItem(ICON_LC_SQUARE_SPLIT_HORIZONTAL, messages::menu::cSplitKeyRegion, keyRegion->getKeyNum() > 1, messages::menu::cKeyRegionOneKey))
        SplitKeyRegion(instrument, keyRegion);

    if (ContextMenuItem(ICON_LC_SQUARE_SPLIT_VERTICAL, messages::menu::cSplitVelocityRegion, velocityRegion->getVelocityNum() > 1, messages::menu::cVelocityRegionOneVelocity))
        SplitVelocityRegion(keyRegion, velocityRegion);

    ContextMenuSeparator();

    if (ContextMenuDangerItem(ICON_LC_TRASH_2, messages::menu::cDeleteRegion))
    {
        DeleteVelocityRegion(keyRegion, velocityRegion);
        SetUnsavedChanges(true);
    }

    EndContextMenu();
}

BankFile::VelocityRegion* FindSelectedRegion(BankFile::Instrument* instrument, BankFile::KeyRegion** outKeyRegion)
{
    *outKeyRegion = nullptr;

    if (!instrument || !sSubSelectedItem || sSubSelectedItem->getItemType() != Item::ItemType::BankFileVelocityRegion)
        return nullptr;

    BankFile::VelocityRegion* velRegion = static_cast<BankFile::VelocityRegion*>(sSubSelectedItem);

    if (sContextKeyRegion)
    {
        for (Item* velItem : sContextKeyRegion->getVelocityRegionList())
        {
            if (velItem != velRegion)
                continue;

            *outKeyRegion = sContextKeyRegion;
            return velRegion;
        }
    }

    for (Item* keyItem : instrument->getKeyRegionList())
    {
        BankFile::KeyRegion* keyRegion = static_cast<BankFile::KeyRegion*>(keyItem);
        for (Item* velItem : keyRegion->getVelocityRegionList())
        {
            if (velItem != velRegion)
                continue;

            *outKeyRegion = keyRegion;
            return velRegion;
        }
    }

    return nullptr;
}

static f32 CalcZoomedWidth(const KeyMapFrame& frame, f32 zoom)
{
    return std::max(zoom * frame.width, frame.minZoomedWidth);
}

static void RequestZoom(KeyMapFrame& frame, f32 target, f32 screenX)
{
    KeyMapView& view = frame.view;

    const f32 curZW = CalcZoomedWidth(frame, view.zoom);
    const f32 sx = sead::MathCalcCommon<f32>::clamp2(0.0f, screenX, frame.width);
    view.anchorNorm = sead::MathCalcCommon<f32>::clamp2(0.0f, (view.scrollX + sx) / curZW, 1.0f);
    view.anchorScreenX = sx;
    view.zoomTarget = sead::MathCalcCommon<f32>::clamp2(frame.zoomMin, target, frame.zoomMax);
    view.viewMoveTime = cPianoViewMoveTime;
}

static void RequestZoomY(KeyMapFrame& frame, f32 target, f32 screenY)
{
    KeyMapView& view = frame.view;

    const f32 curZH = std::max(frame.regionHeight, view.zoomY * frame.regionHeight);
    const f32 sy = sead::MathCalcCommon<f32>::clamp2(0.0f, screenY, frame.regionHeight);
    view.anchorNormY = sead::MathCalcCommon<f32>::clamp2(0.0f, (view.scrollY + sy) / curZH, 1.0f);
    view.anchorScreenY = sy;
    view.zoomTargetY = sead::MathCalcCommon<f32>::clamp2(1.0f, target, 8.0f);
}

static f32 VelToY(const KeyMapFrame& frame, f32 vel)
{
    return (1.0f - vel / 128.0f) * frame.zoomedHeight - frame.view.scrollY;
}

static s32 YToBorderVel(const KeyMapFrame& frame, f32 yOffset)
{
    return 127 - (s32)IM_ROUND(((yOffset + frame.view.scrollY) / frame.zoomedHeight) * 128.0f);
}

static void GetKeyRect(const KeyMapFrame& frame, s32 key, f32& outMin, f32& outMax)
{
    outMin = IM_ROUND((key - frame.beginNote) * frame.noteWidth);
    outMax = IM_ROUND((key - frame.beginNote + 1) * frame.noteWidth);
}

static s32 XToKey(const KeyMapFrame& frame, f32 x)
{
    f32 local = x - frame.canvasPos.x;
    s32 k = frame.beginNote + static_cast<s32>(std::floor(local / frame.noteWidth));
    return sead::MathCalcCommon<s32>::clamp2(frame.beginNote, k, frame.endNote);
}

static void ResetKeyMapView(KeyMapFrame& frame)
{
    KeyMapView& view = frame.view;

    const u32 curInstrumentId = frame.instrument ? frame.instrument->getId() : Item::cInvalidId;

    if (frame.instrument != view.lastInstrument || curInstrumentId != view.lastInstrumentId)
    {
        if (!view.lastInstrument || !frame.instrument)
        {
            view.zoom = view.zoomTarget = 1.0f;
            view.scrollX = view.scrollXTarget = 0.0f;
            view.zoomY = view.zoomTargetY = 1.0f;
            view.scrollY = view.scrollYTarget = 0.0f;
            view.snapView = true;
        }

        view.lastInstrument = frame.instrument;
        view.lastInstrumentId = curInstrumentId;
        view.zoomTarget = view.zoom;
        view.viewMoveTime = cPianoViewMoveTime;
        view.centerView = true;
    }
}

static void UpdateKeyMapViewInput(KeyMapFrame& frame)
{
    KeyMapView& view = frame.view;
    ImGuiIO& io = ImGui::GetIO();

    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
        view.middlePan = true;

    if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle))
        view.middlePan = false;
    if (view.middlePan)
    {
        view.scrollX -= io.MouseDelta.x;
        view.scrollXTarget = view.scrollX;
        view.viewMoveTime = cPianoViewMoveTime;
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }

    if (ImGui::IsWindowHovered())
    {
        const f32 kPanStep = 40.0f;

        if (io.MouseWheel != 0.0f)
        {
            if (shortcuts::Held(shortcuts::Modifier::ZoomHorizontal))
                RequestZoom(frame, view.zoomTarget * (1.0f + io.MouseWheel * 0.20f), io.MousePos.x - frame.visibleCanvasPos.x);
            else if (shortcuts::Held(shortcuts::Modifier::ZoomVertical))
                RequestZoomY(frame, view.zoomTargetY * (1.0f + io.MouseWheel * 0.20f), io.MousePos.y - frame.visibleCanvasPos.y);
            else if (shortcuts::Held(shortcuts::Modifier::PanHorizontal))
            {
                view.scrollXTarget -= io.MouseWheel * kPanStep;
                view.viewMoveTime = cPianoViewMoveTime;
            }
            else
                view.scrollYTarget -= io.MouseWheel * kPanStep;
        }

        if (io.MouseWheelH != 0.0f)
        {
            view.scrollXTarget -= io.MouseWheelH * kPanStep;
            view.viewMoveTime = cPianoViewMoveTime;
        }

        if (shortcuts::Pressed(shortcuts::Action::ZoomIn))
            RequestZoom(frame, view.zoomTarget * 1.25f, frame.width * 0.5f);

        if (shortcuts::Pressed(shortcuts::Action::ZoomOut))
            RequestZoom(frame, view.zoomTarget / 1.25f, frame.width * 0.5f);
    }
}

static void AnimateKeyMapView(KeyMapFrame& frame)
{
    KeyMapView& view = frame.view;
    ImGuiIO& io = ImGui::GetIO();

    const f32 dt = io.DeltaTime > 0.0f ? io.DeltaTime : (1.0f / 60.0f);
    const f32 a = 1.0f - expf(-dt * 40.0f);

    if (fabsf(view.zoomTarget - view.zoom) > 0.0005f)
    {
        view.zoom += (view.zoomTarget - view.zoom) * a;
        if (fabsf(view.zoomTarget - view.zoom) < 0.0015f)
            view.zoom = view.zoomTarget;

        const f32 zw = CalcZoomedWidth(frame, view.zoom);
        view.scrollX = view.anchorNorm * zw - view.anchorScreenX;
        view.scrollXTarget = view.scrollX;
    }
    view.zoom = sead::MathCalcCommon<f32>::clamp2(frame.zoomMin, view.zoom, frame.zoomMax);
    view.zoomTarget = sead::MathCalcCommon<f32>::clamp2(frame.zoomMin, view.zoomTarget, frame.zoomMax);

    if (fabsf(view.zoomTargetY - view.zoomY) > 0.0005f)
    {
        view.zoomY += (view.zoomTargetY - view.zoomY) * a;
        if (fabsf(view.zoomTargetY - view.zoomY) < 0.0015f)
            view.zoomY = view.zoomTargetY;

        const f32 zh = std::max(frame.regionHeight, view.zoomY * frame.regionHeight);
        view.scrollY = view.anchorNormY * zh - view.anchorScreenY;
        view.scrollYTarget = view.scrollY;
    }
    view.zoomY = sead::MathCalcCommon<f32>::clamp2(1.0f, view.zoomY, 8.0f);

    if (fabsf(view.scrollXTarget - view.scrollX) > 0.01f)
    {
        view.scrollX += (view.scrollXTarget - view.scrollX) * a;
        if (fabsf(view.scrollXTarget - view.scrollX) < 0.5f)
            view.scrollX = view.scrollXTarget;
    }

    if (fabsf(view.scrollYTarget - view.scrollY) > 0.01f)
    {
        view.scrollY += (view.scrollYTarget - view.scrollY) * a;
        if (fabsf(view.scrollYTarget - view.scrollY) < 0.5f)
            view.scrollY = view.scrollYTarget;
    }
}

static void CenterKeyMapView(KeyMapFrame& frame, f32 maxScroll)
{
    KeyMapView& view = frame.view;

    const bool hasRegions = frame.instrument && frame.instrument->getKeyRegionList().size() > 0;
    f32 spanMin = static_cast<f32>(cPianoDefaultCenter);
    f32 spanMax = spanMin;
    if (hasRegions)
    {
        s32 keyMin = 127;
        s32 keyMax = 0;
        for (Item* keyRegionItem : frame.instrument->getKeyRegionList())
        {
            const BankFile::KeyRegion* keyRegion = static_cast<BankFile::KeyRegion*>(keyRegionItem);
            keyMin = std::min<s32>(keyMin, keyRegion->getKeyMin());
            keyMax = std::max<s32>(keyMax, keyRegion->getKeyMax());
        }

        spanMin = static_cast<f32>(keyMin);
        spanMax = static_cast<f32>(keyMax + 1);
    }

    const f32 spanX0 = (spanMin - frame.beginNote) * frame.zoomedWidth / frame.numKeys;
    const f32 spanX1 = (spanMax - frame.beginNote) * frame.zoomedWidth / frame.numKeys;
    const f32 center = sead::MathCalcCommon<f32>::clamp2(0.0f, (spanX0 + spanX1) * 0.5f - frame.width * 0.5f, maxScroll);

    if (view.snapView)
    {
        view.snapView = false;
        view.scrollX = view.scrollXTarget = center;
    }
    else if (hasRegions)
    {
        f32 target = view.scrollX;
        if (spanX1 - spanX0 > frame.width)
            target = sead::MathCalcCommon<f32>::clamp2(spanX0, view.scrollX, spanX1 - frame.width);
        else if (spanX0 < view.scrollX || spanX1 > view.scrollX + frame.width)
            target = center;

        if (fabsf(target - view.scrollX) > 0.5f)
        {
            view.viewMoveFrom = view.scrollX;
            view.viewMoveTo = target;
            view.viewMoveTime = 0.0f;
        }
    }
}

static void UpdateKeyMapLayout(KeyMapFrame& frame)
{
    KeyMapView& view = frame.view;

    frame.zoomedWidth = CalcZoomedWidth(frame, view.zoom);
    f32 maxScroll = std::max(0.0f, frame.zoomedWidth - frame.width);

    if (view.centerView)
    {
        view.centerView = false;
        CenterKeyMapView(frame, maxScroll);
    }

    if (view.viewMoveTime < cPianoViewMoveTime)
    {
        const f32 dt = ImGui::GetIO().DeltaTime;
        view.viewMoveTime = std::min(cPianoViewMoveTime, view.viewMoveTime + (dt > 0.0f ? dt : (1.0f / 60.0f)));

        const f32 t = view.viewMoveTime / cPianoViewMoveTime;
        view.scrollX = view.scrollXTarget = view.viewMoveFrom + (view.viewMoveTo - view.viewMoveFrom) * (t * t * (3.0f - 2.0f * t));
    }
    view.scrollX = sead::MathCalcCommon<f32>::clamp2(0.0f, view.scrollX, maxScroll);
    view.scrollXTarget = sead::MathCalcCommon<f32>::clamp2(0.0f, view.scrollXTarget, maxScroll);

    frame.canvasPos.x -= IM_ROUND(view.scrollX);
    frame.canvasSize.x = frame.zoomedWidth;

    frame.noteWidth = frame.zoomedWidth / (f32)frame.numKeys;

    frame.zoomedHeight = std::max(frame.regionHeight, view.zoomY * frame.regionHeight);
    frame.maxScrollY = std::max(0.0f, frame.zoomedHeight - frame.regionHeight);
    view.scrollY = sead::MathCalcCommon<f32>::clamp2(0.0f, view.scrollY, frame.maxScrollY);
    view.scrollYTarget = sead::MathCalcCommon<f32>::clamp2(0.0f, view.scrollYTarget, frame.maxScrollY);
}

static void UpdateVelocityScrollBar(KeyMapFrame& frame)
{
    KeyMapView& view = frame.view;

    frame.vsbTrackX = frame.visibleCanvasPos.x + frame.width - cVelocityScrollWidth - 1.0f;
    frame.vsbTrackY0 = frame.canvasPos.y + 1.0f;
    frame.vsbTrackY1 = frame.canvasPos.y + frame.regionHeight - 1.0f;
    const f32 vsbTrackH = std::max(0.0f, frame.vsbTrackY1 - frame.vsbTrackY0);
    const f32 vsbVisFrac = (frame.zoomedHeight > 0.0f) ? (frame.regionHeight / frame.zoomedHeight) : 1.0f;
    frame.vsbThumbH = std::max(20.0f, vsbVisFrac * vsbTrackH);
    frame.vsbTravel = std::max(0.0f, vsbTrackH - frame.vsbThumbH);

    if (frame.maxScrollY > 0.0f)
    {
        const f32 thumbTop = (frame.vsbTravel > 0.0f) ? (view.scrollY / frame.maxScrollY) * frame.vsbTravel : 0.0f;
        const bool overTrack = frame.mouse.x >= frame.vsbTrackX && frame.mouse.x <= frame.vsbTrackX + cVelocityScrollWidth && frame.mouse.y >= frame.vsbTrackY0 && frame.mouse.y <= frame.vsbTrackY1;
        const bool overThumb = overTrack && frame.mouse.y >= frame.vsbTrackY0 + thumbTop && frame.mouse.y <= frame.vsbTrackY0 + thumbTop + frame.vsbThumbH;
        frame.overVScrollbar = overTrack;

        if (ImGui::IsWindowHovered() && frame.mouseClicked && overTrack && sDrag.mode == DragMode::None)
        {
            view.vScrollGrab = overThumb ? (frame.mouse.y - (frame.vsbTrackY0 + thumbTop)) : (frame.vsbThumbH * 0.5f);
            view.vScrollDrag = true;
        }

        if (!frame.mouseDown)
            view.vScrollDrag = false;

        if (view.vScrollDrag && frame.vsbTravel > 0.0f)
        {
            const f32 t = sead::MathCalcCommon<f32>::clamp2(0.0f, frame.mouse.y - view.vScrollGrab - frame.vsbTrackY0, frame.vsbTravel) / frame.vsbTravel;
            view.scrollY = t * frame.maxScrollY;
            view.scrollYTarget = view.scrollY;
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        }

        frame.vScrollbarHot = overThumb || view.vScrollDrag;
    }
    else
    {
        view.vScrollDrag = false;
    }
}

static void DrawVelocityScrollBar(const KeyMapFrame& frame)
{
    const KeyMapView& view = frame.view;

    if (frame.maxScrollY > 0.0f)
    {
        const f32 thumbTop = (frame.vsbTravel > 0.0f) ? (view.scrollY / frame.maxScrollY) * frame.vsbTravel : 0.0f;
        const ImU32 thumbCol = frame.vScrollbarHot ? GetPianoRollPalette().vScrollThumbHover : GetPianoRollPalette().vScrollThumb;

        frame.draw->AddRectFilled(ImVec2(frame.vsbTrackX, frame.vsbTrackY0), ImVec2(frame.vsbTrackX + cVelocityScrollWidth, frame.vsbTrackY1), GetPianoRollPalette().vScrollTrack, 3.0f);
        frame.draw->AddRectFilled(ImVec2(frame.vsbTrackX, frame.vsbTrackY0 + thumbTop), ImVec2(frame.vsbTrackX + cVelocityScrollWidth, frame.vsbTrackY0 + thumbTop + frame.vsbThumbH), thumbCol, 3.0f);
    }
}

static void DrawGrabBar(ImDrawList* draw, const ImVec2& from, const ImVec2& to, ImGuiMouseCursor cursor)
{
    draw->AddLine(from, to, GetPianoRollPalette().grabBar, 2.0f);
    ImGui::SetMouseCursor(cursor);
}

static bool DrawKeyRegionGap(const KeyMapFrame& frame, const ImVec2& gapMin, const ImVec2& gapMax, ImGuiID id)
{
    frame.draw->AddRect(gapMin, gapMax, cRegionGapColor);
    ImGui::ItemAdd(ImRect(gapMin, gapMax), id);

    if (!frame.overVScrollbar && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        return true;

    if (!frame.overVScrollbar && ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        DeselectVelocity();
    else if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip("%s", messages::bank::cDoubleClickToAdd);

    return false;
}

static void DrawKeyRegionGaps(KeyMapFrame& frame, const KeyRegionSpan& span)
{
    BankFile::KeyRegion* keyRegion = span.keyRegion;
    const ImVec2& canvasPos = frame.canvasPos;
    const f32 bottom = canvasPos.y + frame.regionHeight;

    if (keyRegion == frame.instrument->getKeyRegionList().front() && keyRegion->getKeyMin() != 0)
    {
        if (DrawKeyRegionGap(frame, canvasPos, ImVec2(canvasPos.x + span.x0, bottom), ImGui::GetID("##GapFront")))
        {
            frame.addMode = AddMode::Front;
            frame.addNode = keyRegion;
        }
    }

    if (keyRegion == frame.instrument->getKeyRegionList().back() && keyRegion->getKeyMax() != 127)
    {
        if (DrawKeyRegionGap(frame, ImVec2(canvasPos.x + span.x1, canvasPos.y), ImVec2(canvasPos.x + frame.canvasSize.x, bottom), ImGui::GetID("##GapBack")))
        {
            frame.addMode = AddMode::Back;
            frame.addNode = keyRegion;
        }
    }

    BankFile::KeyRegion* next = keyRegion->getNext(*frame.instrument);
    if (next && keyRegion->getKeyMax() + 1 != next->getKeyMin())
    {
        f32 nextX0, ignored;
        GetKeyRect(frame, next->getKeyMin(), nextX0, ignored);

        if (DrawKeyRegionGap(frame, ImVec2(canvasPos.x + span.x1, canvasPos.y), ImVec2(canvasPos.x + nextX0, bottom), ImGui::GetID(keyRegion)))
        {
            frame.addMode = AddMode::After;
            frame.addNode = keyRegion;
        }
    }
}

static void DrawVelocityRegionName(const KeyMapFrame& frame, BankFile::VelocityRegion* velRegion, const ImVec2& p0, const ImVec2& p1, ImU32 nameColor)
{
    const char* name = messages::common::cNull;
    if (velRegion->getWaveFileRef().isAttached())
    {
        name = velRegion->getWaveFileRef().getItem()->getName().cstr();
    }

    const f32 visLeft  = std::max(p0.x, frame.visibleCanvasPos.x);
    const f32 visRight = std::min(p1.x, frame.visibleCanvasPos.x + frame.width);
    const f32 visTop   = std::max(p0.y, frame.canvasPos.y);
    const f32 visBot   = std::min(p1.y, frame.canvasPos.y + frame.regionHeight);
    const f32 avail    = (visRight - visLeft) - 8.0f;

    if (avail > 1.0f && visBot > visTop)
    {
        const char* shown = name;
        sead::FixedSafeString<256> truncated;

        if (ImGui::CalcTextSize(name).x > avail)
        {
            const f32 ellW = ImGui::CalcTextSize("...").x;
            s32 len = 0;
            while (name[len] != '\0') len++;

            s32 best = len;
            for (s32 s = len; s >= 0; s--)
            {
                if (ellW + ImGui::CalcTextSize(name + s).x <= avail) best = s;
                else break;
            }
            truncated.format("...%s", name + best);
            shown = truncated.cstr();
        }

        const ImVec2 sz = ImGui::CalcTextSize(shown);
        const ImVec2 tp(
            visLeft + (visRight - visLeft - sz.x) * 0.5f,
            visTop + (visBot - visTop - sz.y) * 0.5f
        );

        frame.draw->PushClipRect(ImVec2(visLeft, visTop), ImVec2(visRight, visBot), true);
        frame.draw->AddText(tp, nameColor, shown);
        frame.draw->PopClipRect();
    }
}

static void DrawVelocityRegion(KeyMapFrame& frame, const KeyRegionSpan& span, BankFile::VelocityRegion* velRegion)
{
    KeyMapView& view = frame.view;

    s32 velMin = velRegion->getVelocityMin();
    s32 velMax = velRegion->getVelocityMax();

    f32 y0 = VelToY(frame, (f32)(velMax + 1));
    f32 y1 = VelToY(frame, (f32)velMin);

    ImVec2 p0 = ImVec2(frame.canvasPos.x + span.x0, frame.canvasPos.y + y0);
    ImVec2 p1 = ImVec2(frame.canvasPos.x + span.x1, frame.canvasPos.y + y1);

    ImVec2 ip0 = ImVec2(p0.x, std::max(p0.y, frame.canvasPos.y));
    ImVec2 ip1 = ImVec2(p1.x, std::min(p1.y, frame.canvasPos.y + frame.regionHeight));
    ImGui::ItemAdd(ImRect(ip0, ip1), ImGui::GetID(velRegion));

    if (!sDroppedWavPath.isEmpty() && !sWavDropTargetVel && frame.mouse.x >= ip0.x && frame.mouse.x <= ip1.x && frame.mouse.y >= ip0.y && frame.mouse.y <= ip1.y)
    {
        sWavDropTargetVel = velRegion;
        frame.draw->AddRect(ip0, ip1, IM_COL32(120, 230, 140, 255), 0.0f, 0, 2.5f);
    }

    if (ImGui::IsItemHovered())
    {
        frame.drawGrabBar = true;
    }

    if (velRegion == sDrag.velRegion)
    {
        sDrag.v0 = p0;
        sDrag.v1 = p1;
    }

    bool hoveredRegionX = frame.mouse.x >= p0.x && frame.mouse.x <= p1.x && span.hoveredRegionY;
    bool onTopEdge = hoveredRegionX && (frame.mouse.y <= p0.y + cRegionEdgeSize) && (frame.mouse.y >= p0.y - cRegionEdgeSize);
    bool onBottomEdge = hoveredRegionX && (frame.mouse.y >= p1.y - cRegionEdgeSize) && (frame.mouse.y <= p1.y + cRegionEdgeSize);

    if (frame.drawGrabBar)
    {
        frame.splitter.SetCurrentChannel(frame.draw, 1);

        if (sDrag.mode == DragMode::None)
        {
            if (!span.onLeftEdge && !span.onRightEdge)
            {
                if (onTopEdge && velMax < 127 && velRegion->getNext(*span.keyRegion))
                    DrawGrabBar(frame.draw, p0, ImVec2(p1.x, p0.y), ImGuiMouseCursor_ResizeNS);
                else if (onBottomEdge && velMin > 0 && velRegion->getPrev(*span.keyRegion))
                    DrawGrabBar(frame.draw, ImVec2(p0.x, p1.y), p1, ImGuiMouseCursor_ResizeNS);
            }
        }
        else if (sDrag.mode == DragMode::ResizeVTop && sDrag.velRegion == velRegion)
        {
            DrawGrabBar(frame.draw, sDrag.v0, ImVec2(sDrag.v1.x, sDrag.v0.y), ImGuiMouseCursor_ResizeNS);
        }

        frame.splitter.SetCurrentChannel(frame.draw, 0);
    }

    const bool selected = (sSubSelectedItem == velRegion);
    ImU32 color = selected ? ImGui::GetColorU32(ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive)) : ImGui::GetColorU32(ImGui::GetStyleColorVec4(ImGuiCol_Button));
    ImU32 borderColor = selected ? IM_COL32(255, 226, 120, 255) : IM_COL32(0, 0, 0, 160);
    ImU32 nameColor = IM_COL32_WHITE;

    if (!gThemeIsDark)
    {
        color = ImGui::GetColorU32(ImGuiCol_CheckMark, selected ? cRegionLightSelectedAlpha : cRegionLightAlpha);
        borderColor = selected ? ImGui::GetColorU32(ImGuiCol_CheckMark) : ImGui::GetColorU32(ImGuiCol_CheckMark, cRegionLightBorderAlpha);
        nameColor = ImGui::GetColorU32(ImGuiCol_Text);
    }

    if (!frame.overVScrollbar && ImGui::IsItemClicked(ImGuiMouseButton_Left))
    {
        SelectVelocity(span.keyRegion, velRegion);

        if (sDrag.mode == DragMode::None)
        {
            if (span.onLeftEdge)
            {
                sDrag.mode = DragMode::ResizeL;
                view.edgeOffset = span.r0.x - frame.mouse.x;
            }
            else if (span.onRightEdge)
            {
                sDrag.mode = DragMode::ResizeR;
                view.edgeOffset = span.r1.x - frame.mouse.x;
            }
            else if (onTopEdge && velMax < 127 && velRegion->getNext(*span.keyRegion))
            {
                sDrag.mode = DragMode::ResizeVTop;
                view.edgeOffset = p0.y - frame.mouse.y;
                sDrag.velRegion = velRegion;
                sDrag.vNext = velRegion->getNext(*span.keyRegion);
            }
            else if (onBottomEdge && velMin > 0 && velRegion->getPrev(*span.keyRegion))
            {
                sDrag.mode = DragMode::ResizeVTop;
                view.edgeOffset = p1.y - frame.mouse.y;
                sDrag.velRegion = velRegion->getPrev(*span.keyRegion);
                sDrag.vNext = velRegion;
            }

            sDrag.region = span.keyRegion;
            sDrag.prev = span.prev;
            sDrag.next = span.next;
            sDrag.initialCanvasPos = frame.canvasPos;
            sDrag.initialCanvasPos.x += view.scrollX;
        }
    }
    else if (!frame.overVScrollbar && ImGui::IsItemClicked(ImGuiMouseButton_Right))
    {
        ImGui::OpenPopup("VelocityRegionContextMenu");
        SelectVelocity(span.keyRegion, velRegion);

        sContextSplitKey = XToKey(frame, frame.mouse.x);
        sContextSplitVel = YToBorderVel(frame, frame.mouse.y - frame.canvasPos.y);
    }

    frame.draw->AddRectFilled(p0, p1, color);
    frame.draw->AddRect(p0, p1, borderColor, 0.0f, 0, selected ? 2.0f : 1.0f);

    DrawVelocityRegionName(frame, velRegion, p0, p1, nameColor);
}

static void DrawKeyRegionGrabBar(const KeyMapFrame& frame, const KeyRegionSpan& span)
{
    if (sDrag.mode == DragMode::None)
    {
        if (span.onLeftEdge)
            DrawGrabBar(frame.draw, span.r0, ImVec2(span.r0.x, span.r1.y), ImGuiMouseCursor_ResizeEW);
        else if (span.onRightEdge)
            DrawGrabBar(frame.draw, ImVec2(span.r1.x, span.r0.y), span.r1, ImGuiMouseCursor_ResizeEW);
    }
    else if (sDrag.mode == DragMode::ResizeL)
    {
        DrawGrabBar(frame.draw, sDrag.r0, ImVec2(sDrag.r0.x, sDrag.r1.y), ImGuiMouseCursor_ResizeEW);
    }
    else if (sDrag.mode == DragMode::ResizeR)
    {
        DrawGrabBar(frame.draw, ImVec2(sDrag.r1.x, sDrag.r0.y), sDrag.r1, ImGuiMouseCursor_ResizeEW);
    }
}

static void DrawKeyRegion(KeyMapFrame& frame, BankFile::KeyRegion* keyRegion)
{
    frame.splitter.SetCurrentChannel(frame.draw, 0);

    KeyRegionSpan span;
    span.keyRegion = keyRegion;
    span.prev = keyRegion->getPrevNeighbor(*frame.instrument);
    span.next = keyRegion->getNextNeighbor(*frame.instrument);

    f32 ignored;
    GetKeyRect(frame, keyRegion->getKeyMin(), span.x0, ignored);

    if (keyRegion->getKeyMax() == 127)
        span.x1 = frame.canvasSize.x;
    else
        GetKeyRect(frame, keyRegion->getKeyMax() + 1, span.x1, ignored);

    span.r0 = ImVec2(frame.canvasPos.x + span.x0, frame.canvasPos.y);
    span.r1 = ImVec2(frame.canvasPos.x + span.x1, frame.canvasPos.y + frame.regionHeight);

    DrawKeyRegionGaps(frame, span);

    span.hoveredRegionY = frame.mouse.y >= span.r0.y && frame.mouse.y <= span.r1.y;

    span.onLeftEdge  = span.hoveredRegionY && (frame.mouse.x <= span.r0.x + cRegionEdgeSize) && (frame.mouse.x >= span.r0.x - cRegionEdgeSize);
    span.onRightEdge = span.hoveredRegionY && (frame.mouse.x >= span.r1.x - cRegionEdgeSize) && (frame.mouse.x <= span.r1.x + cRegionEdgeSize);

    if (span.keyRegion == sDrag.region)
    {
        sDrag.r0 = span.r0;
        sDrag.r1 = span.r1;
    }

    for (Item* velRegionItem : keyRegion->getVelocityRegionList())
        DrawVelocityRegion(frame, span, static_cast<BankFile::VelocityRegion*>(velRegionItem));

    frame.splitter.SetCurrentChannel(frame.draw, 1);

    if (frame.drawGrabBar)
        DrawKeyRegionGrabBar(frame, span);
}

static void DrawKeyMapGrid(const KeyMapFrame& frame)
{
    for (s32 gk = frame.beginNote; gk <= frame.endNote; gk++)
    {
        if (gk % 12 != 0)
            continue;

        f32 gMin, gMax;
        GetKeyRect(frame, gk, gMin, gMax);
        const f32 lx = frame.canvasPos.x + gMin;
        frame.draw->AddLine(ImVec2(lx, frame.canvasPos.y), ImVec2(lx, frame.canvasPos.y + frame.regionHeight), GetPianoRollPalette().gridLineOctave);
    }

    {
        static const s32 kVelTicks[] = { 0, 16, 32, 48, 64, 80, 96, 112, 127 };
        for (s32 i = 0; i < (s32)(sizeof(kVelTicks) / sizeof(kVelTicks[0])); i++)
        {
            const f32 gy = frame.canvasPos.y + VelToY(frame, (f32)kVelTicks[i]);
            if (gy < frame.canvasPos.y - 0.5f || gy > frame.canvasPos.y + frame.regionHeight + 0.5f)
                continue;

            frame.draw->AddLine(ImVec2(frame.visibleCanvasPos.x, gy), ImVec2(frame.visibleCanvasPos.x + frame.width, gy), GetPianoRollPalette().gridLineVel);
            frame.draw->AddLine(ImVec2(frame.visibleCanvasPos.x, gy), ImVec2(frame.visibleCanvasPos.x + cVelocityTickWidth, gy), GetPianoRollPalette().gridLineVelTick, 1.0f);
        }

        static const s32 cVelLabels[] = { 127, 0, 64, 32, 96, 16, 48, 80, 112 };

        const f32 fontSize = ImGui::GetFontSize();
        const f32 labelX = frame.visibleCanvasPos.x + cVelocityTickWidth + ImGui::GetStyle().ItemInnerSpacing.x;
        const ImU32 labelColor = (GetPianoRollPalette().gridLineVelTick & ~IM_COL32_A_MASK) | (static_cast<ImU32>(cVelocityLabelAlpha) << IM_COL32_A_SHIFT);
        f32 labelY[IM_ARRAYSIZE(cVelLabels)];
        s32 labelNum = 0;

        for (s32 i = 0; i < IM_ARRAYSIZE(cVelLabels) && frame.regionHeight >= fontSize; i++)
        {
            const f32 gy = frame.canvasPos.y + VelToY(frame, static_cast<f32>(cVelLabels[i]));
            if (gy < frame.canvasPos.y - 0.5f || gy > frame.canvasPos.y + frame.regionHeight + 0.5f)
                continue;

            const f32 y = ImFloor(sead::Mathf::clamp2(frame.canvasPos.y, gy - fontSize * 0.5f, frame.canvasPos.y + frame.regionHeight - fontSize));

            bool overlap = false;
            for (s32 j = 0; j < labelNum; j++)
            {
                if (fabsf(y - labelY[j]) < fontSize * cVelocityLabelGapEm)
                    overlap = true;
            }

            if (overlap)
                continue;

            labelY[labelNum++] = y;

            char text[8];
            snprintf(text, sizeof(text), "%d", cVelLabels[i]);
            frame.draw->AddText(ImVec2(labelX, y), labelColor, text);
        }
    }
}

static void AddKeyRegion(KeyMapFrame& frame)
{
    snd::internal::driver::SoundThreadLock lock;

    BankFile::KeyRegion* newRegion = nullptr;
    if (frame.addMode == AddMode::Front)
    {
        if (frame.addNode)
        {
            newRegion = new BankFile::KeyRegion(0, frame.addNode->getKeyMin() - 1);
            frame.addNode->insertFront(newRegion);
            SetUnsavedChanges(true);
        }
        else
        {
            newRegion = new BankFile::KeyRegion(0, 127);
            frame.instrument->getKeyRegionList().pushBack(newRegion);
            SetUnsavedChanges(true);
        }
    }
    else if (frame.addMode == AddMode::Back)
    {
        SEAD_ASSERT(frame.addNode);
        newRegion = new BankFile::KeyRegion(frame.addNode->getKeyMax() + 1, 127);
        frame.addNode->insertBack(newRegion);
        SetUnsavedChanges(true);
    }
    else if (frame.addMode == AddMode::After)
    {
        SEAD_ASSERT(frame.addNode);
        BankFile::KeyRegion* next = frame.addNode->getNext(*frame.instrument);
        u8 newMin = frame.addNode->getKeyMax() + 1;
        u8 newMax = next ? next->getKeyMin() - 1 : 127;

        newRegion = new BankFile::KeyRegion(newMin, newMax);
        frame.addNode->insertBack(newRegion);
        SetUnsavedChanges(true);
    }

    newRegion->setId(0);
    newRegion->setEnableName(true);
    newRegion->getName() = "KeyRegion";

    sBfsar.updateList(frame.instrument->getKeyRegionList());
    SetUnsavedChanges(true);

    BankFile::VelocityRegion* newVelRegion = new BankFile::VelocityRegion(0, 127);
    newVelRegion->setId(0);
    newVelRegion->setEnableName(true);
    newVelRegion->getName() = "VelocityRegion";

    newRegion->getVelocityRegionList().pushBack(newVelRegion);
    SetUnsavedChanges(true);

    SelectVelocity(newRegion, newVelRegion);
}

static void SelectRegionByShortcut()
{
    if (sDrag.mode == DragMode::None && sContextKeyRegion && sSubSelectedItem && sSubSelectedItem->getItemType() == Item::ItemType::BankFileVelocityRegion && ImGui::IsWindowFocused())
    {
        BankFile::VelocityRegion* velRegion = static_cast<BankFile::VelocityRegion*>(sSubSelectedItem);
        BankFile::KeyRegion* keyRegion = sContextKeyRegion;
        if (shortcuts::Pressed(shortcuts::Action::SelectNext))
        {
            BankFile::VelocityRegion* prev = velRegion->getPrev(*keyRegion);
            if (prev)
            {
                SelectVelocity(keyRegion, prev);
            }
        }

        if (shortcuts::Pressed(shortcuts::Action::SelectPrevious))
        {
            BankFile::VelocityRegion* next = velRegion->getNext(*keyRegion);
            if (next)
            {
                SelectVelocity(keyRegion, next);
            }
        }
    }
}

static void DragVelocityRegionEdge(KeyMapFrame& frame)
{
    KeyMapView& view = frame.view;

    f32 y = frame.mouse.y + view.edgeOffset - frame.canvasPos.y;
    s32 mouseVel = YToBorderVel(frame, y);

    BankFile::VelocityRegion* velRegion = sDrag.velRegion;
    BankFile::VelocityRegion* vNext = sDrag.vNext;

    if (mouseVel < velRegion->getVelocityMin())
    {
        mouseVel = velRegion->getVelocityMin();
    }

    if (mouseVel > vNext->getVelocityMax() - 1)
    {
        mouseVel = vNext->getVelocityMax() - 1;
    }

    snd::internal::driver::SoundThreadLock lock;

    if (mouseVel != velRegion->getVelocityMax() || mouseVel + 1 != vNext->getVelocityMin())
    {
        velRegion->setVelocityMax(mouseVel);
        vNext->setVelocityMin(mouseVel + 1);
        SetUnsavedChanges(true);
    }
}

static void DragKeyRegionEdge(KeyMapFrame& frame)
{
    KeyMapView& view = frame.view;

    view.mouseKey = XToKey(frame, frame.mouse.x + view.edgeOffset + (sDrag.mode == DragMode::ResizeL ? 1.0f : -1.0f));

    BankFile::KeyRegion* region = sDrag.region;

    if (sDrag.mode == DragMode::ResizeL)
    {
        if (view.mouseKey > region->getKeyMax())
        {
            view.mouseKey = region->getKeyMax();
        }

        if (region->getPrev(*frame.instrument))
        {
            BankFile::KeyRegion* prev = region->getPrev(*frame.instrument);
            if (view.mouseKey < prev->getKeyMin() + 1)
            {
                view.mouseKey = prev->getKeyMin() + 1;
            }
        }

        if (sDrag.prev == nullptr && region->getPrev(*frame.instrument))
        {
            view.mouseKey = std::clamp(view.mouseKey, region->getPrev(*frame.instrument)->getKeyMax() + 1, (s32)region->getKeyMax());
        }
    }
    else if (sDrag.mode == DragMode::ResizeR)
    {
        if (view.mouseKey < region->getKeyMin())
        {
            view.mouseKey = region->getKeyMin();
        }

        if (region->getNext(*frame.instrument))
        {
            BankFile::KeyRegion* next = region->getNext(*frame.instrument);
            if (view.mouseKey > next->getKeyMax() - 1)
            {
                view.mouseKey = next->getKeyMax() - 1;
            }
        }

        if (sDrag.next == nullptr && region->getNext(*frame.instrument))
        {
            view.mouseKey = std::clamp(view.mouseKey, (s32)region->getKeyMin(), region->getNext(*frame.instrument)->getKeyMin() - 1);
        }
    }

    if (sDrag.mode == DragMode::ResizeL)
    {
        snd::internal::driver::SoundThreadLock lock;

        s32 newMin = view.mouseKey;

        const u8 oldMin = region->getKeyMin();
        const u8 oldPrevMax = sDrag.prev ? sDrag.prev->getKeyMax() : 0;

        if (sDrag.prev)
            sDrag.prev->setKeyMax(newMin - 1, *frame.instrument);

        region->setKeyMin(newMin, *frame.instrument);

        if (sDrag.prev)
            sDrag.prev->setKeyMax(region->getKeyMin() - 1, *frame.instrument);

        if (region->getKeyMin() != oldMin || (sDrag.prev && sDrag.prev->getKeyMax() != oldPrevMax))
            SetUnsavedChanges(true);
    }
    else if (sDrag.mode == DragMode::ResizeR)
    {
        snd::internal::driver::SoundThreadLock lock;

        s32 newMax = view.mouseKey;

        const u8 oldMax = region->getKeyMax();
        const u8 oldNextMin = sDrag.next ? sDrag.next->getKeyMin() : 0;

        if (sDrag.next)
            sDrag.next->setKeyMin(newMax + 1, *frame.instrument);

        region->setKeyMax(newMax, *frame.instrument);

        if (sDrag.next)
            sDrag.next->setKeyMin(region->getKeyMax() + 1, *frame.instrument);

        if (region->getKeyMax() != oldMax || (sDrag.next && sDrag.next->getKeyMin() != oldNextMin))
            SetUnsavedChanges(true);
    }
}

static void DrawKeyMapRegions(KeyMapFrame& frame)
{
    frame.splitter.Split(frame.draw, 2);

    if (frame.instrument->getKeyRegionList().size() == 0)
    {
        const ImVec2 gapMax(frame.canvasPos.x + frame.canvasSize.x, frame.canvasPos.y + frame.regionHeight);

        if (DrawKeyRegionGap(frame, frame.canvasPos, gapMax, ImGui::GetID(frame.instrument)))
        {
            frame.addMode = AddMode::Front;
            frame.addNode = nullptr;
        }
    }

    frame.drawGrabBar = sDrag.mode != DragMode::None;
    for (Item* keyRegionItem : frame.instrument->getKeyRegionList())
        DrawKeyRegion(frame, static_cast<BankFile::KeyRegion*>(keyRegionItem));

    frame.splitter.Merge(frame.draw);

    DrawKeyMapGrid(frame);

    if (frame.addMode != AddMode::None)
        AddKeyRegion(frame);

    SelectRegionByShortcut();

    if (sDrag.mode != DragMode::None && frame.mouseDown && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
        if (sDrag.mode == DragMode::ResizeVTop)
            DragVelocityRegionEdge(frame);
        else
            DragKeyRegionEdge(frame);
    }

    if (sDrag.mode != DragMode::None && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        sDrag = {};
    }

    if (sDrag.mode == DragMode::None && sContextKeyRegion && ImGui::IsWindowFocused() && shortcuts::Pressed(shortcuts::Action::DeleteSelection) && sSubSelectedItem && sSubSelectedItem->getItemType() == Item::ItemType::BankFileVelocityRegion)
    {
        BankFile::VelocityRegion* velRegion = static_cast<BankFile::VelocityRegion*>(sSubSelectedItem);
        BankFile::KeyRegion* keyRegion = sContextKeyRegion;
        DeleteVelocityRegion(keyRegion, velRegion);
    }

    if (sContextKeyRegion && ImGui::IsPopupOpen("VelocityRegionContextMenu") && sSubSelectedItem && sSubSelectedItem->getItemType() == Item::ItemType::BankFileVelocityRegion)
    {
        BankFile::VelocityRegion* velRegion = static_cast<BankFile::VelocityRegion*>(sSubSelectedItem);
        BankFile::KeyRegion* keyRegion = sContextKeyRegion;
        VelocityContextMenu(frame.instrument, keyRegion, velRegion);
    }
}

static void DrawKeyMapPlaceholder(const KeyMapFrame& frame)
{
    const char* text = messages::bank::cNoInstrumentSelected;
    ImVec2 ts = ImGui::CalcTextSize(text);

    f32 x = frame.canvasPos.x + frame.canvasSize.x / 2.0f - ts.x / 2.0f;
    f32 y = frame.canvasPos.y + frame.regionHeight / 2.0f - ts.y / 2.0f;

    frame.draw->AddText(ImVec2(x, y), GetPianoRollPalette().placeholderText, text);
}

static void DrawKeyMapKeyboard(KeyMapFrame& frame, s32 rootKey)
{
    KeyMapView& view = frame.view;

    ImGui::SetCursorScreenPos(ImVec2(frame.canvasPos.x, frame.canvasPos.y + frame.regionHeight));

    ImGui_PianoKeyboard(
        "Keyboard",
        ImVec2(frame.zoomedWidth, frame.keyboardHeight),
        &view.prevNote,
        frame.beginNote,
        frame.endNote,
        &KeyboardFunc,
        frame.instrument,
        rootKey
    );

    const s32 contextKey = TakeContextKey();
    if (contextKey >= 0)
    {
        BankFile::KeyRegion* keyRegion = nullptr;
        BankFile::VelocityRegion* velRegion = FindSelectedRegion(frame.instrument, &keyRegion);

        if (velRegion && velRegion->getRootKey() != contextKey)
        {
            velRegion->setRootKey(static_cast<u8>(contextKey));
            SetUnsavedChanges(true);
        }
    }
}

static void DrawKeyMapZoomBar(KeyMapFrame& frame)
{
    KeyMapView& view = frame.view;

    f32 leftNorm = view.scrollX / frame.zoomedWidth;
    f32 rightNorm = std::min(1.0f, (view.scrollX + frame.width) / frame.zoomedWidth);

    const ZoomScrollResult scrollResult = ZoomScrollBar("###PianoScroll", std::max(1.0f, frame.width), &leftNorm, &rightNorm, 1.0f / frame.zoomMax, 1.0f / frame.zoomMin);

    if (scrollResult == ZoomScrollResult::Reset)
    {
        RequestZoom(frame, frame.zoomMin, frame.width * 0.5f);
    }
    else if (scrollResult != ZoomScrollResult::None)
    {
        if (scrollResult == ZoomScrollResult::Zoomed)
        {
            view.zoom = view.zoomTarget = 1.0f / (rightNorm - leftNorm);
            view.scrollX = leftNorm * view.zoom * frame.width;
        }
        else
        {
            view.scrollX = leftNorm * frame.zoomedWidth;
        }

        view.scrollXTarget = view.scrollX;
        view.viewMoveTime = cPianoViewMoveTime;
    }
}

void DrawKeyboardWithRegions(f32 width, f32 keyboardHeight, f32 regionHeight, s32 beginNote, s32 endNote, BankFile::Instrument* instrument)
{
    static KeyMapView sView;

    s32 rootKey = -1;
    {
        BankFile::KeyRegion* selKey = nullptr;
        const BankFile::VelocityRegion* selVel = FindSelectedRegion(instrument, &selKey);
        if (selVel)
            rootKey = selVel->getRootKey();
    }

    KeyMapFrame frame = { sView, instrument, ImGui::GetWindowDrawList() };
    frame.width = width;
    frame.keyboardHeight = keyboardHeight;
    frame.regionHeight = regionHeight;
    frame.beginNote = beginNote;
    frame.endNote = endNote;
    frame.numKeys = std::max(1, endNote - beginNote + 1);
    frame.canvasPos = ImGui::GetCursorScreenPos();
    frame.visibleCanvasPos = frame.canvasPos;
    frame.canvasSize = ImVec2(width, regionHeight + keyboardHeight);

    if (sDrag.mode != DragMode::None)
    {
        frame.canvasPos = sDrag.initialCanvasPos;
    }

    ResetKeyMapView(frame);

    frame.minZoomedWidth = ImFloor(ImGui::GetFontSize() * cPianoMinNoteWidthEm) * frame.numKeys;
    frame.zoomMin = std::max(1.0f, frame.minZoomedWidth / std::max(1.0f, frame.width));
    frame.zoomMax = std::max(frame.zoomMin, ImGui::GetFontSize() * cPianoMaxNoteWidthEm * frame.numKeys / std::max(1.0f, frame.width));

    UpdateKeyMapViewInput(frame);
    AnimateKeyMapView(frame);
    UpdateKeyMapLayout(frame);

    frame.mouse = ImGui::GetIO().MousePos;
    frame.mouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    frame.mouseClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);

    UpdateVelocityScrollBar(frame);

    frame.draw->PushClipRect(frame.visibleCanvasPos, ImVec2(frame.visibleCanvasPos.x + frame.width, frame.visibleCanvasPos.y + frame.regionHeight + frame.keyboardHeight), true);

    // BG
    frame.draw->AddRectFilled(
        frame.canvasPos,
        ImVec2(frame.canvasPos.x + frame.canvasSize.x, frame.canvasPos.y + frame.canvasSize.y),
        GetPianoRollPalette().canvasBg
    );

    frame.draw->PushClipRect(
        ImVec2(frame.visibleCanvasPos.x, frame.canvasPos.y),
        ImVec2(frame.visibleCanvasPos.x + frame.width, frame.canvasPos.y + frame.regionHeight),
        true
    );

    if (instrument)
        DrawKeyMapRegions(frame);
    else
        DrawKeyMapPlaceholder(frame);

    frame.draw->PopClipRect();

    DrawVelocityScrollBar(frame);
    DrawKeyMapKeyboard(frame, rootKey);

    frame.draw->PopClipRect();

    ImVec2 afterKB = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2(frame.visibleCanvasPos.x, afterKB.y));

    DrawKeyMapZoomBar(frame);

    ImGui::Spacing();
    DrawRegionPanels(instrument);
}
