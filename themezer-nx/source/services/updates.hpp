// Newer versions of Themezer-NX and of NXThemes Installer: the latest release of each on GitHub, checked once at the
// start against the app's own version and the installed installer's (its .nro's own, as hbmenu shows it).

#pragma once

#include "services/themezer.hpp"
#include <cstdint>
#include <functional>
#include <string>

namespace tz::updates
{

enum class Program : uint8_t
{
    App,
    Installer,
};

struct Release
{
    std::string version; // "4.1.0"
    std::string url;     // its .nro
    uint64_t size = 0;   // the .nro's, from GitHub; 0 when it didn't say
    std::string current; // what's here now
    std::string notes;   // GitHub release body, Markdown shown as written
};

// Checks both on a worker; what's newer comes in on the main thread, and then found is called.
void check(std::function<void()> found);
// The newer release of it, or nullptr: none, or not checked yet.
const Release* available(Program program);
int availableCount();
#ifdef TZ_DESKTOP
void setTestRelease(Program program, Release release);
#endif
// It's been updated: no longer offered.
void done(Program program);

// Puts the new file staged in place of the one at path. The old one stays until the new one's in, and comes back when
// it can't be. A failure carries the destination path.
std::expected<void, themezer::Error> replace(const std::string& staged, const std::string& path);

} // namespace tz::updates
