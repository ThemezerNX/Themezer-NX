// A theme's or a splash screen's page: its preview large, what it is, and installing or downloading it. Also the
// picture alone, filling the screen.

#pragma once

#include "scenes/frame.hpp"
#include "services/themezer.hpp"
#include "ui/overlays.hpp"
#include "ui/scene.hpp"
#include "ui/widgets.hpp"

#include <functional>
#include <memory>

namespace tz::scenes
{

class DetailsScene final : public ui::Scene
{
public:
    explicit DetailsScene(themezer::Item item, std::vector<ui::Crumb> trail = currentTrail());
    void update(ui::Input& input, float dt) override;
    void tick(float dt) override;
    void draw() override;
    bool acceptsInput() const override { return !leaving_; }
    std::vector<ui::Crumb> crumbs() const override;

private:
    std::vector<ui::Hint> hints() const;
    std::shared_ptr<gfx::Picture> picture() const;
    void openInstall();
    // X: what else can be done with it: its pack, uninstalling it.
    std::vector<ui::Action> options() const;
    void openPack() const;
    void view();

    float packRow_ = -1; // where its pack's fact was drawn, for a tap
    themezer::Item item_;
    std::vector<ui::Crumb> trail_; // where it was opened from, for the top bar
    ui::HintBar hintBar_;
    Backdrop backdrop_;
    float appear_ = 0;
    bool leaving_ = false;
};

class PictureScene final : public ui::Scene
{
public:
    // The HD picture at url, the thumbnail and then the ThumbHash meanwhile.
    PictureScene(std::string url, std::string thumbUrl, std::string thumbHash);
    // The picture picture gives, a spinner while it gives none.
    explicit PictureScene(std::function<std::shared_ptr<gfx::Picture>()> picture);
    void update(ui::Input& input, float dt) override;
    void tick(float dt) override;
    void draw() override;
    bool covers() const override { return false; }
    bool acceptsInput() const override { return !leaving_; }

private:
    std::function<std::shared_ptr<gfx::Picture>()> picture_;
    std::function<bool()> final_; // whether the picture is the one to show, not a stand-in for it
    float appear_ = 0;
    bool leaving_ = false;
};

// The facts about an item, a line each with an icon: from (x, y) down, width wide. Returns where they end.
// packRow, when given, gets the top of the row of a theme's pack, a link, or -1 without one.
float drawFacts(const themezer::Item& item, float x, float y, float width, gfx::Accent accent,
                float* packRow = nullptr);

} // namespace tz::scenes
