// A page of the home scene: packs, themes or splashes, on the SD card or Themezer. The home scene draws what's round
// it (backdrop, top bar, hints); the page draws what's between the top bar and the hints and handles its own keys.

#pragma once

#include "gfx/palette.hpp"
#include "gfx/texture.hpp"
#include "ui/input.hpp"
#include "ui/overlays.hpp"
#include "ui/widgets.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace tz::scenes
{

// What a page's list is narrowed to, which goes with it to the other mode's page of the same section: the search, and
// for themes the part of the HOME Menu and whether they're from packs. Not the sort: the two modes' sorts differ.
struct Filters
{
    std::string search;
    int target = -1;
    std::optional<bool> inPack;
    bool operator==(const Filters&) const = default;
};

class Page
{
public:
    virtual ~Page() = default;
    // When its tab is shown, and hidden again.
    virtual void onShow() {}
    virtual void onHide() {}
    // When a scene over the home scene closes.
    virtual void onResume() {}
    // Every frame, also while hidden.
    virtual void tick(float /*dt*/) {}
    virtual void update(ui::Input& input, float dt) = 0;
    virtual void draw() = 0;
    virtual std::vector<ui::Hint> hints() const = 0;
    virtual gfx::Accent accent() const = 0;
    virtual Filters filters() const { return {}; }
    // Before it's shown, when the other mode's page of its section was.
    virtual void setFilters(const Filters& /*filters*/) {}
    // The blurred picture behind the screen, for what's selected.
    virtual std::shared_ptr<gfx::Picture> backdrop() const { return nullptr; }

};

// Where a page's content goes: under the top bar, above the hints.
namespace page
{
constexpr float kSide = 48;
constexpr float kFiltersTop = gfx::metric::kTopBarHeight + 2;
constexpr float kFiltersHeight = 44;
constexpr float kGridTop = kFiltersTop + kFiltersHeight + 18;
constexpr float kBottom = gfx::metric::kHeight - gfx::metric::kHintBarHeight;
constexpr int kColumns = 4;
constexpr float kGap = 22;
} // namespace page

} // namespace tz::scenes
