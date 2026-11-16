// The settings, kept in /Themes/ThemezerNX/settings.json.

#pragma once

#include <set>
#include <string>

namespace tz
{

struct Config
{
    bool music = true;
    bool sfx = true;
    // The start-up check of the SD card's items for updates: it sends their Quick IDs and versions to Themezer.
    bool updateCheck = true;
    // Migrations done on this SD card ("migrations": {"v3-names": true}), by ID: each runs once. IDs a newer version wrote are kept.
    std::set<std::string> migrations;

    void load();
    void save() const;
};

} // namespace tz
