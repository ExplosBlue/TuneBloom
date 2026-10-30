#include <platform/WindowMove.h>

#if !defined(SEAD_PLATFORM_MACOSX)

#if defined(SEAD_PLATFORM_WINDOWS)

#include <basis/win/seadWindows.h>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

namespace windowmove
{

static constexpr UINT_PTR cFrameTimerId = 0x5442;

static WNDPROC sPrevWndProc = nullptr;

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
        case WM_ENTERSIZEMOVE:
        case WM_ENTERMENULOOP:
            SetTimer(hwnd, cFrameTimerId, USER_TIMER_MINIMUM, nullptr);
            break;

        case WM_EXITSIZEMOVE:
        case WM_EXITMENULOOP:
            KillTimer(hwnd, cFrameTimerId);
            break;

        case WM_TIMER:
            if (wParam != cFrameTimerId)
                break;

            RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
            return 0;
    }

    return CallWindowProcW(sPrevWndProc, hwnd, msg, wParam, lParam);
}

void KeepDrawing(GLFWwindow* window)
{
    HWND hwnd = glfwGetWin32Window(window);

    if (!hwnd || sPrevWndProc)
        return;

    sPrevWndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&WndProc)));
}

}

#else

namespace windowmove
{

void KeepDrawing(GLFWwindow*)
{
}

}

#endif

#endif
