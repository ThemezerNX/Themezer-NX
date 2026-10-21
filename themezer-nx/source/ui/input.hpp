// One frame of the controller and the touch screen, as Themezer-3DS reads its own: presses, repeats while a direction
// is held, taps and drags.

#pragma once

#include <switch.h>

namespace tz::ui
{

using Keys = u64;

namespace key
{
constexpr Keys kA = HidNpadButton_A;
constexpr Keys kB = HidNpadButton_B;
constexpr Keys kX = HidNpadButton_X;
constexpr Keys kY = HidNpadButton_Y;
constexpr Keys kL = HidNpadButton_L;
constexpr Keys kR = HidNpadButton_R;
constexpr Keys kZL = HidNpadButton_ZL;
constexpr Keys kZR = HidNpadButton_ZR;
constexpr Keys kPlus = HidNpadButton_Plus;
constexpr Keys kMinus = HidNpadButton_Minus;
// The D-pad and the left stick count as the same directions.
constexpr Keys kUp = HidNpadButton_Up | HidNpadButton_StickLUp;
constexpr Keys kDown = HidNpadButton_Down | HidNpadButton_StickLDown;
constexpr Keys kLeft = HidNpadButton_Left | HidNpadButton_StickLLeft;
constexpr Keys kRight = HidNpadButton_Right | HidNpadButton_StickLRight;
// The right stick scrolls.
constexpr Keys kScrollUp = HidNpadButton_StickRUp;
constexpr Keys kScrollDown = HidNpadButton_StickRDown;
} // namespace key

struct Input
{
    Keys down = 0;   // pressed this frame
    Keys held = 0;   // held now
    Keys repeat = 0; // pressed this frame, or repeating while held (directions)

    // The touch screen, in screen pixels.
    bool touching = false;
    float x = 0;
    float y = 0;
    bool touchStarted = false;
    bool touchEnded = false;
    float startX = 0;
    float startY = 0;
    // Released without having moved: a press at (x, y).
    bool tap = false;
    // Moved since it started: a drag, not a tap. dx/dy is this frame's movement.
    bool dragging = false;
    float dx = 0;
    float dy = 0;
    // Pixels a second when the touch ended, for flinging lists.
    float velocityY = 0;

    bool pressed(Keys keys) const { return (down & keys) != 0; }
    bool repeated(Keys keys) const { return (repeat & keys) != 0; }
    bool tapIn(float left, float top, float width, float height) const
    {
        return tap && x >= left && x < left + width && y >= top && y < top + height;
    }
    // Acts as if keys were pressed, for buttons on the screen that do what a key does.
    void press(Keys keys)
    {
        down |= keys;
        repeat |= keys;
    }
    // Once something has used the touch, the rest of the frame shouldn't.
    void consumeTouch()
    {
        tap = false;
        touchStarted = false;
    }
    void clear() { *this = Input{}; }
};

class InputReader
{
public:
    void init();
    Input read(float dt);

private:
    PadState pad_{};
    Keys lastHeld_ = 0;
    float heldTime_[4] = {}; // up, down, left, right
    float nextRepeat_[4] = {};
    bool touching_ = false;
    float startX_ = 0;
    float startY_ = 0;
    float lastX_ = 0;
    float lastY_ = 0;
    bool moved_ = false;
    float velocity_ = 0;
};

} // namespace tz::ui
