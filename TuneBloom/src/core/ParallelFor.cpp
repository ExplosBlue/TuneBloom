#include <core/ParallelFor.h>

#include <algorithm>
#include <atomic>
#include <thread>
#include <vector>

static constexpr u32 cThreadMax = 8;

void ParallelFor(u32 count, const std::function<void(u32)>& func)
{
    const u32 threadNum = std::min({ count, std::max(std::thread::hardware_concurrency(), 1u), cThreadMax });

    if (threadNum <= 1)
    {
        for (u32 i = 0; i < count; i++)
            func(i);

        return;
    }

    std::atomic<u32> next(0);

    auto work = [&]()
    {
        for (u32 i = next.fetch_add(1); i < count; i = next.fetch_add(1))
            func(i);
    };

    std::vector<std::thread> threads;
    threads.reserve(threadNum - 1);

    for (u32 i = 1; i < threadNum; i++)
        threads.emplace_back(work);

    work();

    for (std::thread& thread : threads)
        thread.join();
}
