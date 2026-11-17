// Which of the Themezer items on the SD card Themezer has newer versions of: how old a file is, the request that asks
// about them, a pack's themes against its folder's, and the swap of old files for new. Nothing from SDL, curl or libnx,
// so a desktop test links it.
//
// A file's version is when it was made, in Unix milliseconds: the Id in a theme's info.json when Themezer built it
// ("Themezer:<ID>:<ms>"), else when the file last changed on the SD card.

#pragma once
#include <functional>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tz::outdated
{

// Items in a request, each asked in a field of its own. The API allows a cost of 43000 and an item costs 240, so 179 at
// most (180 are refused); 100 leave room for a dearer answer.
constexpr size_t kBatch = 100;

// An ID in capitals, as Themezer gives it: "1e93d" is "1E93D".
std::string canonical(std::string_view id);

// The milliseconds in a theme's Id, "Themezer:<ID>:<ms>": what's after the last colon. Nothing when the Id isn't that.
std::optional<int64_t> millisOfId(std::string_view id);
// The ID in a theme's Id, "Themezer:<ID>:<ms>", in capitals; empty when the Id isn't that or the ID isn't hex.
std::string idOfId(std::string_view id);
// A file's version: its Id's when it has one, else the time it last changed (in seconds, as stat gives it) in ms.
// Nothing for a time of 0 or less: a failed stat leaves 0, and 0 would have every item look old.
std::optional<int64_t> versionOf(std::optional<int64_t> idMillis, int64_t modifiedSeconds);
// A pack's: its oldest theme file's, in ms. Nothing when no time is above 0.
std::optional<int64_t> packVersion(const std::vector<int64_t>& modifiedSeconds);

// What the SD card has: an item's letter (Themezer's: 'T' theme, 'S' splash, 'P' pack), ID and version.
struct File
{
    char letter = 'T';
    std::string id;
    int64_t version = 0;
};

struct Query
{
    std::string quickId; // "T1E93D"
    std::string version; // ms, as digits
};

// One query for each item, in the order they come: IDs in capitals, those that aren't hex left out (a remote splash's
// "R12AB" is a code, not an ID), versions of 0 or less too. An item on the SD card more than once is as new as its
// newest copy.
std::vector<Query> queries(const std::vector<File>& files);
// The fields of the requests that ask about them, at most kBatch in each: "a0:checkUpdates(items:[…]){...F} a1:…", the
// answer about queries[n * kBatch + i] in field a<i> of request n. A field of its own, as Themezer answers an ID from
// before its IDs changed (Themezer-NX 3's) under the item's current one: the field says which file it was asked for.
std::vector<std::string> fields(const std::vector<Query>& queries);

// A pack on Themezer against the one on the SD card, by their themes' IDs (not the case, not repeats).
struct PackDiff
{
    std::vector<std::string> added;   // in the pack, not in the folder
    std::vector<std::string> removed; // in the folder, not in the pack
};
PackDiff diffPack(const std::vector<std::string>& pack, const std::vector<std::string>& folder);

// Whether two paths are one file, as the SD card sees it: FAT doesn't tell cases apart.
bool samePath(const std::string& a, const std::string& b);

// Puts the staged file at path with put, and only when that worked takes the other copies of the item (others) off the
// SD card, never the file just put: not by a spelling that differs only in case, nor by one FAT folds to it beyond
// ASCII (the same size and time as the new file). Those taken, and those that are the new file, go in replaced: where
// they were is where the new file is now. False, and the old copies left, when put didn't work.
bool replace(const std::string& staged, const std::string& path, const std::vector<std::string>& others,
             std::vector<std::string>& replaced, std::function<bool(const std::string&, const std::string&)> put);

// Where a path is after from became to: itself when it's from, under from + "/" when it's in it, else as it was.
std::string followed(const std::string& path, const std::string& from, const std::string& to);
// Where a path is after each of moves, in order.
std::string rebased(std::string path, const std::vector<std::pair<std::string, std::string>>& moves);

} // namespace tz::outdated
