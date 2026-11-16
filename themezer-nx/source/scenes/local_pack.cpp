#include "scenes/local_pack.hpp"

#include "app/app.hpp"
#include "domain/installs.hpp"
#include "gfx/renderer.hpp"
#include "gfx/text.hpp"
#include "scenes/actions.hpp"
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
constexpr float kSide = 48;
constexpr float kColumnW = 380;
constexpr float kTop = metric::kTopBarHeight + 16;
constexpr float kGridX = kSide + kColumnW + 40;
constexpr int kColumns = 2;
constexpr float kGap = 22;
// as wide as the SD card's pages ask for theirs, to share them
constexpr int kThumbWidth = 480;
} // namespace

LocalPackScene::LocalPackScene(std::string dir, std::vector<ui::Crumb> trail)
    : dir_(std::move(dir)), trail_(std::move(trail))
{
    sfx::play(sfx::Sound::Open);
    grid_.setBounds(kGridX, kTop + 6, metric::kWidth - kGridX - kSide, metric::kHeight - metric::kHintBarHeight - kTop - 6);
    const float cellWidth = (metric::kWidth - kGridX - kSide - kGap * (kColumns - 1)) / kColumns;
    grid_.setLayout(kColumns, std::round((cellWidth - 20) * 9 / 16) + 20 + card::kTextHeight, kGap);
    rebuild();
}

std::vector<ui::Crumb> LocalPackScene::crumbs() const
{
    std::vector<ui::Crumb> crumbs = trail_;
    crumbs.push_back(pageCrumb(last_.name));
    return crumbs;
}

void LocalPackScene::rebuild()
{
    local::Library& library = local::Library::get();
    generation_ = library.generation();
    const local::Pack* pack = library.findPack(dir_);
    if (!pack)
    {
        // gone from the SD card
        leaving_ = true;
        return;
    }
    last_ = *pack;
    std::vector<const library::Entry*> themes;
    for (size_t index : pack->themes)
        themes.push_back(&library.entries()[index]);
    // in the parts' order, as Themezer lists a pack's; one whose part isn't known yet last
    std::stable_sort(themes.begin(), themes.end(), [](const library::Entry* a, const library::Entry* b) {
        const int x = a->target < 0 ? themezer::kTargetCount : a->target;
        const int y = b->target < 0 ? themezer::kTargetCount : b->target;
        return x != y ? x < y : a->name < b->name;
    });
    themes_.clear();
    for (const library::Entry* theme : themes)
        themes_.push_back(theme->path);
    grid_.setCount(static_cast<int>(themes_.size()));
}

const library::Entry* LocalPackScene::cover() const
{
    const local::Pack* pack = local::Library::get().findPack(dir_);
    return pack ? local::Library::get().cover(*pack) : nullptr;
}

const library::Entry* LocalPackScene::themeAt(int index) const
{
    if (index < 0 || index >= static_cast<int>(themes_.size()))
        return nullptr;
    return local::Library::get().find(themes_[static_cast<size_t>(index)]);
}

std::vector<ui::Hint> LocalPackScene::hints() const
{
    std::vector<ui::Hint> hints{{ui::key::kB, "Back"}};
    if (cover())
        hints.push_back({ui::key::kY, "View"});
    hints.push_back({ui::key::kX, "Options"});
    if (!themes_.empty())
        hints.push_back({ui::key::kA, "Open"});
    return hints;
}

void LocalPackScene::openOptions()
{
    std::vector<ui::Action> actions;
    const std::string dir = dir_;
    if (auto update = updateAction(Kind::Packs, last_.themezerId))
        actions.push_back(std::move(*update));
    // queued, its themes come out again instead
    std::vector<int> queued;
    for (int slot = 0; slot < installs::kSlotCount; ++slot)
    {
        if (const installs::Queued* waiting = installs::queued(slot); waiting && waiting->path.starts_with(dir + "/"))
            queued.push_back(slot);
    }
    if (!queued.empty())
        actions.push_back({Icon::Error, "Cancel install", "", true, false, [queued] { unqueueAndSay(queued); }});
    else
        actions.push_back({Icon::Install, "Install All", "", true, false, [dir] { installLocalPack(dir); }});
    if (!last_.themezerId.empty())
        actions.push_back({Icon::Globe, "Show on Themezer", "", true, false,
                           [quickId = "P" + last_.themezerId] { showOnThemezer(quickId); }});
    // the page goes by itself when the folder has
    actions.push_back({Icon::Trash, "Delete pack", "", true, true, [dir] { deleteLocalPack(dir); }});
    App::get().scenes().emplace<ui::ActionSheet>(last_.name, std::move(actions), accentOf(Kind::Packs).solid, 0);
}

void LocalPackScene::tick(float dt)
{
    if (generation_ != local::Library::get().generation())
        rebuild();
    const library::Entry* picture = cover();
    backdrop_.set(picture ? localBackdrop(*picture) : nullptr, accentOf(Kind::Packs).solid);
    backdrop_.tick(dt);
    approach(appear_, leaving_ ? 0.0f : 1.0f, dt, leaving_ ? 16 : 10);
    if (leaving_ && appear_ < 0.05f)
        close();
}

void LocalPackScene::update(ui::Input& input, float dt)
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
    const library::Entry* picture = cover();
    if (picture && (input.pressed(ui::key::kY) || input.tapIn(kSide, kTop, kColumnW, std::round(kColumnW * 9 / 16))))
    {
        input.consumeTouch();
        sfx::play(sfx::Sound::Confirm);
        viewLocal(*picture);
        return;
    }
    if (input.pressed(ui::key::kX))
    {
        openOptions();
        return;
    }
    if (input.pressed(ui::key::kPlus))
    {
        openMenu();
        return;
    }
    if (grid_.update(input, dt) == ui::GridView::Event::Activated)
    {
        if (const library::Entry* theme = themeAt(grid_.selected()))
            openLocal(*theme);
    }
    // the names and parts of what's on screen, from inside the files
    local::Library& library = local::Library::get();
    const auto [first, last] = grid_.visibleRange();
    for (int index = first; index <= last; ++index)
    {
        if (const library::Entry* theme = themeAt(index); theme && !theme->infoRead)
            library.wantInfo(static_cast<size_t>(theme - library.entries().data()));
    }
}

void LocalPackScene::draw()
{
    const float shown = ease(appear_);
    {
        const Layer layer(0, 0, shown);
        backdrop_.draw(0.6f);
    }
    const Accent accent = accentOf(Kind::Packs);
    {
        const Layer layer(0, (1 - shown) * 40, shown);
        const float pictureH = std::round(kColumnW * 9 / 16);
        shadow(kSide, kTop, kColumnW, pictureH, kPictureRadius, 24);
        const library::Entry* picture = cover();
        if (auto loaded = picture ? localPicture(*picture, kThumbWidth) : nullptr)
        {
            loaded->drawCover(kSide, kTop, kColumnW, pictureH, kPictureRadius);
        }
        else
        {
            roundedRect(kSide, kTop, kColumnW, pictureH, kPictureRadius, color::kSurfaceRaised);
            icon(Icon::Pack, kSide + (kColumnW - metric::kIconLarge) / 2, kTop + (pictureH - metric::kIconLarge) / 2,
                 metric::kIconLarge, color::kTextFaint);
        }
        const size_t count = last_.themes.size();
        card::overlay(std::to_string(count) + (count == 1 ? " theme" : " themes"), badgeOf(last_), kSide, kTop,
                      kColumnW, pictureH, accent, 12, hasUpdate(last_));
        // as Themezer's pack page has them, where its description would be: the space under the picture
        float y = drawHeading(last_.name, last_.author, kSide, kTop + pictureH + 24, kColumnW, accent, 2);
        y = drawFact(Icon::SdCard, megabytes(last_.size), kSide, y, kColumnW, accent);
        if (!last_.themezerId.empty())
            y = drawFact(Icon::Hash, "Quick ID P" + last_.themezerId, kSide, y, kColumnW, accent);
    }
    {
        const Layer layer((1 - shown) * 40, 0, shown);
        const Accent themes = accentOf(Kind::Themes);
        grid_.draw(
            [&](int index, float x, float y, float w, float h, bool selected) {
                const library::Entry* theme = themeAt(index);
                if (!theme)
                    return;
                card::Content content;
                content.name = theme->name;
                content.creator = theme->author;
                content.picture = localPicture(*theme, kThumbWidth);
                content.badge = badgeOf(*theme);
                content.update = hasUpdate(*theme);
                if (theme->target >= 0)
                    content.chip = themezer::targetLabel(theme->target);
                card::draw(content, x, y, w, h, selected, themes);
            },
            [&](float x, float y, float w, float h, bool down) { card::highlight(x, y, w, h, down, themes); });
    }
    {
        const Layer layer(0, 0, shown);
        drawTopBar(1.0f, trail_);
    }
    // not faded with the page: faded, its keys lose their depth
    drawHints(hintBar_, hints(), accent.solid);
}

} // namespace tz::scenes
