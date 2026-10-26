#include "scenes/frame.hpp"

#include "app/app.hpp"
#include "domain/installed.hpp"
#include "domain/installs.hpp"
#include "services/item_updates.hpp"
#include "services/local.hpp"
#include "services/updates.hpp"
#include "gfx/renderer.hpp"
#include "gfx/text.hpp"
#include "scenes/actions.hpp"
#include "scenes/images.hpp"
#include "sys/audio.hpp"
#include "ui/overlays.hpp"
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

std::vector<ui::Crumb> currentTrail()
{
    return App::get().scenes().trail();
}

ui::Crumb modeCrumb(bool online)
{
    // "Online", not "Themezer": the logo next to it says that; and in white, the colours being the sections'
    return {ui::Crumb::Kind::Mode, online ? "Online" : "SD card", online ? 1 : 0, online ? Icon::Globe : Icon::SdCard,
            Accent{color::kText, color::kText}};
}

ui::Crumb sectionCrumb(themezer::Kind section)
{
    const char* name = section == themezer::Kind::Packs ? "Packs" : section == themezer::Kind::Themes ? "Themes" : "Splashes";
    return {ui::Crumb::Kind::Section, name, static_cast<int>(section), iconOf(section), accentOf(section)};
}

ui::Crumb pageCrumb(std::string name)
{
    return {ui::Crumb::Kind::Page, std::move(name)};
}

float pillsLeft()
{
    return 32 + brandWidth(Brand::Logo) + 20;
}

float modePillWidth(const ui::Crumb& mode, ui::Keys key)
{
    const float glyph = key ? ui::buttonWidth(key, metric::kButtonGlyph) + 12 : 0;
    return std::round(16 + glyph + metric::kIcon + 10 + textWidth(mode.text, metric::kTextNormal) + 20);
}

float drawModePill(const ui::Crumb& mode, float x, float y, float width, ui::Keys key, bool down)
{
    if (width <= 0)
        width = modePillWidth(mode, key);
    const float middle = y + kPillHeight / 2;
    roundedRect(x, y, width, kPillHeight, kPillHeight / 2, color::kSurface);
    float left = x + 16;
    if (key)
        left += ui::drawButton(key, left, textTop(middle, metric::kButtonGlyph) + (down ? ui::kKeyLip : 0),
                               metric::kButtonGlyph, down ? color::kText : color::kTextDim) +
                12;
    icon(mode.icon, left, std::round(middle - metric::kIcon / 2), metric::kIcon, mode.accent.light);
    drawText(mode.text, left + metric::kIcon + 10, textTop(middle, metric::kTextNormal), metric::kTextNormal,
             mode.accent.light);
    return x + width;
}

float drawSectionPill(const ui::Crumb& section, float x, float y)
{
    // the tab bar with only the selected tab: its highlight inside the bar's pill
    const float inner = std::round(16 + metric::kIcon + 10 + textWidth(section.text, metric::kTextNormal) + 16);
    const float width = inner + 12;
    const float middle = y + kPillHeight / 2;
    roundedRect(x, y, width, kPillHeight, kPillHeight / 2, color::kSurface);
    roundedRect(x + 6, y + 6, inner, kPillHeight - 12, (kPillHeight - 12) / 2, withAlpha(section.accent.solid, 0.4f));
    icon(section.icon, x + 6 + 16, std::round(middle - metric::kIcon / 2), metric::kIcon, section.accent.light);
    drawText(section.text, x + 6 + 16 + metric::kIcon + 10, textTop(middle, metric::kTextNormal), metric::kTextNormal,
             section.accent.light);
    return x + width;
}

namespace
{
// Where the last frame drew a page's mode and section pills, for a tap; and whether - is down, to sink its key.
struct Spot
{
    float x = 0, y = 0, w = 0, h = 0;
    bool contains(float px, float py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};
Spot modeSpot, sectionSpot;
bool minusDown = false;

// The trail from x, before right: the pills, then the pages; the pages that don't fit go from the first, as "…".
void drawTrail(const std::vector<ui::Crumb>& trail, float x, float right)
{
    const float y = std::round((metric::kTopBarHeight - kPillHeight) / 2);
    const float middle = y + kPillHeight / 2;
    std::vector<std::string> pages;
    for (const ui::Crumb& crumb : trail)
    {
        const float start = x;
        if (crumb.kind == ui::Crumb::Kind::Mode)
        {
            // with its key, as on the home screen: - goes there
            x = drawModePill(crumb, x, y, 0, ui::key::kMinus, minusDown) + 12;
            modeSpot = {start, y, x - 12 - start, kPillHeight};
        }
        else if (crumb.kind == ui::Crumb::Kind::Section)
        {
            x = drawSectionPill(crumb, x, y) + 12;
            sectionSpot = {start, y, x - 12 - start, kPillHeight};
        }
        else
        {
            pages.push_back(crumb.text);
        }
    }
    if (pages.empty())
        return;
    constexpr const char* kSeparator = "›  ";
    const float size = metric::kTextNormal;
    const float top = textTop(middle, size);
    x += 4;
    size_t first = 0;
    const auto width = [&](size_t from) {
        float total = from > 0 ? textWidth(std::string(kSeparator) + "…  ", size) : 0;
        for (size_t i = from; i < pages.size(); ++i)
            total += textWidth(kSeparator, size) + textWidth(pages[i], size) + 12;
        return total;
    };
    while (first + 1 < pages.size() && width(first) > right - x)
        ++first;
    if (first > 0)
    {
        x += drawText(kSeparator, x, top, size, color::kTextFaint);
        x += drawText("…  ", x, top, size, color::kTextDim);
    }
    for (size_t i = first; i < pages.size(); ++i)
    {
        x += drawText(kSeparator, x, top, size, color::kTextFaint);
        x += drawText(ellipsize(pages[i], right - x, size), x, top, size, color::kTextDim) + 12;
    }
}
} // namespace

bool trailKeys(ui::Input& input, const std::vector<ui::Crumb>& trail)
{
    const ui::Crumb* mode = nullptr;
    const ui::Crumb* section = nullptr;
    for (const ui::Crumb& crumb : trail)
    {
        if (crumb.kind == ui::Crumb::Kind::Mode)
            mode = &crumb;
        else if (crumb.kind == ui::Crumb::Kind::Section)
            section = &crumb;
    }
    if (!mode || !section)
        return false;
    minusDown = (input.held & ui::key::kMinus) != 0;
    const auto tapped = [&](const Spot& spot) {
        return input.tap && spot.contains(input.x, input.y) && spot.contains(input.startX, input.startY);
    };
    const bool online = mode->value != 0;
    const auto kind = static_cast<themezer::Kind>(section->value);
    if (input.pressed(ui::key::kMinus) || tapped(modeSpot))
    {
        input.consumeTouch();
        sfx::play(sfx::Sound::Tab);
        goHome(!online, kind);
        return true;
    }
    if (tapped(sectionSpot))
    {
        input.consumeTouch();
        sfx::play(sfx::Sound::Back);
        goHome(online, kind);
        return true;
    }
    return false;
}

float drawTopBar(float alpha, const std::vector<ui::Crumb>& trail)
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
    const float statusLeft = x - textWidth(clock, metric::kTextNormal);
    if (!trail.empty())
        drawTrail(trail, pillsLeft(), statusLeft - 32);
    return statusLeft;
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
    // a pack, as the SD card's: its themes' strongest, Queued or Installed; Downloaded when all of them are
    if (item.kind == themezer::Kind::Packs)
    {
        Badge strongest = Badge::None;
        for (const themezer::Item& theme : item.themes)
            strongest = std::max(strongest, badgeOf(theme));
        if (strongest > Badge::Downloaded)
            return strongest;
        return local::Library::get().has(item) ? Badge::Downloaded : Badge::None;
    }
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

bool hasUpdate(const library::Entry& entry)
{
    const auto kind = entry.kind == library::Kind::Splash ? themezer::Kind::Splashes : themezer::Kind::Themes;
    return itemupdates::find(kind, entry.themezerId) != nullptr;
}

bool hasUpdate(const local::Pack& pack)
{
    return itemupdates::find(themezer::Kind::Packs, pack.themezerId) != nullptr;
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

float badgeWidth(Badge which)
{
    return metric::kIcon * 0.75f + 8 + textWidth(badgeLabel(which), metric::kTextSmall) + 24;
}

// Over the preview's top right: queued and installed in the accent, downloaded quieter.
void badge(Badge which, float right, float top, Accent accent)
{
    if (which == Badge::None)
        return;
    const float height = 32;
    const float iconSize = metric::kIcon * 0.75f;
    const char* label = badgeLabel(which);
    const float width = badgeWidth(which);
    const bool strong = which != Badge::Downloaded;
    const Color ink = strong ? color::kOnAccent : color::kText;
    roundedRect(right - width, top, width, height, height / 2, strong ? accent.solid : rgba(12, 10, 9, 190));
    icon(badgeIcon(which), right - width + 12, top + (height - iconSize) / 2, iconSize, strong ? ink : accent.light);
    drawText(label, right - width + 12 + iconSize + 8, textTop(top + height / 2, metric::kTextSmall), metric::kTextSmall,
             ink);
}
// Over the preview's top left: there is a newer version on Themezer. Only its icon where the badge leaves no room for
// the word (room is the width it may take).
void updateMarker(float x, float top, float room, Accent accent)
{
    const float height = 32;
    const float iconSize = metric::kIcon * 0.75f;
    const float whole = iconSize + 8 + textWidth("Update", metric::kTextSmall) + 24;
    const bool word = whole <= room;
    const float width = word ? whole : height;
    roundedRect(x, top, width, height, height / 2, rgba(12, 10, 9, 190));
    icon(Icon::Download, x + (word ? 12 : (height - iconSize) / 2), top + (height - iconSize) / 2, iconSize,
         accent.light);
    if (word)
        drawText("Update", x + 12 + iconSize + 8, textTop(top + height / 2, metric::kTextSmall), metric::kTextSmall,
                 color::kText);
}
} // namespace

void overlay(const std::string& text, Badge which, float x, float y, float w, float h, Accent accent, float inset,
             bool update)
{
    badge(which, x + w - inset, y + inset, accent);
    if (update)
        updateMarker(x + inset, y + inset, w - 2 * inset - (which == Badge::None ? 0 : badgeWidth(which) + 8), accent);
    if (!text.empty())
        chip(text, x + inset, y + h - inset);
}

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
    overlay(content.chip, content.badge, px, py, previewW, previewH, accent, 8, content.update);

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
    // by path, size and time, so a file replaced by another isn't shown as the old one: a splash's update is as big
    const std::string key = "sd:" + std::to_string(width) + ":" + std::to_string(entry.size) + ":" +
                            std::to_string(entry.modified) + ":" + entry.path;
    return images().load(key, [entry, width] { return library::readPreview(entry, width); });
}

std::shared_ptr<Picture> localBackdrop(const library::Entry& entry)
{
    const std::string key =
        "sd-blur:" + std::to_string(entry.size) + ":" + std::to_string(entry.modified) + ":" + entry.path;
    return images().load(key, [entry] {
        // its ThumbHash, as Themezer's pictures have, made from it at a size ThumbHash needs no more than
        SDL_Surface* small = library::readPreview(entry, 100);
        SDL_Surface* blur = thumbHashOf(small);
        SDL_FreeSurface(small);
        return blur;
    });
}

float drawHeading(const std::string& name, const std::string& creator, float x, float y, float width, Accent accent,
                  int maxLines)
{
    y += drawWrapped(name, x, y, width, metric::kTextTitle, color::kText, maxLines);
    y += 4;
    if (!creator.empty())
    {
        drawText("by " + creator, x, y, metric::kTextNormal, accent.light, Align::Left, width);
        y += lineHeight(metric::kTextNormal);
    }
    y += 16;
    rect(x, y, width, 1, color::kDivider);
    return y + 10;
}

float drawFact(Icon which, const std::string& text, float x, float y, float width, Accent accent, bool link)
{
    if (text.empty())
        return y;
    const float iconSize = metric::kIcon * 0.75f;
    const float middle = y + kFactRow / 2;
    icon(which, x, std::round(middle - iconSize / 2), iconSize, accent.light);
    const float textX = x + iconSize + 14;
    const float shown = drawText(text, textX, textTop(middle, metric::kTextSmall), metric::kTextSmall,
                                 link ? accent.light : color::kText, Align::Left, width - iconSize - 14 - (link ? 24 : 0));
    if (link)
        drawText("›", textX + shown + 10, textTop(middle, metric::kTextSmall), metric::kTextSmall, accent.light);
    return y + kFactRow;
}

bool factTapped(const ui::Input& input, float x, float top, float width)
{
    const auto inside = [&](float px, float py) { return px >= x && px < x + width && py >= top && py < top + kFactRow; };
    return top >= 0 && input.tap && inside(input.x, input.y) && inside(input.startX, input.startY);
}

namespace
{
// The pills at the hint bar's left end, where it's free: what's queued to install, what's newer. Where the last frame
// drew them, for a tap, and the one a finger is on.
struct LeftPill
{
    Icon icon;
    std::string label;
    void (*open)();
    Spot spot;
};
std::vector<LeftPill> leftPills;
int leftDown = -1;
} // namespace

void updateHints(ui::HintBar& bar, ui::Input& input, const std::vector<ui::Hint>& hints)
{
    leftDown = -1;
    for (size_t i = 0; i < leftPills.size(); ++i)
    {
        const Spot& spot = leftPills[i].spot;
        if (!spot.contains(input.x, input.y) || !spot.contains(input.startX, input.startY))
            continue;
        if (input.touching && !input.dragging)
            leftDown = static_cast<int>(i);
        if (input.tap)
        {
            input.consumeTouch();
            sfx::play(sfx::Sound::Confirm);
            leftPills[i].open();
        }
        break;
    }
    bar.update(input, hints);
}

void drawHints(ui::HintBar& bar, const std::vector<ui::Hint>& hints, Color accent)
{
    bar.draw(hints, 1.0f, accent);
    std::vector<LeftPill> pills;
    if (const int queued = installs::queuedCount(); queued > 0)
        pills.push_back({Icon::Queue, std::to_string(queued) + " queued", openQueue, {}});
    if (const int newer = updates::availableCount() + static_cast<int>(itemupdates::count()); newer > 0)
        pills.push_back({Icon::Refresh, newer == 1 ? std::string("Update") : std::to_string(newer) + " updates",
                         openUpdates, {}});
    // keys as the hints are, from the bar's other end, as many as there's room for
    const float iconSize = metric::kIcon * 0.75f;
    const float height = 44;
    const float top = std::round(metric::kHeight - metric::kHintBarHeight +
                                 (metric::kHintBarHeight - height - ui::kKeyLip) / 2);
    float x = 16;
    for (size_t i = 0; i < pills.size(); ++i)
    {
        LeftPill& pill = pills[i];
        const float width = std::round(18 + iconSize + 10 + textWidth(pill.label, metric::kTextSmall) + 18);
        if (x + width + 16 > bar.left())
        {
            pills.erase(pills.begin() + static_cast<long>(i), pills.end());
            break;
        }
        const float face = ui::drawKey(x, top, width, height, color::kSurfaceHigh, leftDown == static_cast<int>(i));
        icon(pill.icon, x + 18, std::round(face + (height - iconSize) / 2), iconSize, accent::kApp.light);
        drawText(pill.label, x + 18 + iconSize + 10, textTop(face + height / 2, metric::kTextSmall), metric::kTextSmall,
                 color::kText);
        pill.spot = {x, top, width, height + ui::kKeyLip};
        x += width + 10;
    }
    leftPills = std::move(pills);
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

namespace
{
constexpr std::optional<bool> kPacked[] = {std::nullopt, false, true};
constexpr const char* kPackedLabels[] = {"All themes", "Standalone only", "From packs only"};
constexpr Icon kPackedIcons[] = {Icon::Layers, Icon::Theme, Icon::Pack};

size_t packedIndex(std::optional<bool> inPack)
{
    return !inPack ? 0 : *inPack ? 2 : 1;
}
} // namespace

const char* packedLabel(std::optional<bool> inPack)
{
    return kPackedLabels[packedIndex(inPack)];
}

void choosePacked(std::optional<bool> current, Color accent, std::function<void(std::optional<bool>)> choose)
{
    std::vector<ui::Action> actions;
    for (size_t i = 0; i < std::size(kPacked); ++i)
    {
        ui::Action action{kPackedIcons[i], kPackedLabels[i], "", true, false, [choose, current, i] {
                              if (current != kPacked[i])
                                  choose(kPacked[i]);
                          }};
        action.checked = current == kPacked[i];
        actions.push_back(std::move(action));
    }
    App::get().scenes().emplace<ui::ActionSheet>("Show", std::move(actions), accent,
                                                 static_cast<int>(packedIndex(current)));
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
