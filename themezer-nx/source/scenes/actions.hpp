// What the screens do with items and the app: open one, install or download it (or a whole pack), the menu, the
// queue, the settings and the Quick ID.

#pragma once

#include "services/themezer.hpp"

namespace tz::scenes
{

// Its page: a pack's themes, or a theme's or splash's details.
void openItem(const themezer::Item& item);

// Downloads it (unless it's on the SD card already) and queues it for NXThemes Installer; kept in /Themes/ThemezerNX
// when save, else only until the next start.
void install(const themezer::Item& item, bool save);
void download(const themezer::Item& item);
// A pack's themes, one for each part of the HOME Menu: where a pack has more than one for a part, which is asked.
void installPack(const themezer::Item& pack, bool save);
void downloadPack(const themezer::Item& pack);

void openMenu();
void openQueue();
void openSettings();
void openAbout();
void openQuickId();

// For the hints: whether installing can be done at all.
bool canInstall();

} // namespace tz::scenes
