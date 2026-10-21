// The previews the screens show: downloaded and decoded on the images worker as they're looked at, kept while they're
// used, and the ThumbHash placeholders that stand in for them meanwhile.

#pragma once

#include "gfx/texture.hpp"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace tz::scenes
{

class Images
{
public:
    // The picture at url when it's in; asked for otherwise, and nullptr until it comes.
    std::shared_ptr<gfx::Picture> get(const std::string& url);
    // The same for a picture make makes, on the images worker: one from the SD card, say. key tells them apart.
    std::shared_ptr<gfx::Picture> load(const std::string& key, std::function<SDL_Surface*()> make);
    // Lets go of a picture, for one whose file is gone.
    void forget(const std::string& key);
    // The blurry placeholder of a ThumbHash.
    std::shared_ptr<gfx::Picture> placeholder(const std::string& thumbHash);
    // Once a frame: lets go of what hasn't been looked at for a while when there's a lot.
    void tick();
    void clear();

private:
    struct Entry
    {
        std::shared_ptr<gfx::Picture> picture;
        bool requested = false;
        bool failed = false;
        float used = 0;
        float seen = 0; // when it came on screen, this time
        float failedAt = 0;
    };
    std::unordered_map<std::string, Entry> pictures_;
    std::unordered_map<std::string, Entry> placeholders_;
    float lastSweep_ = 0;
};

Images& images();

} // namespace tz::scenes
