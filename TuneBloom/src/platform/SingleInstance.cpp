#include <platform/SingleInstance.h>

#if defined(SEAD_PLATFORM_WINDOWS)

#include <basis/win/seadWindows.h>

#include <core/PlayerFile.h>
#include <ui/file/OpenQueue.h>

#include <cstdio>

namespace singleinstance
{

static constexpr DWORD cPathBufferSize = 2048;
static constexpr DWORD cBusyWaitMs     = 1000;

static void FormatPipeName(char* out, size_t size)
{
    DWORD sessionId = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &sessionId);

    std::snprintf(out, size, "\\\\.\\pipe\\TuneBloom.Open.%lu", static_cast<unsigned long>(sessionId));
}

static HANDLE ConnectToRunningApp(const char* pipeName)
{
    HANDLE pipe = CreateFileA(pipeName, GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);

    if (pipe != INVALID_HANDLE_VALUE || GetLastError() != ERROR_PIPE_BUSY)
        return pipe;

    if (!WaitNamedPipeA(pipeName, cBusyWaitMs))
        return INVALID_HANDLE_VALUE;

    return CreateFileA(pipeName, GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
}

bool ForwardToRunningApp(const char* path)
{
    if (!playerfile::IsPlayerFile(path))
        return false;

    char fullPath[cPathBufferSize];
    const DWORD length = GetFullPathNameA(path, cPathBufferSize, fullPath, nullptr);

    if (length == 0 || length >= cPathBufferSize)
        return false;

    char pipeName[64];
    FormatPipeName(pipeName, sizeof(pipeName));

    HANDLE pipe = ConnectToRunningApp(pipeName);

    if (pipe == INVALID_HANDLE_VALUE)
        return false;

    ULONG serverProcessId = 0;

    if (GetNamedPipeServerProcessId(pipe, &serverProcessId))
        AllowSetForegroundWindow(serverProcessId);

    DWORD written = 0;
    const bool sent = WriteFile(pipe, fullPath, length, &written, nullptr) && written == length;

    CloseHandle(pipe);

    return sent;
}

static void ReceivePath(HANDLE pipe)
{
    char path[cPathBufferSize];
    DWORD total = 0;
    DWORD read = 0;

    while (total < cPathBufferSize - 1 && ReadFile(pipe, path + total, cPathBufferSize - 1 - total, &read, nullptr) && read > 0)
        total += read;

    path[total] = '\0';

    if (total > 0)
        openqueue::PushExternal(path);
}

static DWORD WINAPI ListenThread(LPVOID)
{
    char pipeName[64];
    FormatPipeName(pipeName, sizeof(pipeName));

    for (;;)
    {
        HANDLE pipe = CreateNamedPipeA(pipeName, PIPE_ACCESS_INBOUND | FILE_FLAG_FIRST_PIPE_INSTANCE, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 0, cPathBufferSize, 0, nullptr);

        if (pipe == INVALID_HANDLE_VALUE)
            return 0;

        if (ConnectNamedPipe(pipe, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED)
            ReceivePath(pipe);

        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }
}

void StartListening()
{
    HANDLE thread = CreateThread(nullptr, 0, &ListenThread, nullptr, 0, nullptr);

    if (thread)
        CloseHandle(thread);
}

}

#else

namespace singleinstance
{

bool ForwardToRunningApp(const char*)
{
    return false;
}

void StartListening()
{
}

}

#endif
