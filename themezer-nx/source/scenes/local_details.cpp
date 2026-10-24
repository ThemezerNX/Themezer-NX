#include "scenes/local_details.hpp"

#include "app/app.hpp"
#include "domain/installs.hpp"
#include "gfx/renderer.hpp"
#include "gfx/text.hpp"
#include "scenes/actions.hpp"
#include "services/local.hpp"
#include "sys/audio.hpp"
#include "ui/overlays.hpp"

#include <cmath>

namespace tz::scenes
{

using namespace gfx;
using library::Kind;
using ui::approach;
using ui::ease;

namespace
{
constexpr float kPreviewX = 48;
constexpr float kPreviewY = metric::kTopBarHeight + 16;
constexpr float kPreviewW = 800;
constexpr float kPreviewH = 450;
constexpr float kColumnX = kPreviewX + kPreviewW + 40;
constexpr float kColumnW = metric::kWidth - kColumnX - 48;

Accent accentOf(const library::Entry& entry)
{
    return entry.kind == Kind::Splash ? accent::kSplashes : accent::kThemes;
}

// The Quick ID themezer.net shows for an item, from its ID: T for themes, S for splashes.
std::string quickIdOf(const library::Entry& entry)
{
    return (entry.kind == Kind::Splash ? "S" : "T") + entry.themezerId;
}
} // namespace

LocalDetailsScene::LocalDetailsScene(std::string path) : path_(std::move(path))
{
    sfx::play(sfx::Sound::Open);
    if (const library::Entry* found = entry())
        last_ = *found;
    trail_ = currentTrail();
}

std::vector<ui::Crumb> LocalDetailsScene::crumbs() const
{
    std::vector<ui::Crumb> crumbs = trail_;
    crumbs.push_back(pageCrumb(last_.name));
    return crumbs;
}

const library::Entry* LocalDetailsScene::entry() const
{
    return local::Library::get().find(path_);
}

void LocalDetailsScene::leave()
{
    leaving_ = true;
}

int LocalDetailsScene::queuedSlot() const
{
    const library::Entry* found = entry();
    if (!found)
        return -1;
    const int slot = found->kind == Kind::Splash ? installs::kSplashSlot : found->target;
    const installs::Queued* queued = installs::queued(slot);
    return queued && queued->path == found->path ? slot : -1;
}

std::vector<ui::Hint> LocalDetailsScene::hints() const
{
    // queued, A takes it out again
    return {{ui::key::kB, "Back"},
            {ui::key::kY, "View"},
            {ui::key::kX, "Options"},
            {ui::key::kA, queuedSlot() >= 0 ? "Cancel install" : "Install"}};
}

void LocalDetailsScene::openOptions()
{
    const library::Entry* found = entry();
    if (!found)
        return;
    const library::Entry current = *found;
    const int slot = current.kind == Kind::Splash ? installs::kSplashSlot : current.target;
    std::vector<ui::Action> actions;
    if (current.kind == Kind::Theme && local::inPack(current))
        actions.push_back({Icon::Pack, "Open pack", "", true, false, [current] { openPackOf(current); }});
    if (badgeOf(current) == Badge::Installed)
        actions.push_back({Icon::Trash, "Uninstall", "", true, true, [slot] { uninstallPart(slot); }});
    if (!current.themezerId.empty())
        actions.push_back({Icon::Globe, "Show on Themezer", "", true, false,
                           [quickId = quickIdOf(current)] { showOnThemezer(quickId); }});
    // the page goes by itself when the file has
    actions.push_back({Icon::Trash, "Delete", "", true, true, [current] { deleteLocal(current); }});
    App::get().scenes().emplace<ui::ActionSheet>(current.name, std::move(actions), accentOf(current).solid);
}

void LocalDetailsScene::tick(float dt)
{
    backdrop_.tick(dt);
    approach(appear_, leaving_ ? 0.0f : 1.0f, dt, leaving_ ? 16 : 10);
    if (leaving_ && appear_ < 0.05f)
        close();
    if (const library::Entry* found = entry())
    {
        last_ = *found;
        if (!found->infoRead)
        {
            const auto& entries = local::Library::get().entries();
            local::Library::get().wantInfo(static_cast<size_t>(found - entries.data()));
        }
        backdrop_.set(localBackdrop(*found), accentOf(*found).solid);
    }
    else if (!leaving_)
    {
        // gone from the SD card
        leave();
    }
}

void LocalDetailsScene::update(ui::Input& input, float)
{
    if (leaving_)
        return;
    const auto shown = hints();
    updateHints(hintBar_, input, shown);
    if (trailKeys(input, trail_))
        return;
    const library::Entry* found = entry();
    if (!found)
        return;
    if (input.pressed(ui::key::kB))
    {
        sfx::play(sfx::Sound::Back);
        leave();
    }
    else if (input.pressed(ui::key::kA))
    {
        if (const int slot = queuedSlot(); slot >= 0)
        {
            unqueueAndSay(slot);
        }
        else
        {
            sfx::play(sfx::Sound::Confirm);
            installLocal(*found);
        }
    }
    else if (input.pressed(ui::key::kX))
    {
        openOptions();
    }
    else if (factTapped(input, kColumnX, packRow_, kColumnW))
    {
        input.consumeTouch();
        sfx::play(sfx::Sound::Confirm);
        openPackOf(*found);
    }
    else if (input.pressed(ui::key::kY) || input.tapIn(kPreviewX, kPreviewY, kPreviewW, kPreviewH))
    {
        input.consumeTouch();
        sfx::play(sfx::Sound::Confirm);
        viewLocal(*found);
    }
    else if (input.pressed(ui::key::kPlus))
    {
        openMenu();
    }
}

void LocalDetailsScene::draw()
{
    const library::Entry& entry = last_;
    const float shown = ease(appear_);
    {
        const Layer layer(0, 0, shown);
        backdrop_.draw(0.55f);
    }
    const Accent accent = accentOf(entry);
    {
        const Layer layer(0, (1 - shown) * 40, shown);
        shadow(kPreviewX, kPreviewY, kPreviewW, kPreviewH, kPictureRadius, 28);
        if (auto picture = localPicture(entry, static_cast<int>(kPreviewW)))
        {
            picture->drawCover(kPreviewX, kPreviewY, kPreviewW, kPreviewH, kPictureRadius);
        }
        else
        {
            roundedRect(kPreviewX, kPreviewY, kPreviewW, kPreviewH, kPictureRadius, color::kSurfaceRaised);
            icon(entry.kind == Kind::Splash ? Icon::Splash : Icon::Theme, kPreviewX + (kPreviewW - metric::kIconLarge) / 2,
                 kPreviewY + (kPreviewH - metric::kIconLarge) / 2, metric::kIconLarge, color::kTextFaint);
        }
        // what its card shows over its picture: its part, and what's become of it here
        card::overlay(entry.kind == Kind::Theme && entry.target >= 0 ? themezer::targetLabel(entry.target) : "",
                      badgeOf(entry), kPreviewX, kPreviewY, kPreviewW, kPreviewH, accent, 16);
        // the file it's in, under the picture
        drawText(entry.fileName, kPreviewX + 4, kPreviewY + kPreviewH + 20, metric::kTextCaption, color::kTextFaint,
                 Align::Left, kPreviewW - 8);
    }
    {
        const Layer layer((1 - shown) * 40, 0, shown);
        float y = drawHeading(entry.name, entry.author, kColumnX, kPreviewY - 6, kColumnW, accent, 3);
        // as Themezer's page has them: the pack it's in (a link to it), then what its file says
        packRow_ = -1;
        if (entry.kind == Kind::Theme && local::inPack(entry))
        {
            const local::Pack* pack = local::Library::get().findPack(entry.path.substr(0, entry.path.rfind('/')));
            packRow_ = y;
            y = drawFact(Icon::Pack, "From the pack " + (pack ? pack->name : entry.folder), kColumnX, y, kColumnW,
                         accent, true);
        }
        if (!entry.layout.empty())
            y = drawFact(Icon::LayoutGrid, "Layout: " + entry.layout, kColumnX, y, kColumnW, accent);
        y = drawFact(Icon::SdCard, megabytes(entry.size), kColumnX, y, kColumnW, accent);
        if (!entry.themezerId.empty())
            y = drawFact(Icon::Hash, "Quick ID " + quickIdOf(entry), kColumnX, y, kColumnW, accent);
    }
    {
        const Layer layer(0, 0, shown);
        drawTopBar(1.0f, trail_);
    }
    // the bar every screen has, not faded in with the page: faded, its keys are drawn without their depth
    drawHints(hintBar_, hints(), accent.solid);
}

} // namespace tz::scenes
