// Work off the main thread, as Themezer-3DS does it: the main thread draws and handles input, nothing else. A job runs
// on a lane's worker, and what it hands back runs on the main thread at the start of the next frame.

#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <utility>

namespace tz
{

// Asks work to stop early, and drops its result. Copies share the one flag.
class CancelToken
{
public:
    void cancel() const { flag_->store(true); }
    bool cancelled() const { return flag_->load(); }

private:
    std::shared_ptr<std::atomic<bool>> flag_ = std::make_shared<std::atomic<bool>>(false);
};

namespace jobs
{

enum class Lane : uint8_t
{
    Network, // the API, and downloads: one at a time
    Images,  // previews: a few at once, each worker with a connection of its own
};

void init();
void exit();

void run(Lane lane, std::function<void()> work);
// From any thread: runs on the main thread, during the next frame.
void post(std::function<void()> done);
// The main thread, once a frame.
void drain();

// work on the lane, then done with its result on the main thread, unless the token was cancelled by then.
template <class Result>
void async(Lane lane, CancelToken token, std::function<Result(const CancelToken&)> work,
           std::function<void(Result)> done)
{
    run(lane, [token, work = std::move(work), done = std::move(done)]() mutable {
        if (token.cancelled())
            return;
        auto result = std::make_shared<Result>(work(token));
        post([token, result, done = std::move(done)]() mutable {
            if (!token.cancelled())
                done(std::move(*result));
        });
    });
}

} // namespace jobs
} // namespace tz
