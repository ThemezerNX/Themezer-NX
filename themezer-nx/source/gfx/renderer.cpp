#include "gfx/renderer.hpp"

#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace tz::gfx
{

namespace
{
SDL_Window* window = nullptr;
SDL_Renderer* renderer = nullptr;
std::vector<SDL_Rect> clips;
float layerX = 0;
float layerY = 0;
float layerOpacity = 1;
} // namespace

Color mix(Color a, Color b, float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    const auto channel = [&](int shift) {
        const float from = static_cast<float>((a >> shift) & 0xFF);
        const float to = static_cast<float>((b >> shift) & 0xFF);
        return static_cast<Color>(std::lround(from + (to - from) * t)) << shift;
    };
    return channel(0) | channel(8) | channel(16) | channel(24);
}

bool init()
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
        return false;
    IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG | IMG_INIT_WEBP);
    TTF_Init();
    // smooth scaling, for pictures and the shapes' corners
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    window = SDL_CreateWindow("Themezer-NX", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              static_cast<int>(metric::kWidth), static_cast<int>(metric::kHeight), SDL_WINDOW_SHOWN);
    if (!window)
        return false;
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC |
                                                  SDL_RENDERER_TARGETTEXTURE);
    // what there is, where there's no GPU (a computer drawing off screen)
    if (!renderer)
        renderer = SDL_CreateRenderer(window, -1, 0);
    if (!renderer)
        return false;
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    return true;
}

void exit()
{
    if (renderer)
        SDL_DestroyRenderer(renderer);
    if (window)
        SDL_DestroyWindow(window);
    renderer = nullptr;
    window = nullptr;
    TTF_Quit();
    IMG_Quit();
    // all of SDL, the audio too, which is closed by now
    SDL_Quit();
}

SDL_Renderer* sdl()
{
    return renderer;
}

void beginFrame()
{
    clips.clear();
    SDL_RenderSetClipRect(renderer, nullptr);
    SDL_SetRenderDrawColor(renderer, red(color::kBackground), green(color::kBackground), blue(color::kBackground), 255);
    SDL_RenderClear(renderer);
}

void endFrame()
{
    SDL_RenderPresent(renderer);
}

void pushClip(float x, float y, float width, float height)
{
    SDL_Rect rect{static_cast<int>(std::floor(x)), static_cast<int>(std::floor(y)),
                  static_cast<int>(std::ceil(width)), static_cast<int>(std::ceil(height))};
    if (!clips.empty())
    {
        SDL_Rect cut;
        if (!SDL_IntersectRect(&rect, &clips.back(), &cut))
            cut = {rect.x, rect.y, 0, 0};
        rect = cut;
    }
    clips.push_back(rect);
    // SDL takes an empty rectangle for no clip at all: one off the screen cuts everything
    if (rect.w <= 0 || rect.h <= 0)
        rect = {-2, -2, 1, 1};
    SDL_RenderSetClipRect(renderer, &rect);
}

void popClip()
{
    if (!clips.empty())
        clips.pop_back();
    if (clips.empty())
    {
        SDL_RenderSetClipRect(renderer, nullptr);
        return;
    }
    SDL_Rect rect = clips.back();
    if (rect.w <= 0 || rect.h <= 0)
        rect = {-2, -2, 1, 1};
    SDL_RenderSetClipRect(renderer, &rect);
}

float time()
{
    return static_cast<float>(SDL_GetTicks64()) / 1000.0f;
}

Layer::Layer(float dx, float dy, float opacity)
    : dx_(std::round(dx)), dy_(std::round(dy)), outerOpacity_(layerOpacity)
{
    layerX += dx_;
    layerY += dy_;
    layerOpacity *= std::clamp(opacity, 0.0f, 1.0f);
}

Layer::~Layer()
{
    layerX -= dx_;
    layerY -= dy_;
    layerOpacity = outerOpacity_;
}

Color faded(Color color)
{
    return layerOpacity >= 1 ? color : withAlpha(color, layerOpacity);
}

float opacity()
{
    return layerOpacity;
}

float offsetX()
{
    return layerX;
}

float offsetY()
{
    return layerY;
}

} // namespace tz::gfx
