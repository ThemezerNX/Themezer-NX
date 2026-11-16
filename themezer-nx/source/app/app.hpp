// The app: its scenes, its settings, what the console's bar says, and the frame loop.

#pragma once

#include <string>

#include "app/config.hpp"
#include "ui/scene.hpp"

namespace tz
{

class App
{
public:
    static App& get();

    struct Status
    {
        int hour = 0;
        int minute = 0;
        int battery = 100; // percent
        bool charging = false;
        bool online = true;
    };

    // self: the .nro it was started from (argv[0]), for updating it.
    int run(const char* self);
    ui::SceneStack& scenes() { return scenes_; }
    const Status& status() const { return status_; }
    Config& config() { return config_; }
    void applyConfig();
    // Leaves at the end of the frame, starting NXThemes Installer when installs are queued.
    void quit() { quitting_ = true; }

    // The .nro it runs from, and where a new one goes until the app closes: it can't be replaced while it runs.
    const std::string& selfPath() const { return selfPath_; }
    std::string updatePath() const { return selfPath_ + ".new"; }
    // A new one is at updatePath(): it replaces the .nro on exit; with restart the app relaunches into it
    // (unless leaving for NXThemes Installer with installs queued).
    void updateOnExit(bool restart)
    {
        updateStaged_ = true;
        restartAfterUpdate_ = restart;
    }

private:
    App() = default;
    void updateStatus(float dt);

    ui::SceneStack scenes_;
    Status status_;
    float statusAge_ = 0;
    bool statusReading_ = false; // on a worker
    Config config_;
    bool quitting_ = false;
    std::string selfPath_; // without "sdmc:"
    bool updateStaged_ = false;
    bool restartAfterUpdate_ = false;
};

} // namespace tz
