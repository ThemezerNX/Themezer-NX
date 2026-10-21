// What the screens do with items and the app: open one, install or download it (or a whole pack), what's on the SD
// card, the menu, the settings and the Quick ID.

#pragma once

#include "domain/library.hpp"
#include "services/themezer.hpp"
#include "ui/overlays.hpp"

#include <optional>
#include <string>
#include <vector>

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

// A theme or a splash on the SD card: its page, queueing it, its picture filling the screen, deleting it (asked first).
void openLocal(const library::Entry& entry);
void installLocal(const library::Entry& entry);
void viewLocal(const library::Entry& entry);
void deleteLocal(const library::Entry& entry);
// A pack's folder on the SD card, by its path: its page, queueing its themes as installPack does, deleting it.
void openLocalPack(const std::string& dir);
void installLocalPack(const std::string& dir);
void deleteLocalPack(const std::string& dir);
// What's installed in a slot taken out, asked first.
void uninstallPart(int slot);
// A Themezer item for a file on the SD card, for queueing it.
themezer::Item localItem(const library::Entry& entry);
// The item's page on Themezer, looked up by its Quick ID.
void showOnThemezer(const std::string& quickId);

// The menu: first the page's actions, then what it always has.
void openMenu(std::vector<ui::Action> first = {});
// What's queued to install, each to be taken out.
void openQueue();
void openSettings();
void openAbout();
void openQuickId();

// For the hints: whether installing can be done at all.
bool canInstall();

// Text typed on the Switch's keyboard, its spaces at the ends cut; nothing when it was cancelled.
std::optional<std::string> askText(const char* guide, const std::string& initial = {}, size_t maxLength = 128);

} // namespace tz::scenes
