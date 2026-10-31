#include <platform/SingleInstance.h>

#if defined(SEAD_PLATFORM_WINDOWS)

#include <basis/win/seadWindows.h>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

namespace singleinstance
{

static constexpr const wchar_t* cWindowProperty = L"TuneBloom.Open";
static constexpr const wchar_t* cStartLockName  = L"Local\\TuneBloom.Start";
static constexpr DWORD          cPathBufferSize = 2048;
static constexpr DWORD          cStartWaitMs    = 5000;
static constexpr UINT           cSendWaitMs     = 5000;

static constexpr Target cTargetOrder[] = { Target::OpenDocument, Target::Player, Target::Editor };

struct Offer
{
    const char* path = nullptr;
    DWORD size = 0;
    Target target = Target::Editor;
    bool isTaken = false;
};

static HANDLE sStartLock = nullptr;
static TakeFunc sTake = nullptr;
static WNDPROC sPrevWndProc = nullptr;

static void TakeStartLock()
{
    sStartLock = CreateSemaphoreW(nullptr, 1, 1, cStartLockName);

    if (sStartLock && WaitForSingleObject(sStartLock, cStartWaitMs) != WAIT_OBJECT_0)
    {
        CloseHandle(sStartLock);
        sStartLock = nullptr;
    }
}

static void ReleaseStartLock()
{
    if (!sStartLock)
        return;

    ReleaseSemaphore(sStartLock, 1, nullptr);
    CloseHandle(sStartLock);
    sStartLock = nullptr;
}

static BOOL CALLBACK OfferToWindow(HWND hwnd, LPARAM param)
{
    Offer* offer = reinterpret_cast<Offer*>(param);

    if (!GetPropW(hwnd, cWindowProperty))
        return TRUE;

    DWORD processId = 0;
    GetWindowThreadProcessId(hwnd, &processId);
    AllowSetForegroundWindow(processId);

    COPYDATASTRUCT data = {};
    data.dwData = static_cast<ULONG_PTR>(offer->target);
    data.cbData = offer->size;
    data.lpData = const_cast<char*>(offer->path);

    DWORD_PTR result = 0;

    if (!SendMessageTimeoutW(hwnd, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data), SMTO_ABORTIFHUNG, cSendWaitMs, &result) || !result)
        return TRUE;

    offer->isTaken = true;

    return FALSE;
}

bool IsSupported()
{
    return true;
}

bool ForwardToRunningApp(const char* path)
{
    char fullPath[cPathBufferSize];
    const DWORD length = GetFullPathNameA(path, cPathBufferSize, fullPath, nullptr);

    if (length == 0 || length >= cPathBufferSize)
        return false;

    TakeStartLock();

    for (Target target : cTargetOrder)
    {
        Offer offer;
        offer.path = fullPath;
        offer.size = length + 1;
        offer.target = target;

        EnumWindows(&OfferToWindow, reinterpret_cast<LPARAM>(&offer));

        if (offer.isTaken)
        {
            ReleaseStartLock();
            return true;
        }
    }

    return false;
}

static bool TakeCopiedPath(const COPYDATASTRUCT* data)
{
    if (!data || !data->lpData || data->cbData == 0 || data->cbData > cPathBufferSize || data->dwData > static_cast<ULONG_PTR>(Target::Editor))
        return false;

    const char* path = static_cast<const char*>(data->lpData);

    if (path[data->cbData - 1] != '\0')
        return false;

    return sTake(path, static_cast<Target>(data->dwData));
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_COPYDATA)
        return TakeCopiedPath(reinterpret_cast<const COPYDATASTRUCT*>(lParam)) ? TRUE : FALSE;

    if (msg == WM_NCDESTROY)
        RemovePropW(hwnd, cWindowProperty);

    return CallWindowProcW(sPrevWndProc, hwnd, msg, wParam, lParam);
}

void StartListening(GLFWwindow* window, TakeFunc take)
{
    HWND hwnd = window ? glfwGetWin32Window(window) : nullptr;

    if (hwnd && take && !sPrevWndProc)
    {
        sTake = take;
        sPrevWndProc = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(hwnd, GWLP_WNDPROC));
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&WndProc));
        SetPropW(hwnd, cWindowProperty, hwnd);
    }

    ReleaseStartLock();
}

}

#else

namespace singleinstance
{

bool IsSupported()
{
    return false;
}

bool ForwardToRunningApp(const char*)
{
    return false;
}

void StartListening(GLFWwindow*, TakeFunc)
{
}

}

#endif
