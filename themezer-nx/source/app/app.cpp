#include "app/app.hpp"

#include "domain/installs.hpp"
#include "gfx/draw.hpp"
#include "gfx/renderer.hpp"
#include "gfx/text.hpp"
#include "domain/installed.hpp"
#include "scenes/actions.hpp"
#include "scenes/home.hpp"
#include "services/local.hpp"
#include "scenes/images.hpp"
#include "sys/audio.hpp"
#include "sys/jobs.hpp"
#include "sys/net.hpp"
#include "ui/overlays.hpp"

#include <SDL2/SDL.h>
#include <switch.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
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

void App::updateStatus(float dt)
{
    statusAge_ += dt;
    if (statusAge_ < 1)
        return;
    statusAge_ = 0;
    const std::time_t now = std::time(nullptr);
    if (const std::tm* local = std::localtime(&now))
    {
        status_.hour = local->tm_hour;
        status_.minute = local->tm_min;
    }
    u32 charge = 100;
    if (R_SUCCEEDED(psmGetBatteryChargePercentage(&charge)))
        status_.battery = static_cast<int>(charge);
    PsmChargerType charger = PsmChargerType_Unconnected;
    if (R_SUCCEEDED(psmGetChargerType(&charger)))
        status_.charging = charger != PsmChargerType_Unconnected;
    NifmInternetConnectionType type;
    u32 strength = 0;
    NifmInternetConnectionStatus connection;
    status_.online = R_SUCCEEDED(nifmGetInternetConnectionStatus(&type, &strength, &connection)) &&
                     connection == NifmInternetConnectionStatus_Connected;
}

int App::run()
{
    romfsInit();
    socketInitializeDefault();
    // started by nxlink: the logs go back to it
    const int nxlink = nxlinkStdio();
    // nothing held back, so a crash doesn't lose the last lines
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("Themezer-NX " APP_VERSION " starting\n");
    nifmInitialize(NifmServiceType_User);
    psmInitialize();
    if (!gfx::init() || !gfx::initText())
    {
        std::printf("Couldn't start the screen: %s\n", SDL_GetError());
        gfx::exit();
        return 1;
    }
    gfx::loadIcons();

    mkdir("/Themes", 0777);
    mkdir("/Themes/ThemezerNX", 0777);
    mkdir("/Themes/ThemezerNX/Splashes", 0777);
    mkdir("/Themes/ThemezerNX/.tmp", 0777);
    installs::removeTemporary();

    config_.load();
    audio::init();
    audio::quietHome();
    applyConfig();
    net::init();
    jobs::init();

    ui::InputReader reader;
    reader.init();
    // what's installed, and what's on the SD card (on a worker), as the files are now
    installed::refresh();
    // which parts have the installer's files, for nxlink's log
    std::string parts;
    for (int slot = 0; slot < installs::kSlotCount; ++slot)
    {
        // and what's in it, when the app installed it
        const installed::Part& part = installed::part(slot);
        if (part.custom)
            parts += (parts.empty() ? "" : ", ") + installs::slotLabel(slot) +
                     (part.name.empty() ? "" : " (" + part.name + ")");
    }
    std::printf("Installed: %s\n", parts.empty() ? "nothing" : parts.c_str());
    local::Library::get().rescan();
    scenes_.emplace<scenes::HomeScene>();
    if (!installs::installerPath())
        scenes::offerInstaller();

    float last = gfx::time();
    while (appletMainLoop() && !quitting_ && !scenes_.empty())
    {
        const float now = gfx::time();
        const float dt = std::clamp(now - last, 0.0f, 0.1f);
        last = now;

        // a frame's own work in its parts, for the log when it's long enough to show as a stutter
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
            std::printf("Slow frame: %.0f ms (posted work %.0f, status %.0f, scenes %.0f, pictures %.0f, draw %.0f)\n",
                        static_cast<double>(drawn - start) * ms, static_cast<double>(drained - start) * ms,
                        static_cast<double>(statused - drained) * ms, static_cast<double>(scened - statused) * ms,
                        static_cast<double>(updated - scened) * ms, static_cast<double>(drawn - updated) * ms);
        gfx::endFrame();
    }

    const char* installer = installs::installerPath();
    if (installer && installs::queuedCount() > 0)
    {
        const std::string args = installs::installerArgs(installer);
        // the menu doesn't leave with more than the loader can pass on (see exitApp); this is for any other way out
        if (args.size() <= installs::kMaxArgs)
        {
            // what goes in, to know at the next start what's installed
            installed::noteQueued();
            std::printf("Starting %s\n", args.c_str());
            envSetNextLoad(installer, args.c_str());
        }
    }

    scenes_.clear();
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
    std::printf("Themezer-NX done\n");
    // Started by nxlink, stdout and stderr are copies of its socket. They go before the sockets do: else the C
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
