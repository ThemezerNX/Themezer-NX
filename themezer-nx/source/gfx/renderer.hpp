// The screen: SDL's window and renderer, frames, clipping, and the Layers that move and fade what's drawn in them.

#pragma once

#include "gfx/palette.hpp"

#include <SDL2/SDL.h>

namespace tz::gfx
{

bool init();
void exit();
SDL_Renderer* sdl();

// A frame: beginFrame, the drawing, then endFrame, which waits for the screen's refresh, so the app runs at 60 frames
// a second.
void beginFrame();
void endFrame();

// Drawing outside the rectangle is cut off until popClip. Clips nest (each is cut to the one before).
void pushClip(float x, float y, float width, float height);
void popClip();

// Seconds since start, for animations.
float time();

// While one lives, what's drawn is moved by (dx, dy), in whole pixels, and its alpha is times opacity, on top of the
// Layers around it. Clips don't move with it.
class Layer
{
public:
    Layer(float dx, float dy, float opacity = 1.0f);
    ~Layer();
    Layer(const Layer&) = delete;
    Layer& operator=(const Layer&) = delete;

private:
    float dx_, dy_;
    float outerOpacity_;
};

// A colour as the Layers make it, for the drawing functions.
Color faded(Color color);
float opacity();
// Where the Layers have moved what's drawn.
float offsetX();
float offsetY();

} // namespace tz::gfx
