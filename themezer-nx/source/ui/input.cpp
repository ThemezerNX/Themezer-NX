#include "ui/input.hpp"

#include <cmath>

namespace tz::ui
{

namespace
{
constexpr Keys kDirections[4] = {key::kUp, key::kDown, key::kLeft, key::kRight};
constexpr float kRepeatDelay = 0.32f;
constexpr float kRepeatInterval = 0.075f;
// held longer, the repeat speeds up for long lists
constexpr float kFastAfter = 1.4f;
constexpr float kFastInterval = 0.035f;
// a touch that moves less than this is a tap
constexpr float kTapSlop = 16.0f;
} // namespace

void InputReader::init()
{
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad_);
    hidInitializeTouchScreen();
}

Input InputReader::read(float dt)
{
    padUpdate(&pad_);
    Input input;
    input.held = padGetButtons(&pad_);
    input.down = input.held & ~lastHeld_;
    input.repeat = input.down;
    lastHeld_ = input.held;
    for (int i = 0; i < 4; ++i)
    {
        const Keys keys = kDirections[i];
        if (input.down & keys)
        {
            heldTime_[i] = 0;
            nextRepeat_[i] = kRepeatDelay;
        }
        else if (input.held & keys)
        {
            heldTime_[i] += dt;
            if (heldTime_[i] >= nextRepeat_[i])
            {
                input.repeat |= keys;
                nextRepeat_[i] += heldTime_[i] > kFastAfter ? kFastInterval : kRepeatInterval;
            }
        }
    }

    HidTouchScreenState touch{};
    const bool touching = hidGetTouchScreenStates(&touch, 1) && touch.count > 0;
    if (touching)
    {
        const float x = static_cast<float>(touch.touches[0].x);
        const float y = static_cast<float>(touch.touches[0].y);
        if (!touching_)
        {
            touching_ = true;
            moved_ = false;
            startX_ = lastX_ = x;
            startY_ = lastY_ = y;
            velocity_ = 0;
            input.touchStarted = true;
        }
        if (!moved_ && (std::fabs(x - startX_) > kTapSlop || std::fabs(y - startY_) > kTapSlop))
            moved_ = true;
        input.dx = x - lastX_;
        input.dy = y - lastY_;
        if (dt > 0)
            velocity_ = velocity_ * 0.6f + (input.dy / dt) * 0.4f;
        lastX_ = x;
        lastY_ = y;
        input.touching = true;
        input.x = x;
        input.y = y;
    }
    else if (touching_)
    {
        touching_ = false;
        input.touchEnded = true;
        input.x = lastX_;
        input.y = lastY_;
        input.tap = !moved_;
        input.velocityY = moved_ ? velocity_ : 0;
    }
    input.dragging = touching_ && moved_;
    input.startX = startX_;
    input.startY = startY_;
    return input;
}

} // namespace tz::ui
