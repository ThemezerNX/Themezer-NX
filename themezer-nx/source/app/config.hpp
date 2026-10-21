// The settings, kept in /Themes/ThemezerNX/settings.json.

#pragma once

namespace tz
{

struct Config
{
    bool music = true;
    bool sfx = true;

    void load();
    void save() const;
};

} // namespace tz
