// One-time changes to what an older version left on the SD card, run at the start until each is done once. Each has
// an ID, kept in settings.json's "migrations" once it's done (Config::migrations); an ID never changes once released.
//
//   v3-names  Themezer-NX 3's downloads, renamed in place to Themezer's names ("<name> by <author> (Home Menu-25A2).nxtheme",
//             a pack's "(1B18)"); most carry an ID from before Themezer's IDs changed, which Themezer answers under the
//             item's current one, so updates and the online cards' badges know it. Nothing changes for what Themezer no
//             longer has; never in place of another file. Done once Themezer answered, or with nothing of NX 3's to ask about.
//   plain-ids  Names with their Themezer ID's letter, as Themezer gave them before its IDs stopped overlapping
//             ("(Themezer-T1E93D)", a pack's folder "(Themezer-P1E93C)"), renamed in place without it (names::plainId),
//             anywhere in /Themes. A remote install's "R" code isn't an ID and stays. Never in place of another file.
//             Done in one go: nothing to ask.

#pragma once

#include <functional>
#include <set>
#include <string>

namespace tz::migrate
{

// Runs the migrations not in done, oldest first, each on a worker; finished gets the ID of each that is done, on the main
// thread. One that can't be done now (offline) stops the rest until the next start. Runs again, on the main thread, after
// the SD card is read again when something on it changed. Main thread, once the SD card is read.
void run(std::set<std::string> done, std::function<void(const char* id)> finished, std::function<void()> then);

} // namespace tz::migrate
