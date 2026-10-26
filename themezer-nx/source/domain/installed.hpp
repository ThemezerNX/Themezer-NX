// What the console has installed: NXThemes Installer's files for each part of the HOME Menu, and Hekate's splash. The
// files say whether a part has a custom theme; which one, only Themezer-NX can say, from what it queued: it keeps that
// in /Themes/ThemezerNX/installed.json with a fingerprint of the file it went into, and forgets it when the file is
// gone or isn't the same any more (installed with something else).

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
    std::string source;     // the file it was installed from
    std::string themezerId; // its ID on Themezer, when it's from there
    std::string fingerprint; // of the installed file (see library::fingerprint), for a splash to be told by its file
};

// Reads the files and the record, and brings the record up to date. Blocks a moment (a few small reads).
void refresh();
const Part& part(int slot);

// On the way out to NXThemes Installer: what's queued goes in the record, to be checked at the next start.
void noteQueued();

// Files or folders moved, from and to in the order they moved: the record's sources follow them, so what was installed
// from a file still says where it is. Blocks a moment.
void moved(const std::vector<std::pair<std::string, std::string>>& moves);

// Takes the part's theme out, as NXThemes Installer's uninstall does: its files go, and Nintendo's show after a
// restart. False when a file couldn't be deleted.
bool uninstall(int slot);

} // namespace tz::installed
