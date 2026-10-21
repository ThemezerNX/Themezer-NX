#include "sys/jobs.hpp"

#include "sys/lock.hpp"

#include <switch.h>

#include <deque>
#include <mutex>
#include <vector>

namespace tz::jobs
{

namespace
{

// curl and its TLS want a deeper stack than the default
constexpr size_t kStackSize = 0x80000;
constexpr int kMaxWorkers = 3;

struct Queue
{
    Lock lock;
    Condition wake;
    std::deque<std::function<void()>> work;
    bool stopping = false;
    // the newest first: the previews just scrolled to before those scrolled past
    bool newestFirst = false;
    // libnx keeps pointers to a Thread it made, so they stay where they are: never copied or moved
    Thread threads[kMaxWorkers];
    int threadCount = 0;
};

Queue lanes[2];
constexpr int kWorkers[2] = {1, 3};
static_assert(kWorkers[0] <= kMaxWorkers && kWorkers[1] <= kMaxWorkers);

Lock postedLock;
std::vector<std::function<void()>> posted;

void worker(void* arg)
{
    Queue& queue = *static_cast<Queue*>(arg);
    while (true)
    {
        std::function<void()> work;
        {
            std::lock_guard held(queue.lock);
            while (!queue.stopping && queue.work.empty())
                queue.wake.wait(queue.lock);
            if (queue.stopping)
                return;
            if (queue.newestFirst)
            {
                work = std::move(queue.work.back());
                queue.work.pop_back();
            }
            else
            {
                work = std::move(queue.work.front());
                queue.work.pop_front();
            }
        }
        work();
    }
}

} // namespace

void init()
{
    for (size_t lane = 0; lane < std::size(lanes); ++lane)
    {
        Queue& queue = lanes[lane];
        queue.stopping = false;
        queue.newestFirst = static_cast<Lane>(lane) == Lane::Images;
        for (int i = 0; i < kWorkers[lane]; ++i)
        {
            Thread& thread = queue.threads[queue.threadCount];
            if (R_FAILED(threadCreate(&thread, worker, &queue, nullptr, kStackSize, 0x2C, -2)))
                continue;
            if (R_FAILED(threadStart(&thread)))
            {
                threadClose(&thread);
                continue;
            }
            ++queue.threadCount;
        }
    }
}

void exit()
{
    for (Queue& queue : lanes)
    {
        {
            std::lock_guard held(queue.lock);
            queue.stopping = true;
            queue.work.clear();
        }
        queue.wake.wakeAll();
        for (int i = 0; i < queue.threadCount; ++i)
        {
            threadWaitForExit(&queue.threads[i]);
            threadClose(&queue.threads[i]);
        }
        queue.threadCount = 0;
    }
    std::lock_guard held(postedLock);
    posted.clear();
}

void run(Lane lane, std::function<void()> work)
{
    Queue& queue = lanes[static_cast<size_t>(lane)];
    {
        std::lock_guard held(queue.lock);
        queue.work.push_back(std::move(work));
    }
    queue.wake.wakeOne();
}

void post(std::function<void()> done)
{
    std::lock_guard held(postedLock);
    posted.push_back(std::move(done));
}

void drain()
{
    std::vector<std::function<void()>> ready;
    {
        std::lock_guard held(postedLock);
        ready.swap(posted);
    }
    for (auto& done : ready)
        done();
}

} // namespace tz::jobs
