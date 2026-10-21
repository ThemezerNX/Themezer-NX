#include "scenes/pack.hpp"

#include "app/app.hpp"
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

PackScene::PackScene(themezer::Item pack) : pack_(std::move(pack))
{
    sfx::play(sfx::Sound::Open);
    backdrop_.set(placeholderOf(pack_), accentOf(Kind::Packs).solid);
    grid_.setBounds(kGridX, kTop + 6, metric::kWidth - kGridX - kSide, metric::kHeight - metric::kHintBarHeight - kTop - 6);
    const float cellWidth = (metric::kWidth - kGridX - kSide - kGap * (kColumns - 1)) / kColumns;
    grid_.setLayout(kColumns, std::round((cellWidth - 20) * 9 / 16) + 20 + card::kTextHeight, kGap);
    grid_.setCount(static_cast<int>(pack_.themes.size()));
}

std::vector<ui::Hint> PackScene::hints() const
{
    std::vector<ui::Hint> hints{{ui::key::kB, "Back"}, {ui::key::kX, "More"}};
    hints.push_back({ui::key::kY, canInstall() ? "Install all" : "Download all"});
    if (!pack_.themes.empty())
        hints.push_back({ui::key::kA, "Open"});
    return hints;
}

void PackScene::openMore()
{
    const bool installable = canInstall();
    const themezer::Item pack = pack_;
    std::vector<ui::Action> actions;
    actions.push_back({Icon::Install, "Install all and keep them",
                       "One theme for each part of the HOME Menu, saved and queued", installable, false,
                       [pack] { installPack(pack, true); }, nullptr, ui::key::kY});
    actions.push_back({Icon::Install, "Install all without keeping them",
                       "Queued to install, then deleted at the next start", installable, false,
                       [pack] { installPack(pack, false); }});
    actions.push_back({Icon::Download, "Download all", "Every theme saved in /Themes/ThemezerNX, not installed", true,
                       false, [pack] { downloadPack(pack); }});
    if (!pack_.preview.hd.empty())
        actions.push_back({Icon::Eye, "View the collage", "The pack's picture, filling the screen", true, false, [pack] {
                               App::get().scenes().emplace<PictureScene>(pack.preview.hd, pack.preview.thumb,
                                                                         pack.preview.thumbHash);
                           }});
    App::get().scenes().emplace<ui::ActionSheet>(pack_.name, std::move(actions), accentOf(Kind::Packs).solid);
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
    hintBar_.update(input, shown);
    if (input.pressed(ui::key::kB))
    {
        sfx::play(sfx::Sound::Back);
        leaving_ = true;
        return;
    }
    if (input.pressed(ui::key::kY))
    {
        sfx::play(sfx::Sound::Confirm);
        if (canInstall())
            installPack(pack_, true);
        else
            downloadPack(pack_);
        return;
    }
    if (input.pressed(ui::key::kX))
    {
        openMore();
        return;
    }
    if (input.pressed(ui::key::kPlus))
    {
        openMenu();
        return;
    }
    if (input.pressed(ui::key::kMinus))
    {
        openQueue();
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
        shadow(kSide, kTop, kColumnW, collageH, 16, 24);
        std::shared_ptr<Picture> picture = images().get(pack_.preview.thumb);
        if (!picture)
            picture = placeholderOf(pack_);
        if (picture)
            picture->drawCover(kSide, kTop, kColumnW, collageH, 16);
        else
            roundedRect(kSide, kTop, kColumnW, collageH, 16, color::kSurfaceRaised);
        float y = kTop + collageH + 24;
        y += drawWrapped(pack_.name, kSide, y, kColumnW, metric::kTextLarge, color::kText, 2);
        drawText("by " + pack_.creator, kSide, y + 2, metric::kTextNormal, accent.light, Align::Left, kColumnW);
        y += lineHeight(metric::kTextNormal) + 16;
        const float iconSize = metric::kIcon * 0.75f;
        const std::string count = std::to_string(pack_.themes.size()) + (pack_.themes.size() == 1 ? " theme" : " themes");
        icon(Icon::Theme, kSide, std::round(y + 22 - iconSize / 2), iconSize, accent.light);
        drawText(count, kSide + iconSize + 14, textTop(y + 22, metric::kTextSmall), metric::kTextSmall, color::kText);
        y += 44;
        drawFacts(pack_, kSide, y, kColumnW, accent);
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
        drawTopBar();
        hintBar_.draw(hints(), 1.0f, accent.solid);
    }
}

} // namespace tz::scenes
