// The SD card's themes and splashes, and the packs their folders make, as the last scan found them, kept in memory
// only: scanned on a worker at the start and when something's downloaded or deleted, their info.json read as they're
// looked at.

#pragma once

#include "domain/library.hpp"
#include "services/themezer.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

namespace tz::local
{

// A folder of themes under /Themes, as Themezer-NX keeps a pack's: "/Themes/ThemezerNX/<name> by <author> (<ID>)".
struct Pack
{
    std::string dir;
    std::string name;
    std::string author;
    std::string themezerId;     // from the folder's name, when Themezer-NX made it
    std::vector<size_t> themes; // indexes into Library::entries()
    uint64_t size = 0;
    int64_t modified = 0; // its newest theme's
};

// Whether a theme is in a pack: in a folder of its own, not loose in /Themes or in Themezer-NX's own folder.
bool inPack(const library::Entry& entry);

class Library
{
public:
    static Library& get();

    // Scans again on a worker; what's known stays until the new scan is in. then runs on the main thread once, after the
    // last scan that's pending is in.
    void rescan(std::function<void()> then = {});
    bool scanned() const { return scanned_; }
    // Goes up with every change, for what shows the entries to notice.
    uint32_t generation() const { return generation_; }

    const std::vector<library::Entry>& entries() const { return entries_; }
    // Asks for an entry's info.json, read on a worker. Main thread.
    void wantInfo(size_t index);
    // Takes an entry off the SD card. False when it couldn't be deleted.
    std::expected<void, themezer::Error> remove(size_t index);
    // The entry at path, or nullptr.
    const library::Entry* find(const std::string& path) const;

    // A Themezer item's file on the SD card, wherever it is and whatever it's called; nullptr when there's none.
    const library::Entry* findItem(library::Kind kind, const std::string& id) const;

    const std::vector<Pack>& packs() const { return packs_; }
    const Pack* findPack(const std::string& dir) const;
    // A Themezer pack's folder on the SD card, by the pack's ID.
    const Pack* findPackById(const std::string& id) const;
    // The theme that shows for a pack: its Home Menu's, else its first.
    const library::Entry* cover(const Pack& pack) const;
    // Takes a pack's themes off the SD card, and its folder when nothing else is in it. False when one couldn't be.
    std::expected<void, themezer::Error> removePack(std::string dir);

    // Whether a Themezer item is on the SD card: a theme or a splash by its ID, a pack when all its themes are.
    bool has(const themezer::Item& item) const;

private:
    void index();

    std::vector<library::Entry> entries_;
    std::vector<Pack> packs_;
    std::unordered_set<std::string> ids_; // "<kind>:<ID>" of the Themezer items on the SD card
    std::unordered_set<std::string> reading_;
    bool scanned_ = false;
    bool scanning_ = false;
    bool again_ = false;
    std::vector<std::function<void()>> after_; // for the scan that's last
    uint32_t generation_ = 0;
};

} // namespace tz::local
