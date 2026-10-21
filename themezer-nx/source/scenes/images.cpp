#include "scenes/images.hpp"

#include "gfx/renderer.hpp"
#include "sys/jobs.hpp"
#include "sys/lock.hpp"
#include "sys/net.hpp"

#include <algorithm>
#include <mutex>
#include <vector>

namespace tz::scenes
{

namespace
{

constexpr size_t kKeep = 160;        // pictures kept, the least recently used going first
constexpr float kStillWanted = 2.5f; // seconds: a picture not looked at since is skipped when its turn comes

// When each picture was last looked at, for the worker to skip those scrolled past.
Lock wantedLock;
std::unordered_map<std::string, float> wanted;

void want(const std::string& url, float now)
{
    std::lock_guard held(wantedLock);
    wanted[url] = now;
}

bool stillWanted(const std::string& url, float now)
{
    std::lock_guard held(wantedLock);
    auto it = wanted.find(url);
    return it != wanted.end() && now - it->second < kStillWanted;
}

} // namespace

Images& images()
{
    static Images instance;
    return instance;
}

std::shared_ptr<gfx::Picture> Images::get(const std::string& url)
{
    if (url.empty())
        return nullptr;
    const float now = gfx::time();
    Entry& entry = pictures_[url];
    entry.used = now;
    if (entry.picture || entry.failed)
        return entry.picture;
    want(url, now);
    if (entry.requested)
        return nullptr;
    entry.requested = true;
    jobs::run(jobs::Lane::Images, [url] {
        const auto skipped = [url] {
            jobs::post([url] {
                auto& images = scenes::images();
                if (auto it = images.pictures_.find(url); it != images.pictures_.end())
                    it->second.requested = false;
            });
        };
        if (!stillWanted(url, gfx::time()))
        {
            skipped();
            return;
        }
        net::Response response = net::get(url, nullptr, nullptr, 30);
        SDL_Surface* surface = response.ok() ? gfx::decodeImage(response.body.data(), response.body.size()) : nullptr;
        const bool failed = !surface;
        jobs::post([url, surface, failed] {
            auto& images = scenes::images();
            auto it = images.pictures_.find(url);
            if (it == images.pictures_.end())
            {
                SDL_FreeSurface(surface);
                return;
            }
            it->second.requested = false;
            it->second.failed = failed;
            it->second.picture = gfx::Picture::fromSurface(surface);
        });
    });
    return nullptr;
}

std::shared_ptr<gfx::Picture> Images::placeholder(const std::string& thumbHash)
{
    if (thumbHash.empty())
        return nullptr;
    Entry& entry = placeholders_[thumbHash];
    entry.used = gfx::time();
    if (!entry.picture && !entry.failed)
    {
        entry.picture = gfx::Picture::fromSurface(gfx::thumbHashSurface(thumbHash));
        entry.failed = !entry.picture;
    }
    return entry.picture;
}

void Images::tick()
{
    const float now = gfx::time();
    if (now - lastSweep_ < 1)
        return;
    lastSweep_ = now;
    const auto sweep = [now](std::unordered_map<std::string, Entry>& map, size_t keep) {
        if (map.size() <= keep)
            return;
        std::vector<std::pair<float, std::string>> byAge;
        for (const auto& [key, entry] : map)
        {
            // what's on screen, or being downloaded, stays
            if (now - entry.used > 1 && !entry.requested)
                byAge.emplace_back(entry.used, key);
        }
        std::sort(byAge.begin(), byAge.end());
        for (size_t i = 0; i < byAge.size() && map.size() > keep; ++i)
            map.erase(byAge[i].second);
    };
    sweep(pictures_, kKeep);
    sweep(placeholders_, kKeep * 2);
}

void Images::clear()
{
    pictures_.clear();
    placeholders_.clear();
}

} // namespace tz::scenes
