// What every screen shares: the blurred backdrop, the top bar, the item cards, and the messages of an empty page.

#pragma once

#include "gfx/draw.hpp"
#include "gfx/texture.hpp"
#include "domain/library.hpp"
#include "services/local.hpp"
#include "services/themezer.hpp"
#include "ui/input.hpp"

#include <memory>
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
// With a trail, after the logo: where the screen is, "Themezer › Packs › Pixel Forest", the last part brightest.
float drawTopBar(float alpha = 1.0f, const std::vector<std::string>& trail = {});
// The trail to a screen opened now, called name: the screens' under it, then its own.
std::vector<std::string> trailTo(std::string name);

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
} // namespace card

// A file's picture from the SD card, no wider than width, read on the images worker; nullptr until it's in.
std::shared_ptr<gfx::Picture> localPicture(const library::Entry& entry, int width);
// Its ThumbHash's picture, for the backdrop, as the website's pictures have.
std::shared_ptr<gfx::Picture> localBackdrop(const library::Entry& entry);

// One of the chips under the tabs that say what a list is (its sort, its filter, its search), from x, its text cut to
// end before right. Returns its width; 0 when there's no room for it.
float filterChip(gfx::Icon icon, const std::string& text, bool highlighted, gfx::Accent accent, float x, float top,
                 float height, float right);

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
