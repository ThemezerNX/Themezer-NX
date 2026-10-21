// The settings, kept in /Themes/ThemezerNX/settings.json.

#pragma once

namespace tz
{

struct Config
{
    bool music = true;
    bool sfx = true;
    // the home screen's tab last shown: 0 to 2 on the SD card, 3 to 5 on Themezer
    int lastTab = 3;

    void load();
    void save() const;
};

} // namespace tz
