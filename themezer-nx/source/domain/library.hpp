// The themes and splash screens on the SD card: .nxtheme files and Hekate splashes (720x1280 .bmp) anywhere in
// /Themes, where NXThemes Installer finds them. Nothing about them is kept between starts: the files are what's known.

#pragma once

#include <SDL2/SDL.h>

#include <cstdint>
#include <string>
#include <vector>

namespace tz::library
{

enum class Kind : uint8_t
{
    Theme,
    Splash,
};

struct Entry
{
    Kind kind = Kind::Theme;
    std::string path; // "/Themes/ThemezerNX/Name by Author (Home Menu-1E93D).nxtheme"
    std::string fileName;
    uint64_t size = 0;
    int64_t modified = 0; // when the file last changed: when it came, for a download
    // From the file name when Themezer-NX gave it, else from the theme's info.json (readInfo), else the file name.
    std::string name;
    std::string author;
    int target = -1;         // themes: the part of the HOME Menu (themezer::targetLabel), -1 when not known yet
    std::string layout;      // themes: the layout it's made with, from info.json
    std::string themezerId;  // the item's ID on Themezer, when the file name has it
    std::string folder;      // the folder it's in under /Themes, for a pack's themes: "Pixel Forest by soulsart"
    bool infoRead = false;
    std::string fingerprint; // splashes: to know the one Hekate has, which is a copy
};

constexpr const char* kRoot = "/Themes";

// The files in /Themes and its folders (not the ones starting with '.'), without opening them: the names and parts
// Themezer-NX's file names give. Blocks: for a worker.
std::vector<Entry> scan();
// Reads a theme's info.json for its name, author and part. Blocks. False when the file can't be read.
bool readInfo(Entry& entry);
// The picture: a theme's background, or the splash turned to lie as the screen shows it, no wider than maxWidth. Any
// thread. nullptr when there's none.
SDL_Surface* readPreview(const Entry& entry, int maxWidth);
bool remove(const Entry& entry);

// A file's size and the CRC-32 of its start, which tell files apart without reading them whole; empty when it isn't
// there.
std::string fingerprint(const std::string& path);

// What Themezer-NX's file names say: "<name> by <author> (<part>-<ID>).nxtheme", "<name> by <author> (splash-<ID>).bmp".
struct NameParts
{
    std::string name;
    std::string author;
    std::string tag; // "Home Menu", "splash", "remote-splash"...
    std::string id;
};
bool parseFileName(const std::string& fileName, NameParts& parts);
// And a pack's folder: "<name> by <author> (<ID>)", without a tag.
bool parseFolderName(const std::string& folder, NameParts& parts);

} // namespace tz::library
