#include "scenes/actions.hpp"

#include "app/app.hpp"
#include "domain/installs.hpp"
#include "scenes/details.hpp"
#include "scenes/frame.hpp"
#include "scenes/pack.hpp"
#include "sys/audio.hpp"
#include "ui/overlays.hpp"

#include <switch.h>

#include <cstring>
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

// The text typed on the Switch's keyboard, or nothing when it was cancelled or left empty.
std::string keyboard(const char* guide, const std::string& initial, size_t maxLength)
{
    SwkbdConfig config;
    if (R_FAILED(swkbdCreate(&config, 0)))
        return {};
    std::string out(maxLength + 1, '\0');
    swkbdConfigMakePresetDefault(&config);
    swkbdConfigSetGuideText(&config, guide);
    swkbdConfigSetInitialText(&config, initial.c_str());
    swkbdConfigSetStringLenMax(&config, static_cast<u32>(maxLength));
    const Result result = swkbdShow(&config, out.data(), out.size());
    swkbdClose(&config);
    if (R_FAILED(result))
        return {};
    out.resize(std::strlen(out.c_str()));
    return out;
}

std::string trim(std::string text)
{
    while (!text.empty() && text.back() == ' ')
        text.pop_back();
    while (!text.empty() && text.front() == ' ')
        text.erase(text.begin());
    return text;
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
            ui::toast(queue ? std::to_string(result.done.size()) + " themes queued. Exit through + to install them."
                            : std::to_string(result.done.size()) + " themes saved in /Themes/ThemezerNX",
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

} // namespace

bool canInstall()
{
    return installs::installerPath() != nullptr;
}

void openItem(const Item& item)
{
    if (item.kind == themezer::Kind::Packs)
        scenes().emplace<PackScene>(item);
    else
        scenes().emplace<DetailsScene>(item);
}

void install(const Item& item, bool save)
{
    if (!canInstall())
    {
        scenes().emplace<ui::Dialog>(Icon::Warning, color::kWarning, "NXThemes Installer isn't there",
                                     "Installing needs NXThemes Installer at sd:/switch/NXThemesInstaller.nro. "
                                     "You can still download it.");
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
        install(pack.themes.empty() ? pack : pack.themes.front(), save); // says why it can't
        return;
    }
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
    auto shared = std::make_shared<Item>(pack);
    auto chosen = std::make_shared<std::vector<int>>();
    choosePackThemes(shared, byTarget, chosen, byTarget->begin(), [shared, chosen, save] {
        int replaced = 0;
        for (int index : *chosen)
            replaced += installs::queued(installs::slotOf(shared->themes[static_cast<size_t>(index)])) ? 1 : 0;
        if (replaced == 0)
        {
            runPackDownloads(*shared, *chosen, true, save);
            return;
        }
        scenes().emplace<ui::Dialog>(
            Icon::Queue, color::kWarning, "Replace queued installs?",
            "This replaces " + std::to_string(replaced) + (replaced == 1 ? " install" : " installs") +
                " already queued.",
            std::vector<ui::DialogButton>{{"Keep them", nullptr},
                                          {"Replace", [shared, chosen, save] {
                                               runPackDownloads(*shared, *chosen, true, save);
                                           }}});
    });
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

void openQueue()
{
    std::vector<ui::Action> actions;
    actions.push_back({Icon::Power, "Exit and install (" + std::to_string(installs::queuedCount()) + ")",
                       "Starts NXThemes Installer with what's queued", canInstall(), false, [] { App::get().quit(); },
                       nullptr, ui::key::kPlus});
    for (int slot = 0; slot < installs::kSlotCount; ++slot)
    {
        const installs::Queued* queued = installs::queued(slot);
        if (!queued)
            continue;
        const std::string label = installs::slotLabel(slot) + ": " + queued->name;
        actions.push_back({Icon::Trash, label, "Takes it out of the queue", true, false, [slot, label] {
                               scenes().emplace<ui::Dialog>(
                                   Icon::Trash, color::kWarning, "Take it out of the queue?", label,
                                   std::vector<ui::DialogButton>{{"Keep it", nullptr},
                                                                 {"Take out",
                                                                  [slot] {
                                                                      installs::unqueue(slot);
                                                                      sfx::play(sfx::Sound::Back);
                                                                  },
                                                                  true}});
                           }});
    }
    if (actions.size() == 1)
    {
        ui::toast("Nothing is queued yet. Install a theme or a splash to queue it.");
        return;
    }
    // on the first install, not on leaving: a quick A doesn't exit
    scenes().emplace<ui::ActionSheet>("Queued installs", std::move(actions), color::kPrimary, 1);
}

void openSettings()
{
    // each a box ticked while it's on
    const auto box = [](bool on) { return on ? Icon::SquareCheck : Icon::Square; };
    std::vector<ui::Action> actions;
    ui::Action music{box(App::get().config().music), "Music", "The music that plays in the background"};
    music.toggle = [box](ui::Action& action) {
        Config& config = App::get().config();
        config.music = !config.music;
        config.save();
        App::get().applyConfig();
        action.icon = box(config.music);
    };
    ui::Action sounds{box(App::get().config().sfx), "Sound effects", "The sounds of moving round and choosing"};
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
    const std::string quickId = trim(keyboard("Quick ID (find it on themezer.net)", "", 64));
    if (quickId.empty())
        return;
    scenes().push(ui::ProgressOverlay::start<std::expected<Item, themezer::Error>>(
        {"Looking up " + quickId, true, color::kPrimary},
        [quickId](ui::TaskProgress&, const CancelToken& token) { return themezer::lookupQuickId(quickId, token); },
        [](std::expected<Item, themezer::Error> result) {
            if (result)
                openItem(*result);
            else if (result.error().notFound)
                scenes().emplace<ui::Dialog>(Icon::SearchEmpty, color::kTextDim, "Nothing found",
                                             result.error().message);
            else
                showError("Couldn't look it up", result.error());
        }));
}

void openMenu()
{
    const int queued = installs::queuedCount();
    std::vector<ui::Action> actions;
    actions.push_back({Icon::Keyboard, "Install by Quick ID", "Type the ID themezer.net shows for an item", true,
                       false, [] { openQuickId(); }});
    actions.push_back({Icon::Queue, "Queued installs (" + std::to_string(queued) + ")",
                       "What NXThemes Installer installs when you exit", true, false, [] { openQueue(); }, nullptr,
                       ui::key::kMinus});
    actions.push_back({Icon::Settings, "Settings", "Music and sound effects", true, false, [] { openSettings(); }});
    actions.push_back({Icon::Power, queued > 0 && canInstall() ? "Exit and install (" + std::to_string(queued) + ")"
                                                                : "Exit",
                       queued > 0 && canInstall() ? "Starts NXThemes Installer with the queued installs"
                                                  : "Back to the HOME Menu",
                       true, false, [] { App::get().quit(); }, nullptr, ui::key::kPlus});
    scenes().emplace<ui::ActionSheet>("Themezer", std::move(actions), color::kPrimary);
}

} // namespace tz::scenes
