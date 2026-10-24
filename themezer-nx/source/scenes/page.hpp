// A page of the home scene: the packs, the themes or the splashes, on the SD card or on Themezer. The home scene draws
// what's round it (the backdrop, the top bar with the mode button and the tabs, the hints); the page draws what's
// between the top bar and the hints, and handles its own keys.

#pragma once

#include "gfx/palette.hpp"
#include "gfx/texture.hpp"
#include "ui/input.hpp"
#include "ui/overlays.hpp"
#include "ui/widgets.hpp"

#include <memory>
#include <vector>

namespace tz::scenes
{

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
