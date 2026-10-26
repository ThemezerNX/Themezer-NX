#include "services/item_updates.hpp"

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
    std::vector<int64_t> modified; // of its themes that have an ID: the others are never replaced, so never get newer
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

// The pack's folder on the SD card, the case of the ID not mattering
const local::Pack* folderOf(const std::string& id)
{
    const std::string want = outdated::canonical(id);
    for (const local::Pack& pack : local::Library::get().packs())
    {
        if (!pack.themezerId.empty() && outdated::canonical(pack.themezerId) == want)
            return &pack;
    }
    return nullptr;
}

// Themezer's pack against the folder, by the themes' IDs
outdated::PackDiff diffOf(const themezer::Item& pack, const local::Pack& folder)
{
    const auto& entries = local::Library::get().entries();
    std::vector<std::string> listed, there;
    for (const themezer::Item& theme : pack.themes)
        listed.push_back(theme.id);
    for (size_t index : folder.themes)
    {
        if (index < entries.size() && !entries[index].themezerId.empty())
            there.push_back(entries[index].themezerId);
    }
    return outdated::diffPack(listed, there);
}

// Every copy of a theme or splash on the SD card, the newest first
std::vector<const library::Entry*> copies(library::Kind kind, const std::string& id)
{
    auto found = local::Library::get().findAll(kind, id);
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
        Folder folder{pack.themezerId, {}};
        for (size_t index : pack.themes)
        {
            if (index < library.entries().size() && !library.entries()[index].themezerId.empty())
                folder.modified.push_back(library.entries()[index].modified);
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
        std::expected<std::vector<themezer::Item>, themezer::Error> result;
        if (token.cancelled())
            result = std::unexpected(themezer::Error{{}, {}, true});
        else if (!asked.empty())
            result = themezer::checkUpdates(asked, token);
        std::printf("Item updates: %zu items on the SD card asked about, %zu newer\n", asked.size(),
                    result ? result->size() : 0);
        jobs::post([result = std::move(result), done] {
            running = false;
            if (result)
            {
                items = *result;
                reindex();
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
    const auto at = indexes.find(keyOf(kind, id));
    return at == indexes.end() ? nullptr : &items[at->second];
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
            const local::Pack* folder = folderOf(item.id);
            if (!folder)
                continue;
            for (const themezer::Item& theme : item.themes)
            {
                if (theme.downloadUrl.empty() || theme.id.empty())
                    continue;
                one.steps.push_back(stepFor(theme, folder->dir, copies(library::Kind::Theme, theme.id)));
                covered.insert(one.steps.back().key);
            }
            // a pack with nothing to download in the answer would have every theme of the folder taken out of it
            if (one.steps.empty())
                continue;
            for (const std::string& gone : diffOf(item, *folder).removed)
            {
                for (size_t index : folder->themes)
                {
                    if (index < entries.size() && outdated::canonical(entries[index].themezerId) == gone)
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
            const auto kind = item.kind == themezer::Kind::Splashes ? library::Kind::Splash : library::Kind::Theme;
            const auto files = copies(kind, item.id);
            if (files.empty() || item.downloadUrl.empty())
                continue;
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
    const local::Pack* folder = item.kind == themezer::Kind::Packs ? folderOf(item.id) : nullptr;
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
