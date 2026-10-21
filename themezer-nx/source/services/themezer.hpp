// Themezer's catalogue of Switch packs, themes and splash screens, through its GraphQL API. The calls block: they run
// on a worker.

#pragma once

#include "sys/jobs.hpp"
#include "sys/net.hpp"

#include <expected>
#include <string>
#include <vector>

namespace tz::themezer
{

enum class Kind : uint8_t
{
    Packs,
    Themes,
    Splashes,
};

// The parts of the HOME Menu a theme is for, in the API's order.
constexpr int kTargetCount = 7;
const char* targetLabel(int target); // "Home Menu", or "Unknown"
int targetFromApi(const char* name); // -1 when unknown

enum class Sort : uint8_t
{
    Trending,
    Created,
    Updated,
    Downloads,
    Saves,
    Count,
};
const char* sortLabel(Sort sort);

struct Preview
{
    std::string hd;
    std::string thumb;
    std::string thumbHash; // base64
};

// A pack, a theme or a splash screen. A pack has its themes; a theme in a pack knows the pack.
struct Item
{
    Kind kind = Kind::Themes;
    std::string id;
    std::string quickId;
    std::string name;
    std::string creator;
    std::string description;
    std::string createdAt; // ISO 8601
    std::string updatedAt;
    std::string downloadUrl;
    int downloads = 0;
    int saves = 0;
    int target = -1; // themes: the part of the HOME Menu
    Preview preview;
    std::vector<Item> themes; // packs
    std::string packId;
    std::string packName;
    std::string packCreator;
    // A remote install (a Quick ID for a file somewhere else): only its name, author, date, target and download.
    bool remote = false;
};

struct Query
{
    Kind kind = Kind::Packs;
    int target = -1; // themes: one part of the HOME Menu, or -1 for all
    Sort sort = Sort::Trending;
    bool ascending = false;
    std::string search;
    int page = 1;
    int limit = 24;
};

struct Page
{
    std::vector<Item> items;
    int page = 1;
    int pageCount = 1;
    int itemCount = 0;
};

// What went wrong, in words for the user, and a detail for the log.
struct Error
{
    std::string message;
    std::string detail;
    bool cancelled = false;
    bool notFound = false;
    bool fromApi = false; // the API said what's wrong, as opposed to the connection or the server failing
};

std::expected<Page, Error> fetchPage(const Query& query, const CancelToken& token);
std::expected<Item, Error> lookupQuickId(const std::string& quickId, const CancelToken& token);
// Downloads an item's file to path (its folder made first).
std::expected<void, Error> download(const std::string& url, const std::string& path, const CancelToken& token,
                                    net::Progress* progress);

// The date of an ISO 8601 time: "2026-10-21".
std::string dateOf(const std::string& iso);

} // namespace tz::themezer
