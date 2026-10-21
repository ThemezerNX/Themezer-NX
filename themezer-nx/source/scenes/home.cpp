#include "scenes/home.hpp"

#include "app/app.hpp"
#include "gfx/renderer.hpp"
#include "scenes/actions.hpp"
#include "services/local.hpp"
#include "sys/audio.hpp"
#include "ui/overlays.hpp"

#include <algorithm>
#include <cmath>

namespace tz::scenes
{

using namespace gfx;
using ui::approach;
using ui::ease;

namespace
{
constexpr int kTabs = 3; // in each mode
constexpr float kBarHeight = 52;
// how far a page comes in from, from the side of its tab
constexpr float kSlide = 32;
} // namespace

HomeScene::HomeScene()
{
    for (themezer::Kind kind : {themezer::Kind::Packs, themezer::Kind::Themes, themezer::Kind::Splashes})
    {
        local_.push_back(std::make_unique<LibraryPage>(kind));
        online_.push_back(std::make_unique<BrowsePage>(kind));
    }
    // where it was last: 0 to 2 are the SD card's tabs, 3 to 5 Themezer's
    const int last = std::clamp(App::get().config().lastTab, 0, 2 * kTabs - 1);
    onlineMode_ = last >= kTabs;
    tab_ = last % kTabs;
    tabs_.setTabs({{"Packs", accent::kPacks, Icon::Pack},
                   {"Themes", accent::kThemes, Icon::Theme},
                   {"Splashes", accent::kSplashes, Icon::Splash}});
    tabs_.select(tab_);
    page().onShow();
}

Page& HomeScene::page()
{
    return *(onlineMode_ ? online_ : local_)[static_cast<size_t>(tab_)];
}

void HomeScene::remember()
{
    Config& config = App::get().config();
    const int tab = onlineMode_ ? kTabs + tab_ : tab_;
    if (config.lastTab != tab)
    {
        config.lastTab = tab;
        config.save();
    }
}

void HomeScene::switchMode()
{
    page().onHide();
    onlineMode_ = !onlineMode_;
    // back on the SD card: what changed there while away (a download, the files changed elsewhere)
    if (!onlineMode_)
        local::Library::get().rescan();
    page().onShow();
    appear_ = 0;
    side_ = 0;
    remember();
}

void HomeScene::switchTo(int tab, int side)
{
    page().onHide();
    tab_ = tab;
    page().onShow();
    appear_ = 0;
    side_ = side;
    remember();
}

void HomeScene::openMenu()
{
    std::vector<ui::Action> actions{{onlineMode_ ? Icon::SdCard : Icon::Globe,
                                     onlineMode_ ? "Show the SD card" : "Browse Themezer", "", true, false,
                                     [this] { switchMode(); }, nullptr, ui::key::kMinus}};
    for (ui::Action& action : page().menuActions())
        actions.push_back(std::move(action));
    scenes::openMenu(std::move(actions));
}

void HomeScene::onResume()
{
    page().onResume();
}

void HomeScene::tick(float dt)
{
    for (auto& page : local_)
        page->tick(dt);
    for (auto& page : online_)
        page->tick(dt);
    approach(appear_, 1, dt, 9);
    backdrop_.set(page().backdrop(), page().accent().solid);
    backdrop_.tick(dt);
}

void HomeScene::update(ui::Input& input, float dt)
{
    hints_ = page().hints();
    hints_.push_back({ui::key::kPlus, "Menu"});
    hints_.push_back({ui::key::kMinus, onlineMode_ ? "SD card" : "Themezer"});
    hintBar_.update(input, hints_);

    // the mode button first: a tap on it isn't the tab bar's
    const auto onModeButton = [&](float x, float y) {
        return x >= modeX_ && x < modeX_ + modeSize_ && y >= modeY_ && y < modeY_ + modeSize_;
    };
    const bool fromModeButton = onModeButton(input.startX, input.startY) && onModeButton(input.x, input.y);
    const bool modePressed = input.pressed(ui::key::kMinus) || (input.tap && fromModeButton);
    modePress_.update((input.held & ui::key::kMinus) || (input.touching && !input.dragging && fromModeButton),
                      modePressed);
    if (modePressed)
    {
        sfx::play(sfx::Sound::Tab);
        input.consumeTouch();
        switchMode();
        return;
    }
    if (tabs_.update(input))
    {
        switchTo(tabs_.selected(), tabs_.movedForward() ? 1 : -1);
        return;
    }
    if (input.pressed(ui::key::kPlus))
    {
        openMenu();
        return;
    }
    page().update(input, dt);
}

void HomeScene::drawModeButton(float x, float y, float size)
{
    modeX_ = x;
    modeY_ = y;
    modeSize_ = size;
    // a round key, its icon the mode it's in
    const float top = ui::drawKey(x, y, size, size - ui::kKeyLip, color::kSurfaceHigh, modePress_.down());
    const float iconSize = metric::kIcon;
    icon(onlineMode_ ? Icon::Globe : Icon::SdCard, x + (size - iconSize) / 2, top + (size - ui::kKeyLip - iconSize) / 2,
         iconSize, onlineMode_ ? accent::kApp.light : color::kText);
}

void HomeScene::draw()
{
    backdrop_.draw(0.62f);
    {
        const float shown = ease(appear_);
        const Layer layer(static_cast<float>(side_) * kSlide * (1 - shown), side_ == 0 ? (1 - shown) * 16 : 0, shown);
        page().draw();
    }
    const float statusLeft = drawTopBar();
    // the mode button and the tabs, in the middle of the bar
    const float barWidth = std::min(tabs_.naturalWidth(metric::kTextNormal), statusLeft - 360);
    const float total = kBarHeight + 12 + barWidth;
    const float left = std::round((metric::kWidth - total) / 2);
    const float top = std::round((metric::kTopBarHeight - kBarHeight) / 2);
    drawModeButton(left, top, kBarHeight);
    tabs_.draw(left + kBarHeight + 12, top, barWidth, kBarHeight, metric::kTextNormal);
    hintBar_.draw(hints_, 1.0f, page().accent().solid);
}

} // namespace tz::scenes
