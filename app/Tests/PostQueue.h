#pragma once

#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>

namespace djmix::test
{

// Collects what a controller posts to the "message thread", for the test to run.
struct PostQueue
{
    std::mutex mutex;
    std::condition_variable posted;
    std::deque<std::function<void()>> queue;

    std::function<void (std::function<void()>)> poster()
    {
        return [this] (std::function<void()> f)
        {
            {
                std::lock_guard lock (mutex);
                queue.push_back (std::move (f));
            }
            posted.notify_all();
        };
    }

    // Runs what has been posted, waiting up to `seconds` for something.
    bool runNext (double seconds = 10.0)
    {
        std::function<void()> f;
        {
            std::unique_lock lock (mutex);
            if (! posted.wait_for (lock, std::chrono::duration<double> (seconds), [this] { return ! queue.empty(); }))
                return false;
            f = std::move (queue.front());
            queue.pop_front();
        }
        f();
        return true;
    }
};

} // namespace djmix::test
