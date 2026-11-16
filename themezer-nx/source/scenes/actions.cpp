#include "scenes/actions.hpp"

#include "sys/log.hpp"
#include "app/app.hpp"
#include "services/reporter.hpp"
#include "core/version.hpp"
#include "domain/installed.hpp"
#include "domain/installs.hpp"
#include "domain/names.hpp"
#include "domain/outdated.hpp"
#include "scenes/local_details.hpp"
#include "scenes/local_pack.hpp"
#include "services/item_updates.hpp"
#include "services/local.hpp"
#include "services/updates.hpp"
#include "scenes/details.hpp"
#include "scenes/frame.hpp"
#include "scenes/pack.hpp"
#include "sys/audio.hpp"
#include "sys/session.hpp"
#include "ui/overlays.hpp"

#include <switch.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <map>
#include <memory>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace tz::scenes
{

using namespace gfx;
using themezer::Item;

namespace
{

ui::SceneStack& scenes()
{
    return App::get().scenes();
}

void showError(const std::string& title, const themezer::Error& error, bool warning = false, bool reportable = true)
{
    if (error.kind == themezer::ErrorKind::Cancelled)
    {
        ui::toast("Cancelled");
        return;
    }
    log::error("shown: %s: %s (%s)", themezer::kindName(error.kind), error.message.c_str(), error.detail.c_str());
    std::vector<ui::DialogButton> buttons{{"OK", nullptr, false, ui::key::kA | ui::key::kB}};
    if (error.kind == themezer::ErrorKind::OutOfMemory)
        log::logMemory();
    if (reportable && themezer::shareable(error.kind))
        buttons.insert(buttons.begin(), {"Share error", [error] { report::share(error); }, false, ui::key::kY});
    scenes().emplace<ui::Dialog>(warning ? Icon::Warning : Icon::Error, warning ? color::kWarning : color::kDanger,
                                 title, error.message, std::move(buttons));
}

bool validateLocal(library::Entry& entry)
{
    if (access(entry.path.c_str(), R_OK) != 0)
    {
        showError("Couldn't read it", themezer::storageError(entry.path));
        return false;
    }
    try
    {
        if (library::readInfo(entry))
            return true;
    }
    catch (const std::bad_alloc&)
    {
        showError("Couldn't install it", {"There isn't enough memory to read this file.", entry.path,
                                          themezer::ErrorKind::OutOfMemory});
        return false;
    }
    const bool fromThemezer = names::fromThemezer(entry.fileName.empty() ? entry.path : entry.fileName);
    const std::string message = fromThemezer
        ? "Its files are damaged or unsupported. Try downloading it again."
        : "Its files are damaged or unsupported. It isn't from Themezer: get it again from where it came from.";
    showError("Couldn't install it", {message, entry.errorDetail, themezer::ErrorKind::InvalidData}, false, fromThemezer);
    return false;
}

void queueAndSay(const Item& item, const std::string& path)
{
    const int slot = installs::slotOf(item);
    const installs::Queued* before = installs::queued(slot);
    if (before && before->item == installs::itemKey(item))
    {
        ui::toast("“" + item.name + "” is queued already");
        return;
    }
    const std::string replaced = before ? before->name : "";
    installs::queue(slot, path, item);
    ui::toast(replaced.empty() ? "“" + item.name + "” is queued. Exit through + to install it."
                               : "“" + item.name + "” is queued instead of “" + replaced + "”",
              ui::ToastKind::Success);
}

// "3 themes queued. Exit through + to install them.", one or more
std::string queuedMessage(size_t count)
{
    return count == 1 ? std::string("1 theme queued. Exit through + to install it.")
                      : std::to_string(count) + " themes queued. Exit through + to install them.";
}

// Where a download is kept: a pack's themes in the pack's folder (the one on the SD card already, else one named as
// Themezer names the pack's zip), splashes in Splashes, the rest in /Themes/ThemezerNX. Main thread.
std::string folderFor(const Item& item)
{
    const std::string root = "/Themes/ThemezerNX";
    if (item.kind == themezer::Kind::Splashes)
        return root + "/Splashes";
    if (item.packId.empty())
        return root;
    if (const local::Pack* pack = local::Library::get().findPackById(item.packId))
        return pack->dir;
    return root + "/" + names::packFolder(item.packName, item.packCreator, item.packId);
}

// Its name when the download doesn't say, as Themezer would name it.
std::string fileFor(const Item& item)
{
    const std::string id = item.remote ? std::string() : item.id;
    return item.kind == themezer::Kind::Splashes ? names::splashFile(item.name, item.creator, id)
                                                 : names::themeFile(item.name, item.creator, item.target, id);
}

// The item's file on the SD card already, wherever it is and whatever it's called (downloaded here, or from the
// website); empty when there's none. Main thread.
std::string onSdCard(const Item& item)
{
    if (item.remote)
        return {};
    const auto kind = item.kind == themezer::Kind::Splashes ? library::Kind::Splash : library::Kind::Theme;
    const library::Entry* entry = local::Library::get().findItem(kind, item.id);
    return entry ? entry->path : std::string();
}

struct PackResult
{
    std::vector<std::pair<int, std::string>> done; // the theme's index and where it went
    int failed = 0;
    bool cancelled = false;
    themezer::Error firstError{{}, {}, themezer::ErrorKind::Server};
};

// Downloads the themes of the pack at these indexes (where they aren't on the SD card yet), one after another.
void runPackDownloads(const Item& pack, std::vector<int> chosen, bool queue, bool save)
{
    auto themes = std::make_shared<std::vector<Item>>(pack.themes);
    // what's there already, and where the rest go: the Library is the main thread's
    std::vector<std::string> there, folders;
    for (int index : chosen)
    {
        const Item& theme = (*themes)[static_cast<size_t>(index)];
        there.push_back(onSdCard(theme));
        folders.push_back(folderFor(theme));
    }
    auto overlay = ui::ProgressOverlay::start<PackResult>(
        {queue ? "Installing " + pack.name : "Downloading " + pack.name, true, accentOf(themezer::Kind::Packs).solid},
        [themes, chosen, save, there, folders](ui::TaskProgress& progress, const CancelToken& token) {
            PackResult result;
            const int total = static_cast<int>(chosen.size());
            for (int i = 0; i < total; ++i)
            {
                const Item& theme = (*themes)[static_cast<size_t>(chosen[static_cast<size_t>(i)])];
                progress.set(i, total);
                progress.download.received = 0;
                progress.download.total = 0;
                progress.setStatus("Theme " + std::to_string(i + 1) + " of " + std::to_string(total) + ": " +
                                   theme.name);
                std::string path = there[static_cast<size_t>(i)];
                std::expected<void, themezer::Error> done;
                if (path.empty() && save)
                {
                    auto saved = themezer::downloadInto(theme.downloadUrl, folders[static_cast<size_t>(i)],
                                                        fileFor(theme), token, &progress.download);
                    if (saved)
                        path = *saved;
                    else
                        done = std::unexpected(saved.error());
                }
                else if (path.empty())
                {
                    path = installs::temporaryPath(theme);
                    if (!installs::exists(path))
                        done = themezer::download(theme.downloadUrl, path, token, &progress.download);
                }
                if (!done)
                {
                    if (done.error().kind == themezer::ErrorKind::Cancelled)
                    {
                        result.cancelled = true;
                        break;
                    }
                    if (result.failed++ == 0)
                        result.firstError = done.error();
                    continue;
                }
                result.done.emplace_back(chosen[static_cast<size_t>(i)], path);
            }
            progress.set(total, total);
            return result;
        },
        [themes, queue, total = chosen.size()](PackResult result) {
            local::Library::get().rescan();
            if (queue)
            {
                for (const auto& [index, path] : result.done)
                {
                    const Item& theme = (*themes)[static_cast<size_t>(index)];
                    installs::queue(installs::slotOf(theme), path, theme);
                }
            }
            if (result.cancelled)
            {
                ui::toast(result.done.empty() ? "Cancelled"
                                              : "Cancelled after " + std::to_string(result.done.size()) + " of " +
                                                    std::to_string(total) + " themes",
                          ui::ToastKind::Warning);
                return;
            }
            if (result.failed > 0)
            {
                showError(std::to_string(result.failed) + (result.failed == 1 ? " theme" : " themes") +
                              " didn't download",
                          result.firstError, true);
                return;
            }
            const size_t done = result.done.size();
            ui::toast(queue ? queuedMessage(done)
                            : std::to_string(done) + (done == 1 ? " theme" : " themes") + " saved on the SD card",
                      ui::ToastKind::Success);
        });
    scenes().push(std::move(overlay));
}

struct UpdateResult
{
    std::vector<std::string> keys;                          // of what was put in place, for itemupdates::forget
    std::vector<std::pair<std::string, std::string>> moves; // files and folders that went elsewhere, in the order
    int failed = 0;                                         // items
    bool cancelled = false;
    themezer::Error firstError{{}, {}, themezer::ErrorKind::Server};
};

std::string leafOf(const std::string& path)
{
    return path.substr(path.rfind('/') + 1);
}

// Whether the console has the item installed, not only queued: its file changing leaves that copy as it is. Main
// thread.
bool installedCopy(const Item& item)
{
    if (item.kind == themezer::Kind::Packs)
        return std::any_of(item.themes.begin(), item.themes.end(), installedCopy);
    const int slot = installs::slotOf(item);
    const installed::Part& part = installed::part(slot);
    return slot >= 0 && part.custom && itemupdates::same(part.themezerId, item);
}

// Puts the files of these plans on the SD card, one after another: each downloaded to the temporary folder, then in
// place of its old copies, so a download that fails or is cut leaves the old file as it was. A pack's folder changes
// (the themes Themezer no longer lists move out of it, it takes Themezer's name for it) only when all of its themes got
// in.
void runUpdates(std::vector<itemupdates::Plan> plans, size_t count, bool installedCopies)
{
    int total = 0;
    for (const itemupdates::Plan& plan : plans)
        total += static_cast<int>(plan.steps.size());
    const std::string name = plans.front().name;
    auto overlay = ui::ProgressOverlay::start<UpdateResult>(
        {count == 1 ? "Updating " + name : "Updating " + std::to_string(count) + " items", true, color::kPrimary},
        [plans = std::move(plans), total](ui::TaskProgress& progress, const CancelToken& token) {
            UpdateResult result;
            int at = 0;
            for (const itemupdates::Plan& plan : plans)
            {
                int failedSteps = 0;
                const auto fail = [&](themezer::Error error) {
                    if (result.firstError.message.empty())
                        result.firstError = std::move(error);
                    ++failedSteps;
                };
                for (const itemupdates::Step& step : plan.steps)
                {
                    progress.set(at, total);
                    progress.download.received = 0;
                    progress.download.total = 0;
                    progress.setStatus(std::to_string(++at) + " of " + std::to_string(total) + ": " + step.name);
                    auto staged = themezer::downloadInto(step.url, installs::temporaryFolder(), step.fallback, token,
                                                         &progress.download);
                    if (!staged)
                    {
                        if (staged.error().kind == themezer::ErrorKind::Cancelled)
                        {
                            result.cancelled = true;
                            break;
                        }
                        fail(staged.error());
                        continue;
                    }
                    // the items before it may have moved its copies: a theme its pack no longer lists, a pack folder
                    // renamed
                    std::vector<std::string> others;
                    for (const std::string& other : step.others)
                        others.push_back(outdated::rebased(other, result.moves));
                    const std::string folder = step.folder.empty()
                                                   ? others.front().substr(0, others.front().rfind('/'))
                                                   : outdated::rebased(step.folder, result.moves);
                    const std::string final = folder + "/" + leafOf(*staged);
                    std::vector<std::string> replaced;
                    std::optional<themezer::Error> putError;
                    if (!outdated::replace(*staged, final, others, replaced,
                                           [&putError](const std::string& from, const std::string& to) {
                                               auto result = updates::replace(from, to);
                                               if (!result)
                                                   putError = result.error();
                                               return bool(result);
                                           }))
                    {
                        fail(putError.value_or(themezer::storageError(final)));
                        continue;
                    }
                    for (const std::string& from : replaced)
                        result.moves.emplace_back(from, final);
                    result.keys.push_back(step.key);
                }
                if (result.cancelled)
                    break;
                if (failedSteps > 0)
                {
                    ++result.failed;
                    continue;
                }
                // the files are updated already, and their version doesn't depend on where they are: a move that
                // fails is left as it is
                for (const auto& [from, to] : plan.takeOut)
                {
                    // one of that name in the folder above stays where it is
                    if (access(to.c_str(), F_OK) != 0 && rename(from.c_str(), to.c_str()) == 0)
                        result.moves.emplace_back(from, to);
                    else
                        log::info("Item updates: %s stays in its folder", from.c_str());
                }
                if (const auto& [from, to] = plan.rename;
                    !from.empty() && access(to.c_str(), F_OK) != 0 && rename(from.c_str(), to.c_str()) == 0)
                    result.moves.emplace_back(from, to);
                result.keys.push_back(plan.key);
            }
            progress.set(total, total);
            return result;
        },
        [count, name, installedCopies](UpdateResult result) {
            // what was queued from a file that moved, and what was installed from it, say where it is now
            for (const auto& [from, to] : result.moves)
                installs::moved(from, to);
            if (!result.moves.empty())
                installed::moved(result.moves);
            // what comes off the list is what's updated: a pack's themes are on it too, and count for themselves
            const size_t listed = itemupdates::count();
            itemupdates::forget(result.keys);
            const size_t updated = listed - itemupdates::count();
            local::Library::get().rescan();
            if (result.cancelled)
            {
                ui::toast(updated == 0 ? "Cancelled"
                                       : "Cancelled after " + std::to_string(updated) + " of " + std::to_string(count) +
                                             " items",
                          ui::ToastKind::Warning);
                return;
            }
            if (result.failed > 0)
            {
                showError(std::to_string(result.failed) + (result.failed == 1 ? " item" : " items") + " didn't update",
                          result.firstError, true);
                return;
            }
            // installing is what puts a file on the console: the one installed now is the old
            ui::toast((count == 1 ? "“" + name + "” is updated" : std::to_string(updated) + " items updated") +
                          (installedCopies ? ". The installed copy changes when you install it again" : ""),
                      ui::ToastKind::Success);
        });
    scenes().push(std::move(overlay));
}

// Asks, for each part of the HOME Menu with more than one theme in the pack, which one, then goes on with the choice.
void choosePackThemes(std::shared_ptr<Item> pack, std::shared_ptr<std::map<int, std::vector<int>>> byTarget,
                      std::shared_ptr<std::vector<int>> chosen, std::map<int, std::vector<int>>::iterator at,
                      std::function<void()> then)
{
    while (at != byTarget->end() && at->second.size() == 1)
    {
        chosen->push_back(at->second.front());
        ++at;
    }
    if (at == byTarget->end())
    {
        then();
        return;
    }
    std::vector<ui::Action> actions;
    for (int index : at->second)
    {
        const Item& theme = pack->themes[static_cast<size_t>(index)];
        auto next = std::next(at);
        actions.push_back({Icon::Theme, theme.name, "by " + theme.creator, true, false,
                           [pack, byTarget, chosen, next, index, then] {
                               chosen->push_back(index);
                               choosePackThemes(pack, byTarget, chosen, next, then);
                           }});
    }
    scenes().emplace<ui::ActionSheet>("Which " + std::string(themezer::targetLabel(at->first)) + " theme?",
                                      std::move(actions), accentOf(themezer::Kind::Packs).solid);
}

// Leaves, to NXThemes Installer when something's queued: not with more than the loader can pass on to it.
void exitApp()
{
    const char* installer = installs::installerPath();
    if (installer && installs::queuedCount() > 0 && installs::installerArgs(installer).size() > installs::kMaxArgs)
    {
        scenes().emplace<ui::Dialog>(Icon::Warning, color::kWarning, "Too much to install at once",
                                     "The queued files' names are too long for NXThemes Installer to take all at once. "
                                     "Take some out of the queue, and install them next time.");
        return;
    }
    if (installer && installs::queuedCount() > 0)
    {
        if (auto copied = installs::copyForInstaller(); !copied)
        {
            showError("Couldn't prepare the install", copied.error());
            return;
        }
    }
    App::get().quit();
}

// Which of the pack's themes to install, one for each part (asked where it has more than one), then whether to
// replace what's queued already; go gets the indexes of the ones chosen.
void choosePack(Item pack, std::function<void(std::vector<int>)> go)
{
    auto byTarget = std::make_shared<std::map<int, std::vector<int>>>();
    for (size_t i = 0; i < pack.themes.size(); ++i)
    {
        if (pack.themes[i].target >= 0)
            (*byTarget)[pack.themes[i].target].push_back(static_cast<int>(i));
    }
    if (byTarget->empty())
    {
        ui::toast("This pack has no themes to install", ui::ToastKind::Warning);
        return;
    }
    auto shared = std::make_shared<Item>(std::move(pack));
    auto chosen = std::make_shared<std::vector<int>>();
    choosePackThemes(shared, byTarget, chosen, byTarget->begin(), [shared, chosen, go] {
        int replaced = 0;
        for (int index : *chosen)
            replaced += installs::queued(installs::slotOf(shared->themes[static_cast<size_t>(index)])) ? 1 : 0;
        if (replaced == 0)
        {
            go(*chosen);
            return;
        }
        scenes().emplace<ui::Dialog>(
            Icon::Queue, color::kWarning, "Replace queued installs?",
            "This replaces " + std::to_string(replaced) + (replaced == 1 ? " install" : " installs") +
                " already queued.",
            std::vector<ui::DialogButton>{{"Keep them", nullptr}, {"Replace", [chosen, go] { go(*chosen); }}});
    });
}

} // namespace

bool canInstall()
{
    return installs::installerPath() != nullptr;
}

namespace
{
constexpr const char* kInstallerUrl =
    "https://github.com/exelix11/SwitchThemeInjector/releases/latest/download/NXThemesInstaller.nro";
constexpr const char* kInstallerPath = "/switch/NXThemesInstaller.nro";

void downloadInstaller()
{
    scenes().push(ui::ProgressOverlay::start<std::expected<void, themezer::Error>>(
        {"Downloading NXThemes Installer", true, color::kPrimary},
        [](ui::TaskProgress& progress, const CancelToken& token) -> std::expected<void, themezer::Error> {
            auto done = themezer::download(kInstallerUrl, kInstallerPath, token, &progress.download);
            if (done && !version::isNro(kInstallerPath))
            {
                unlink(kInstallerPath);
                return std::unexpected(
                    themezer::Error{"What came wasn't NXThemes Installer. Try again later.", {},
                                    themezer::ErrorKind::InvalidData});
            }
            return done;
        },
        [](std::expected<void, themezer::Error> result) {
            if (result)
                ui::toast("NXThemes Installer is ready", ui::ToastKind::Success);
            else
                showError("Couldn't download NXThemes Installer", result.error());
        }));
}
} // namespace

void offerInstaller()
{
    scenes().emplace<ui::Dialog>(
        Icon::Download, color::kPrimary, "Download NXThemes Installer?",
        "Themezer-NX installs themes with NXThemes Installer, which isn't on the SD card. It's downloaded from its "
        "releases on GitHub to sd:/switch/NXThemesInstaller.nro.",
        std::vector<ui::DialogButton>{{"Not now", nullptr}, {"Download", [] { downloadInstaller(); }}});
}

namespace
{
bool keyboardUp = false;
} // namespace

bool tookKeyboard()
{
    return std::exchange(keyboardUp, false);
}

std::optional<std::string> askText(const char* guide, const std::string& initial, size_t maxLength)
{
    keyboardUp = true;
    SwkbdConfig config;
    if (R_FAILED(swkbdCreate(&config, 0)))
        return std::nullopt;
    std::string out(maxLength + 1, '\0');
    swkbdConfigMakePresetDefault(&config);
    swkbdConfigSetGuideText(&config, guide);
    swkbdConfigSetInitialText(&config, initial.c_str());
    swkbdConfigSetStringLenMax(&config, static_cast<u32>(maxLength));
    session::holdWatch(true);
    const Result result = swkbdShow(&config, out.data(), out.size());
    session::holdWatch(false);
    swkbdClose(&config);
    if (R_FAILED(result))
        return std::nullopt;
    out.resize(std::strlen(out.c_str()));
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    while (!out.empty() && out.front() == ' ')
        out.erase(out.begin());
    return out;
}

themezer::Item localItem(const library::Entry& entry)
{
    Item item;
    item.kind = entry.kind == library::Kind::Splash ? themezer::Kind::Splashes : themezer::Kind::Themes;
    // a file that isn't from Themezer goes by its path (see installs::queue)
    item.id = entry.themezerId.empty() ? "file:" + entry.path : entry.themezerId;
    item.name = entry.name;
    item.creator = entry.author;
    item.target = entry.target;
    return item;
}

void unqueueAndSay(int slot)
{
    const installs::Queued* queued = installs::queued(slot);
    if (!queued)
        return;
    const std::string name = queued->name;
    installs::unqueue(slot);
    sfx::play(sfx::Sound::Back);
    ui::toast("“" + name + "” won't be installed");
}

void unqueueAndSay(const std::vector<int>& slots)
{
    if (slots.size() == 1)
    {
        unqueueAndSay(slots.front());
        return;
    }
    for (int slot : slots)
        installs::unqueue(slot);
    sfx::play(sfx::Sound::Back);
    ui::toast(std::to_string(slots.size()) + " themes won't be installed");
}

void openLocal(const library::Entry& entry)
{
    scenes().emplace<LocalDetailsScene>(entry.path);
}

void installLocal(const library::Entry& entry)
{
    if (!canInstall())
    {
        offerInstaller();
        return;
    }
    library::Entry known = entry;
    log::info("install: %s", known.path.c_str());
    if (!validateLocal(known))
        return;
    queueAndSay(localItem(known), known.path);
}

void openLocalPack(const std::string& dir)
{
    scenes().emplace<LocalPackScene>(dir);
}

void openPackOf(const library::Entry& theme)
{
    scenes().emplace<LocalPackScene>(theme.path.substr(0, theme.path.rfind('/')),
                                     std::vector<ui::Crumb>{modeCrumb(false), sectionCrumb(themezer::Kind::Packs)});
}

void installLocalPack(const std::string& dir)
{
    local::Library& library = local::Library::get();
    const local::Pack* found = library.findPack(dir);
    if (!found || found->themes.empty())
        return;
    if (!canInstall())
    {
        offerInstaller();
        return;
    }
    Item pack;
    std::vector<std::string> paths;
    for (size_t index : found->themes)
    {
        library::Entry theme = library.entries()[index];
        log::info("install: %s", theme.path.c_str());
        if (!validateLocal(theme))
            return;
        pack.themes.push_back(localItem(theme));
        paths.push_back(theme.path);
    }
    auto themes = std::make_shared<std::vector<Item>>(pack.themes);
    choosePack(std::move(pack), [themes, paths](std::vector<int> chosen) {
        for (int index : chosen)
        {
            const Item& theme = (*themes)[static_cast<size_t>(index)];
            installs::queue(installs::slotOf(theme), paths[static_cast<size_t>(index)], theme);
        }
        ui::toast(queuedMessage(chosen.size()), ui::ToastKind::Success);
    });
}

void deleteLocalPack(const std::string& dir)
{
    const local::Pack* pack = local::Library::get().findPack(dir);
    if (!pack)
        return;
    const size_t count = pack->themes.size();
    scenes().emplace<ui::Dialog>(
        Icon::Trash, color::kDanger, "Delete “" + pack->name + "”?",
        "This deletes its " + std::to_string(count) + (count == 1 ? " theme" : " themes") + " from the SD card.",
        std::vector<ui::DialogButton>{
            {"Keep it", nullptr},
            {"Delete",
             [dir] {
                 const auto ok = local::Library::get().removePack(dir);
                 // what was queued from it has nothing to install any more
                 for (int slot = 0; slot < installs::kSlotCount; ++slot)
                 {
                     const installs::Queued* queued = installs::queued(slot);
                     if (queued && queued->path.starts_with(dir + "/") && !installs::exists(queued->path))
                         installs::unqueue(slot);
                 }
                 if (!ok)
                 {
                     showError("Couldn't delete it", ok.error());
                     return;
                 }
                 sfx::play(sfx::Sound::Back);
                 ui::toast("Deleted");
             },
             true}});
}

void showOnThemezer(const std::string& quickId)
{
    scenes().push(ui::ProgressOverlay::start<std::expected<Item, themezer::Error>>(
        {"Looking it up", true, color::kPrimary},
        [quickId](ui::TaskProgress&, const CancelToken& token) { return themezer::lookupQuickId(quickId, token); },
        [](std::expected<Item, themezer::Error> result) {
            if (result)
                openFound(*result);
            else
                showError("Couldn't look it up", result.error());
        }));
}

void viewLocal(const library::Entry& entry)
{
    scenes().emplace<PictureScene>([entry] { return localPicture(entry, static_cast<int>(metric::kWidth)); });
}

void deleteLocal(const library::Entry& entry)
{
    const std::string path = entry.path;
    const int slot = entry.kind == library::Kind::Splash ? installs::kSplashSlot : entry.target;
    scenes().emplace<ui::Dialog>(
        Icon::Trash, color::kDanger, "Delete “" + entry.name + "”?", "This deletes the file from the SD card.",
        std::vector<ui::DialogButton>{
            {"Keep it", nullptr},
            {"Delete",
             [path, slot] {
                 local::Library& library = local::Library::get();
                 const auto& entries = library.entries();
                 for (size_t i = 0; i < entries.size(); ++i)
                 {
                     if (entries[i].path != path)
                         continue;
                     if (auto removed = library.remove(i); !removed)
                     {
                         showError("Couldn't delete it", removed.error());
                         return;
                     }
                     // queued, it isn't any more: there's nothing to install
                     if (const installs::Queued* queued = installs::queued(slot); queued && queued->path == path)
                         installs::unqueue(slot);
                     sfx::play(sfx::Sound::Back);
                     ui::toast("Deleted");
                     return;
                 }
             },
             true}});
}

void uninstallPart(int slot)
{
    const bool splash = slot == installs::kSplashSlot;
    const installed::Part& part = installed::part(slot);
    const std::string what = part.name.empty() ? (splash ? "the splash" : "the " + installs::slotLabel(slot) + " theme")
                                               : "“" + part.name + "”";
    scenes().emplace<ui::Dialog>(
        Icon::Trash, color::kDanger, "Uninstall " + what + "?",
        splash ? "Hekate's own splash comes back after a restart."
               : "Nintendo's " + installs::slotLabel(slot) + " comes back after a restart.",
        std::vector<ui::DialogButton>{{"Keep it", nullptr},
                                      {"Uninstall",
                                       [slot] {
                                           if (auto removed = installed::uninstall(slot); removed)
                                               ui::toast("Uninstalled: restart the console to see the change",
                                                         ui::ToastKind::Success);
                                           else
                                               showError("Couldn't uninstall it", removed.error());
                                       },
                                       true}});
}

void openItem(const Item& item)
{
    if (item.kind == themezer::Kind::Packs)
        scenes().emplace<PackScene>(item);
    else
        scenes().emplace<DetailsScene>(item);
}

void openFound(const Item& item)
{
    std::vector<ui::Crumb> trail{modeCrumb(true), sectionCrumb(item.kind)};
    if (item.kind == themezer::Kind::Packs)
        scenes().emplace<PackScene>(item, std::move(trail));
    else
        scenes().emplace<DetailsScene>(item, std::move(trail));
}

void install(const Item& item, bool save)
{
    if (!canInstall())
    {
        offerInstaller();
        return;
    }
    if (installs::slotOf(item) < 0)
    {
        showError(
            "Couldn't install it",
            {"This theme doesn't say which part of the HOME Menu it's for.", {}, themezer::ErrorKind::InvalidData});
        return;
    }
    // on the SD card already, downloaded here or from the website
    if (const std::string there = onSdCard(item); !there.empty())
    {
        queueAndSay(item, there);
        return;
    }
    if (!save)
    {
        const std::string path = installs::temporaryPath(item);
        if (installs::exists(path))
        {
            queueAndSay(item, path);
            return;
        }
        scenes().push(ui::ProgressOverlay::start<std::expected<void, themezer::Error>>(
            {"Downloading " + item.name, true, accentOf(item.kind).solid},
            [url = item.downloadUrl, path](ui::TaskProgress& progress, const CancelToken& token) {
                return themezer::download(url, path, token, &progress.download);
            },
            [item, path](std::expected<void, themezer::Error> result) {
                if (result)
                    queueAndSay(item, path);
                else
                    showError("Couldn't download it", result.error());
            }));
        return;
    }
    scenes().push(ui::ProgressOverlay::start<std::expected<std::string, themezer::Error>>(
        {"Downloading " + item.name, true, accentOf(item.kind).solid},
        [url = item.downloadUrl, folder = folderFor(item), file = fileFor(item)](ui::TaskProgress& progress,
                                                                                  const CancelToken& token) {
            return themezer::downloadInto(url, folder, file, token, &progress.download);
        },
        [item](std::expected<std::string, themezer::Error> result) {
            local::Library::get().rescan();
            if (result)
                queueAndSay(item, *result);
            else
                showError("Couldn't download it", result.error());
        }));
}

void download(const Item& item)
{
    if (!onSdCard(item).empty())
    {
        ui::toast("“" + item.name + "” is on the SD card already");
        return;
    }
    scenes().push(ui::ProgressOverlay::start<std::expected<std::string, themezer::Error>>(
        {"Downloading " + item.name, true, accentOf(item.kind).solid},
        [url = item.downloadUrl, folder = folderFor(item), file = fileFor(item)](ui::TaskProgress& progress,
                                                                                  const CancelToken& token) {
            return themezer::downloadInto(url, folder, file, token, &progress.download);
        },
        [name = item.name](std::expected<std::string, themezer::Error> result) {
            local::Library::get().rescan();
            if (result)
                ui::toast("“" + name + "” is saved on the SD card", ui::ToastKind::Success);
            else
                showError("Couldn't download it", result.error());
        }));
}

void installPack(const Item& pack, bool save)
{
    if (!canInstall())
    {
        offerInstaller();
        return;
    }
    choosePack(pack, [pack, save](std::vector<int> chosen) { runPackDownloads(pack, std::move(chosen), true, save); });
}

void downloadPack(const Item& pack)
{
    std::vector<int> all;
    for (size_t i = 0; i < pack.themes.size(); ++i)
        all.push_back(static_cast<int>(i));
    if (all.empty())
    {
        ui::toast("This pack has no themes", ui::ToastKind::Warning);
        return;
    }
    runPackDownloads(pack, all, false, true);
}

void openSettings()
{
    // each a box ticked while it's on
    const auto box = [](bool on) { return on ? Icon::SquareCheck : Icon::Square; };
    std::vector<ui::Action> actions;
    ui::Action music{box(App::get().config().music), "Music"};
    music.toggle = [box](ui::Action& action) {
        Config& config = App::get().config();
        config.music = !config.music;
        config.save();
        App::get().applyConfig();
        action.icon = box(config.music);
    };
    ui::Action sounds{box(App::get().config().sfx), "Sound effects"};
    sounds.toggle = [box](ui::Action& action) {
        Config& config = App::get().config();
        config.sfx = !config.sfx;
        config.save();
        App::get().applyConfig();
        action.icon = box(config.sfx);
    };
    actions.push_back(std::move(music));
    actions.push_back(std::move(sounds));
    scenes().emplace<ui::ActionSheet>("Settings", std::move(actions), color::kPrimary);
}

void openQuickId()
{
    const auto typed = askText("Quick ID (find it on themezer.net)", "", 64);
    if (!typed || typed->empty())
        return;
    const std::string quickId = *typed;
    scenes().push(ui::ProgressOverlay::start<std::expected<Item, themezer::Error>>(
        {"Looking up " + quickId, true, color::kPrimary},
        [quickId](ui::TaskProgress&, const CancelToken& token) { return themezer::lookupQuickId(quickId, token); },
        [](std::expected<Item, themezer::Error> result) {
            if (result)
                openFound(*result);
            else if (result.error().kind == themezer::ErrorKind::NotFound)
                scenes().emplace<ui::Dialog>(Icon::SearchEmpty, color::kTextDim, "Nothing found",
                                             result.error().message);
            else
                showError("Couldn't look it up", result.error());
        }));
}

void openQueue()
{
    const int count = installs::queuedCount();
    if (count == 0)
    {
        ui::toast("Nothing is queued yet. Install a theme or a splash to queue it.");
        return;
    }
    // what's queued, each with its part's icon; then leaving to install them, last, so a quick A doesn't
    std::vector<ui::Action> actions;
    for (int slot = 0; slot < installs::kSlotCount; ++slot)
    {
        const installs::Queued* queued = installs::queued(slot);
        if (!queued)
            continue;
        actions.push_back({slot == installs::kSplashSlot ? Icon::Splash : targetIcon(slot), queued->name,
                           installs::slotLabel(slot) + " · A cancels its install", true, false,
                           [slot] { unqueueAndSay(slot); }});
    }
    actions.push_back({Icon::Power, "Exit and install", "Themezer-NX closes and NXThemes Installer installs them",
                       canInstall(), false, exitApp, nullptr, ui::key::kPlus});
    scenes().emplace<ui::ActionSheet>("Queued installs (" + std::to_string(count) + ")", std::move(actions),
                                      color::kPrimary, 0);
}

namespace
{
// A release's .nro, downloaded to staged: kept only when it's an app and as big as GitHub said, so a cut download
// never takes the old one's place.
std::expected<void, themezer::Error> downloadNro(const updates::Release& release, const std::string& staged,
                                                 ui::TaskProgress& progress, const CancelToken& token)
{
    if (auto done = themezer::download(release.url, staged, token, &progress.download); !done)
        return done;
    struct stat info;
    const bool whole = release.size == 0 ||
                       (stat(staged.c_str(), &info) == 0 && static_cast<uint64_t>(info.st_size) == release.size);
    if (whole && version::isNro(staged.c_str()))
        return {};
    unlink(staged.c_str());
    return std::unexpected(
        themezer::Error{"What came wasn't the whole app. Try again later.", {}, themezer::ErrorKind::InvalidData});
}

void updateApp()
{
    const updates::Release* found = updates::available(updates::Program::App);
    if (!found)
        return;
    const updates::Release release = *found;
    const std::string staged = App::get().updatePath();
    scenes().push(ui::ProgressOverlay::start<std::expected<void, themezer::Error>>(
        {"Updating Themezer-NX to " + release.version, true, color::kPrimary},
        [release, staged](ui::TaskProgress& progress, const CancelToken& token) {
            return downloadNro(release, staged, progress, token);
        },
        [release](std::expected<void, themezer::Error> result) {
            if (!result)
            {
                showError("Couldn't download Themezer-NX " + release.version, result.error());
                return;
            }
            updates::done(updates::Program::App);
            // it goes in place as the app closes: at once, into the new version; with installs queued, on the way
            // out to NXThemes Installer, and it opens the next time
            const bool restart = installs::queuedCount() == 0;
            App::get().updateOnExit(restart);
            if (restart)
                App::get().quit();
            else
                ui::toast("Themezer-NX " + release.version + " is ready: it opens the next time you start it",
                          ui::ToastKind::Success);
        }));
}

void updateInstaller()
{
    const updates::Release* found = updates::available(updates::Program::Installer);
    const char* installed = installs::installerPath();
    if (!found || !installed)
        return;
    const updates::Release release = *found;
    const std::string path = installed;
    scenes().push(ui::ProgressOverlay::start<std::expected<void, themezer::Error>>(
        {"Updating NXThemes Installer to " + release.version, true, color::kPrimary},
        [release, path](ui::TaskProgress& progress, const CancelToken& token) -> std::expected<void, themezer::Error> {
            const std::string staged = path + ".new";
            if (auto done = downloadNro(release, staged, progress, token); !done)
                return done;
            if (auto put = updates::replace(staged, path); !put)
                return std::unexpected(put.error());
            return {};
        },
        [release](std::expected<void, themezer::Error> result) {
            if (!result)
            {
                showError("Couldn't update NXThemes Installer", result.error());
                return;
            }
            updates::done(updates::Program::Installer);
            ui::toast("NXThemes Installer " + release.version + " is installed", ui::ToastKind::Success);
        }));
}
} // namespace

void openUpdates()
{
    std::vector<ui::Action> actions;
    if (const updates::Release* app = updates::available(updates::Program::App))
        actions.push_back({Icon::Refresh, "Themezer-NX " + app->version,
                           "You have " + app->current + ". Themezer-NX restarts into it.", true, false,
                           [] { updateApp(); }});
    if (const updates::Release* installer = updates::available(updates::Program::Installer))
        actions.push_back({Icon::Refresh, "NXThemes Installer " + installer->version,
                           "You have " + installer->current + ".", true, false, [] { updateInstaller(); }});
    // the items on the SD card that Themezer has newer: all of them, then each
    const auto& items = itemupdates::found();
    if (!items.empty())
    {
        actions.push_back({Icon::Download, "Update all (" + std::to_string(items.size()) + ")",
                           "Each is downloaded again and takes the place of the old file.", true, false,
                           [] { updateItems(itemupdates::found()); }});
        for (const Item& item : items)
            actions.push_back({iconOf(item.kind), item.name, itemupdates::describe(item), true, false,
                               [item] { updateItems({item}); }});
    }
    if (actions.empty())
    {
        ui::toast("Everything is up to date");
        return;
    }
    scenes().emplace<ui::ActionSheet>("Updates", std::move(actions), color::kPrimary, 0);
}

void updateItems(const std::vector<Item>& items)
{
    // a check that's still running would find what's updated now as old as it was
    if (itemupdates::checking())
    {
        ui::toast("Still checking for updates. Try again in a moment.", ui::ToastKind::Warning);
        return;
    }
    auto plans = itemupdates::plan(items);
    if (plans.empty())
    {
        ui::toast("That isn't on the SD card any more", ui::ToastKind::Warning);
        return;
    }
    runUpdates(std::move(plans), items.size(), std::any_of(items.begin(), items.end(), installedCopy));
}

std::optional<ui::Action> updateAction(themezer::Kind kind, const std::string& id)
{
    const Item* newer = itemupdates::find(kind, id);
    if (!newer)
        return std::nullopt;
    return ui::Action{Icon::Download, "Update", itemupdates::describe(*newer), true, false,
                      [item = *newer] { updateItems({item}); }};
}

void checkForUpdates()
{
    if (!local::Library::get().scanned())
    {
        ui::toast("Still reading the SD card");
        return;
    }
    if (itemupdates::checking())
    {
        ui::toast("Already checking");
        return;
    }
    ui::toast("Checking the SD card's items for updates");
    itemupdates::check([](const std::expected<size_t, themezer::Error>& result) {
        if (!result)
        {
            if (result.error().kind != themezer::ErrorKind::Cancelled)
                showError("Couldn't check for updates", result.error());
            return;
        }
        if (*result == 0)
            ui::toast("Everything on the SD card is up to date");
        else
            openUpdates();
    });
}

void openMenu()
{
    const int queued = installs::queuedCount();
    std::vector<ui::Action> actions;
    if (queued > 0)
        actions.push_back(
            {Icon::Queue, "Queued installs (" + std::to_string(queued) + ")", "", true, false, [] { openQueue(); }});
    if (const int newer = updates::availableCount() + static_cast<int>(itemupdates::count()); newer > 0)
        actions.push_back(
            {Icon::Refresh, "Updates (" + std::to_string(newer) + ")", "", true, false, [] { openUpdates(); }});
    actions.push_back({Icon::Search, "Check themes for updates", "", true, false, [] { checkForUpdates(); }});
    actions.push_back({Icon::Settings, "Settings", "", true, false, [] { openSettings(); }});
    actions.push_back({Icon::Info, "About and licenses", "", true, false, [] { openAbout(); }});
    actions.push_back({Icon::Power,
                       queued > 0 && canInstall() ? "Exit and install (" + std::to_string(queued) + ")" : "Exit", "",
                       true, false, exitApp, nullptr, ui::key::kPlus});
    scenes().emplace<ui::ActionSheet>("Themezer", std::move(actions), color::kPrimary);
}

#ifdef TZ_DESKTOP
void testLocalFile(const std::string& path)
{
    library::Entry entry;
    entry.path = path;
    entry.fileName = path.substr(path.rfind('/') + 1);
    entry.name = entry.fileName;
    if (path.ends_with(".bmp")) entry.kind = library::Kind::Splash;
    log::info("install: %s", path.c_str());
    if (validateLocal(entry)) queueAndSay(localItem(entry), path);
}

void testDownload(const std::string& url)
{
    jobs::run(jobs::Lane::Network, [url] {
        net::Progress progress;
        auto result = themezer::download(url, "/Themes/ThemezerNX/local-test.nxtheme", CancelToken{}, &progress);
        jobs::post([result] { if (!result) showError("Couldn't download it", result.error()); });
    });
}

void testError(bool network)
{
    showError("Couldn't load the theme",
              {network ? "Couldn't reach Themezer. Check the console's internet connection and try again."
                       : "The theme file couldn't be understood.",
               network ? "Connection refused" : "/Themes/ThemezerNX/example.nxtheme",
               network ? themezer::ErrorKind::Network : themezer::ErrorKind::InvalidData});
}
#endif
} // namespace tz::scenes
