// The app's colours and sizes, as Themezer-3DS has them after the Themezer website: its dark mode's stone greys,
// frosted surfaces over a blurred picture of what's selected, its primary green, and the accent colour of each kind of
// content (packs pink, themes green, splashes orange). The sizes are for the Switch's 1280x720 screen.

#pragma once

#include <cstdint>

namespace tz::gfx
{

// r in the low byte, a in the high one
using Color = uint32_t;

constexpr Color rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
{
    return static_cast<Color>(r) | (static_cast<Color>(g) << 8) | (static_cast<Color>(b) << 16) |
           (static_cast<Color>(a) << 24);
}

constexpr uint8_t red(Color color) { return color & 0xFF; }
constexpr uint8_t green(Color color) { return (color >> 8) & 0xFF; }
constexpr uint8_t blue(Color color) { return (color >> 16) & 0xFF; }
constexpr uint8_t alpha(Color color) { return color >> 24; }

constexpr Color withAlpha(Color color, float alpha)
{
    const float a = alpha < 0 ? 0 : (alpha > 1 ? 1 : alpha);
    return (color & 0x00FFFFFF) | (static_cast<Color>(static_cast<float>(color >> 24) * a) << 24);
}

Color mix(Color a, Color b, float t);

namespace color
{
// The page under everything: stone 950, which the website's frosted cards are made of too.
constexpr Color kBackground = rgba(12, 10, 9);
// Frosted surfaces over the blurred picture: the website's cards (stone 950 at 32 %) and its denser panels.
constexpr Color kFrosted = rgba(12, 10, 9, 82);
constexpr Color kSurface = rgba(12, 10, 9, 163);
constexpr Color kSurfaceSolid = rgba(28, 25, 23);   // stone 900
constexpr Color kSurfaceRaised = rgba(41, 37, 36);  // stone 800
constexpr Color kSurfaceHigh = rgba(68, 64, 60);    // stone 700
constexpr Color kDivider = rgba(255, 255, 255, 20);
constexpr Color kSelection = rgba(255, 255, 255, 30);

constexpr Color kText = rgba(255, 255, 255);
constexpr Color kTextDim = rgba(168, 162, 158);   // stone 400
constexpr Color kTextFaint = rgba(120, 113, 108); // stone 500

constexpr Color kPrimary = rgba(10, 179, 121);      // Themezer's green (primary 500)
constexpr Color kPrimaryLight = rgba(47, 216, 152); // primary 400
constexpr Color kSecondary = rgba(255, 50, 204);    // the pink of Themezer's logo
constexpr Color kOrange = rgba(249, 115, 22);
constexpr Color kOrangeLight = rgba(251, 146, 60);
constexpr Color kPink = rgba(236, 72, 153);
constexpr Color kPinkLight = rgba(244, 114, 182);
constexpr Color kOnAccent = rgba(12, 10, 9);
constexpr Color kDanger = rgba(239, 68, 68);
constexpr Color kWarning = rgba(251, 191, 36);

constexpr Color kScrim = rgba(12, 10, 9, 170); // behind dialogs
constexpr Color kShadow = rgba(0, 0, 0, 120);
} // namespace color

// Each kind of content has its accent: solid fills selected things (with kOnAccent text), light is for icons and text
// on the dark surfaces.
struct Accent
{
    Color solid;
    Color light;
};

namespace accent
{
constexpr Accent kPacks{color::kPink, color::kPinkLight};
constexpr Accent kThemes{color::kPrimary, color::kPrimaryLight};
constexpr Accent kSplashes{color::kOrange, color::kOrangeLight};
constexpr Accent kApp = kThemes;
} // namespace accent

namespace metric
{
constexpr float kWidth = 1280;
constexpr float kHeight = 720;

constexpr float kTopBarHeight = 80; // the logo, the tabs and the status
constexpr float kHintBarHeight = 64;
constexpr float kPadding = 24;
constexpr float kRadius = 16; // cards
constexpr float kRadiusSmall = 10;

constexpr float kIcon = 32;
constexpr float kIconLarge = 96;

// Font sizes in pixels.
constexpr float kTextTitle = 40;  // an item's name on its page
constexpr float kTextLarge = 30;  // titles
constexpr float kTextNormal = 25; // most text
constexpr float kTextSmall = 21;  // second lines and hints
constexpr float kTextCaption = 18; // what goes under a picture: its description, its file
constexpr float kButtonGlyph = 30;
} // namespace metric

} // namespace tz::gfx
