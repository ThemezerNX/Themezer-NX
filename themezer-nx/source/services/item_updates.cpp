#include "services/item_updates.hpp"

#include "sys/log.hpp"
#include "domain/library.hpp"
#include "domain/names.hpp"
#include "domain/outdated.hpp"
#include "services/local.hpp"
#include "sys/jobs.hpp"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace tz::itemupdates
{

namespace
{

// main thread
std::vector<themezer::Item> items;
std::unordered_map<std::string, size_t> indexes; // key to place in items
// The current key of each key on the SD card that Themezer answered under another: an ID from before Themezer's IDs
// changed, which Themezer-NX 3 named its files and folders with ("P25AD" for "P196D9").
std::unordered_map<std::string, std::string> renamed;
bool running = false;
CancelToken current;

std::string keyOf(themezer::Kind kind, const std::string& id)
{
    const char letter = kind == themezer::Kind::Packs ? 'P' : kind == themezer::Kind::Splashes ? 'S' : 'T';
    return std::string(1, letter) + outdated::canonical(id);
}

std::string keyOf(const themezer::Item& item)
{
    return keyOf(item.kind, item.id);
}

themezer::Kind kindOf(library::Kind kind)
{
    return kind == library::Kind::Splash ? themezer::Kind::Splashes : themezer::Kind::Themes;
}

// The key of the item an ID on the SD card is now
std::string currentKey(themezer::Kind kind, const std::string& id)
{
    std::string key = keyOf(kind, id);
    const auto at = renamed.find(key);
    return at == renamed.end() ? key : at->second;
}

void reindex()
{
    indexes.clear();
    for (size_t i = 0; i < items.size(); ++i)
        indexes.emplace(keyOf(items[i]), i);
}

std::string parent(const std::string& path)
{
    return path.substr(0, path.rfind('/'));
}

std::string leaf(const std::string& path)
{
    return path.substr(path.rfind('/') + 1);
}

// What the check needs of the Library, copied: it's the main thread's, and the check runs on a worker.
struct Local
{
    library::Kind kind = library::Kind::Theme;
    std::string path;
    std::string id;
    int64_t modified = 0;
};
struct Folder
{
    std::string id;
    // of its themes that have an ID: the others are never replaced, so never get newer
    std::vector<int64_t> modified;
    std::vector<std::string> themeIds;
};

// How old each Themezer item on the SD card is, packs first; short when the token cancels. Blocks (it opens every
// theme's info.json): for a worker.
std::vector<outdated::File> sweep(const std::vector<Folder>& packs, const std::vector<Local>& files,
                                  const CancelToken& token)
{
    std::vector<outdated::File> out;
    for (const Folder& pack : packs)
    {
        if (const auto version = outdated::packVersion(pack.modified))
            out.push_back({'P', pack.id, *version});
    }
    for (const Local& file : files)
    {
        if (token.cancelled())
            break;
        const bool theme = file.kind == library::Kind::Theme;
        if (const auto version = outdated::versionOf(theme ? library::builtAt(file.path) : std::nullopt, file.modified))
            out.push_back({theme ? 'T' : 'S', file.id, *version});
    }
    return out;
}

// The pack's folder on the SD card, by its ID or an old one, the case not mattering
const local::Pack* folderOf(const themezer::Item& item)
{
    const std::string want = keyOf(item);
    for (const local::Pack& pack : local::Library::get().packs())
    {
        if (!pack.themezerId.empty() && currentKey(themezer::Kind::Packs, pack.themezerId) == want)
            return &pack;
    }
    return nullptr;
}

// The current ID of a theme on the SD card, without its letter
std::string currentThemeId(const library::Entry& entry)
{
    return currentKey(themezer::Kind::Themes, entry.themezerId).substr(1);
}

// Themezer's pack against the folder, by the themes' current IDs
outdated::PackDiff diffOf(const themezer::Item& pack, const local::Pack& folder)
{
    const auto& entries = local::Library::get().entries();
    std::vector<std::string> listed, there;
    for (const themezer::Item& theme : pack.themes)
        listed.push_back(theme.id);
    for (size_t index : folder.themes)
    {
        if (index < entries.size() && !entries[index].themezerId.empty())
            there.push_back(currentThemeId(entries[index]));
    }
    return outdated::diffPack(listed, there);
}

// Every copy of a theme or splash on the SD card, by its ID or an old one, the newest first
std::vector<const library::Entry*> copies(const themezer::Item& item)
{
    const std::string want = keyOf(item);
    std::vector<const library::Entry*> found;
    for (const library::Entry& entry : local::Library::get().entries())
    {
        if (!entry.themezerId.empty() && currentKey(kindOf(entry.kind), entry.themezerId) == want)
            found.push_back(&entry);
    }
    std::stable_sort(found.begin(), found.end(),
                     [](const library::Entry* a, const library::Entry* b) { return a->modified > b->modified; });
    return found;
}

Step stepFor(const themezer::Item& item, std::string folder, const std::vector<const library::Entry*>& files)
{
    Step step{keyOf(item), item.name, item.downloadUrl, std::move(folder), {}, {}};
    step.fallback = item.kind == themezer::Kind::Splashes
                        ? names::splashFile(item.name, item.creator, item.id)
                        : names::themeFile(item.name, item.creator, item.target, item.id);
    for (const library::Entry* file : files)
        step.others.push_back(file->path);
    return step;
}

} // namespace

void check(std::function<void(const std::expected<size_t, themezer::Error>&)> done)
{
    if (running)
        return;
    running = true;
    current = CancelToken(); // the last check may have been cancelled
    const local::Library& library = local::Library::get();
    auto packs = std::make_shared<std::vector<Folder>>();
    auto files = std::make_shared<std::vector<Local>>();
    for (const local::Pack& pack : library.packs())
    {
        if (pack.themezerId.empty())
            continue;
        Folder folder{pack.themezerId, {}, {}};
        for (size_t index : pack.themes)
        {
            if (index < library.entries().size() && !library.entries()[index].themezerId.empty())
            {
                folder.modified.push_back(library.entries()[index].modified);
                folder.themeIds.push_back(library.entries()[index].themezerId);
            }
        }
        packs->push_back(std::move(folder));
    }
    for (const library::Entry& entry : library.entries())
    {
        if (!entry.themezerId.empty())
            files->push_back({entry.kind, entry.path, entry.themezerId, entry.modified});
    }

    jobs::run(jobs::Lane::Images, [packs, files, token = current, done] {
        const auto asked = outdated::queries(sweep(*packs, *files, token));
        std::expected<std::vector<themezer::Newer>, themezer::Error> result;
        if (token.cancelled())
            result = std::unexpected(themezer::Error{{}, {}, themezer::ErrorKind::Cancelled});
        else if (!asked.empty())
            result = themezer::checkUpdates(asked, token);
        // A pack answered under another ID than its folder's is in a folder Themezer-NX 3 made, its themes named with
        // old IDs too. Their current IDs are asked for (every version is newer than 1), for an update to replace each
        // theme with its own instead of moving it out of the folder as one Themezer no longer lists.
        std::vector<themezer::Newer> themes;
        if (result)
        {
            std::vector<outdated::File> old;
            for (const themezer::Newer& newer : *result)
            {
                if (newer.item.kind != themezer::Kind::Packs || newer.asked == keyOf(newer.item))
                    continue;
                for (const Folder& pack : *packs)
                {
                    if (keyOf(themezer::Kind::Packs, pack.id) == newer.asked)
                    {
                        for (const std::string& id : pack.themeIds)
                            old.push_back({'T', id, 1});
                    }
                }
            }
            if (!old.empty())
            {
                if (auto answered = themezer::checkUpdates(outdated::queries(old), token))
                    themes = std::move(*answered);
                else
                    result = std::unexpected(answered.error());
            }
        }
        log::info("Item updates: %zu items on the SD card asked about, %zu newer", asked.size(),
                    result ? result->size() : 0);
        if (result)
        {
            for (const themezer::Newer& newer : *result)
                log::info("Item updates: %s %s is newer, asked as %s", keyOf(newer.item).c_str(),
                            newer.item.name.c_str(), newer.asked.c_str());
        }
        jobs::post([result = std::move(result), themes = std::move(themes), done] {
            running = false;
            if (result)
            {
                items.clear();
                indexes.clear();
                renamed.clear();
                for (const themezer::Newer& newer : *result)
                {
                    const std::string key = keyOf(newer.item);
                    if (newer.asked != key)
                        renamed[newer.asked] = key;
                    // asked as two IDs, an old one and its current one: listed once
                    if (indexes.emplace(key, items.size()).second)
                        items.push_back(newer.item);
                }
                for (const themezer::Newer& theme : themes)
                {
                    if (theme.asked != keyOf(theme.item))
                        renamed[theme.asked] = keyOf(theme.item);
                }
            }
            if (!done)
                return;
            if (result)
                done(items.size());
            else
                done(std::unexpected(result.error()));
        });
    });
}

bool checking()
{
    return running;
}

void cancel()
{
    current.cancel();
}

const std::vector<themezer::Item>& found()
{
    return items;
}

const themezer::Item* find(themezer::Kind kind, const std::string& id)
{
    if (items.empty())
        return nullptr;
    const auto at = indexes.find(currentKey(kind, id));
    return at == indexes.end() ? nullptr : &items[at->second];
}

bool same(const std::string& id, const themezer::Item& item)
{
    return !id.empty() && currentKey(item.kind, id) == keyOf(item);
}

size_t count()
{
    return items.size();
}

void forget(const std::vector<std::string>& keys)
{
    std::erase_if(items, [&](const themezer::Item& item) {
        return std::find(keys.begin(), keys.end(), keyOf(item)) != keys.end();
    });
    reindex();
}

std::vector<Plan> plan(const std::vector<themezer::Item>& wanted)
{
    const auto& entries = local::Library::get().entries();
    // the themes of the packs planned so far: found() has packs first
    std::unordered_set<std::string> covered;
    std::vector<Plan> plans;
    for (const themezer::Item& item : wanted)
    {
        Plan one{keyOf(item), item.name, {}, {}, {}};
        if (item.kind == themezer::Kind::Packs)
        {
            const local::Pack* folder = folderOf(item);
            if (!folder)
            {
                log::info("Item updates: %s has no folder on the SD card", one.key.c_str());
                continue;
            }
            for (const themezer::Item& theme : item.themes)
            {
                if (theme.downloadUrl.empty() || theme.id.empty())
                    continue;
                one.steps.push_back(stepFor(theme, folder->dir, copies(theme)));
                covered.insert(one.steps.back().key);
            }
            // a pack with nothing to download in the answer would have every theme of the folder taken out of it
            if (one.steps.empty())
            {
                log::info("Item updates: %s lists no theme to download (%zu themes)", one.key.c_str(),
                            item.themes.size());
                continue;
            }
            for (const std::string& gone : diffOf(item, *folder).removed)
            {
                for (size_t index : folder->themes)
                {
                    if (index < entries.size() && !entries[index].themezerId.empty() &&
                        currentThemeId(entries[index]) == gone)
                        one.takeOut.emplace_back(entries[index].path,
                                                 parent(folder->dir) + "/" + leaf(entries[index].path));
                }
            }
            const std::string name = names::packFolder(item.name, item.creator, item.id);
            if (!item.name.empty() && !item.creator.empty() && !outdated::samePath(name, leaf(folder->dir)))
                one.rename = {folder->dir, parent(folder->dir) + "/" + name};
        }
        else
        {
            if (covered.count(one.key) > 0)
                continue;
            const auto files = copies(item);
            if (files.empty() || item.downloadUrl.empty())
            {
                log::info("Item updates: %s has %s", one.key.c_str(),
                            files.empty() ? "no file on the SD card" : "no download");
                continue;
            }
            // beside the newest copy (a pack's theme stays in its pack, a splash with the splashes), found as it's put
            // in: a pack updated before it may have taken it out
            one.steps.push_back(stepFor(item, {}, files));
        }
        plans.push_back(std::move(one));
    }
    return plans;
}

std::string describe(const themezer::Item& item)
{
    std::string text = "Updated " + themezer::dateOf(item.updatedAt);
    const local::Pack* folder = item.kind == themezer::Kind::Packs ? folderOf(item) : nullptr;
    if (!folder || item.themes.empty())
        return text;
    const outdated::PackDiff diff = diffOf(item, *folder);
    if (!diff.added.empty())
        text += ", adds " + std::to_string(diff.added.size()) + (diff.added.size() == 1 ? " theme" : " themes");
    if (!diff.removed.empty())
        text += ", moves " + std::to_string(diff.removed.size()) + " out of the folder";
    return text;
}

} // namespace tz::itemupdates
