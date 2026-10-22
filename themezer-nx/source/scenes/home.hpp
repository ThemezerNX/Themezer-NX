// The app's first screen, as Themezer-3DS's: two modes with the same tabs (packs, themes, splashes), the SD card's and
// Themezer's. The button before the tabs, which says which it is, or -, switches them, staying on the same tab.

#pragma once

#include "scenes/browse.hpp"
#include "scenes/frame.hpp"
#include "scenes/library_page.hpp"
#include "scenes/page.hpp"
#include "ui/scene.hpp"
#include "ui/widgets.hpp"

#include <memory>
#include <vector>

namespace tz::scenes
{

class HomeScene final : public ui::Scene
{
public:
    HomeScene();
    void update(ui::Input& input, float dt) override;
    void tick(float dt) override;
    void draw() override;
    void onResume() override;

private:
    Page& page();
    void switchMode();
    void switchTo(int tab, int side);
    void remember();
    void openMenu();
    // Draws it, its width from its label; returns where it ends.
    float drawModeButton(float x, float y, float height);

    std::vector<std::unique_ptr<Page>> local_;
    std::vector<std::unique_ptr<Page>> online_;
    bool onlineMode_ = true;
    int tab_ = 0;

    ui::TabBar tabs_;
    ui::HintBar hintBar_;
    std::vector<ui::Hint> hints_;
    Backdrop backdrop_;
    ui::KeyPress modePress_;
    float modeX_ = 0, modeY_ = 0, modeW_ = 0, modeH_ = 0; // where the last frame drew the mode button
    float appear_ = 1;                             // the page, coming in from the side of its tab
    int side_ = 0;
};

} // namespace tz::scenes
