// Text in the Switch's own fonts: the standard one, plus the Chinese and Korean ones for what it lacks. Sizes are in pixels.

#pragma once

#include "gfx/palette.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace tz::gfx
{

enum class Align : uint8_t
{
    Left,
    Center,
    Right,
};

bool initText();
void exitText();
// Once a frame: lines not drawn for a while are let go.
void beginTextFrame();

float textWidth(std::string_view text, float size);
float lineHeight(float size);
// The y to draw a line at so the middle of its capitals is at middle: centres text in a box and aligns it with icons.
float textTop(float middle, float size);
// The middle of the capitals of a line drawn at top.
float textMiddle(float top, float size);

// One line, with y at its top, cut short with "…" to fit maxWidth (0 for no limit). x is the left edge, the centre
// or the right edge depending on align. Drawn from a whole pixel, which keeps it sharp. Returns the width drawn.
float drawText(std::string_view text, float x, float y, float size, Color color, Align align = Align::Left,
               float maxWidth = 0);
// Wrapped to width, at most maxLines lines (0 for no limit; the last ends in "…" when cut). Returns the height drawn.
float drawWrapped(std::string_view text, float x, float y, float width, float size, Color color, int maxLines = 0,
                  Align align = Align::Left);

std::string ellipsize(std::string_view text, float maxWidth, float size);
std::vector<std::string> wrap(std::string_view text, float width, float size, int maxLines = 0);

} // namespace tz::gfx
