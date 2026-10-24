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
    explicit PackScene(themezer::Item pack, std::vector<ui::Crumb> trail = currentTrail());
    void update(ui::Input& input, float dt) override;
    void tick(float dt) override;
    void draw() override;
    bool acceptsInput() const override { return !leaving_; }
    std::vector<ui::Crumb> crumbs() const override;

private:
    std::vector<ui::Hint> hints() const;
    // The slots its themes are queued in.
    std::vector<int> queuedSlots() const;
    // X: the ways to get it all, or to cancel its install.
    void openOptions();
    void view();

    themezer::Item pack_;
    std::vector<ui::Crumb> trail_; // where it was opened from, for the top bar
    ui::GridView grid_;
    ui::HintBar hintBar_;
    Backdrop backdrop_;
    float appear_ = 0;
    bool leaving_ = false;
};

} // namespace tz::scenes
