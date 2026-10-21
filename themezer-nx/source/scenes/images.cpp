#include "scenes/images.hpp"

#include "gfx/renderer.hpp"
#include "sys/jobs.hpp"
#include "sys/lock.hpp"
#include "sys/net.hpp"

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <vector>

namespace tz::scenes
{

namespace
{

// pictures kept, the least recently used going first: about 120 thumbnails
constexpr size_t kKeepBytes = 48 * 1024 * 1024;
constexpr size_t kKeepPlaceholders = 320;
constexpr float kStillWanted = 2.5f; // seconds: a picture not looked at since is skipped when its turn comes
// a card that's only flashing past, the grid scrolling fast, isn't worth downloading for
constexpr float kSettle = 0.12f;

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
    return load(url, [url] {
        net::Response response = net::get(url, nullptr, nullptr, 30);
        return response.ok() ? gfx::decodeImage(response.body.data(), response.body.size()) : nullptr;
    });
}

std::shared_ptr<gfx::Picture> Images::load(const std::string& key, std::function<SDL_Surface*()> make)
{
    const float now = gfx::time();
    Entry& entry = pictures_[key];
    if (now - entry.used > 0.25f)
        entry.seen = now;
    entry.used = now;
    // a picture that didn't come is tried again, a while later
    if (entry.failed && now - entry.failedAt > 10)
        entry.failed = false;
    if (entry.picture || entry.failed)
        return entry.picture;
    want(key, now);
    if (entry.requested || now - entry.seen < kSettle)
        return nullptr;
    entry.requested = true;
    jobs::run(jobs::Lane::Images, [key, make = std::move(make)] {
        if (!stillWanted(key, gfx::time()))
        {
            jobs::post([key] {
                auto& images = scenes::images();
                if (auto it = images.pictures_.find(key); it != images.pictures_.end())
                    it->second.requested = false;
            });
            return;
        }
        SDL_Surface* surface = make();
        const bool failed = !surface;
        jobs::post([key, surface, failed] {
            auto& images = scenes::images();
            auto it = images.pictures_.find(key);
            if (it == images.pictures_.end())
            {
                SDL_FreeSurface(surface);
                return;
            }
            it->second.requested = false;
            it->second.failed = failed;
            it->second.failedAt = gfx::time();
            it->second.picture = gfx::Picture::fromSurface(surface);
        });
    });
    return nullptr;
}

void Images::forget(const std::string& key)
{
    if (auto it = pictures_.find(key); it != pictures_.end() && !it->second.requested)
        pictures_.erase(it);
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
    // how much a picture takes, as a texture
    const auto bytes = [](const Entry& entry) {
        return entry.picture ? static_cast<size_t>(entry.picture->width()) * entry.picture->height() * 4 : 0;
    };
    const auto sweep = [&](std::unordered_map<std::string, Entry>& map, size_t keepBytes, size_t keepCount) {
        size_t total = 0;
        for (const auto& [key, entry] : map)
            total += bytes(entry);
        if (total <= keepBytes && map.size() <= keepCount)
            return;
        std::vector<std::pair<float, std::string>> byAge;
        for (const auto& [key, entry] : map)
        {
            // what's on screen, or being downloaded, stays
            if (now - entry.used > 1 && !entry.requested)
                byAge.emplace_back(entry.used, key);
        }
        std::sort(byAge.begin(), byAge.end());
        for (size_t i = 0; i < byAge.size() && (total > keepBytes || map.size() > keepCount); ++i)
        {
            auto it = map.find(byAge[i].second);
            total -= bytes(it->second);
            map.erase(it);
        }
    };
    sweep(pictures_, kKeepBytes, SIZE_MAX);
    sweep(placeholders_, SIZE_MAX, kKeepPlaceholders);
}

void Images::clear()
{
    pictures_.clear();
    placeholders_.clear();
}

} // namespace tz::scenes
