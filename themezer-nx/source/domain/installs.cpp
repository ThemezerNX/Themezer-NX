#include "domain/installs.hpp"

#include "sys/log.hpp"
#include "domain/names.hpp"
#include "domain/outdated.hpp"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <optional>
#include <vector>

namespace tz::installs
{

namespace
{

// For the app's temporary files alone, emptied at each start. Outside /Themes, which NXThemes Installer lists; its dot
// hides it from the homebrew menu, which shows a folder in /switch that has no .nro.
constexpr const char* kTemporaryFolder = "/switch/.themezer-nx";
// where 3.3 kept them
constexpr const char* kOldTemporaryFolder = "/Themes/ThemezerNX/.tmp";

constexpr const char* kInstallerPaths[] = {
    "/switch/NXThemesInstaller.nro",
    "/switch/NXThemesInstaller/NXThemesInstaller.nro",
    "/switch/Switch_themes_Installer/NXThemesInstaller.nro",
};

std::array<std::optional<Queued>, kSlotCount> slots;

// A file for NXThemes Installer copied out of the way, as installs::copyForInstaller makes them.
std::expected<void, themezer::Error> copyFile(const std::string& from, const std::string& to)
{
    FILE* in = std::fopen(from.c_str(), "rb");
    if (!in)
        return std::unexpected(themezer::storageError(from));
    FILE* out = std::fopen(to.c_str(), "wb");
    if (!out)
    {
        auto error = themezer::storageError(to);
        std::fclose(in);
        return std::unexpected(error);
    }
    std::vector<char> buffer(256 * 1024);
    bool ok = true;
    for (size_t read; (read = std::fread(buffer.data(), 1, buffer.size(), in)) > 0;)
    {
        if (std::fwrite(buffer.data(), 1, read, out) != read)
        {
            ok = false;
            break;
        }
    }
    ok = ok && !std::ferror(in);
    int failure = errno;
    std::fclose(in);
    if (std::fclose(out) != 0)
    {
        ok = false;
        failure = errno;
    }
    if (!ok)
    {
        unlink(to.c_str());
    return std::unexpected(themezer::storageError(to, failure));
    }
    return {};
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

const char* temporaryFolder()
{
    return kTemporaryFolder;
}

std::string temporaryPath(const themezer::Item& item)
{
    if (item.kind == themezer::Kind::Splashes)
        return std::string(kTemporaryFolder) + (item.remote ? "/remote-splash-" : "/splash-") + names::safe(item.id) +
               ".bmp";
    return std::string(kTemporaryFolder) + (item.remote ? "/remote-theme-" : "/theme-") + names::safe(item.id) +
           ".nxtheme";
}

void removeTemporary()
{
    for (const char* folder : {kTemporaryFolder, kOldTemporaryFolder})
    {
        DIR* dir = opendir(folder);
        if (!dir)
            continue;
        while (dirent* entry = readdir(dir))
        {
            const std::string name = entry->d_name;
            if (name != "." && name != "..")
                unlink((std::string(folder) + "/" + name).c_str());
        }
        closedir(dir);
    }
    rmdir(kOldTemporaryFolder);
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
    {
        // a file on the SD card that isn't from Themezer goes by its path (see localItem)
        const bool fromThemezer = item.id.rfind("file:", 0) != 0;
        slots[static_cast<size_t>(slot)] =
            Queued{std::move(path), item.name, item.creator, fromThemezer ? item.id : "", itemKey(item)};
    }
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

void moved(const std::string& from, const std::string& to)
{
    for (auto& slot : slots)
    {
        if (slot)
            slot->path = outdated::followed(slot->path, from, to);
    }
}

bool installerTakes(const std::string& path)
{
    return path.find(',') == std::string::npos && path.find("(_)") == std::string::npos &&
           std::all_of(path.begin(), path.end(), [](char c) { return static_cast<unsigned char>(c) < 0x80; });
}

std::string pathForInstaller(int slot)
{
    const Queued* waiting = queued(slot);
    if (!waiting || installerTakes(waiting->path))
        return waiting ? waiting->path : std::string();
    const size_t dot = waiting->path.rfind('.');
    return std::string(kTemporaryFolder) + "/install-" + std::to_string(slot) +
           (dot == std::string::npos ? std::string() : waiting->path.substr(dot));
}

std::expected<void, themezer::Error> copyForInstaller()
{
    for (int slot = 0; slot < kSlotCount; ++slot)
    {
        const Queued* waiting = queued(slot);
        if (!waiting || installerTakes(waiting->path))
            continue;
        if (auto copied = copyFile(waiting->path, pathForInstaller(slot)); !copied)
        {
            log::info("Couldn't copy %s for NXThemes Installer: not installed", waiting->path.c_str());
            return std::unexpected(copied.error());
        }
    }
    return {};
}

std::string installerArgs(const char* installer)
{
    // the installer splits its arguments at spaces: it reads "(_)" in a path as one
    std::string out = std::string(installer) + " installtheme=";
    bool first = true;
    for (int slot = 0; slot < kSlotCount; ++slot)
    {
        if (!queued(slot))
            continue;
        if (!first)
            out += ',';
        first = false;
        for (char c : pathForInstaller(slot))
            out += c == ' ' ? std::string("(_)") : std::string(1, c);
    }
    return out;
}

} // namespace tz::installs
