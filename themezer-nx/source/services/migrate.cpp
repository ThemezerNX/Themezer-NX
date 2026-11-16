#include "services/migrate.hpp"

#include "domain/installed.hpp"
#include "domain/installs.hpp"
#include "domain/library.hpp"
#include "domain/names.hpp"
#include "domain/outdated.hpp"
#include "services/local.hpp"
#include "services/themezer.hpp"
#include "sys/jobs.hpp"
#include "sys/log.hpp"
#include "ui/overlays.hpp"

#include <unistd.h>

#include <cstdio>
#include <iterator>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace tz::migrate
{

namespace
{

// A file or a pack's folder Themezer-NX 3 named: the letter and ID in its name, and where it is
struct Old
{
    char letter;
    std::string id;
    std::string path;
};

char letterOf(themezer::Kind kind)
{
    return kind == themezer::Kind::Packs ? 'P' : kind == themezer::Kind::Splashes ? 'S' : 'T';
}

// The name Themezer gives the item, for its file or its pack's folder
std::string nameOf(const themezer::Item& item)
{
    if (item.kind == themezer::Kind::Packs)
        return names::packFolder(item.name, item.creator, item.id);
    if (item.kind == themezer::Kind::Splashes)
        return names::splashFile(item.name, item.creator, item.id);
    return names::themeFile(item.name, item.creator, item.target, item.id);
}

// After a migration renamed files and folders, from and to in order: what was queued from them or installed from them
// follows them, and the SD card is read again before then. Main thread.
void renamed(const std::vector<std::pair<std::string, std::string>>& moves, std::function<void(bool done)> then,
             bool done)
{
    if (moves.empty())
    {
        then(done);
        return;
    }
    for (const auto& [from, to] : moves)
        installs::moved(from, to);
    installed::moved(moves);
    local::Library::get().rescan([then, done] { then(done); });
}

void v3Names(std::function<void(bool done)> then)
{
    const local::Library& library = local::Library::get();
    // Themezer-NX 3 wrote only in its own folder: a name of that shape elsewhere ("Mine by me (2024)") is someone's own
    const std::string root = std::string(library::kRoot) + "/ThemezerNX/";
    std::vector<Old> old;
    for (const library::Entry& entry : library.entries())
    {
        if (entry.v3Name && entry.path.starts_with(root))
            old.push_back({entry.kind == library::Kind::Splash ? 'S' : 'T', entry.themezerId, entry.path});
    }
    // the folders after the themes in them, whose paths have the folder's old name
    for (const local::Pack& pack : library.packs())
    {
        if (pack.v3Name && pack.dir.starts_with(root))
            old.push_back({'P', pack.themezerId, pack.dir});
    }
    std::vector<outdated::File> files;
    for (const Old& one : old)
        files.push_back({one.letter, one.id, 1});
    // hex IDs only, not a remote install's code; every version is newer than 1, so each item Themezer still has answers
    const std::vector<outdated::Query> asked = outdated::queries(files);
    if (asked.empty())
    {
        then(true);
        return;
    }

    jobs::run(jobs::Lane::Images, [old = std::move(old), asked, then] {
        std::vector<std::pair<std::string, std::string>> moves;
        auto answers = themezer::checkUpdates(asked, CancelToken{});
        const bool done = answers.has_value();
        if (!answers)
        {
            log::info("Themezer-NX 3's names: kept for now (%s)", answers.error().message.c_str());
        }
        else
        {
            // what each Quick ID is now; one two items answer for is left as it is
            std::unordered_map<std::string, const themezer::Item*> current;
            std::unordered_set<std::string> twice;
            for (const themezer::Newer& newer : *answers)
            {
                if (!current.emplace(newer.asked, &newer.item).second)
                    twice.insert(newer.asked);
            }
            for (const Old& one : old)
            {
                const std::string key = one.letter + outdated::canonical(one.id);
                const auto at = current.find(key);
                if (at == current.end() || twice.count(key) > 0 || letterOf(at->second->kind) != one.letter)
                    continue;
                const std::string to = one.path.substr(0, one.path.rfind('/') + 1) + nameOf(*at->second);
                // never in place of another file: with one of that name there, both stay as they are
                if (access(to.c_str(), F_OK) == 0 || rename(one.path.c_str(), to.c_str()) != 0)
                {
                    log::info("Themezer-NX 3's names: %s stays as it is", one.path.c_str());
                    continue;
                }
                log::info("Themezer-NX 3's names: %s is now %s", one.path.c_str(), to.c_str());
                moves.emplace_back(one.path, to);
            }
            log::info("Themezer-NX 3's names: %zu asked about, %zu renamed", asked.size(), moves.size());
        }
        jobs::post([moves = std::move(moves), then, done] {
            if (!moves.empty())
                ui::toast(moves.size() == 1 ? std::string("1 download from Themezer-NX 3 has Themezer's name now")
                                            : std::to_string(moves.size()) +
                                                  " downloads from Themezer-NX 3 have Themezer's names now",
                          ui::ToastKind::Success);
            renamed(moves, then, done);
        });
    });
}

void plainIds(std::function<void(bool done)> then)
{
    const local::Library& library = local::Library::get();
    // the folders after the files in them, whose paths have the folder's old name
    std::vector<std::string> paths;
    for (const library::Entry& entry : library.entries())
        paths.push_back(entry.path);
    for (const local::Pack& pack : library.packs())
        paths.push_back(pack.dir);
    jobs::run(jobs::Lane::Images, [paths = std::move(paths), then] {
        std::vector<std::pair<std::string, std::string>> moves;
        for (const std::string& from : paths)
        {
            const size_t slash = from.rfind('/');
            const std::string leaf = from.substr(slash + 1);
            const std::string plain = names::plainId(leaf);
            if (plain == leaf)
                continue;
            const std::string to = from.substr(0, slash + 1) + plain;
            // never in place of another file: with one of that name there, both stay as they are
            if (access(to.c_str(), F_OK) == 0 || rename(from.c_str(), to.c_str()) != 0)
            {
                log::info("Plain IDs: %s stays as it is", from.c_str());
                continue;
            }
            moves.emplace_back(from, to);
        }
        log::info("Plain IDs: %zu renamed", moves.size());
        // nothing to ask: done whatever stays as it is, which no later start would rename either
        jobs::post([moves = std::move(moves), then] { renamed(moves, then, true); });
    });
}

struct Migration
{
    const char* id; // never changed once released: settings.json keeps it
    void (*run)(std::function<void(bool done)> then);
};

// oldest first
constexpr Migration kMigrations[] = {
    {"v3-names", v3Names},
    {"plain-ids", plainIds},
};

void from(size_t index, std::set<std::string> done, std::function<void(const char* id)> finished,
          std::function<void()> then)
{
    while (index < std::size(kMigrations) && done.count(kMigrations[index].id) > 0)
        ++index;
    if (index == std::size(kMigrations))
    {
        then();
        return;
    }
    kMigrations[index].run([index, done = std::move(done), finished, then](bool ok) mutable {
        if (!ok)
        {
            // the ones after it may count on it: all of them wait for the next start
            then();
            return;
        }
        finished(kMigrations[index].id);
        from(index + 1, std::move(done), std::move(finished), std::move(then));
    });
}

} // namespace

void run(std::set<std::string> done, std::function<void(const char* id)> finished, std::function<void()> then)
{
    from(0, std::move(done), std::move(finished), std::move(then));
}

} // namespace tz::migrate
