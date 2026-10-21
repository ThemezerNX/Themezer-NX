// What every screen shares: the blurred backdrop, the top bar, the item cards, and the messages of an empty page.

#pragma once

#include "gfx/draw.hpp"
#include "gfx/texture.hpp"
#include "services/themezer.hpp"

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
};

// The top bar: Themezer's logo on the left, the time, Wi-Fi and battery on the right. Returns where the status
// starts, for what goes between.
float drawTopBar(float alpha = 1.0f);

gfx::Accent accentOf(themezer::Kind kind);
gfx::Icon iconOf(themezer::Kind kind);
// The website's icons for its sorts and for the parts of the HOME Menu (-1 for all of them).
gfx::Icon sortIcon(themezer::Sort sort);
gfx::Icon targetIcon(int target);
// The picture standing in for an item: its ThumbHash.
std::shared_ptr<gfx::Picture> placeholderOf(const themezer::Item& item);

// An item's card: its preview over its name and creator. The selected one is raised, with a ring in accent.
namespace card
{
constexpr float kTextHeight = 72; // under the preview
void draw(const themezer::Item& item, float x, float y, float w, float h, bool selected, gfx::Accent accent,
          bool showTarget);
// The selected card's key: a face on a ledge, with a ring in accent.
void highlight(float x, float y, float w, float h, bool down, gfx::Accent accent);
} // namespace card

// A page with nothing to show: a large icon, what's the matter, and what to do, centred between top and bottom.
void drawEmptyState(gfx::Icon icon, gfx::Color iconColor, std::string_view title, std::string_view message, float top,
                    float bottom);

} // namespace tz::scenes
