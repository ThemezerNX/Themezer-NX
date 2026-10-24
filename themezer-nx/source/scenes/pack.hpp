// A pack's page: its collage and what it is beside a grid of its themes, and installing or downloading them all.

#pragma once

#include "scenes/frame.hpp"
#include "services/themezer.hpp"
#include "ui/grid_view.hpp"
#include "ui/scene.hpp"
#include "ui/widgets.hpp"

namespace tz::scenes
{

class PackScene final : public ui::Scene
{
public:
    explicit PackScene(themezer::Item pack);
    void update(ui::Input& input, float dt) override;
    void tick(float dt) override;
    void draw() override;
    bool acceptsInput() const override { return !leaving_; }
    std::string crumb() const override { return trail_.back(); }

private:
    std::vector<ui::Hint> hints() const;
    // The slots its themes are queued in.
    std::vector<int> queuedSlots() const;
    void openInstall();
    void view();

    themezer::Item pack_;
    std::vector<std::string> trail_; // where it is, for the top bar
    ui::GridView grid_;
    ui::HintBar hintBar_;
    Backdrop backdrop_;
    float appear_ = 0;
    bool leaving_ = false;
};

} // namespace tz::scenes
