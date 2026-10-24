// The app: its scenes, its settings, what the console's bar says, and the frame loop.

#pragma once

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

    int run();
    ui::SceneStack& scenes() { return scenes_; }
    const Status& status() const { return status_; }
    Config& config() { return config_; }
    void applyConfig();
    // Leaves at the end of the frame, starting NXThemes Installer when installs are queued.
    void quit() { quitting_ = true; }

private:
    App() = default;
    void updateStatus(float dt);

    ui::SceneStack scenes_;
    Status status_;
    float statusAge_ = 0;
    bool statusReading_ = false; // on a worker
    Config config_;
    bool quitting_ = false;
};

} // namespace tz
