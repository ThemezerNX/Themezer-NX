#include "scenes/actions.hpp"

#include "app/app.hpp"
#include "domain/installed.hpp"
#include "domain/installs.hpp"
#include "scenes/local_details.hpp"
#include "scenes/local_pack.hpp"
#include "services/local.hpp"
#include "scenes/details.hpp"
#include "scenes/frame.hpp"
#include "scenes/pack.hpp"
#include "sys/audio.hpp"
#include "ui/overlays.hpp"

#include <switch.h>

#include <cstring>
#include <unistd.h>
#include <map>
#include <memory>
#include <string>
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

void showError(const std::string& title, const themezer::Error& error)
{
    if (error.cancelled)
    {
        ui::toast("Cancelled");
        return;
    }
    scenes().emplace<ui::Dialog>(Icon::Error, color::kDanger, title, error.message);
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

struct PackResult
{
    std::vector<std::pair<int, std::string>> done; // the theme's index and where it went
    int failed = 0;
    bool cancelled = false;
    themezer::Error firstError;
};

// Downloads the themes of the pack at these indexes (where they aren't on the SD card yet), one after another.
void runPackDownloads(const Item& pack, std::vector<int> chosen, bool queue, bool save)
{
    auto themes = std::make_shared<std::vector<Item>>(pack.themes);
    auto overlay = ui::ProgressOverlay::start<PackResult>(
        {queue ? "Installing " + pack.name : "Downloading " + pack.name, true, accentOf(themezer::Kind::Packs).solid},
        [themes, chosen, save](ui::TaskProgress& progress, const CancelToken& token) {
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
                const std::string path = save ? installs::savedPath(theme) : installs::temporaryPath(theme);
                if (!installs::exists(path))
                {
                    auto done = themezer::download(theme.downloadUrl, path, token, &progress.download);
                    if (!done)
                    {
                        if (done.error().cancelled)
                        {
                            result.cancelled = true;
                            break;
                        }
                        if (result.failed++ == 0)
                            result.firstError = done.error();
                        continue;
                    }
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
                scenes().emplace<ui::Dialog>(
                    Icon::Warning, color::kWarning,
                    std::to_string(result.failed) + (result.failed == 1 ? " theme" : " themes") + " didn't download",
                    result.firstError.message);
                return;
            }
            const size_t done = result.done.size();
            ui::toast(queue ? queuedMessage(done)
                            : std::to_string(done) + (done == 1 ? " theme" : " themes") + " saved in /Themes/ThemezerNX",
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
    App::get().quit();
}

// NXThemes Installer can't be given a file with a comma or "(_)" in its path; says so.
bool installerTakes(const std::string& path)
{
    if (installs::installerTakes(path))
        return true;
    ui::toast("NXThemes Installer can't take a file with a comma or “(_)” in its name or folder", ui::ToastKind::Error);
    return false;
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

// Whether the file is a homebrew app: an .nro has "NRO0" 16 bytes in, where an error page wouldn't.
bool isNro(const char* path)
{
    char magic[4] = {};
    FILE* file = std::fopen(path, "rb");
    const bool read = file && std::fseek(file, 16, SEEK_SET) == 0 && std::fread(magic, 1, 4, file) == 4;
    if (file)
        std::fclose(file);
    return read && std::memcmp(magic, "NRO0", 4) == 0;
}

void downloadInstaller()
{
    scenes().push(ui::ProgressOverlay::start<std::expected<void, themezer::Error>>(
        {"Downloading NXThemes Installer", true, color::kPrimary},
        [](ui::TaskProgress& progress, const CancelToken& token) -> std::expected<void, themezer::Error> {
            auto done = themezer::download(kInstallerUrl, kInstallerPath, token, &progress.download);
            if (done && !isNro(kInstallerPath))
            {
                unlink(kInstallerPath);
                return std::unexpected(
                    themezer::Error{"What came wasn't NXThemes Installer. Try again later.", {}, false, false});
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

std::optional<std::string> askText(const char* guide, const std::string& initial, size_t maxLength)
{
    SwkbdConfig config;
    if (R_FAILED(swkbdCreate(&config, 0)))
        return std::nullopt;
    std::string out(maxLength + 1, '\0');
    swkbdConfigMakePresetDefault(&config);
    swkbdConfigSetGuideText(&config, guide);
    swkbdConfigSetInitialText(&config, initial.c_str());
    swkbdConfigSetStringLenMax(&config, static_cast<u32>(maxLength));
    const Result result = swkbdShow(&config, out.data(), out.size());
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
    // a theme whose file name doesn't say its part: its info.json does
    if (known.kind == library::Kind::Theme && known.target < 0 && !known.infoRead)
        library::readInfo(known);
    if (known.kind == library::Kind::Theme && known.target < 0)
    {
        ui::toast("This theme doesn't say which part of the HOME Menu it's for", ui::ToastKind::Error);
        return;
    }
    if (installerTakes(known.path))
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
        if (!installerTakes(theme.path))
            return;
        // a theme whose file name doesn't say its part: its info.json does
        if (theme.target < 0 && !theme.infoRead)
            library::readInfo(theme);
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
        Icon::Trash, color::kWarning, "Delete “" + pack->name + "”?",
        "Its " + std::to_string(count) + (count == 1 ? " theme is" : " themes are") + " taken off the SD card.",
        std::vector<ui::DialogButton>{
            {"Keep it", nullptr},
            {"Delete",
             [dir] {
                 const bool ok = local::Library::get().removePack(dir);
                 // what was queued from it has nothing to install any more
                 for (int slot = 0; slot < installs::kSlotCount; ++slot)
                 {
                     const installs::Queued* queued = installs::queued(slot);
                     if (queued && queued->path.starts_with(dir + "/") && !installs::exists(queued->path))
                         installs::unqueue(slot);
                 }
                 if (!ok)
                 {
                     ui::toast("Couldn't delete all of it from the SD card", ui::ToastKind::Error);
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
        Icon::Trash, color::kWarning, "Delete “" + entry.name + "”?", "It's taken off the SD card.",
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
                     if (!library.remove(i))
                     {
                         ui::toast("Couldn't delete it from the SD card", ui::ToastKind::Error);
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
        Icon::Trash, color::kWarning, "Uninstall " + what + "?",
        splash ? "Hekate's own splash comes back after a restart."
               : "Nintendo's " + installs::slotLabel(slot) + " comes back after a restart.",
        std::vector<ui::DialogButton>{{"Keep it", nullptr},
                                      {"Uninstall",
                                       [slot] {
                                           if (installed::uninstall(slot))
                                               ui::toast("Uninstalled: restart the console to see the change",
                                                         ui::ToastKind::Success);
                                           else
                                               ui::toast("Couldn't delete its files", ui::ToastKind::Error);
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
        ui::toast("The app doesn't know which part of the HOME Menu this is for", ui::ToastKind::Error);
        return;
    }
    const std::string path = save ? installs::savedPath(item) : installs::temporaryPath(item);
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
            local::Library::get().rescan();
            if (result)
                queueAndSay(item, path);
            else
                showError("Couldn't download it", result.error());
        }));
}

void download(const Item& item)
{
    const std::string path = installs::savedPath(item);
    if (installs::exists(path))
    {
        ui::toast("“" + item.name + "” is on the SD card already, in /Themes/ThemezerNX");
        return;
    }
    scenes().push(ui::ProgressOverlay::start<std::expected<void, themezer::Error>>(
        {"Downloading " + item.name, true, accentOf(item.kind).solid},
        [url = item.downloadUrl, path](ui::TaskProgress& progress, const CancelToken& token) {
            return themezer::download(url, path, token, &progress.download);
        },
        [name = item.name](std::expected<void, themezer::Error> result) {
            local::Library::get().rescan();
            if (result)
                ui::toast("“" + name + "” is saved in /Themes/ThemezerNX", ui::ToastKind::Success);
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
            else if (result.error().notFound)
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

void openMenu()
{
    const int queued = installs::queuedCount();
    std::vector<ui::Action> actions;
    if (queued > 0)
        actions.push_back(
            {Icon::Queue, "Queued installs (" + std::to_string(queued) + ")", "", true, false, [] { openQueue(); }});

    actions.push_back({Icon::Settings, "Settings", "", true, false, [] { openSettings(); }});
    actions.push_back({Icon::Info, "About and licenses", "", true, false, [] { openAbout(); }});
    actions.push_back({Icon::Power,
                       queued > 0 && canInstall() ? "Exit and install (" + std::to_string(queued) + ")" : "Exit", "",
                       true, false, exitApp, nullptr, ui::key::kPlus});
    scenes().emplace<ui::ActionSheet>("Themezer", std::move(actions), color::kPrimary);
}

} // namespace tz::scenes
