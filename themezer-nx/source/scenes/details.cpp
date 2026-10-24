#include "scenes/details.hpp"

#include "app/app.hpp"
#include "domain/installs.hpp"
#include "gfx/renderer.hpp"
#include "gfx/text.hpp"
#include "scenes/actions.hpp"
#include "scenes/images.hpp"
#include "sys/audio.hpp"
#include "ui/overlays.hpp"

#include <algorithm>
#include <cmath>

namespace tz::scenes
{

using namespace gfx;
using ui::approach;
using ui::ease;
using themezer::Kind;

namespace
{

constexpr float kPreviewX = 48;
constexpr float kPreviewY = metric::kTopBarHeight + 16;
constexpr float kPreviewW = 800;
constexpr float kPreviewH = 450;
constexpr float kColumnX = kPreviewX + kPreviewW + 40;
constexpr float kColumnW = metric::kWidth - kColumnX - 48;


} // namespace

float drawFacts(const themezer::Item& item, float x, float y, float width, Accent accent, float* packRow)
{
    const auto fact = [&](Icon which, const std::string& text, bool link = false) {
        y = drawFact(which, text, x, y, width, accent, link);
    };
    if (!item.remote)
    {
        fact(Icon::Download, thousands(item.downloads) + (item.downloads == 1 ? " download" : " downloads"));
        fact(Icon::Bookmark, thousands(item.saves) + (item.saves == 1 ? " save" : " saves"));
        fact(Icon::Calendar, "Updated " + themezer::dateOf(item.updatedAt));
    }
    else
    {
        fact(Icon::Globe, "A remote install, not on Themezer");
        fact(Icon::Calendar, "Added " + themezer::dateOf(item.createdAt));
    }
    if (packRow)
        *packRow = -1;
    if (!item.packName.empty() && item.kind == Kind::Themes)
    {
        if (packRow)
            *packRow = y;
        fact(Icon::Pack, "From the pack " + item.packName, true);
    }
    fact(Icon::Hash, "Quick ID " + item.quickId);
    return y;
}

DetailsScene::DetailsScene(themezer::Item item, std::vector<ui::Crumb> trail)
    : item_(std::move(item)), trail_(std::move(trail))
{
    sfx::play(sfx::Sound::Open);
    backdrop_.set(placeholderOf(item_), accentOf(item_.kind).solid);
}

std::vector<ui::Crumb> DetailsScene::crumbs() const
{
    std::vector<ui::Crumb> crumbs = trail_;
    crumbs.push_back(pageCrumb(item_.name));
    return crumbs;
}

std::shared_ptr<Picture> DetailsScene::picture() const
{
    if (auto hd = images().get(item_.preview.hd))
        return hd;
    if (auto thumb = images().get(item_.preview.thumb))
        return thumb;
    return placeholderOf(item_);
}

std::vector<ui::Hint> DetailsScene::hints() const
{
    std::vector<ui::Hint> hints{{ui::key::kB, "Back"}};
    if (!item_.preview.hd.empty())
        hints.push_back({ui::key::kY, "View"});
    if (!options().empty())
        hints.push_back({ui::key::kX, "Options"});
    // A opens the install sheet; queued, A takes it out again
    hints.push_back(
        {ui::key::kA, installs::isQueued(item_) ? "Cancel install" : "Install"});
    return hints;
}

void DetailsScene::openInstall()
{
    const themezer::Item item = item_;
    std::vector<ui::Action> actions;
    // the ways to get it, as a pack's sheet has: saved in /Themes/ThemezerNX or only until the next start, or only saved
    actions.push_back({Icon::Install, "Install & Save", "", true, false, [item] { install(item, true); }});
    actions.push_back({Icon::Install, "Install only", "", true, false, [item] { install(item, false); }});
    actions.push_back({Icon::Download, "Download only", "", true, false, [item] { download(item); }});
    App::get().scenes().emplace<ui::ActionSheet>(item_.name, std::move(actions), accentOf(item_.kind).solid, 0);
}

std::vector<ui::Action> DetailsScene::options() const
{
    std::vector<ui::Action> actions;
    if (item_.kind == Kind::Themes && !item_.packId.empty())
        actions.push_back({Icon::Pack, "Open pack", "", true, false, [id = item_.packId] { showOnThemezer("P" + id); }});
    if (badgeOf(item_) == Badge::Installed)
        actions.push_back({Icon::Trash, "Uninstall", "", true, true,
                           [slot = installs::slotOf(item_)] { uninstallPart(slot); }});
    return actions;
}

void DetailsScene::openPack() const
{
    // looked up as its Quick ID would be: the theme has only the pack's ID and name
    showOnThemezer("P" + item_.packId);
}

void DetailsScene::view()
{
    App::get().scenes().emplace<PictureScene>(item_.preview.hd, item_.preview.thumb, item_.preview.thumbHash);
}

void DetailsScene::tick(float dt)
{
    backdrop_.tick(dt);
    approach(appear_, leaving_ ? 0.0f : 1.0f, dt, leaving_ ? 16 : 10);
    if (leaving_ && appear_ < 0.05f)
        close();
}

void DetailsScene::update(ui::Input& input, float dt)
{
    (void)dt;
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
    }
    else if (input.pressed(ui::key::kA))
    {
        if (installs::isQueued(item_))
        {
            unqueueAndSay(installs::slotOf(item_));
            return;
        }
        openInstall();
    }
    else if (input.pressed(ui::key::kX))
    {
        if (std::vector<ui::Action> actions = options(); !actions.empty())
            App::get().scenes().emplace<ui::ActionSheet>(item_.name, std::move(actions), accentOf(item_.kind).solid);
    }
    else if (factTapped(input, kColumnX, packRow_, kColumnW))
    {
        input.consumeTouch();
        sfx::play(sfx::Sound::Confirm);
        openPack();
    }
    else if (input.pressed(ui::key::kY) && !item_.preview.hd.empty())
    {
        sfx::play(sfx::Sound::Confirm);
        view();
    }
    else if (input.tapIn(kPreviewX, kPreviewY, kPreviewW, kPreviewH) && !item_.preview.hd.empty())
    {
        input.consumeTouch();
        sfx::play(sfx::Sound::Confirm);
        view();
    }
    else if (input.pressed(ui::key::kPlus))
    {
        openMenu();
    }
}

void DetailsScene::draw()
{
    const float shown = ease(appear_);
    {
        const Layer layer(0, 0, shown);
        backdrop_.draw(0.55f);
    }
    const Accent accent = accentOf(item_.kind);
    {
        const Layer layer(0, (1 - shown) * 40, shown);
        shadow(kPreviewX, kPreviewY, kPreviewW, kPreviewH, kPictureRadius, 28);
        if (auto picture = this->picture())
        {
            picture->drawCover(kPreviewX, kPreviewY, kPreviewW, kPreviewH, kPictureRadius);
        }
        else
        {
            roundedRect(kPreviewX, kPreviewY, kPreviewW, kPreviewH, kPictureRadius, color::kSurfaceRaised);
            icon(iconOf(item_.kind), kPreviewX + (kPreviewW - metric::kIconLarge) / 2,
                 kPreviewY + (kPreviewH - metric::kIconLarge) / 2, metric::kIconLarge, color::kTextFaint);
        }
        // the HD picture loading over the small one
        if (!item_.preview.hd.empty() && !images().get(item_.preview.hd))
            spinner(kPreviewX + kPreviewW - 32, kPreviewY + kPreviewH - 32, 14, color::kText);
        // what its card shows over its picture: its part, and what's become of it here
        card::overlay(item_.kind == Kind::Themes && item_.target >= 0 ? themezer::targetLabel(item_.target) : "",
                      badgeOf(item_), kPreviewX, kPreviewY, kPreviewW, kPreviewH, accent, 16);

        if (!item_.description.empty())
            drawWrapped(item_.description, kPreviewX + 4, kPreviewY + kPreviewH + 20, kPreviewW - 8,
                        metric::kTextCaption, color::kTextDim, 3);
    }
    {
        const Layer layer((1 - shown) * 40, 0, shown);
        const float y = drawHeading(item_.name, item_.creator, kColumnX, kPreviewY - 6, kColumnW, accent, 3);
        drawFacts(item_, kColumnX, y, kColumnW, accent, &packRow_);
    }

    {
        const Layer layer(0, 0, shown);
        drawTopBar(1.0f, trail_);
    }
    // the bar every screen has, not faded in with the page: faded, its keys are drawn without their depth
    drawHints(hintBar_, hints(), accent.solid);
}

PictureScene::PictureScene(std::string url, std::string thumbUrl, std::string thumbHash)
{
    picture_ = [url, thumbUrl, thumbHash] {
        std::shared_ptr<Picture> picture = images().get(url);
        if (!picture)
            picture = images().get(thumbUrl);
        if (!picture)
            picture = images().placeholder(thumbHash);
        return picture;
    };
    final_ = [url] { return images().get(url) != nullptr; };
    sfx::play(sfx::Sound::Open);
}

PictureScene::PictureScene(std::function<std::shared_ptr<Picture>()> picture) : picture_(std::move(picture))
{
    final_ = [this] { return picture_() != nullptr; };
    sfx::play(sfx::Sound::Open);
}

void PictureScene::tick(float dt)
{
    approach(appear_, leaving_ ? 0.0f : 1.0f, dt, leaving_ ? 18 : 12);
    if (leaving_ && appear_ < 0.05f)
        close();
}

void PictureScene::update(ui::Input& input, float)
{
    if (input.pressed(ui::key::kB | ui::key::kA | ui::key::kY) || input.tap)
    {
        input.consumeTouch();
        sfx::play(sfx::Sound::Close);
        leaving_ = true;
    }
}

void PictureScene::draw()
{
    const float shown = ease(appear_);
    fill(withAlpha(rgba(0, 0, 0), shown));
    const std::shared_ptr<Picture> picture = picture_();
    const Layer layer(0, 0, shown);
    // from a little smaller, as if it came forward
    const float scale = 0.92f + 0.08f * shown;
    const float w = metric::kWidth * scale;
    const float h = metric::kHeight * scale;
    if (picture)
        picture->drawContain((metric::kWidth - w) / 2, (metric::kHeight - h) / 2, w, h);
    if (!final_())
        spinner(metric::kWidth - 48, 48, 18, color::kText);
}

} // namespace tz::scenes
