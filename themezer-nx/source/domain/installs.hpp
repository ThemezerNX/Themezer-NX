// The installs queued for NXThemes Installer, which the app starts on its way out with the queued files, and where a
// download goes to be installed only.

#pragma once

#include "services/themezer.hpp"

#include <string>

namespace tz::installs
{

// One slot for each part of the HOME Menu, and one for the splash screen.
constexpr int kSplashSlot = themezer::kTargetCount;
constexpr int kSlotCount = themezer::kTargetCount + 1;

// NXThemes Installer on the SD card, or nullptr.
const char* installerPath();

std::string slotLabel(int slot);
// The slot an item goes in, or -1.
int slotOf(const themezer::Item& item);

// Where an item's put to be installed only, cleared at the next start.
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
// NXThemes Installer takes its files as one list split at commas, "(_)" in a path read as a space: a path with either
// in it, or with a character outside ASCII (which it isn't known to take), can't be given to it.
bool installerTakes(const std::string& path);
// What NXThemes Installer is given for a slot: its file, or for one it can't take, a copy in /switch/.themezer-nx.
std::string pathForInstaller(int slot);
// On the way out: those copies made; a slot whose copy can't be made is taken out of the queue. Blocks.
void copyForInstaller();

} // namespace tz::installs
