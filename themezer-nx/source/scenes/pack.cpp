#include "scenes/pack.hpp"

#include "app/app.hpp"
#include "domain/installs.hpp"
#include "gfx/renderer.hpp"
#include "gfx/text.hpp"
#include "scenes/actions.hpp"
#include "scenes/details.hpp"
#include "scenes/images.hpp"
#include "sys/audio.hpp"
#include "ui/overlays.hpp"

#include <cmath>

namespace tz::scenes
{

using namespace gfx;
using ui::approach;
using ui::ease;
using themezer::Kind;

namespace
{
constexpr float kSide = 48;
constexpr float kColumnW = 380;
constexpr float kTop = metric::kTopBarHeight + 16;
constexpr float kGridX = kSide + kColumnW + 40;
constexpr int kColumns = 2;
constexpr float kGap = 22;
} // namespace

PackScene::PackScene(themezer::Item pack, std::vector<ui::Crumb> trail)
    : pack_(std::move(pack)), trail_(std::move(trail))
{
    sfx::play(sfx::Sound::Open);
    backdrop_.set(placeholderOf(pack_), accentOf(Kind::Packs).solid);
    grid_.setBounds(kGridX, kTop + 6, metric::kWidth - kGridX - kSide, metric::kHeight - metric::kHintBarHeight - kTop - 6);
    const float cellWidth = (metric::kWidth - kGridX - kSide - kGap * (kColumns - 1)) / kColumns;
    grid_.setLayout(kColumns, std::round((cellWidth - 20) * 9 / 16) + 20 + card::kTextHeight, kGap);
    grid_.setCount(static_cast<int>(pack_.themes.size()));
}

std::vector<ui::Crumb> PackScene::crumbs() const
{
    std::vector<ui::Crumb> crumbs = trail_;
    crumbs.push_back(pageCrumb(pack_.name));
    return crumbs;
}

std::vector<int> PackScene::queuedSlots() const
{
    std::vector<int> slots;
    for (const themezer::Item& theme : pack_.themes)
    {
        if (installs::isQueued(theme))
            slots.push_back(installs::slotOf(theme));
    }
    return slots;
}

std::vector<ui::Hint> PackScene::hints() const
{
    std::vector<ui::Hint> hints{{ui::key::kB, "Back"}};
    if (!pack_.preview.hd.empty())
        hints.push_back({ui::key::kY, "View"});
    // queued, X takes its themes out again, as a theme's A does
    hints.push_back({ui::key::kX, queuedSlots().empty() ? "Install All" : "Cancel install"});
    if (!pack_.themes.empty())
        hints.push_back({ui::key::kA, "Open"});
    return hints;
}

void PackScene::openInstall()
{
    const themezer::Item pack = pack_;
    // a theme's install sheet, for all of them
    std::vector<ui::Action> actions;
    actions.push_back({Icon::Install, "Install & Save All", "", true, false, [pack] { installPack(pack, true); }});
    actions.push_back({Icon::Install, "Install All only", "", true, false, [pack] { installPack(pack, false); }});
    actions.push_back({Icon::Download, "Download All only", "", true, false, [pack] { downloadPack(pack); }});
    App::get().scenes().emplace<ui::ActionSheet>(pack_.name, std::move(actions), accentOf(Kind::Packs).solid, 0);
}

void PackScene::view()
{
    App::get().scenes().emplace<PictureScene>(pack_.preview.hd, pack_.preview.thumb, pack_.preview.thumbHash);
}

void PackScene::tick(float dt)
{
    backdrop_.tick(dt);
    approach(appear_, leaving_ ? 0.0f : 1.0f, dt, leaving_ ? 16 : 10);
    if (leaving_ && appear_ < 0.05f)
        close();
}

void PackScene::update(ui::Input& input, float dt)
{
    if (leaving_)
        return;
    const auto shown = hints();
    updateHints(hintBar_, input, shown);
    if (trailKeys(input, trail_))
        return;
    if (input.pressed(ui::key::kB))
    {
        sfx::play(sfx::Sound::Back);
        leaving_ = true;
        return;
    }
    if (!pack_.preview.hd.empty() &&
        (input.pressed(ui::key::kY) || input.tapIn(kSide, kTop, kColumnW, std::round(kColumnW * 9 / 16))))
    {
        input.consumeTouch();
        sfx::play(sfx::Sound::Confirm);
        view();
        return;
    }
    if (input.pressed(ui::key::kX))
    {
        if (const std::vector<int> queued = queuedSlots(); !queued.empty())
            unqueueAndSay(queued);
        else
            openInstall();
        return;
    }
    if (input.pressed(ui::key::kPlus))
    {
        openMenu();
        return;
    }
    if (grid_.update(input, dt) == ui::GridView::Event::Activated && !pack_.themes.empty())
        openItem(pack_.themes[static_cast<size_t>(grid_.selected())]);
}

void PackScene::draw()
{
    const float shown = ease(appear_);
    {
        const Layer layer(0, 0, shown);
        backdrop_.draw(0.6f);
    }
    const Accent accent = accentOf(Kind::Packs);
    {
        // the pack: its collage, name, creator and facts
        const Layer layer(0, (1 - shown) * 40, shown);
        const float collageH = std::round(kColumnW * 9 / 16);
        shadow(kSide, kTop, kColumnW, collageH, kPictureRadius, 24);
        std::shared_ptr<Picture> picture = images().get(pack_.preview.thumb);
        if (!picture)
            picture = placeholderOf(pack_);
        if (picture)
            picture->drawCover(kSide, kTop, kColumnW, collageH, kPictureRadius);
        else
            roundedRect(kSide, kTop, kColumnW, collageH, kPictureRadius, color::kSurfaceRaised);
        // what its card shows over its collage: how many themes, and what's become of them here
        const size_t count = pack_.themes.size();
        card::overlay(std::to_string(count) + (count == 1 ? " theme" : " themes"), badgeOf(pack_), kSide, kTop, kColumnW,
                      collageH, accent, 12);
        float y = kTop + collageH + 16;
        // its description under it, as a theme's is
        if (!pack_.description.empty())
            y += drawWrapped(pack_.description, kSide + 4, y, kColumnW - 8, metric::kTextCaption, color::kTextDim, 2) + 12;
        else
            y += 8;
        // its name on two lines where they fit over its four facts, else on one
        const float under = 4 + lineHeight(metric::kTextNormal) + 26 + 4 * kFactRow;
        const float room = metric::kHeight - metric::kHintBarHeight - 4 - y - under;
        const int lines = room >= 2 * lineHeight(metric::kTextTitle) ? 2 : 1;
        drawFacts(pack_, kSide, drawHeading(pack_.name, pack_.creator, kSide, y, kColumnW, accent, lines), kColumnW,
                  accent);
    }
    {
        const Layer layer((1 - shown) * 40, 0, shown);
        grid_.draw(
            [&](int index, float x, float y, float w, float h, bool selected) {
                card::draw(pack_.themes[static_cast<size_t>(index)], x, y, w, h, selected, accentOf(Kind::Themes), true);
            },
            [&](float x, float y, float w, float h, bool down) {
                card::highlight(x, y, w, h, down, accentOf(Kind::Themes));
            });
    }
    {
        const Layer layer(0, 0, shown);
        drawTopBar(1.0f, trail_);
    }
    // the bar every screen has, not faded in with the page: faded, its keys are drawn without their depth
    drawHints(hintBar_, hints(), accent.solid);
}

} // namespace tz::scenes
