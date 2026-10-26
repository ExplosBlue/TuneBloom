#include <ui/file/OpenQueue.h>

#include <prim/seadSafeString.h>

#include <cstring>
#include <mutex>
#include <vector>

namespace openqueue
{

static constexpr s32 cExternalPathMax  = 512;
static constexpr s32 cExternalCountMax = 64;

static std::mutex sExternalMutex;
static sead::FixedSafeString<cExternalPathMax> sExternalPaths[cExternalCountMax];
static s32 sExternalCount = 0;

static std::vector<Request> sRequests;

void PushExternal(const char* path)
{
    if (!path || !path[0] || std::strlen(path) >= static_cast<size_t>(cExternalPathMax))
        return;

    std::lock_guard<std::mutex> lock(sExternalMutex);

    if (sExternalCount < cExternalCountMax)
        sExternalPaths[sExternalCount++] = path;
}

static void TakeExternalPaths()
{
    std::lock_guard<std::mutex> lock(sExternalMutex);

    for (s32 i = 0; i < sExternalCount; i++)
        sRequests.push_back({ sExternalPaths[i].cstr(), Mode::Pin });

    sExternalCount = 0;
}

void Push(const char* path, Mode mode)
{
    if (!path || !path[0])
        return;

    TakeExternalPaths();

    sRequests.push_back({ path, mode });
}

bool Take(Request* outRequest)
{
    TakeExternalPaths();

    if (sRequests.empty())
        return false;

    *outRequest = std::move(sRequests.front());
    sRequests.erase(sRequests.begin());

    return true;
}

bool IsEmpty()
{
    TakeExternalPaths();

    return sRequests.empty();
}

void Clear()
{
    TakeExternalPaths();

    sRequests.clear();
}

}
