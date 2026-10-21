// The pieces screens are made of, as Themezer-3DS draws them: keycaps on a ledge, the hint bar, tabs in a pill.

#pragma once

#include "gfx/draw.hpp"
#include "gfx/palette.hpp"
#include "gfx/renderer.hpp"
#include "ui/input.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace tz::ui
{

// Keys on the screen (the hints, a grid's selected card) stand on a ledge this high, and sink into it while they're
// down.
constexpr float kKeyLip = 4;
// A key with its face at y, round at the ends (or with corners of radius), or sunk into the ledge while down. face must
// be opaque: the ledge is a darker copy of it underneath. Returns where the face's top is, for what's on it.
float drawKey(float x, float y, float w, float h, gfx::Color face, bool down, float radius = -1);

// Whether a key on the screen is down: while its button is held or a finger is on it, and for a moment at least after
// it's pressed, so that a quick press shows too. Update it in the frame's update, and ask once, in its draw.
class KeyPress
{
public:
    void update(bool held, bool pressed)
    {
        held_ = held;
        if (pressed)
            pressedAt_ = gfx::time();
    }
    bool down()
    {
        const bool held = held_;
        held_ = false;
        return held || gfx::time() - pressedAt_ < kTime;
    }

private:
    static constexpr float kTime = 0.11f;
    bool held_ = false;
    float pressedAt_ = -1;
};

// A button and what it does, along the bottom of the screen. Tapping a hint presses its button.
struct Hint
{
    Keys key = 0;
    std::string label;
};

class HintBar
{
public:
    // Turns taps on the hints into their buttons. Call before handling input.
    void update(Input& input, const std::vector<Hint>& hints);
    // A's key in accent.
    void draw(const std::vector<Hint>& hints, float alpha = 1.0f, gfx::Color accent = gfx::color::kPrimary);

private:
    struct Spot
    {
        float x, width;
        Keys key;
    };
    Keys keyAt(float x, float y) const; // 0 for none

    std::vector<Spot> spots_;
    float y_ = 0;
    float height_ = 0;
    Keys pressedKey_ = 0;
    float pressedAt_ = -1;
    Keys held_ = 0;
    Keys touchedKey_ = 0;
};

float buttonWidth(Keys key, float size);
// Draws a button's symbol, as big as text of size with y at the top of its line. Returns its width.
float drawButton(Keys key, float x, float y, float size, gfx::Color color);
// A button and what it does, centred on middle. Returns its width.
float hintWidth(Keys key, std::string_view label, float labelSize = gfx::metric::kTextSmall);
float drawHint(Keys key, std::string_view label, float x, float middle, gfx::Color buttonColor, gfx::Color labelColor,
               float labelSize = gfx::metric::kTextSmall);

// Tabs in a pill-shaped bar, as the website's switchers look: the selected one in its accent. L and R switch them,
// drawn at the bar's ends.
struct Tab
{
    std::string label;
    gfx::Accent accent;
    gfx::Icon icon = gfx::Icon::Count;
};

class TabBar
{
public:
    explicit TabBar(Keys previousKey = key::kL, Keys nextKey = key::kR) : previousKey_(previousKey), nextKey_(nextKey) {}

    void setTabs(std::vector<Tab> tabs) { tabs_ = std::move(tabs); }
    int selected() const { return selected_; }
    void select(int index) { selected_ = index; }
    // The two buttons (which then do nothing else), or a tap on a tab. Returns whether the tab changed.
    bool update(Input& input);
    bool movedForward() const { return forward_; }
    void draw(float x, float y, float width, float height, float textSize);
    // How wide draw needs the bar, for its tabs at textSize.
    float naturalWidth(float textSize) const;

private:
    struct Slot
    {
        float x, width;
    };
    float contentWidth(const Tab& tab, float size) const;
    static constexpr int kBefore = -1;
    static constexpr int kAfter = -2;
    static constexpr int kNoSlot = -3;
    int slotAt(float x) const;

    Keys previousKey_;
    Keys nextKey_;
    std::vector<Tab> tabs_;
    std::vector<Slot> slots_;
    float left_ = 0;
    float right_ = 0;
    float top_ = 0;
    float bottom_ = 0;
    int selected_ = 0;
    bool forward_ = true;
    KeyPress previousPress_;
    KeyPress nextPress_;
    float highlightX_ = -1; // the highlight slides to the selected tab
    float highlightWidth_ = 0;
};

// Eases a value towards a target, frame by frame.
void approach(float& value, float target, float dt, float speed = 7.0f);
// 0 to 1, slowing down at the end.
float ease(float t);

} // namespace tz::ui
