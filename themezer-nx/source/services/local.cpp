#include "services/local.hpp"

#include "sys/jobs.hpp"

#include <unistd.h>

#include <algorithm>
#include <memory>
#include <unordered_map>

namespace tz::local
{

namespace
{

std::string key(library::Kind kind, const std::string& id)
{
    return std::string(kind == library::Kind::Splash ? "s:" : "t:") + id;
}

} // namespace

Library& Library::get()
{
    static Library library;
    return library;
}

void Library::rescan()
{
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
            // what was read of the entries already, kept for the ones still there
            for (library::Entry& entry : *found)
            {
                if (const library::Entry* known = library.find(entry.path); known && known->infoRead &&
                                                                           known->size == entry.size)
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
        });
    });
}

void Library::index()
{
    ids_.clear();
    packs_.clear();
    std::unordered_map<std::string, size_t> byDir;
    const std::string root = library::kRoot, own = root + "/ThemezerNX";
    for (size_t i = 0; i < entries_.size(); ++i)
    {
        const library::Entry& entry = entries_[i];
        if (!entry.themezerId.empty())
            ids_.insert(key(entry.kind, entry.themezerId));
        if (entry.kind != library::Kind::Theme)
            continue;
        // a folder's themes are a pack; the loose ones, in /Themes and in Themezer-NX's own folder, aren't
        std::string dir = entry.path.substr(0, entry.path.rfind('/'));
        if (dir == root || dir == own)
            continue;
        const auto [at, added] = byDir.try_emplace(dir, packs_.size());
        if (added)
        {
            Pack pack;
            const std::string folder = dir.substr(dir.rfind('/') + 1);
            library::NameParts parts;
            if (library::parseFolderName(folder, parts))
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

bool Library::remove(size_t index)
{
    if (index >= entries_.size() || !library::remove(entries_[index]))
        return false;
    entries_.erase(entries_.begin() + static_cast<long>(index));
    this->index();
    return true;
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

bool Library::removePack(std::string dir)
{
    const Pack* pack = findPack(dir);
    if (!pack)
        return false;
    bool ok = true;
    std::vector<size_t> gone;
    for (size_t index : pack->themes)
    {
        if (library::remove(entries_[index]))
            gone.push_back(index);
        else
            ok = false;
    }
    // fails, and the folder stays, when something else is in it
    rmdir(dir.c_str());
    std::sort(gone.rbegin(), gone.rend());
    for (size_t index : gone)
        entries_.erase(entries_.begin() + static_cast<long>(index));
    index();
    return ok;
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
