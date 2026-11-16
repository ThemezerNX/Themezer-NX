// The settings, kept in /Themes/ThemezerNX/settings.json.

#pragma once

namespace tz
{

struct Config
{
    bool music = true;
    bool sfx = true;
    // The start-up check of the SD card's items for updates: it sends their Quick IDs and versions to Themezer.
    bool updateCheck = true;

    void load();
    void save() const;
};

} // namespace tz
