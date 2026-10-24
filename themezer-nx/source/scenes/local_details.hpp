// A theme's or a splash's page for its file on the SD card: its picture large, what its file says, whether it's
// installed or queued, and queueing it, deleting it, or opening it on Themezer.

#pragma once

#include "scenes/frame.hpp"
#include "ui/scene.hpp"
#include "ui/widgets.hpp"

#include <string>
#include <vector>

namespace tz::scenes
{

class LocalDetailsScene final : public ui::Scene
{
public:
    explicit LocalDetailsScene(std::string path);
    void update(ui::Input& input, float dt) override;
    void tick(float dt) override;
    void draw() override;
    bool acceptsInput() const override { return !leaving_; }

private:
    const library::Entry* entry() const;
    // Its slot when it's what's queued there, else -1.
    int queuedSlot() const;
    std::vector<ui::Hint> hints() const;
    void openMore();
    void leave();

    std::string path_;
    library::Entry last_; // what it was, drawn while the page goes after the file has
    ui::HintBar hintBar_;
    Backdrop backdrop_;
    float appear_ = 0;
    bool leaving_ = false;
};

} // namespace tz::scenes
