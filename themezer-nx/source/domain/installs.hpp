// Where downloads go on the SD card, and the installs queued for NXThemes Installer, which the app starts on its way
// out with the queued files.

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
    std::string item; // which item it is (see itemKey)
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

} // namespace tz::installs
