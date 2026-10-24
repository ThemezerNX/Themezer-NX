#include "scenes/home.hpp"

#include "app/app.hpp"
#include "gfx/renderer.hpp"
#include "gfx/text.hpp"
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
constexpr themezer::Kind kKinds[kTabs] = {themezer::Kind::Packs, themezer::Kind::Themes, themezer::Kind::Splashes};
constexpr float kBarHeight = kPillHeight;
// how far a page comes in from, from the side of its tab
constexpr float kSlide = 32;
} // namespace

namespace
{
// the one there is, for goHome
HomeScene* current = nullptr;
} // namespace

void goHome(bool online, themezer::Kind section)
{
    if (!current)
        return;
    App::get().scenes().closeAbove(current);
    current->show(online, static_cast<int>(std::find(std::begin(kKinds), std::end(kKinds), section) - std::begin(kKinds)));
}

HomeScene::~HomeScene()
{
    current = nullptr;
}

HomeScene::HomeScene()
{
    current = this;
    for (themezer::Kind kind : kKinds)
    {
        local_.push_back(std::make_unique<LibraryPage>(kind));
        online_.push_back(std::make_unique<BrowsePage>(kind));
    }
    // where it was last: 0 to 2 are the SD card's tabs, 3 to 5 Themezer's
    const int last = std::clamp(App::get().config().lastTab, 0, 2 * kTabs - 1);
    onlineMode_ = last >= kTabs;
    tab_ = last % kTabs;
    // the sections, as every page's top bar names them
    std::vector<ui::Tab> tabs;
    for (themezer::Kind kind : kKinds)
    {
        const ui::Crumb section = sectionCrumb(kind);
        tabs.push_back({section.text, section.accent, section.icon});
    }
    tabs_.setTabs(std::move(tabs));
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
    show(!onlineMode_, tab_);
}

void HomeScene::show(bool online, int tab)
{
    tab = std::clamp(tab, 0, kTabs - 1);
    if (online == onlineMode_ && tab == tab_)
        return;
    page().onHide();
    // back on the SD card: what changed there while away (a download, the files changed elsewhere)
    if (online != onlineMode_ && !online)
        local::Library::get().rescan();
    onlineMode_ = online;
    tab_ = tab;
    tabs_.select(tab_);
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
    hints_.push_back({ui::key::kZR, "Quick ID"});
    hints_.push_back({ui::key::kPlus, "Menu"});
    updateHints(hintBar_, input, hints_);

    // the mode button first: a tap on it isn't the tab bar's
    const auto onModeButton = [&](float x, float y) {
        return x >= modeX_ && x < modeX_ + modeW_ && y >= modeY_ && y < modeY_ + modeH_;
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
    if (input.pressed(ui::key::kZR))
    {
        sfx::play(sfx::Sound::Confirm);
        openQuickId();
        return;
    }
    page().update(input, dt);
}

std::vector<ui::Crumb> HomeScene::crumbs() const
{
    return {modeCrumb(onlineMode_), sectionCrumb(kKinds[tab_])};
}

namespace
{
// as wide for both, so the tabs stay where they are
float modeWidth()
{
    return std::max(modePillWidth(modeCrumb(true), ui::key::kMinus), modePillWidth(modeCrumb(false), ui::key::kMinus));
}
} // namespace

float HomeScene::drawModeButton(float x, float y, float height)
{
    modeX_ = x;
    modeY_ = y;
    modeW_ = modeWidth();
    modeH_ = height;
    // as the tab bar is: a pill with the key that switches it, which sinks and lights up pressed; then the mode it's in
    return drawModePill(modeCrumb(onlineMode_), x, y, modeW_, ui::key::kMinus, modePress_.down());
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
    // the mode button and the tabs after the logo, where every page's top bar has its mode and section
    const float left = pillsLeft();
    const float barWidth = std::min(tabs_.naturalWidth(metric::kTextNormal), statusLeft - 20 - left - modeWidth() - 12);
    const float top = std::round((metric::kTopBarHeight - kBarHeight) / 2);
    const float tabsLeft = drawModeButton(left, top, kBarHeight) + 12;
    tabs_.draw(tabsLeft, top, barWidth, kBarHeight, metric::kTextNormal);
    drawHints(hintBar_, hints_, page().accent().solid);
}

} // namespace tz::scenes
