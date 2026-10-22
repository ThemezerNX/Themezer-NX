// A theme's or a splash screen's page: its preview large, what it is, and installing or downloading it. Also the
// picture alone, filling the screen.

#pragma once

#include "scenes/frame.hpp"
#include "services/themezer.hpp"
#include "ui/scene.hpp"
#include "ui/widgets.hpp"

#include <functional>
#include <memory>

namespace tz::scenes
{

class DetailsScene final : public ui::Scene
{
public:
    explicit DetailsScene(themezer::Item item);
    void update(ui::Input& input, float dt) override;
    void tick(float dt) override;
    void draw() override;
    bool acceptsInput() const override { return !leaving_; }
    void onResume() override;

private:
    std::vector<ui::Hint> hints() const;
    std::shared_ptr<gfx::Picture> picture() const;
    void openMore();
    void view();

    themezer::Item item_;
    ui::HintBar hintBar_;
    Backdrop backdrop_;
    float appear_ = 0;
    bool leaving_ = false;
    bool saved_ = false; // on the SD card already
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
float drawFacts(const themezer::Item& item, float x, float y, float width, gfx::Accent accent, float row = 44);

} // namespace tz::scenes
