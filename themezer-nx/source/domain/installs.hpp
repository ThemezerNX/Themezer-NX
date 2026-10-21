// Where downloads go on the SD card, and the installs queued for NXThemes Installer, which the app starts on its way
// out with the queued files.

#pragma once

#include "services/themezer.hpp"

#include <string>
#include <string_view>

namespace tz::installs
{

// One slot for each part of the HOME Menu, and one for the splash screen.
constexpr int kSplashSlot = themezer::kTargetCount;
constexpr int kSlotCount = themezer::kTargetCount + 1;

// NXThemes Installer on the SD card, or nullptr.
const char* installerPath();

std::string slotLabel(int slot);
// The part of the HOME Menu a file name's label is for ("Lock Screen", as Themezer-NX names its files), or -1.
int targetFromFileLabel(std::string_view label);
// The slot an item goes in, or -1.
int slotOf(const themezer::Item& item);

// Where an item is kept: in /Themes/ThemezerNX, a pack's themes in a folder of their own.
std::string savedPath(const themezer::Item& item);
// Where it's put to be installed only, cleared at the next start.
std::string temporaryPath(const themezer::Item& item);
void removeTemporary();
bool exists(const std::string& path);

struct Queued
{
    std::string path;
    std::string name;
    std::string author;
    std::string themezerId; // empty for a file that isn't from Themezer
    std::string item;       // which item it is (see itemKey)
};

// What tells items apart: their kind and ID.
std::string itemKey(const themezer::Item& item);
void queue(int slot, std::string path, const themezer::Item& item);
bool isQueued(const themezer::Item& item);
void unqueue(int slot);
const Queued* queued(int slot); // nullptr when the slot is free
int queuedCount();
// The installer's arguments for what's queued.
std::string installerArgs(const char* installer);
// The homebrew loader keeps the next program's arguments in 2 KiB, their 0 included: any longer would overrun it.
constexpr size_t kMaxArgs = 2047;
// NXThemes Installer takes its files as one list split at commas, "(_)" in a path read as a space: a path with
// either in it can't be given to it.
bool installerTakes(const std::string& path);

} // namespace tz::installs
