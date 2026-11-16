#include "app/app.hpp"

#include "sys/log.hpp"
#include "domain/installs.hpp"
#include "gfx/draw.hpp"
#include "gfx/renderer.hpp"
#include "gfx/text.hpp"
#include "domain/installed.hpp"
#include "scenes/actions.hpp"
#include "scenes/home.hpp"
#include "services/item_updates.hpp"
#include "services/local.hpp"
#include "services/migrate.hpp"
#include "services/updates.hpp"
#include "scenes/images.hpp"
#include "sys/audio.hpp"
#include "sys/jobs.hpp"
#include "sys/net.hpp"
#include "sys/session.hpp"
#include "services/reporter.hpp"
#include "ui/overlays.hpp"

#include <SDL2/SDL.h>
#include <switch.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace tz
{

App& App::get()
{
    static App app;
    return app;
}

void App::applyConfig()
{
    audio::setMusic(config_.music);
    sfx::setEnabled(config_.sfx);
}

namespace
{
// The time, the battery and Wi-Fi: system calls that take up to tens of ms on the Switch.
App::Status readStatus()
{
    App::Status status;
    const std::time_t now = std::time(nullptr);
    if (const std::tm* local = std::localtime(&now))
    {
        status.hour = local->tm_hour;
        status.minute = local->tm_min;
    }
    u32 charge = 100;
    if (R_SUCCEEDED(psmGetBatteryChargePercentage(&charge)))
        status.battery = static_cast<int>(charge);
    PsmChargerType charger = PsmChargerType_Unconnected;
    if (R_SUCCEEDED(psmGetChargerType(&charger)))
        status.charging = charger != PsmChargerType_Unconnected;
    NifmInternetConnectionType type;
    u32 strength = 0;
    NifmInternetConnectionStatus connection;
    status.online = R_SUCCEEDED(nifmGetInternetConnectionStatus(&type, &strength, &connection)) &&
                    connection == NifmInternetConnectionStatus_Connected;
    return status;
}
} // namespace

void App::updateStatus(float dt)
{
    // once a second, on a worker: on the main thread the calls stuttered (28 ms logged on the Switch)
    statusAge_ += dt;
    if (statusAge_ < 1 || statusReading_)
        return;
    statusAge_ = 0;
    statusReading_ = true;
    jobs::run(jobs::Lane::Images, [] {
        const Status status = readStatus();
        jobs::post([status] {
            App& app = App::get();
            app.status_ = status;
            app.statusReading_ = false;
        });
    });
}

int App::run(const char* self)
{
    // hbmenu and nxlink start it as "sdmc:/switch/themezer-nx.nro"; hbmenu's own place for it otherwise
    selfPath_ = self && std::strncmp(self, "sdmc:/", 6) == 0 ? std::string(self + 5) : "/switch/themezer-nx.nro";
    romfsInit();
    socketInitializeDefault();
    const int nxlink = nxlinkStdio();
    // unbuffered, so a crash keeps the last lines
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    log::init();
    log::info("Themezer-NX " APP_VERSION " starting");
    nifmInitialize(NifmServiceType_User);
    psmInitialize();
    status_ = readStatus();
    if (!gfx::init() || !gfx::initText())
    {
        log::error("Couldn't start the screen: %s", SDL_GetError());
        gfx::exit();
        log::exit();
        return 1;
    }
    gfx::loadIcons();

    mkdir("/Themes", 0777);
    mkdir("/Themes/ThemezerNX", 0777);
    mkdir("/Themes/ThemezerNX/Splashes", 0777);
    mkdir("/switch", 0777);
    mkdir("/switch/.themezer-nx", 0777);
    installs::removeTemporary();

    auto lastSession = session::takeLastSession();
    session::begin();
    config_.load();
    audio::init();
    audio::quietHome();
    applyConfig();
    net::init();
    jobs::init();
    // drop a staged update left by an unclean exit, then check for newer releases
    unlink(updatePath().c_str());
    updates::check([] {
        const updates::Release* app = updates::available(updates::Program::App);
        const updates::Release* installer = updates::available(updates::Program::Installer);
        ui::toast(app && installer ? std::string("2 updates available")
                  : app            ? "Update available: Themezer-NX " + app->version
                                   : "Update available: NXThemes Installer " + installer->version);
    });

    ui::InputReader reader;
    reader.init();
    installed::refresh();
    std::string parts;
    for (int slot = 0; slot < installs::kSlotCount; ++slot)
    {
        const installed::Part& part = installed::part(slot);
        if (part.custom)
            parts += (parts.empty() ? "" : ", ") + installs::slotLabel(slot) +
                     (part.name.empty() ? "" : " (" + part.name + ")");
    }
    log::info("Installed: %s", parts.empty() ? "nothing" : parts.c_str());
    // after the scan: run pending migrations (recorded in settings), then toast what Themezer has newer
    local::Library::get().rescan([] {
        migrate::run(
            App::get().config().migrations,
            [](const char* id) {
                log::info("Migrations: %s done", id);
                App::get().config().migrations.insert(id);
                App::get().config().save();
            },
            [] {
                if (!App::get().config().updateCheck)
                    return;
                itemupdates::check([](const std::expected<size_t, themezer::Error>& found) {
                    if (!found)
                        log::warn("Item updates: %s (%s)", found.error().message.c_str(), found.error().detail.c_str());
                    else if (*found > 0)
                        ui::toast(*found == 1 ? std::string("1 item on the SD card has an update")
                                              : std::to_string(*found) + " items on the SD card have updates");
                });
            });
    });
    scenes_.emplace<scenes::HomeScene>();
    if (!installs::installerPath())
        scenes::offerInstaller();

    if (lastSession)
    {
        const auto report = std::move(*lastSession);
        scenes_.emplace<ui::Dialog>(gfx::Icon::Warning, gfx::color::kWarning, report.message,
                                    "Do you want to share with the developers what happened last time?",
            std::vector<ui::DialogButton>{
                {"Not now", nullptr},
                {"Share", [report] { report::shareLastSession(report); }}});
    }
    float last = gfx::time();
    while (appletMainLoop() && !quitting_ && !scenes_.empty())
    {
        const float now = gfx::time();
        const float dt = std::clamp(now - last, 0.0f, 0.1f);
        last = now;
        session::frame();

        // per-phase timings, logged when a frame is slow enough to stutter
        const Uint64 start = SDL_GetPerformanceCounter();
        jobs::drain();
        const Uint64 drained = SDL_GetPerformanceCounter();
        updateStatus(dt);
        const Uint64 statused = SDL_GetPerformanceCounter();
        ui::Input input = reader.read(dt);
        scenes_.update(input, dt);
        ui::updateToasts(dt);
        const Uint64 scened = SDL_GetPerformanceCounter();
        scenes::images().tick();
        const Uint64 updated = SDL_GetPerformanceCounter();

        gfx::beginFrame();
        gfx::beginTextFrame();
        scenes_.draw();
        ui::drawToasts();
        const Uint64 drawn = SDL_GetPerformanceCounter();
        if (const double ms = 1000.0 / static_cast<double>(SDL_GetPerformanceFrequency());
            !scenes::tookKeyboard() && static_cast<double>(drawn - start) * ms > 50)
            log::info("Slow frame: %.0f ms (posted work %.0f, status %.0f, scenes %.0f, pictures %.0f, draw %.0f)",
                        static_cast<double>(drawn - start) * ms, static_cast<double>(drained - start) * ms,
                        static_cast<double>(statused - drained) * ms, static_cast<double>(scened - statused) * ms,
                        static_cast<double>(updated - scened) * ms, static_cast<double>(drawn - updated) * ms);
        gfx::endFrame();
    }

    // Intentional shutdown may wait for workers or an installer handoff; that wait is not a foreground freeze.
    session::holdWatch(true);
    const char* installer = installs::installerPath();
    if (installer && installs::queuedCount() > 0)
    {
        const std::string args = installs::installerArgs(installer);
        // the menu exit already respects the loader's limit (see exitApp)
        if (args.size() <= installs::kMaxArgs)
        {
            installed::noteQueued();
            log::info("Starting %s", args.c_str());
            if (const Result result = envSetNextLoad(installer, args.c_str()); R_FAILED(result))
                log::error("NXThemes Installer hand-off failed: 0x%08x (%s)", result, installer);
            restartAfterUpdate_ = false;
        }
    }
    if (updateStaged_ && restartAfterUpdate_)
    {
        const std::string arg = "sdmc:" + selfPath_;
        envSetNextLoad(selfPath_.c_str(), (arg.find(' ') != std::string::npos ? "\"" + arg + "\"" : arg).c_str());
    }

    scenes_.clear();
    itemupdates::cancel();
    jobs::exit();
    scenes::images().clear();
    net::exit();
    audio::restoreHome();
    audio::exit();
    gfx::freeIcons();
    gfx::exitText();
    gfx::exit();
    psmExit();
    nifmExit();
    romfsExit();
    // romfs reads from the .nro, so it's replaced only after romfsExit
    if (updateStaged_)
        log::info("Update %s", updates::replace(updatePath(), selfPath_) ? "in place" : "couldn't go in place");
    log::info("Themezer-NX done");
    session::end();
    log::exit();
    // library's closing of stdout at exit closes a socket after socketExit, which crashed the app, and with the
    // sockets left open the bsdsocket service, taking the console with it.
    if (nxlink >= 0)
    {
        std::fflush(stdout);
        std::fflush(stderr);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
        close(nxlink);
    }
    socketExit();
    return 0;
}

} // namespace tz
