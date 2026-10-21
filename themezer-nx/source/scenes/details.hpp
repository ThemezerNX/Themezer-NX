// A theme's or a splash screen's page: its preview large, what it is, and installing or downloading it. Also the
// picture alone, filling the screen.

#pragma once

#include "scenes/frame.hpp"
#include "services/themezer.hpp"
#include "ui/scene.hpp"
#include "ui/widgets.hpp"

namespace tz::scenes
{

class DetailsScene final : public ui::Scene
{
public:
    explicit DetailsScene(themezer::Item item);
    void update(ui::Input& input, float dt) override;
    void tick(float dt) override;
    void draw() override;

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
};

class PictureScene final : public ui::Scene
{
public:
    PictureScene(std::string url, std::string thumbUrl, std::string thumbHash);
    void update(ui::Input& input, float dt) override;
    void draw() override;
    bool covers() const override { return false; }

private:
    std::string url_;
    std::string thumbUrl_;
    std::string thumbHash_;
    float appear_ = 0;
    bool leaving_ = false;
};

// The facts about an item, a line each with an icon: from (x, y) down, width wide. Returns where they end.
float drawFacts(const themezer::Item& item, float x, float y, float width, gfx::Accent accent);

} // namespace tz::scenes
