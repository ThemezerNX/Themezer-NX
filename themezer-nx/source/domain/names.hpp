// What the names of theme and splash files say, Themezer's and the ones older versions of Themezer-NX gave them, and
// the names for downloads.
//
//   Themezer:     "<name> by <author> (<part>[, <layout>]) (Themezer-T1E93D).nxtheme",
//                 "<name> by <author> (Themezer-S1EB6B).bmp", a pack's zip "<name> by <author> (Themezer-P1E93C).zip";
//                 remote installs end in "(Themezer Remote Install)", downloads from before publishing in the part
//   Themezer-NX:  "<name> by <author> (<part>-1E93D).nxtheme", "<name> by <author> (splash-1EB6B).bmp",
//                 "(remote-splash-<Quick ID>)", a pack's folder "<name> by <author> (1E93C)"

#pragma once

#include <string>
#include <string_view>

namespace tz::names
{

struct Parts
{
    std::string name;
    std::string author;
    int target = -1; // themes: the part of the HOME Menu (themezer::targetLabel), -1 when the name doesn't say
    std::string id;  // the item's ID on Themezer ("1E93D", without a Quick ID's letter); empty when it has none
};

// A theme's or splash's file name, with its extension. False when it's none of these names.
bool parseFile(std::string_view fileName, Parts& parts);
// A pack's folder: Themezer's zip unpacked, "<name> by <author> (Themezer-P1E93C)", or Themezer-NX's "(1E93C)".
bool parseFolder(std::string_view folder, Parts& parts);
// The part of the HOME Menu a name says: "Home Menu", "Lockscreen" (Themezer's), "Lock Screen" (Themezer-NX's)…; -1.
int target(std::string_view label);

// The file name a download's Content-Disposition header gives, made safe for the SD card; empty when it gives none.
std::string fromDisposition(std::string_view header);
// What the SD card can't hold in a name left out, its spaces single, no dot first; Unicode and commas kept, as
// Themezer keeps them.
std::string safe(std::string_view name);

// The names Themezer gives, for a download that doesn't say: a theme's (no ID: a remote install), a splash's, and a
// pack's folder (its zip's, unpacked).
std::string themeFile(std::string_view name, std::string_view author, int target, std::string_view id);
std::string splashFile(std::string_view name, std::string_view author, std::string_view id);
std::string packFolder(std::string_view name, std::string_view author, std::string_view id);

} // namespace tz::names
