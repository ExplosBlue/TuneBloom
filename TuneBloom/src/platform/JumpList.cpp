#include <platform/JumpList.h>

#if defined(SEAD_PLATFORM_WINDOWS)

#include <basis/win/seadWindows.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <messages/Messages.h>

namespace jumplist
{

using Microsoft::WRL::ComPtr;

static constexpr DWORD cPathBufferSize = 2048;
static constexpr s32   cTextBufferSize = 128;

static const PROPERTYKEY cTitleKey = { { 0xF29F85E0, 0x4FF9, 0x1068, { 0xAB, 0x91, 0x08, 0x00, 0x2B, 0x27, 0xB3, 0xD9 } }, 2 };

static bool ToWide(const char* text, wchar_t* out, s32 size)
{
    return MultiByteToWideChar(CP_UTF8, 0, text, -1, out, size) > 0;
}

static bool SetLinkTitle(IShellLinkW* link, const char* text)
{
    wchar_t title[cTextBufferSize];
    ComPtr<IPropertyStore> store;

    if (!ToWide(text, title, cTextBufferSize) || FAILED(link->QueryInterface(IID_PPV_ARGS(&store))))
        return false;

    PROPVARIANT value = {};
    value.vt = VT_LPWSTR;
    value.pwszVal = title;

    return SUCCEEDED(store->SetValue(cTitleKey, value)) && SUCCEEDED(store->Commit());
}

static bool CreatePlayerTask(const wchar_t* exePath, ComPtr<IShellLinkW>* outLink)
{
    wchar_t argument[cTextBufferSize];
    ComPtr<IShellLinkW> link;

    if (!ToWide(cPlayerArgument, argument, cTextBufferSize))
        return false;

    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link))))
        return false;

    link->SetPath(exePath);
    link->SetArguments(argument);
    link->SetIconLocation(exePath, 0);

    if (!SetLinkTitle(link.Get(), messages::playerframe::cWindowTitle))
        return false;

    *outLink = link;
    return true;
}

static bool AddTasks(ICustomDestinationList* list, IShellLinkW* task)
{
    ComPtr<IObjectCollection> tasks;
    ComPtr<IObjectArray> taskArray;

    if (FAILED(CoCreateInstance(CLSID_EnumerableObjectCollection, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&tasks))))
        return false;

    if (FAILED(tasks->AddObject(task)) || FAILED(tasks.As(&taskArray)))
        return false;

    return SUCCEEDED(list->AddUserTasks(taskArray.Get()));
}

static void CommitJumpList()
{
    wchar_t exePath[cPathBufferSize];
    const DWORD length = GetModuleFileNameW(nullptr, exePath, cPathBufferSize);

    if (length == 0 || length >= cPathBufferSize)
        return;

    ComPtr<IShellLinkW> task;
    ComPtr<ICustomDestinationList> list;
    ComPtr<IObjectArray> removed;
    UINT slotNum = 0;

    if (!CreatePlayerTask(exePath, &task))
        return;

    if (FAILED(CoCreateInstance(CLSID_DestinationList, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&list))))
        return;

    if (FAILED(list->BeginList(&slotNum, IID_PPV_ARGS(&removed))))
        return;

    if (AddTasks(list.Get(), task.Get()))
        list->CommitList();
    else
        list->AbortList();
}

static DWORD WINAPI UpdateThread(LPVOID)
{
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)))
        return 0;

    CommitJumpList();
    CoUninitialize();

    return 0;
}

void Update()
{
    HANDLE thread = CreateThread(nullptr, 0, &UpdateThread, nullptr, 0, nullptr);

    if (thread)
        CloseHandle(thread);
}

}

#else

namespace jumplist
{

void Update()
{
}

}

#endif
