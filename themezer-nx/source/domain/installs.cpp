#include "domain/installs.hpp"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <optional>

namespace tz::installs
{

namespace
{

constexpr const char* kFolder = "/Themes/ThemezerNX";
constexpr const char* kTemporaryFolder = "/Themes/ThemezerNX/.tmp";

constexpr const char* kInstallerPaths[] = {
    "/switch/NXThemesInstaller.nro",
    "/switch/NXThemesInstaller/NXThemesInstaller.nro",
    "/switch/Switch_themes_Installer/NXThemesInstaller.nro",
};

std::array<std::optional<Queued>, kSlotCount> slots;

// in the names of the files, as the app has always named them (the website's names differ a little)
constexpr const char* kFileLabels[themezer::kTargetCount] = {"Home Menu",     "Lock Screen", "All Apps", "Settings",
                                                             "Player Select", "User Page",   "News"};

const char* fileLabel(int target)
{
    return target >= 0 && target < themezer::kTargetCount ? kFileLabels[target] : "Unknown";
}

// A name safe for the SD card's FAT: no characters it forbids, nothing outside ASCII, single spaces.
std::string safe(const std::string& name)
{
    static const std::string kForbidden = "\\~#*{}/:<>?|\",";
    std::string out;
    bool lastWasSpace = true;
    for (unsigned char c : name)
    {
        if (c == ' ' || (c >= 9 && c <= 13))
        {
            if (!lastWasSpace)
                out += ' ';
            lastWasSpace = true;
        }
        else
        {
            out += kForbidden.find(static_cast<char>(c)) != std::string::npos || c < 32 || c > 126
                       ? '_'
                       : static_cast<char>(c);
            lastWasSpace = false;
        }
    }
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    return out.empty() ? "_" : out;
}

} // namespace

const char* installerPath()
{
    for (const char* path : kInstallerPaths)
    {
        if (access(path, F_OK) != -1)
            return path;
    }
    return nullptr;
}

std::string slotLabel(int slot)
{
    return slot == kSplashSlot ? "Splash" : themezer::targetLabel(slot);
}

int slotOf(const themezer::Item& item)
{
    if (item.kind == themezer::Kind::Splashes)
        return kSplashSlot;
    if (item.kind == themezer::Kind::Themes && item.target >= 0)
        return item.target;
    return -1;
}

std::string savedPath(const themezer::Item& item)
{
    const std::string name = safe(item.name) + " by " + safe(item.creator);
    if (item.kind == themezer::Kind::Splashes)
        return std::string(kFolder) + "/Splashes/" + name + " (" + (item.remote ? "remote-splash-" : "splash-") +
               safe(item.id) + ").bmp";
    const std::string file = name + " (" + safe(fileLabel(item.target)) + "-" + safe(item.id) + ").nxtheme";
    if (!item.packId.empty())
        return std::string(kFolder) + "/" + safe(item.packName) + " by " + safe(item.packCreator) + " (" +
               safe(item.packId) + ")/" + file;
    return std::string(kFolder) + "/" + file;
}

std::string temporaryPath(const themezer::Item& item)
{
    if (item.kind == themezer::Kind::Splashes)
        return std::string(kTemporaryFolder) + (item.remote ? "/remote-splash-" : "/splash-") + safe(item.id) + ".bmp";
    return std::string(kTemporaryFolder) + (item.remote ? "/remote-theme-" : "/theme-") + safe(item.id) + ".nxtheme";
}

void removeTemporary()
{
    DIR* dir = opendir(kTemporaryFolder);
    if (!dir)
        return;
    while (dirent* entry = readdir(dir))
    {
        const std::string name = entry->d_name;
        if (name != "." && name != "..")
            unlink((std::string(kTemporaryFolder) + "/" + name).c_str());
    }
    closedir(dir);
}

bool exists(const std::string& path)
{
    return access(path.c_str(), F_OK) != -1;
}

std::string itemKey(const themezer::Item& item)
{
    return std::to_string(static_cast<int>(item.kind)) + ":" + item.id;
}

void queue(int slot, std::string path, const themezer::Item& item)
{
    if (slot >= 0 && slot < kSlotCount)
        slots[static_cast<size_t>(slot)] = Queued{std::move(path), item.name, itemKey(item)};
}

bool isQueued(const themezer::Item& item)
{
    const std::string key = itemKey(item);
    for (const auto& slot : slots)
    {
        if (slot && slot->item == key)
            return true;
    }
    return false;
}

void unqueue(int slot)
{
    if (slot >= 0 && slot < kSlotCount)
        slots[static_cast<size_t>(slot)].reset();
}

const Queued* queued(int slot)
{
    if (slot < 0 || slot >= kSlotCount || !slots[static_cast<size_t>(slot)])
        return nullptr;
    return &*slots[static_cast<size_t>(slot)];
}

int queuedCount()
{
    int count = 0;
    for (const auto& slot : slots)
        count += slot ? 1 : 0;
    return count;
}

std::string installerArgs(const char* installer)
{
    // the installer splits its arguments at spaces: it reads "(_)" in a path as one
    std::string out = std::string(installer) + " installtheme=";
    bool first = true;
    for (const auto& slot : slots)
    {
        if (!slot)
            continue;
        if (!first)
            out += ',';
        first = false;
        for (char c : slot->path)
            out += c == ' ' ? std::string("(_)") : std::string(1, c);
    }
    return out;
}

} // namespace tz::installs
