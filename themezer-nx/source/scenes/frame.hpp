// What every screen shares: the blurred backdrop, the top bar, the item cards, and the messages of an empty page.

#pragma once

#include "gfx/draw.hpp"
#include "gfx/texture.hpp"
#include "domain/library.hpp"
#include "services/local.hpp"
#include "services/themezer.hpp"
#include "ui/input.hpp"
#include "ui/scene.hpp"
#include "ui/widgets.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string_view>

namespace tz::scenes
{

// A soft blur of the selected item's picture behind the screen, fading from one item to the next, as the Themezer
// website shows behind its pages. Without a picture, a dark wash of the page's accent.
class Backdrop
{
public:
    void set(std::shared_ptr<gfx::Picture> blur, gfx::Color accent);
    void tick(float dt);
    // How dark the picture is made.
    void draw(float dim = 0.6f) const;

private:
    struct Layer
    {
        std::shared_ptr<gfx::Picture> picture;
        gfx::Color accent = gfx::color::kPrimary;
    };
    void drawLayer(const Layer& layer, float alpha) const;

    Layer current_;
    Layer previous_;
    float fade_ = 1;
    bool waiting_ = false; // for a picture, showing the last one meanwhile
    float waited_ = 0;
    gfx::Color waitingAccent_ = gfx::color::kPrimary;
};

// The top bar: Themezer's logo on the left, the time, Wi-Fi and battery on the right. Returns where the status
// starts, for what goes between.
// With a trail, after the logo: where the screen is, its mode and section as the home screen shows them, then the pages
// it was opened from ("Pixel Forest"), as text.
float drawTopBar(float alpha = 1.0f, const std::vector<ui::Crumb>& trail = {});
// Where a screen opened now is: the trail of the screen it's opened from.
std::vector<ui::Crumb> currentTrail();

// The crumbs of a mode, the SD card's or Themezer's, and a section; and a page, called name.
ui::Crumb modeCrumb(bool online);
ui::Crumb sectionCrumb(themezer::Kind section);
ui::Crumb pageCrumb(std::string name);
// The home screen's mode and section look, the same in every top bar: the mode in a plain pill (with the key that
// switches it, on the home screen, sunk while it's down; width 0 for as wide as it needs), the section as the tab bar
// shows the selected tab. Each returns where it ends.
constexpr float kPillHeight = 52;
float modePillWidth(const ui::Crumb& mode, ui::Keys key = 0);
float drawModePill(const ui::Crumb& mode, float x, float y, float width = 0, ui::Keys key = 0, bool down = false);
float drawSectionPill(const ui::Crumb& section, float x, float y);
// Where the home screen's mode pill is, and the pages' trails start: after the logo.
float pillsLeft();
// A page's trail is the way home: - or its mode pill goes back to the home screen in the other mode, its section pill
// to that section's list. True when it did (the page is closing). Before a page handles its own keys.
bool trailKeys(ui::Input& input, const std::vector<ui::Crumb>& trail);
// The home screen, in that mode (true: online) and section, with the screens over it closed. In home.cpp.
void goHome(bool online, themezer::Kind section);

gfx::Accent accentOf(themezer::Kind kind);
gfx::Icon iconOf(themezer::Kind kind);
// The website's icons for its sorts and for the parts of the HOME Menu (-1 for all of them).
gfx::Icon sortIcon(themezer::Sort sort);
gfx::Icon targetIcon(int target);
// The picture standing in for an item: its ThumbHash.
std::shared_ptr<gfx::Picture> placeholderOf(const themezer::Item& item);

// What's become of an item here, strongest first: queued to install, installed, on the SD card.
enum class Badge : uint8_t
{
    None,
    Downloaded,
    Installed,
    Queued,
};
Badge badgeOf(const themezer::Item& item);
Badge badgeOf(const library::Entry& entry);
// A pack on the SD card: its themes' strongest.
Badge badgeOf(const local::Pack& pack);
const char* badgeLabel(Badge badge);
gfx::Icon badgeIcon(Badge badge);

// An item's card: its preview over its name and creator. The selected one is raised, with a ring in accent.
namespace card
{
constexpr float kTextHeight = 72; // under the preview
struct Content
{
    std::string name;
    std::string creator;
    std::shared_ptr<gfx::Picture> picture; // nullptr: icon, on a plain face
    gfx::Icon icon = gfx::Icon::Theme;
    std::string chip; // over the preview's bottom left: "7 themes", "Home Menu"
    Badge badge = Badge::None;
};
void draw(const Content& content, float x, float y, float w, float h, bool selected, gfx::Accent accent);
void draw(const themezer::Item& item, float x, float y, float w, float h, bool selected, gfx::Accent accent,
          bool showTarget);
// The selected card's key: a face on a ledge, with a ring in accent.
void highlight(float x, float y, float w, float h, bool down, gfx::Accent accent);
// What a card shows over its picture: the chip at its bottom left, the badge at its top right; inset from the corners.
// A page shows the same over its big picture.
void overlay(const std::string& chip, Badge badge, float x, float y, float w, float h, gfx::Accent accent,
             float inset = 8);
} // namespace card

// A file's picture from the SD card, no wider than width, read on the images worker; nullptr until it's in.
std::shared_ptr<gfx::Picture> localPicture(const library::Entry& entry, int width);
// Its ThumbHash's picture, for the backdrop, as the website's pictures have.
std::shared_ptr<gfx::Picture> localBackdrop(const library::Entry& entry);

// A page's facts, beside or under its picture, a row each: an icon in the accent and a line; a link (a tap on it opens
// something, as an entry of the page's options does) ends in "›". Returns the next row's top.
constexpr float kFactRow = 40;
float drawFact(gfx::Icon which, const std::string& text, float x, float y, float width, gfx::Accent accent,
               bool link = false);
// Whether a tap was on the fact row at top, from x as wide as width.
bool factTapped(const ui::Input& input, float x, float top, float width);

// The hint bar every screen has, with what's queued to install at its left end ("2 queued"), which a tap opens.
void updateHints(ui::HintBar& bar, ui::Input& input, const std::vector<ui::Hint>& hints);
void drawHints(ui::HintBar& bar, const std::vector<ui::Hint>& hints, gfx::Color accent = gfx::color::kPrimary);

// One of the chips under the tabs that say what a list is (its sort, its filter, its search), from x, its text cut to
// end before right. Returns its width; 0 when there's no room for it.
float filterChip(gfx::Icon icon, const std::string& text, bool highlighted, gfx::Accent accent, float x, float top,
                 float height, float right);

// The filter on lists of themes, Themezer's and the SD card's, for whether a theme is in a pack: all of them (nullopt),
// only standalone ones (false), only those from packs (true). Its sheet calls choose with another one.
const char* packedLabel(std::optional<bool> inPack);
void choosePacked(std::optional<bool> current, gfx::Color accent, std::function<void(std::optional<bool>)> choose);

// The chips' keys, once up from the grid's top row has reached them (focus, the chip's index; -1 off them): left and
// right along them, down or B back to the grid. Returns the chip A chose, or -1.
int chipKeys(ui::Input& input, int& focus, int count);
// The ring round the chip the keys are on.
void chipRing(float x, float top, float width, float height, gfx::Accent accent);

// The number with thousands apart: 5,004.
std::string thousands(int number);
// "1.2 MB"
std::string megabytes(uint64_t bytes);

// A page with nothing to show: a large icon, what's the matter, and what to do, centred between top and bottom.
void drawEmptyState(gfx::Icon icon, gfx::Color iconColor, std::string_view title, std::string_view message, float top,
                    float bottom);

} // namespace tz::scenes
