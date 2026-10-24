#include "scenes/frame.hpp"

#include "app/app.hpp"
#include "domain/installed.hpp"
#include "domain/installs.hpp"
#include "services/local.hpp"
#include "gfx/renderer.hpp"
#include "gfx/text.hpp"
#include "scenes/images.hpp"
#include "sys/audio.hpp"
#include "ui/widgets.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace tz::scenes
{

using namespace gfx;

void Backdrop::set(std::shared_ptr<Picture> blur, Color accent)
{
    // no picture, most likely not in yet: what's shown stays a moment, so the picture fades in from it, not from dark
    if (!blur && current_.picture)
    {
        if (!waiting_)
            waited_ = 0;
        waiting_ = true;
        waitingAccent_ = accent;
        return;
    }
    waiting_ = false;
    if (blur == current_.picture && accent == current_.accent)
        return;
    previous_ = current_;
    current_ = Layer{std::move(blur), accent};
    fade_ = 0;
}

void Backdrop::tick(float dt)
{
    if (waiting_ && (waited_ += dt) > 0.6f)
    {
        // it has none after all
        waiting_ = false;
        previous_ = current_;
        current_ = Layer{nullptr, waitingAccent_};
        fade_ = 0;
    }
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
        // the outline is 16 by 12 at (2, 6) of its 24-unit grid with a stroke of 2: inside, from (3, 7) to (17, 17);
        // the fill keeps 1.5 from it all round, on whole pixels
        const float unit = metric::kIcon / 24.0f;
        const float level = std::clamp(status.battery / 100.0f, 0.0f, 1.0f);
        const float left = std::round(x + 4.5f * unit);
        const float top = std::round(iconTop + 8.5f * unit);
        rect(left, top, std::round(11.0f * unit * level), std::round(iconTop + 15.5f * unit) - top,
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

Badge badgeOf(const themezer::Item& item)
{
    if (installs::isQueued(item))
        return Badge::Queued;
    const int slot = installs::slotOf(item);
    if (slot >= 0 && !item.id.empty())
    {
        const installed::Part& part = installed::part(slot);
        if (part.themezerId == item.id)
            return Badge::Installed;
        // a splash is told by its file: Hekate's is a copy of one on the SD card
        if (slot == installs::kSplashSlot && part.custom)
        {
            for (const library::Entry& entry : local::Library::get().entries())
            {
                if (entry.kind == library::Kind::Splash && entry.themezerId == item.id &&
                    entry.fingerprint == part.fingerprint)
                    return Badge::Installed;
            }
        }
    }
    if (local::Library::get().has(item))
        return Badge::Downloaded;
    return Badge::None;
}

Badge badgeOf(const library::Entry& entry)
{
    const int slot = entry.kind == library::Kind::Splash ? installs::kSplashSlot : entry.target;
    if (const installs::Queued* queued = installs::queued(slot); queued && queued->path == entry.path)
        return Badge::Queued;
    const installed::Part& part = installed::part(slot);
    if (part.custom && (part.source == entry.path || (!entry.themezerId.empty() && part.themezerId == entry.themezerId) ||
                        (entry.kind == library::Kind::Splash && entry.fingerprint == part.fingerprint)))
        return Badge::Installed;
    return Badge::None;
}

Badge badgeOf(const local::Pack& pack)
{
    Badge badge = Badge::None;
    const auto& entries = local::Library::get().entries();
    for (size_t index : pack.themes)
    {
        if (index < entries.size())
            badge = std::max(badge, badgeOf(entries[index]));
    }
    return badge;
}

const char* badgeLabel(Badge badge)
{
    switch (badge)
    {
    case Badge::Queued:
        return "Queued";
    case Badge::Installed:
        return "Installed";
    case Badge::Downloaded:
        return "Downloaded";
    case Badge::None:
        break;
    }
    return "";
}

Icon badgeIcon(Badge badge)
{
    switch (badge)
    {
    case Badge::Queued:
        return Icon::Queue;
    case Badge::Installed:
        return Icon::MonitorCheck;
    default:
        break;
    }
    return Icon::Check;
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

// Over the preview's top right: queued and installed in the accent, downloaded quieter.
void badge(Badge which, float right, float top, Accent accent)
{
    if (which == Badge::None)
        return;
    const float height = 32;
    const float iconSize = metric::kIcon * 0.75f;
    const char* label = badgeLabel(which);
    const float width = iconSize + 8 + textWidth(label, metric::kTextSmall) + 24;
    const bool strong = which != Badge::Downloaded;
    const Color ink = strong ? color::kOnAccent : color::kText;
    roundedRect(right - width, top, width, height, height / 2, strong ? accent.solid : rgba(12, 10, 9, 190));
    icon(badgeIcon(which), right - width + 12, top + (height - iconSize) / 2, iconSize, strong ? ink : accent.light);
    drawText(label, right - width + 12 + iconSize + 8, textTop(top + height / 2, metric::kTextSmall), metric::kTextSmall,
             ink);
}
} // namespace

void highlight(float x, float y, float w, float h, bool down, Accent accent)
{
    // a little bigger than the card, so the ring is round it
    const float top = ui::drawKey(x - 6, y - 6, w + 12, h + 12, color::kSurfaceRaised, down, metric::kRadius + 6);
    roundedOutline(x - 6, top, w + 12, h + 12, metric::kRadius + 6, 4, accent.solid);
}

void draw(const Content& content, float x, float y, float w, float h, bool selected, Accent accent)
{
    if (!selected)
        roundedRect(x, y, w, h, metric::kRadius, color::kFrosted);
    const float previewW = w - 2 * kInset;
    const float previewH = std::round(previewW * 9 / 16);
    const float px = x + kInset;
    const float py = y + kInset;
    if (content.picture)
    {
        content.picture->drawCover(px, py, previewW, previewH, kPreviewRadius);
    }
    else
    {
        roundedRect(px, py, previewW, previewH, kPreviewRadius, color::kSurfaceRaised);
        icon(content.icon, px + (previewW - metric::kIcon) / 2, py + (previewH - metric::kIcon) / 2, metric::kIcon,
             color::kTextFaint);
    }
    badge(content.badge, px + previewW - 8, py + 8, accent);
    if (!content.chip.empty())
        chip(content.chip, px + 8, py + previewH - 8);

    const float textLeft = x + kInset + 4;
    const float textWidthMax = w - 2 * kInset - 8;
    const float textY = py + previewH + 10;
    drawText(content.name, textLeft, textY, metric::kTextNormal, color::kText, Align::Left, textWidthMax);
    drawText(content.creator, textLeft, textY + lineHeight(metric::kTextNormal) + 2, metric::kTextSmall,
             selected ? accent.light : color::kTextDim, Align::Left, textWidthMax);
}

void draw(const themezer::Item& item, float x, float y, float w, float h, bool selected, Accent accent,
          bool showTarget)
{
    Content content;
    content.name = item.name;
    content.creator = item.creator;
    content.picture = images().get(item.preview.thumb);
    if (!content.picture)
        content.picture = placeholderOf(item);
    content.icon = iconOf(item.kind);
    content.badge = badgeOf(item);
    if (item.kind == themezer::Kind::Packs)
    {
        char text[32];
        std::snprintf(text, sizeof(text), item.themes.size() == 1 ? "%zu theme" : "%zu themes", item.themes.size());
        content.chip = text;
    }
    else if (showTarget && item.kind == themezer::Kind::Themes)
    {
        content.chip = themezer::targetLabel(item.target);
    }
    draw(content, x, y, w, h, selected, accent);
}

} // namespace card

std::shared_ptr<Picture> localPicture(const library::Entry& entry, int width)
{
    // by path and size, so a file replaced by another isn't shown as the old one
    const std::string key = "sd:" + std::to_string(width) + ":" + std::to_string(entry.size) + ":" + entry.path;
    return images().load(key, [entry, width] { return library::readPreview(entry, width); });
}

std::shared_ptr<Picture> localBackdrop(const library::Entry& entry)
{
    const std::string key = "sd-blur:" + std::to_string(entry.size) + ":" + entry.path;
    return images().load(key, [entry] {
        // its ThumbHash, as Themezer's pictures have, made from it at a size ThumbHash needs no more than
        SDL_Surface* small = library::readPreview(entry, 100);
        SDL_Surface* blur = thumbHashOf(small);
        SDL_FreeSurface(small);
        return blur;
    });
}

float filterChip(Icon which, const std::string& text, bool highlighted, Accent accent, float x, float top, float height,
                 float right)
{
    const float iconSize = metric::kIcon * 0.75f;
    const float room = right - x - (iconSize + 10 + 32);
    if (room < 40)
        return 0;
    const float middle = top + height / 2;
    const std::string shown = ellipsize(text, room, metric::kTextSmall);
    const float width = iconSize + 10 + textWidth(shown, metric::kTextSmall) + 32;
    roundedRect(x, top, width, height, height / 2, highlighted ? withAlpha(accent.solid, 0.35f) : color::kSurface);
    icon(which, x + 16, std::round(middle - iconSize / 2), iconSize, highlighted ? accent.light : color::kTextDim);
    drawText(shown, x + 16 + iconSize + 10, textTop(middle, metric::kTextSmall), metric::kTextSmall, color::kText);
    return width;
}

int chipKeys(ui::Input& input, int& focus, int count)
{
    focus = std::min(focus, count - 1);
    if (focus < 0)
        return -1;
    if (input.pressed(ui::key::kDown) || input.pressed(ui::key::kB))
    {
        focus = -1;
        sfx::play(sfx::Sound::Cursor);
    }
    else if (input.repeated(ui::key::kLeft) && focus > 0)
    {
        --focus;
        sfx::play(sfx::Sound::Cursor);
    }
    else if (input.repeated(ui::key::kRight) && focus + 1 < count)
    {
        ++focus;
        sfx::play(sfx::Sound::Cursor);
    }
    else if (input.pressed(ui::key::kA))
    {
        sfx::play(sfx::Sound::Confirm);
        return focus;
    }
    return -1;
}

void chipRing(float x, float top, float width, float height, Accent accent)
{
    roundedOutline(x - 5, top - 5, width + 10, height + 10, height / 2 + 5, 3, accent.solid);
}

std::string thousands(int number)
{
    std::string digits = std::to_string(number);
    for (int i = static_cast<int>(digits.size()) - 3; i > 0; i -= 3)
        digits.insert(static_cast<size_t>(i), ",");
    return digits;
}

std::string megabytes(uint64_t bytes)
{
    char text[32];
    std::snprintf(text, sizeof(text), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    return text;
}

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
