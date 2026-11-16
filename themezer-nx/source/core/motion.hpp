// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors

// The physics of scrolling past an end: a drag that gives less the further it goes, a spring back, and flings that
// bounce off an end. Lists, the badge preview and the About page share them so they feel the same.

#pragma once

#include <algorithm>
#include <cmath>

namespace tz::motion
{

// Fraction of a finger's movement that moves a list already past its end: 0.55 at the end, less further out (as on iOS),
// so the overscroll stays under room.
inline float rubberGive(float over, float room)
{
    // the slope of iOS's room * (1 - 1 / (1 + 0.55 * finger / room)), at the distance the list has gone
    constexpr float kGive = 0.55f;
    const float left = 1 - std::fmin(std::fabs(over) / room, 1.0f);
    return kGive * left * left;
}

// One step of a critically damped spring pulling over (the distance from where it rests) and its velocity to 0: as
// fast as it can be without going past, and solved exactly, so a long frame can't make it swing.
inline void springStep(float& over, float& velocity, float omega, float dt)
{
    const float decay = std::exp(-omega * dt);
    const float c = velocity + omega * over;
    over = (over + c * dt) * decay;
    velocity = (velocity - omega * c * dt) * decay;
}

// How fast a scroll comes back from past an end (the spring's angular frequency).
constexpr float kSpring = 18.0f;
// A fling meets an end at most this fast, so it bounces ~30 pixels however hard it was flung.
constexpr float kBounceSpeed = 1400.0f;

// One frame of a scroll no finger holds, resting within [low, high]: past them, the spring back; between them, a fling
// slowing by friction that meets the spring at an end. velocity is 0 once at rest.
inline void coast(float& position, float& velocity, float low, float high, float friction, float dt)
{
    const float rest = std::clamp(position, low, high);
    if (position != rest)
    {
        float over = position - rest;
        velocity = std::clamp(velocity, -kBounceSpeed, kBounceSpeed);
        springStep(over, velocity, kSpring, dt);
        if (std::fabs(over) < 0.5f && std::fabs(velocity) < 20.0f)
            over = velocity = 0;
        position = rest + over;
        return;
    }
    const float next = position + velocity * dt;
    const float end = std::clamp(next, low, high);
    if (end != next)
    {
        float past = 0;
        velocity = std::clamp(velocity, -kBounceSpeed, kBounceSpeed);
        springStep(past, velocity, kSpring, dt * (next - end) / (next - position));
        position = end + past;
        return;
    }
    position = next;
    velocity *= std::exp(-friction * dt);
    if (std::fabs(velocity) < 20.0f)
        velocity = 0;
}

} // namespace tz::motion
