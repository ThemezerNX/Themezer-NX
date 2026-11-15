#include "services/local.hpp"

#include "domain/names.hpp"
#include "sys/jobs.hpp"

#include <unistd.h>

#include <algorithm>
#include <memory>
#include <unordered_map>
#include <utility>

namespace tz::local
{

namespace
{

std::string key(library::Kind kind, const std::string& id)
{
    return std::string(kind == library::Kind::Splash ? "s:" : "t:") + id;
}

} // namespace

bool inPack(const library::Entry& entry)
{
    const std::string root = library::kRoot;
    const std::string_view dir = std::string_view(entry.path).substr(0, entry.path.rfind('/'));
    return dir != root && dir != root + "/ThemezerNX";
}

Library& Library::get()
{
    static Library library;
    return library;
}

void Library::rescan(std::function<void()> then)
{
    if (then)
        after_.push_back(std::move(then));
    // one scan at a time; a change while it runs scans again after it
    if (scanning_)
    {
        again_ = true;
        return;
    }
    scanning_ = true;
    jobs::run(jobs::Lane::Images, [] {
        auto found = std::make_shared<std::vector<library::Entry>>(library::scan());
        jobs::post([found] {
            Library& library = Library::get();
            // what was read of the entries already, kept for the ones still there and the same: a file put in place of
            // another of its size (a splash's update) is a new one
            for (library::Entry& entry : *found)
            {
                if (const library::Entry* known = library.find(entry.path);
                    known && known->infoRead && known->size == entry.size && known->modified == entry.modified)
                    entry = *known;
            }
            library.entries_ = std::move(*found);
            library.reading_.clear();
            library.scanned_ = true;
            library.scanning_ = false;
            library.index();
            if (library.again_)
            {
                library.again_ = false;
                library.rescan();
            }
            else
            {
                // what was asked to follow the scan, which a rescan from it adds to again
                const auto after = std::exchange(library.after_, {});
                for (const auto& then : after)
                    then();
            }
        });
    });
}

void Library::index()
{
    ids_.clear();
    packs_.clear();
    std::unordered_map<std::string, size_t> byDir;
    for (size_t i = 0; i < entries_.size(); ++i)
    {
        const library::Entry& entry = entries_[i];
        if (!entry.themezerId.empty())
            ids_.insert(key(entry.kind, entry.themezerId));
        if (entry.kind != library::Kind::Theme)
            continue;
        if (!inPack(entry))
            continue;
        std::string dir = entry.path.substr(0, entry.path.rfind('/'));
        const auto [at, added] = byDir.try_emplace(dir, packs_.size());
        if (added)
        {
            Pack pack;
            const std::string folder = dir.substr(dir.rfind('/') + 1);
            if (names::Parts parts; names::parseFolder(folder, parts))
            {
                pack.name = parts.name;
                pack.author = parts.author;
                pack.themezerId = parts.id;
            }
            else
            {
                pack.name = folder;
            }
            pack.dir = std::move(dir);
            packs_.push_back(std::move(pack));
        }
        Pack& pack = packs_[at->second];
        pack.themes.push_back(i);
        pack.size += entry.size;
        pack.modified = std::max(pack.modified, entry.modified);
    }
    ++generation_;
}

void Library::wantInfo(size_t index)
{
    if (index >= entries_.size())
        return;
    if (entries_[index].infoRead || !reading_.insert(entries_[index].path).second)
        return;
    jobs::run(jobs::Lane::Images, [entry = entries_[index]]() mutable {
        library::readInfo(entry);
        jobs::post([entry = std::move(entry)] {
            Library& library = Library::get();
            library.reading_.erase(entry.path);
            for (library::Entry& known : library.entries_)
            {
                if (known.path == entry.path)
                {
                    known = entry;
                    ++library.generation_;
                    break;
                }
            }
        });
    });
}

std::expected<void, themezer::Error> Library::remove(size_t index)
{
    if (index >= entries_.size())
        return std::unexpected(themezer::storageError("library index"));
    if (!library::remove(entries_[index]))
        return std::unexpected(themezer::storageError(entries_[index].path));
    entries_.erase(entries_.begin() + static_cast<long>(index));
    this->index();
    return {};
}

const library::Entry* Library::find(const std::string& path) const
{
    for (const library::Entry& entry : entries_)
    {
        if (entry.path == path)
            return &entry;
    }
    return nullptr;
}

const library::Entry* Library::findItem(library::Kind kind, const std::string& id) const
{
    for (const library::Entry& entry : entries_)
    {
        if (entry.kind == kind && !id.empty() && entry.themezerId == id)
            return &entry;
    }
    return nullptr;
}

const Pack* Library::findPackById(const std::string& id) const
{
    for (const Pack& pack : packs_)
    {
        if (!id.empty() && pack.themezerId == id)
            return &pack;
    }
    return nullptr;
}

const Pack* Library::findPack(const std::string& dir) const
{
    for (const Pack& pack : packs_)
    {
        if (pack.dir == dir)
            return &pack;
    }
    return nullptr;
}

const library::Entry* Library::cover(const Pack& pack) const
{
    const library::Entry* first = nullptr;
    for (size_t index : pack.themes)
    {
        if (index >= entries_.size())
            continue;
        if (entries_[index].target == 0)
            return &entries_[index];
        if (!first)
            first = &entries_[index];
    }
    return first;
}

std::expected<void, themezer::Error> Library::removePack(std::string dir)
{
    const Pack* pack = findPack(dir);
    if (!pack)
        return std::unexpected(themezer::storageError(dir));
    std::optional<themezer::Error> error;
    std::vector<size_t> gone;
    for (size_t index : pack->themes)
    {
        if (library::remove(entries_[index]))
            gone.push_back(index);
        else
            error = themezer::storageError(entries_[index].path);
    }
    // fails, and the folder stays, when something else is in it
    if (rmdir(dir.c_str()) != 0)
        error = themezer::storageError(dir);
    std::sort(gone.rbegin(), gone.rend());
    for (size_t index : gone)
        entries_.erase(entries_.begin() + static_cast<long>(index));
    index();
    if (error)
        return std::unexpected(*error);
    return {};
}

bool Library::has(const themezer::Item& item) const
{
    if (item.kind == themezer::Kind::Packs)
        return !item.themes.empty() && std::all_of(item.themes.begin(), item.themes.end(),
                                                    [&](const themezer::Item& theme) { return has(theme); });
    const auto kind = item.kind == themezer::Kind::Splashes ? library::Kind::Splash : library::Kind::Theme;
    return ids_.count(key(kind, item.id)) > 0;
}

} // namespace tz::local
