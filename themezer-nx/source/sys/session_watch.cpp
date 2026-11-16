#include "sys/session.hpp"

#include "sys/files.hpp"
#include "sys/log.hpp"
#include "services/updates.hpp"
#include <switch.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <ctime>
#include <dirent.h>
#include <mutex>
#include <thread>
#include <cstdlib>
#include <sys/stat.h>
#include <unistd.h>

namespace tz::session
{
namespace
{
constexpr const char* kPath = "/Themes/ThemezerNX/session.txt";
constexpr const char* kLast = "/Themes/ThemezerNX/last-session.txt";
constexpr const char* kCrashes = "/atmosphere/crash_reports";
Snapshot base;
std::atomic<uint64_t> lastFrame{0};
std::atomic<bool> foreground{true}, stopping{false}, active{false};
std::atomic<int> held{0};
std::atomic<bool> dirty{true};
std::mutex writeMutex, wakeMutex;
std::condition_variable wake;
Thread writer;
bool started = false, available = true, exitLocked = false;
AppletHookCookie hook;

uint64_t nowMs()
{
#ifdef TZ_DESKTOP
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
#else
    return armTicksToNs(armGetSystemTick()) / 1000000; // independent of clock corrections and time-service availability
#endif
}

std::optional<std::string> read(const std::string& path, size_t limit = 1024 * 1024)
{
    FILE* file = std::fopen(path.c_str(), "rb");
    if (!file)
        return std::nullopt;
    std::string text;
    char block[4096];
    size_t count;
    while ((count = std::fread(block, 1, sizeof(block), file)) != 0 && text.size() + count <= limit)
        text.append(block, count);
    const bool valid = std::feof(file) && !std::ferror(file);
    std::fclose(file);
    return valid ? std::optional<std::string>(std::move(text)) : std::nullopt;
}

void removeJournal()
{
    unlink(kPath);
    unlink((std::string(kPath) + ".old").c_str());
    unlink((std::string(kPath) + ".part").c_str());
}

void snapshot()
{
    std::lock_guard lock(writeMutex);
    if (!active)
        return;
    Snapshot next = base;
    const auto memory = log::memory();
    next.report.memoryUsed = memory.used;
    next.report.memoryAvailable = memory.available;
    next.report.log = log::tail();
    next.foreground = foreground;
    const auto last = lastFrame.load();
    const auto now = nowMs();
    next.stalledMs = foreground && held == 0 && last && now >= last ? now - last : 0;
    const std::string text = toText(next);
    // The writer is the sole owner of this path, including its sibling temporary and backup.
    (void)files::write(kPath, {reinterpret_cast<const uint8_t*>(text.data()), text.size()});
}

void onApplet(AppletHookType type, void*)
{
    if (type == AppletHookType_OnFocusState)
    {
        foreground = appletGetFocusState() == AppletFocusState_InFocus;
        lastFrame = nowMs();
        dirty = true;
        wake.notify_one();
    }
    else if (type == AppletHookType_OnResume)
        lastFrame = nowMs();
    else if (type == AppletHookType_OnExitRequest)
    {
        // A normal HOME close/restart request is not a crash, even if later cleanup times out.
        active = false;
        std::lock_guard lock(writeMutex);
        removeJournal();
    }
}

void changed()
{
    dirty = true;
    wake.notify_one();
}

void run(void*)
{
    uint64_t previous = nowMs(), nextWrite = 0;
    while (!stopping)
    {
        const auto now = nowMs();
        // The whole process may have been suspended by HOME/sleep. That gap is not a foreground freeze.
        if (now - previous > 2000)
            lastFrame = now;
        previous = now;
        try
        {
            if (dirty.exchange(false) || now >= nextWrite)
            {
                snapshot();
                nextWrite = now + 1000;
            }
        }
        catch (...)
        {
            active = false; // diagnostics must not bring down the app if the heap or SD card fails
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::unique_lock lock(wakeMutex);
        wake.wait_for(lock, std::chrono::milliseconds(250));
    }
}
}

std::optional<report::Report> takeLastSession()
{
    std::string source = kPath;
    auto text = read(source);
    if (!text)
    {
        source += ".old"; // a power cut between the two FAT renames leaves the previous complete snapshot here
        text = read(source);
    }
    if (!text)
        return std::nullopt;
    // Consume before asking: a crash in the prompt must not ask for the same session twice.
    if (!updates::replace(source, kLast))
    {
        available = false; // don't overwrite evidence that could not be archived
        log::warn("Couldn't keep the previous session; session tracking is disabled for this run");
        return std::nullopt;
    }
    removeJournal();
    auto previous = parse(*text);
    if (!previous)
        return std::nullopt;
    std::optional<Crash> matching;
    if (DIR* directory = opendir(kCrashes))
    {
        while (dirent* entry = readdir(directory))
        {
            const std::string name = entry->d_name;
            if (auto header = crashReport(name, "Atmosphère Crash Report"); header &&
                previous->started != 0 && previous->programId != 0 && header->programId == previous->programId &&
                header->timestamp >= previous->started && (!matching || header->timestamp > matching->timestamp))
            {
                if (auto body = read(std::string(kCrashes) + "/" + name, 256 * 1024))
                    if (auto decoded = crashReport(name, *body))
                        matching = std::move(decoded);
            }
        }
        closedir(directory);
    }
    const auto how = ending(*previous, matching);
#ifndef TZ_DESKTOP
    // A missing exit marker alone cannot distinguish forced termination or power loss. Don't blame the app for it.
    if (how.kind == "unclean exit")
    {
        log::info("Previous session had no crash, freeze or fatal evidence; not asking after a possible power off");
        return std::nullopt;
    }
#endif
    log::warn("Previous session: %s (%s)", how.kind.c_str(), how.detail.c_str());
    auto report = std::move(previous->report);
    report.kindText = how.kind;
    report.message = how.kind == "crash"         ? "Themezer-NX crashed"
                     : how.kind == "freeze"      ? "Themezer-NX froze"
                     : how.kind == "fatal error" ? "Themezer-NX stopped on an error"
                                                 : "Themezer-NX closed unexpectedly";
    report.detail = how.detail;
    return report;
}

void begin()
{
    if (!available)
        return;
    base = Snapshot{};
    base.report.version = APP_VERSION;
    const auto type = appletGetAppletType();
    base.report.launch = type == AppletType_Application || type == AppletType_SystemApplication ? "application" : "applet";
    svcGetInfo(&base.programId, InfoType_ProgramId, CUR_PROCESS_HANDLE, 0);
#ifdef TZ_DESKTOP
    base.started = std::time(nullptr);
#else
    // creport uses LocalSystemClock, which need not be the clock cached by newlib's time().
    if (R_SUCCEEDED(timeInitialize()))
    {
        timeGetCurrentTime(TimeType_LocalSystemClock, &base.started);
        timeExit();
    }
#endif
    lastFrame = nowMs();
    foreground = appletGetFocusState() == AppletFocusState_InFocus;
    stopping = false;
    active = true;
    snapshot();
    // Higher priority than the main loop: a CPU spin on an applet's single core must not starve the journal.
    if (R_FAILED(threadCreate(&writer, run, nullptr, nullptr, 0x20000, 0x2b, -2)))
    {
        active = false;
        removeJournal();
        log::warn("Couldn't start the session writer");
        return;
    }
    if (R_FAILED(threadStart(&writer)))
    {
        threadClose(&writer);
        active = false;
        removeJournal();
        log::warn("Couldn't start the session writer");
        return;
    }
    started = true;
    exitLocked = R_SUCCEEDED(appletLockExit());
    appletHook(&hook, onApplet, nullptr);
    log::setListener(changed);
}

void frame() { lastFrame = nowMs(); }

void holdWatch(bool hold)
{
    held += hold ? 1 : -1;
    lastFrame = nowMs();
}

void saveNow() { snapshot(); }

void saveFatal() noexcept
{
    try
    {
        // terminate may have been called by the writer itself, or while a logger owns its mutex.
        std::unique_lock lock(writeMutex, std::try_to_lock);
        if (!lock.owns_lock() || !active)
            return;
        active = false; // the periodic writer must not overwrite the fatal marker before abort
        Snapshot next = base;
        const auto memory = log::memory();
        next.report.memoryUsed = memory.used;
        next.report.memoryAvailable = memory.available;
        next.report.log = log::tailNow();
        if (next.report.log.size() == 200)
            next.report.log.erase(next.report.log.begin());
        char clock[16] = "00:00:00";
        const auto now = std::time(nullptr);
        std::tm local{};
        if (localtime_r(&now, &local))
            std::strftime(clock, sizeof(clock), "%H:%M:%S", &local);
        char line[160];
        std::snprintf(line, sizeof(line), "%s.000 E the app stopped on a fatal error (%llu used, %llu available)",
                      clock, static_cast<unsigned long long>(memory.used), static_cast<unsigned long long>(memory.available));
        next.report.log.emplace_back(line);
        const auto text = toText(next);
        (void)files::write(kPath, {reinterpret_cast<const uint8_t*>(text.data()), text.size()});
    }
    catch (...) {} // an exhausted heap must still reach abort and Atmosphere's crash reporter
}

void end()
{
    if (!started)
        return;
    log::setListener(nullptr);
    stopping = true;
    wake.notify_one();
    threadWaitForExit(&writer);
    threadClose(&writer);
    appletUnhook(&hook);
    active = false;
    removeJournal();
    started = false;
    if (exitLocked)
        appletUnlockExit();
    exitLocked = false;
}

#ifdef TZ_DESKTOP
void simulate(const std::string& kind)
{
    log::info("desktop: simulated %s", kind.c_str());
    if (kind == "freeze")
    {
        // Stop the real heartbeat and let the independent writer observe a foreground stall.
        std::this_thread::sleep_for(std::chrono::seconds(11));
        std::_Exit(0); // only the independent writer can have recorded the freeze
    }
    saveNow();
    if (kind == "crash")
    {
        mkdir("/atmosphere", 0777);
        mkdir(kCrashes, 0777);
        char name[96];
        std::snprintf(name, sizeof(name), "%s/%011llu_%016llx.log", kCrashes,
                      static_cast<unsigned long long>(std::time(nullptr)), static_cast<unsigned long long>(base.programId));
        const std::string crash = "Atmosphère Crash Report (v1.7):\nException Info:\n    Type: Data Abort\n"
                                  "    Fault Address: 0x000000000000000c\nCrashed Thread Info:\n"
                                  "    PC: 0x12345678 (themezer-nx + 0x1234)\n    LR: 0x12345000 (themezer-nx + 0xabc)\n"
                                  "    SP: 0x70000000\n";
        (void)files::write(name, {reinterpret_cast<const uint8_t*>(crash.data()), crash.size()});
    }
    std::_Exit(0); // no destructors and no clean-session removal, as with a killed process
}
#endif
}
