#pragma once

#include <atomic>
#include <functional>
#include <thread>

class BackgroundJob
{
public:
    ~BackgroundJob()
    {
        wait();
    }

    bool isBusy() const
    {
        return mThread.joinable();
    }

    void start(std::function<void()> work)
    {
        wait();

        mDone = false;
        mThread = std::thread([this, work = std::move(work)]()
        {
            work();
            mDone = true;
        });
    }

    void wait()
    {
        if (mThread.joinable())
            mThread.join();
    }

    bool finish()
    {
        if (!mThread.joinable() || !mDone)
            return false;

        mThread.join();

        return true;
    }

private:
    std::thread mThread;
    std::atomic<bool> mDone { false };
};
