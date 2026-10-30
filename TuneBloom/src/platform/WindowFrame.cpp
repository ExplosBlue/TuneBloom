#include <platform/WindowFrame.h>

#if defined(SEAD_PLATFORM_WINDOWS)
#include <basis/win/seadWindows.h>
#include <dwmapi.h>

#define GLFW_EXPOSE_NATIVE_WIN32
#endif

#include <GLFW/glfw3.h>

#if defined(SEAD_PLATFORM_WINDOWS)
#include <GLFW/glfw3native.h>
#endif

namespace windowframe
{

static bool IsInSpan(s32 x, const Span& span)
{
    return x >= span.left && x < span.right;
}

bool IsDragPoint(const CaptionArea& area, s32 x, s32 y)
{
    if (y < 0 || y >= area.height)
        return false;

    for (const Span& button : area.buttons)
    {
        if (IsInSpan(x, button))
            return false;
    }

    for (s32 i = 0; i < area.clientSpanNum; i++)
    {
        if (IsInSpan(x, area.clientSpans[i]))
            return false;
    }

    return true;
}

#if !defined(SEAD_PLATFORM_MACOSX)

#if defined(SEAD_PLATFORM_WINDOWS)

static constexpr DWORD cDwmUseImmersiveDarkMode    = 20;
static constexpr DWORD cDwmWindowCornerPreference = 33;
static constexpr DWORD cDwmBorderColor            = 34;
static constexpr DWORD cDwmCornerRound            = 2;

static constexpr WPARAM cButtonHits[cWindowButtonNum] = { HTMINBUTTON, HTMAXBUTTON, HTCLOSE };

static HWND sWindow = nullptr;
static WNDPROC sPrevWndProc = nullptr;
static CaptionArea sCaptionArea;
static ButtonState sButtonStates[cWindowButtonNum] = {};
static bool sIsLeaveTracked = false;

static bool HasCaption(HWND hwnd)
{
    return (GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_CAPTION) == WS_CAPTION;
}

static s32 CalcMaximizedInset(HWND hwnd)
{
    const UINT dpi = GetDpiForWindow(hwnd);

    return GetSystemMetricsForDpi(SM_CYFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
}

static s32 CalcCaptionHeight(HWND hwnd)
{
    RECT frame = {};
    const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE));
    const DWORD exStyle = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE));
    AdjustWindowRectExForDpi(&frame, style, FALSE, exStyle, GetDpiForWindow(hwnd));

    return -frame.top;
}

static LRESULT CalcClientSize(HWND hwnd, WPARAM wParam, LPARAM lParam)
{
    NCCALCSIZE_PARAMS* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
    const LONG top = params->rgrc[0].top;

    const LRESULT result = CallWindowProcW(sPrevWndProc, hwnd, WM_NCCALCSIZE, wParam, lParam);

    if (result != 0)
        return result;

    params->rgrc[0].top = top + (IsZoomed(hwnd) ? CalcMaximizedInset(hwnd) : 0);
    return 0;
}

static LRESULT HitTest(HWND hwnd, WPARAM wParam, LPARAM lParam)
{
    const LRESULT hit = CallWindowProcW(sPrevWndProc, hwnd, WM_NCHITTEST, wParam, lParam);

    if (hit != HTCLIENT)
        return hit;

    POINT point = { static_cast<s16>(LOWORD(lParam)), static_cast<s16>(HIWORD(lParam)) };
    ScreenToClient(hwnd, &point);

    if (!IsZoomed(hwnd) && point.y < GetSystemMetricsForDpi(SM_CYFRAME, GetDpiForWindow(hwnd)))
        return HTTOP;

    if (point.y >= sCaptionArea.height)
        return HTCLIENT;

    for (s32 i = 0; i < cWindowButtonNum; i++)
    {
        if (IsInSpan(point.x, sCaptionArea.buttons[i]))
            return static_cast<LRESULT>(cButtonHits[i]);
    }

    return IsDragPoint(sCaptionArea, point.x, point.y) ? HTCAPTION : HTCLIENT;
}

static void TrackLeave(HWND hwnd)
{
    if (sIsLeaveTracked)
        return;

    TRACKMOUSEEVENT track = {};
    track.cbSize = sizeof(track);
    track.dwFlags = TME_LEAVE | TME_NONCLIENT;
    track.hwndTrack = hwnd;
    sIsLeaveTracked = TrackMouseEvent(&track) != FALSE;
}

static s32 FindButton(WPARAM hit)
{
    for (s32 i = 0; i < cWindowButtonNum; i++)
    {
        if (cButtonHits[i] == hit)
            return i;
    }

    return -1;
}

static void ResetButtonStates(s32 keepIndex)
{
    for (s32 i = 0; i < cWindowButtonNum; i++)
    {
        if (i != keepIndex)
            sButtonStates[i] = ButtonState::Idle;
    }
}

static WPARAM GetButtonCommand(HWND hwnd, WindowButton button)
{
    switch (button)
    {
        case WindowButton::Minimize:
            return SC_MINIMIZE;
        case WindowButton::Maximize:
            return IsZoomed(hwnd) ? SC_RESTORE : SC_MAXIMIZE;
        case WindowButton::Close:
            return SC_CLOSE;
    }

    return 0;
}

static void OnButtonMouseMove(HWND hwnd, s32 index)
{
    ResetButtonStates(index);

    if (index < 0)
        return;

    if (sButtonStates[index] == ButtonState::Idle)
        sButtonStates[index] = ButtonState::Hovered;

    TrackLeave(hwnd);
}

static void OnButtonUp(HWND hwnd, s32 index)
{
    ResetButtonStates(index);

    if (sButtonStates[index] == ButtonState::Held)
        PostMessageW(hwnd, WM_SYSCOMMAND, GetButtonCommand(hwnd, static_cast<WindowButton>(index)), 0);

    sButtonStates[index] = ButtonState::Hovered;
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (!HasCaption(hwnd))
        return CallWindowProcW(sPrevWndProc, hwnd, msg, wParam, lParam);

    switch (msg)
    {
        case WM_NCCALCSIZE:
            if (wParam)
                return CalcClientSize(hwnd, wParam, lParam);
            break;

        case WM_NCHITTEST:
            return HitTest(hwnd, wParam, lParam);

        case WM_NCMOUSEMOVE:
            OnButtonMouseMove(hwnd, FindButton(wParam));
            break;

        case WM_NCMOUSELEAVE:
            sIsLeaveTracked = false;
            ResetButtonStates(-1);
            break;

        case WM_NCLBUTTONDOWN:
        case WM_NCLBUTTONDBLCLK:
        {
            const s32 index = FindButton(wParam);

            if (index < 0)
                break;

            ResetButtonStates(index);
            sButtonStates[index] = ButtonState::Held;
            return 0;
        }

        case WM_NCLBUTTONUP:
        {
            const s32 index = FindButton(wParam);

            if (index < 0)
            {
                ResetButtonStates(-1);
                break;
            }

            OnButtonUp(hwnd, index);
            return 0;
        }
    }

    return CallWindowProcW(sPrevWndProc, hwnd, msg, wParam, lParam);
}

static HWND GetHandle(GLFWwindow* window)
{
    return window ? glfwGetWin32Window(window) : nullptr;
}

void Install(GLFWwindow* window)
{
    HWND hwnd = GetHandle(window);

    if (!hwnd || sPrevWndProc)
        return;

    sWindow = hwnd;
    sPrevWndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&WndProc)));
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

bool IsCustom()
{
    return sWindow && HasCaption(sWindow);
}

void SetDark(GLFWwindow* window, bool isDark)
{
    HWND hwnd = GetHandle(window);

    if (!hwnd)
        return;

    const BOOL useDarkMode = isDark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, cDwmUseImmersiveDarkMode, &useDarkMode, sizeof(useDarkMode));
}

void SetRoundCorners(GLFWwindow* window, u32 borderColor)
{
    HWND hwnd = GetHandle(window);

    if (!hwnd)
        return;

    const DWORD corner = cDwmCornerRound;
    const COLORREF border = RGB(borderColor & 0xFF, (borderColor >> 8) & 0xFF, (borderColor >> 16) & 0xFF);

    DwmSetWindowAttribute(hwnd, cDwmWindowCornerPreference, &corner, sizeof(corner));
    DwmSetWindowAttribute(hwnd, cDwmBorderColor, &border, sizeof(border));
}

void SetCaptionArea(const CaptionArea& area)
{
    sCaptionArea = area;
}

ButtonState GetButtonState(WindowButton button)
{
    return sButtonStates[static_cast<s32>(button)];
}

bool IsWindowMaximized(GLFWwindow* window)
{
    HWND hwnd = GetHandle(window);

    return hwnd && IsZoomed(hwnd);
}

bool CanMaximize(GLFWwindow* window)
{
    HWND hwnd = GetHandle(window);

    return hwnd && (GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_MAXIMIZEBOX);
}

static s32 CalcSizeOffset(GLFWwindow* window)
{
    HWND hwnd = GetHandle(window);

    if (!hwnd || hwnd != sWindow || !HasCaption(hwnd) || IsZoomed(hwnd))
        return 0;

    return CalcCaptionHeight(hwnd);
}

#else

void Install(GLFWwindow*)
{
}

bool IsCustom()
{
    return false;
}

void SetDark(GLFWwindow*, bool)
{
}

void SetRoundCorners(GLFWwindow*, u32)
{
}

void SetCaptionArea(const CaptionArea&)
{
}

ButtonState GetButtonState(WindowButton)
{
    return ButtonState::Idle;
}

bool IsWindowMaximized(GLFWwindow* window)
{
    return window && glfwGetWindowAttrib(window, GLFW_MAXIMIZED);
}

bool CanMaximize(GLFWwindow*)
{
    return true;
}

static s32 CalcSizeOffset(GLFWwindow*)
{
    return 0;
}

#endif

bool HasDrawnButtons()
{
    return IsCustom();
}

f32 GetTitleBarInsetLeft()
{
    return 0.0f;
}

f32 FitTitleBarHeight(f32)
{
    return 0.0f;
}

static s32 ShrinkLimit(s32 value, s32 offset)
{
    return value == GLFW_DONT_CARE ? value : value - offset;
}

void SetSize(GLFWwindow* window, s32 width, s32 height)
{
    glfwSetWindowSize(window, width, height - CalcSizeOffset(window));
}

void SetSizeLimits(GLFWwindow* window, s32 minWidth, s32 minHeight, s32 maxWidth, s32 maxHeight)
{
    const s32 offset = CalcSizeOffset(window);

    glfwSetWindowSizeLimits(window, minWidth, ShrinkLimit(minHeight, offset), maxWidth, ShrinkLimit(maxHeight, offset));
}

#endif

}
