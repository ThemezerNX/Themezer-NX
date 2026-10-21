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
}

const library::Entry* LocalDetailsScene::entry() const
{
    return local::Library::get().find(path_);
}

void LocalDetailsScene::leave()
{
    leaving_ = true;
}

std::vector<ui::Hint> LocalDetailsScene::hints() const
{
    return {{ui::key::kB, "Back"}, {ui::key::kY, "View"}, {ui::key::kX, "More"}, {ui::key::kA, "Install"}};
}

void LocalDetailsScene::openMore()
{
    const library::Entry* found = entry();
    if (!found)
        return;
    const library::Entry current = *found;
    const int slot = current.kind == Kind::Splash ? installs::kSplashSlot : current.target;
    std::vector<ui::Action> actions;
    if (const installs::Queued* queued = installs::queued(slot); queued && queued->path == current.path)
        actions.push_back({Icon::Trash, "Take it out of the queue", "", true, false, [slot] {
                               installs::unqueue(slot);
                               sfx::play(sfx::Sound::Back);
                           }});
    if (badgeOf(current) == Badge::Installed)
        actions.push_back({Icon::Trash, "Uninstall it", "", true, true, [slot] { uninstallPart(slot); }});
    if (!current.themezerId.empty())
        actions.push_back({Icon::Globe, "Show it on Themezer", "", true, false,
                           [quickId = quickIdOf(current)] { showOnThemezer(quickId); }});
    // the page goes by itself when the file has
    actions.push_back({Icon::Trash, "Delete it", "", true, true, [current] { deleteLocal(current); }});
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
    hintBar_.update(input, shown);
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
        sfx::play(sfx::Sound::Confirm);
        installLocal(*found);
    }
    else if (input.pressed(ui::key::kX))
    {
        openMore();
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
        shadow(kPreviewX, kPreviewY, kPreviewW, kPreviewH, 20, 28);
        if (auto picture = localPicture(entry, static_cast<int>(kPreviewW)))
        {
            picture->drawCover(kPreviewX, kPreviewY, kPreviewW, kPreviewH, 20);
        }
        else
        {
            roundedRect(kPreviewX, kPreviewY, kPreviewW, kPreviewH, 20, color::kSurfaceRaised);
            icon(entry.kind == Kind::Splash ? Icon::Splash : Icon::Theme, kPreviewX + (kPreviewW - metric::kIconLarge) / 2,
                 kPreviewY + (kPreviewH - metric::kIconLarge) / 2, metric::kIconLarge, color::kTextFaint);
        }
        // the file it's in, under the picture
        drawText(entry.fileName, kPreviewX + 4, kPreviewY + kPreviewH + 24, metric::kTextSmall, color::kTextFaint,
                 Align::Left, kPreviewW - 8);
    }
    {
        const Layer layer((1 - shown) * 40, 0, shown);
        float y = kPreviewY;
        const float chipHeight = 40;
        const float iconSize = metric::kIcon * 0.75f;
        const std::string kind = entry.kind == Kind::Splash
                                     ? std::string("Hekate splash")
                                     : std::string("Theme · ") + (entry.target >= 0 ? themezer::targetLabel(entry.target)
                                                                                    : "part not known");
        const float chipWidth = iconSize + 10 + textWidth(kind, metric::kTextSmall) + 32;
        roundedRect(kColumnX, y, chipWidth, chipHeight, chipHeight / 2, withAlpha(accent.solid, 0.35f));
        icon(entry.kind == Kind::Splash ? Icon::Splash : targetIcon(entry.target), kColumnX + 16,
             std::round(y + (chipHeight - iconSize) / 2), iconSize, accent.light);
        drawText(kind, kColumnX + 16 + iconSize + 10, textTop(y + chipHeight / 2, metric::kTextSmall),
                 metric::kTextSmall, color::kText);
        y += chipHeight + 20;
        y += drawWrapped(entry.name, kColumnX, y, kColumnW, metric::kTextTitle, color::kText, 3);
        y += 4;
        if (!entry.author.empty())
        {
            drawText("by " + entry.author, kColumnX, y, metric::kTextNormal, accent.light, Align::Left, kColumnW);
            y += lineHeight(metric::kTextNormal);
        }
        y += 12;
        // installed now, or queued to be
        if (const Badge badge = badgeOf(entry); badge != Badge::None)
        {
            const float middle = y + 16;
            icon(badgeIcon(badge), kColumnX, std::round(middle - iconSize / 2), iconSize, accent.light);
            drawText(badge == Badge::Queued ? "Queued to install" : "Installed now", kColumnX + iconSize + 12,
                     textTop(middle, metric::kTextSmall), metric::kTextSmall, color::kText);
            y += 32;
        }
        y += 12;
        rect(kColumnX, y, kColumnW, 1, color::kDivider);
        y += 16;
        const float row = 44;
        const auto fact = [&](Icon which, const std::string& text) {
            if (text.empty())
                return;
            const float middle = y + row / 2;
            icon(which, kColumnX, std::round(middle - iconSize / 2), iconSize, accent.light);
            drawText(text, kColumnX + iconSize + 14, textTop(middle, metric::kTextSmall), metric::kTextSmall,
                     color::kText, Align::Left, kColumnW - iconSize - 14);
            y += row;
        };
        if (!entry.folder.empty())
            fact(Icon::Folder, entry.folder);
        if (!entry.layout.empty())
            fact(Icon::LayoutGrid, entry.layout);
        fact(Icon::SdCard, megabytes(entry.size));
        if (!entry.themezerId.empty())
            fact(Icon::Hash, "Quick ID " + quickIdOf(entry));
    }
    {
        const Layer layer(0, 0, shown);
        drawTopBar();
        hintBar_.draw(hints(), 1.0f, accent.solid);
    }
}

} // namespace tz::scenes
