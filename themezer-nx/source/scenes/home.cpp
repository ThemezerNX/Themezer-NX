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
    hints_.push_back({ui::key::kMinus, onlineMode_ ? "SD card" : "Themezer"});
    hintBar_.update(input, hints_);

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

namespace
{
const char* modeLabel(bool online)
{
    return online ? "Themezer" : "SD card";
}

// as wide for both, so the tabs stay where they are
float modeWidth()
{
    const float label = std::max(textWidth(modeLabel(true), metric::kTextNormal),
                                 textWidth(modeLabel(false), metric::kTextNormal));
    return std::round(16 + ui::buttonWidth(ui::key::kMinus, metric::kButtonGlyph) + 12 + metric::kIcon + 10 + label + 20);
}
} // namespace

float HomeScene::drawModeButton(float x, float y, float height)
{
    const float width = modeWidth();
    modeX_ = x;
    modeY_ = y;
    modeW_ = width;
    modeH_ = height;
    // as the tab bar is: a pill with the key that switches it, which sinks and lights up pressed; then the mode it's in
    const bool down = modePress_.down();
    const float middle = y + height / 2;
    roundedRect(x, y, width, height, height / 2, color::kSurface);
    float left = x + 16;
    left += ui::drawButton(ui::key::kMinus, left, textTop(middle, metric::kButtonGlyph) + (down ? ui::kKeyLip : 0),
                           metric::kButtonGlyph, down ? color::kText : color::kTextDim) +
            12;
    const float iconSize = metric::kIcon;
    icon(onlineMode_ ? Icon::Globe : Icon::SdCard, left, std::round(middle - iconSize / 2), iconSize,
         onlineMode_ ? accent::kApp.light : color::kText);
    drawText(modeLabel(onlineMode_), left + iconSize + 10, textTop(middle, metric::kTextNormal), metric::kTextNormal,
             color::kText);
    return x + width;
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
    // the mode button and the tabs, in the middle of the bar where there's room, between the logo and the status
    const float from = 32 + brandWidth(Brand::Logo) + 20;
    const float to = statusLeft - 20;
    const float modeW = modeWidth();
    const float barWidth = std::min(tabs_.naturalWidth(metric::kTextNormal), to - from - modeW - 12);
    const float total = modeW + 12 + barWidth;
    const float left = std::round(std::clamp((metric::kWidth - total) / 2, from, to - total));
    const float top = std::round((metric::kTopBarHeight - kBarHeight) / 2);
    const float tabsLeft = drawModeButton(left, top, kBarHeight) + 12;
    tabs_.draw(tabsLeft, top, barWidth, kBarHeight, metric::kTextNormal);
    hintBar_.draw(hints_, 1.0f, page().accent().solid);
}

} // namespace tz::scenes
