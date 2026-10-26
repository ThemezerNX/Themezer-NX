// The Themezer items on the SD card that Themezer has newer versions of: found once at the start of a run and when asked,
// kept for the user to update, and planned for the update: what to download, and which files it takes the place of.
// How old each file is comes from the file alone (see outdated), so nothing is recorded between runs.

#pragma once

#include "services/themezer.hpp"

#include <expected>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace tz::itemupdates
{

// Asks Themezer which of the SD card's items are newer, on a worker; done gets, on the main thread, how many are, or
// what went wrong (a cancel too). Main thread, one check at a time: a call while one runs does nothing.
void check(std::function<void(const std::expected<size_t, themezer::Error>&)> done);
bool checking();
// Stops a check, without waiting for it.
void cancel();

// The newer items, packs first. Their themes, for a pack, are all those Themezer lists.
const std::vector<themezer::Item>& found();
// The newer item with this ID, or nullptr; the ID's case doesn't matter. For marking items as they are drawn.
const themezer::Item* find(themezer::Kind kind, const std::string& id);
size_t count();
// Takes updated items off the list, by the keys of a plan and its steps ("T1E93D", "S1EB6B", "P1E93C").
void forget(const std::vector<std::string>& keys);

// One file to download, and what it replaces.
struct Step
{
    std::string key;      // of the item downloaded
    std::string name;
    std::string url;
    std::string folder;   // where it goes, under the name Themezer gives it; empty: beside its newest copy (others' first)
    std::string fallback; // the name when Themezer gives none
    std::vector<std::string> others; // every copy of it on the SD card now, which the new file takes the place of
};

// What it takes to update one item.
struct Plan
{
    std::string key;
    std::string name;
    // A theme or splash: one step. A pack: a step for each theme Themezer lists, those the folder lacks too.
    std::vector<Step> steps;
    // A pack's themes Themezer no longer lists, from where they are to the folder it's in (not deleted: they are the
    // user's).
    std::vector<std::pair<std::string, std::string>> takeOut;
    // A pack's folder from its name to the one Themezer gives; both empty when it keeps its name.
    std::pair<std::string, std::string> rename;
};

// The plans for these items, in their order. Reads the Library and changes nothing; main thread. A theme a pack in the
// list has a step for gets none of its own, so Update all downloads it once. An item that isn't on the SD card any more
// gets no plan.
std::vector<Plan> plan(const std::vector<themezer::Item>& items);
// What the list says of an item: when it changed, and for a pack what it does to the folder: "Updated 2026-10-21, adds
// 2 themes, moves 1 out of the folder". Reads the Library; main thread.
std::string describe(const themezer::Item& item);

} // namespace tz::itemupdates
