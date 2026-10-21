#include "scenes/frame.hpp"

#include "app/app.hpp"
#include "domain/installs.hpp"
#include "gfx/renderer.hpp"
#include "gfx/text.hpp"
#include "scenes/images.hpp"
#include "ui/widgets.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace tz::scenes
{

using namespace gfx;

void Backdrop::set(std::shared_ptr<Picture> blur, Color accent)
{
    if (blur == current_.picture && accent == current_.accent)
        return;
    previous_ = current_;
    current_ = Layer{std::move(blur), accent};
    fade_ = 0;
}

void Backdrop::tick(float dt)
{
    fade_ = std::min(1.0f, fade_ + dt * 3.5f);
    if (fade_ >= 1)
        previous_ = {};
}

void Backdrop::drawLayer(const Layer& layer, float alpha) const
{
    if (layer.picture)
        layer.picture->drawCover(0, 0, metric::kWidth, metric::kHeight, 0, alpha);
    else
        gradientV(0, 0, metric::kWidth, metric::kHeight, withAlpha(mix(color::kBackground, layer.accent, 0.22f), alpha),
                  withAlpha(color::kBackground, alpha));
}

void Backdrop::draw(float dim) const
{
    fill(color::kBackground);
    if (fade_ < 1)
        drawLayer(previous_, 1.0f);
    drawLayer(current_, fade_);
    fill(withAlpha(color::kBackground, dim));
}

float drawTopBar(float alpha)
{
    const float height = metric::kTopBarHeight;
    const float middle = height / 2;
    const Layer layer(0, 0, alpha);
    gradientV(0, 0, metric::kWidth, height + 24, rgba(12, 10, 9, 200), rgba(12, 10, 9, 0));
    brand(Brand::Logo, 32, std::round(middle - brandHeight(Brand::Logo) / 2));

    const App::Status& status = App::get().status();
    const float iconTop = std::round(middle - metric::kIcon / 2);
    float x = metric::kWidth - 32;

    // the battery: Lucide's outline, filled to the charge level, and the percentage
    char percent[8];
    std::snprintf(percent, sizeof(percent), "%d%%", status.battery);
    x -= textWidth(percent, metric::kTextSmall);
    drawText(percent, x, textTop(middle, metric::kTextSmall), metric::kTextSmall, color::kText);
    x -= 6 + metric::kIcon;
    if (status.charging)
    {
        icon(Icon::BatteryCharging, x, iconTop, metric::kIcon, color::kText);
    }
    else
    {
        icon(Icon::Battery, x, iconTop, metric::kIcon, color::kText);
        // the outline's inside is from (4.5, 9.5) of its 24-unit grid, 11 by 5
        const float unit = metric::kIcon / 24.0f;
        const float level = std::clamp(status.battery / 100.0f, 0.0f, 1.0f);
        rect(x + 4.5f * unit, iconTop + 9.5f * unit, std::round(11.0f * unit * level), 5.0f * unit,
             status.battery <= 15 ? color::kDanger : color::kText);
    }

    x -= 20 + metric::kIcon;
    icon(status.online ? Icon::Wifi : Icon::WifiOff, x, iconTop, metric::kIcon,
         status.online ? color::kText : color::kTextFaint);

    char clock[8];
    std::snprintf(clock, sizeof(clock), "%02d:%02d", status.hour, status.minute);
    x -= 20;
    drawText(clock, x, textTop(middle, metric::kTextNormal), metric::kTextNormal, color::kText, Align::Right);
    return x - textWidth(clock, metric::kTextNormal);
}

Accent accentOf(themezer::Kind kind)
{
    switch (kind)
    {
    case themezer::Kind::Packs:
        return accent::kPacks;
    case themezer::Kind::Splashes:
        return accent::kSplashes;
    case themezer::Kind::Themes:
        break;
    }
    return accent::kThemes;
}

Icon iconOf(themezer::Kind kind)
{
    switch (kind)
    {
    case themezer::Kind::Packs:
        return Icon::Pack;
    case themezer::Kind::Splashes:
        return Icon::Splash;
    case themezer::Kind::Themes:
        break;
    }
    return Icon::Theme;
}

Icon sortIcon(themezer::Sort sort)
{
    switch (sort)
    {
    case themezer::Sort::Trending:
        return Icon::Flame;
    case themezer::Sort::Created:
        return Icon::CalendarPlus;
    case themezer::Sort::Updated:
        return Icon::CalendarCog;
    case themezer::Sort::Downloads:
        return Icon::Download;
    default:
        break;
    }
    return Icon::Bookmark;
}

Icon targetIcon(int target)
{
    // in the API's order: the Home Menu, the lockscreen, all apps, settings, player selection, the user page, news
    constexpr Icon kIcons[themezer::kTargetCount] = {Icon::House,  Icon::Lock, Icon::LayoutGrid, Icon::Settings,
                                                     Icon::Users,  Icon::User, Icon::Newspaper};
    return target >= 0 && target < themezer::kTargetCount ? kIcons[target] : Icon::Layers;
}

std::shared_ptr<Picture> placeholderOf(const themezer::Item& item)
{
    return images().placeholder(item.preview.thumbHash);
}

namespace card
{

namespace
{
constexpr float kInset = 10;
constexpr float kPreviewRadius = 10;

// A small pill over the preview's bottom left: what part of the HOME Menu, how many themes.
void chip(std::string_view text, float x, float bottom)
{
    const float height = 32;
    const float width = textWidth(text, metric::kTextSmall) + 24;
    roundedRect(x, bottom - height, width, height, height / 2, rgba(12, 10, 9, 190));
    drawText(text, x + 12, textTop(bottom - height / 2, metric::kTextSmall), metric::kTextSmall, color::kText);
}
} // namespace

void highlight(float x, float y, float w, float h, bool down, Accent accent)
{
    // a little bigger than the card, so the ring is round it
    const float top = ui::drawKey(x - 6, y - 6, w + 12, h + 12, color::kSurfaceRaised, down, metric::kRadius + 6);
    roundedOutline(x - 6, top, w + 12, h + 12, metric::kRadius + 6, 4, accent.solid);
}

void draw(const themezer::Item& item, float x, float y, float w, float h, bool selected, Accent accent,
          bool showTarget)
{
    if (!selected)
        roundedRect(x, y, w, h, metric::kRadius, color::kFrosted);
    const float previewW = w - 2 * kInset;
    const float previewH = std::round(previewW * 9 / 16);
    const float px = x + kInset;
    const float py = y + kInset;

    std::shared_ptr<Picture> picture = images().get(item.preview.thumb);
    if (!picture)
        picture = placeholderOf(item);
    if (picture)
    {
        picture->drawCover(px, py, previewW, previewH, kPreviewRadius);
    }
    else
    {
        roundedRect(px, py, previewW, previewH, kPreviewRadius, color::kSurfaceRaised);
        const Icon which = iconOf(item.kind);
        icon(which, px + (previewW - metric::kIcon) / 2, py + (previewH - metric::kIcon) / 2, metric::kIcon,
             color::kTextFaint);
    }
    if (installs::isQueued(item))
    {
        // over the preview's top right: queued to install
        const float height = 32;
        const float width = metric::kIcon * 0.75f + 8 + textWidth("Queued", metric::kTextSmall) + 24;
        const float chipX = px + previewW - 8 - width;
        roundedRect(chipX, py + 8, width, height, height / 2, accent.solid);
        icon(Icon::Check, chipX + 12, py + 8 + (height - metric::kIcon * 0.75f) / 2, metric::kIcon * 0.75f,
             color::kOnAccent);
        drawText("Queued", chipX + 12 + metric::kIcon * 0.75f + 8, textTop(py + 8 + height / 2, metric::kTextSmall),
                 metric::kTextSmall, color::kOnAccent);
    }
    if (item.kind == themezer::Kind::Packs)
    {
        char text[32];
        std::snprintf(text, sizeof(text), item.themes.size() == 1 ? "%zu theme" : "%zu themes", item.themes.size());
        chip(text, px + 8, py + previewH - 8);
    }
    else if (showTarget && item.kind == themezer::Kind::Themes)
    {
        chip(themezer::targetLabel(item.target), px + 8, py + previewH - 8);
    }

    const float textLeft = x + kInset + 4;
    const float textWidthMax = w - 2 * kInset - 8;
    const float textY = py + previewH + 10;
    drawText(item.name, textLeft, textY, metric::kTextNormal, selected ? color::kText : color::kText, Align::Left,
             textWidthMax);
    drawText(item.creator, textLeft, textY + lineHeight(metric::kTextNormal) + 2, metric::kTextSmall,
             selected ? accent.light : color::kTextDim, Align::Left, textWidthMax);
}

} // namespace card

void drawEmptyState(Icon which, Color iconColor, std::string_view title, std::string_view message, float top,
                    float bottom)
{
    const float width = 760;
    const auto lines = wrap(message, width, metric::kTextNormal, 4);
    const float messageHeight = static_cast<float>(lines.size()) * lineHeight(metric::kTextNormal);
    const float height = metric::kIconLarge + 24 + lineHeight(metric::kTextLarge) + 12 + messageHeight;
    float y = std::round((top + bottom - height) / 2);
    icon(which, (metric::kWidth - metric::kIconLarge) / 2, y, metric::kIconLarge, iconColor);
    y += metric::kIconLarge + 24;
    drawText(title, metric::kWidth / 2, y, metric::kTextLarge, color::kText, Align::Center, width);
    y += lineHeight(metric::kTextLarge) + 12;
    drawWrapped(message, (metric::kWidth - width) / 2, y, width, metric::kTextNormal, color::kTextDim, 4,
                Align::Center);
}

} // namespace tz::scenes
