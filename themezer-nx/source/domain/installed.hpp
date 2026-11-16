// What the console has installed: NXThemes Installer's files for each part of the HOME Menu, and Hekate's splash. Which
// theme is in one is recorded in /Themes/ThemezerNX/installed.json with a fingerprint of the file, and dropped when the
// file is gone or no longer the same (installed with something else).

#pragma once

#include "services/themezer.hpp"

#include <string>
#include <utility>
#include <vector>

namespace tz::installed
{

// One per part of the HOME Menu, then the splash, as installs' slots.
struct Part
{
    bool custom = false; // the installer's file is there (the splash: Hekate's)
    // What it is, when Themezer-NX installed it; empty when it's someone else's, or Nintendo's.
    std::string name;
    std::string author;
    std::string source;
    std::string themezerId; // its ID on Themezer, when it's from there
    std::string fingerprint; // of the installed file (see library::fingerprint), for a splash to be told by its file
};

// Reads the files and the record, and brings the record up to date. Blocks a moment (a few small reads).
void refresh();
const Part& part(int slot);

// On the way out to NXThemes Installer: what's queued goes in the record, to be checked at the next start.
void noteQueued();

// Files or folders moved (from, to, in order): the record's sources follow them. Blocks a moment.
void moved(const std::vector<std::pair<std::string, std::string>>& moves);

// Takes the part's theme out, as NXThemes Installer's uninstall does: its files go, and Nintendo's show after a
// restart. A failure carries the path that could not be deleted.
std::expected<void, themezer::Error> uninstall(int slot);

} // namespace tz::installed
