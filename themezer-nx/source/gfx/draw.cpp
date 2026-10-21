#include "gfx/draw.hpp"

#include "gfx/renderer.hpp"
#include "gfx/texture.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <utility>

namespace tz::gfx
{

namespace
{

constexpr const char* kIconNames[] = {
    "theme",  "pack",     "splash", "download",    "install", "check",   "check_circle", "search",
    "search_empty", "settings", "info", "refresh", "sort", "filter", "wifi", "wifi_off", "warning", "error", "trash",
    "eye",    "square",   "square_check", "battery", "battery_charging", "keyboard", "power", "music", "volume",
    "volume_off", "queue", "save", "heart", "calendar", "user", "hash", "image",
    "flame", "bookmark", "calendar_plus", "calendar_cog", "house", "lock", "layout_grid", "newspaper", "users", "layers",
    "sd_card", "globe", "monitor_check", "folder",
};
static_assert(std::size(kIconNames) == static_cast<size_t>(Icon::Count));

std::shared_ptr<Picture> icons[static_cast<size_t>(Icon::Count)];
std::shared_ptr<Picture> largeIcons[static_cast<size_t>(Icon::Count)];
std::shared_ptr<Picture> brands[2];

// Round shapes are drawn from textures of discs and rings made at the very size they're drawn, so their edges are
// smooth and sharp: a disc's quarters are a rounded rectangle's corners.
std::map<int, SDL_Texture*> discs;
std::map<std::pair<int, int>, SDL_Texture*> rings;

// How much of the pixel at (x, y) is between the radii inner and outer round (r, r), sampled 4 by 4.
float coverage(int x, int y, float r, float inner, float outer)
{
    int inside = 0;
    for (int sy = 0; sy < 4; ++sy)
    {
        for (int sx = 0; sx < 4; ++sx)
        {
            const float dx = x + (sx + 0.5f) / 4 - r;
            const float dy = y + (sy + 0.5f) / 4 - r;
            const float d = std::sqrt(dx * dx + dy * dy);
            if (d <= outer && d >= inner)
                ++inside;
        }
    }
    return inside / 16.0f;
}

SDL_Texture* makeRound(int radius, float inner)
{
    const int size = radius * 2;
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, size, size, 32, SDL_PIXELFORMAT_ABGR8888);
    if (!surface)
        return nullptr;
    for (int y = 0; y < size; ++y)
    {
        auto* row = reinterpret_cast<Uint32*>(static_cast<Uint8*>(surface->pixels) + y * surface->pitch);
        for (int x = 0; x < size; ++x)
        {
            const auto a = static_cast<Uint32>(std::lround(255 * coverage(x, y, radius, inner, radius)));
            row[x] = rgba(255, 255, 255, static_cast<uint8_t>(a));
        }
    }
    SDL_Texture* texture = SDL_CreateTextureFromSurface(sdl(), surface);
    SDL_FreeSurface(surface);
    if (texture)
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    return texture;
}

SDL_Texture* disc(int radius)
{
    auto& texture = discs[radius];
    if (!texture)
        texture = makeRound(radius, -1);
    return texture;
}

SDL_Texture* ring(int radius, int width)
{
    auto& texture = rings[{radius, width}];
    if (!texture)
        texture = makeRound(radius, static_cast<float>(radius - width));
    return texture;
}

void setDrawColor(Color color)
{
    color = faded(color);
    SDL_SetRenderDrawColor(sdl(), red(color), green(color), blue(color), alpha(color));
}

void tint(SDL_Texture* texture, Color color)
{
    color = faded(color);
    SDL_SetTextureColorMod(texture, red(color), green(color), blue(color));
    SDL_SetTextureAlphaMod(texture, alpha(color));
}

void fillRect(float x, float y, float w, float h)
{
    if (w <= 0 || h <= 0)
        return;
    const SDL_FRect r{x + offsetX(), y + offsetY(), w, h};
    SDL_RenderFillRectF(sdl(), &r);
}

void copy(SDL_Texture* texture, int sx, int sy, int sw, int sh, float x, float y)
{
    const SDL_Rect src{sx, sy, sw, sh};
    const SDL_FRect dst{x + offsetX(), y + offsetY(), static_cast<float>(sw), static_cast<float>(sh)};
    SDL_RenderCopyF(sdl(), texture, &src, &dst);
}

// The four corners of a w by h box, each a quarter of texture (radius r).
void corners(SDL_Texture* texture, float x, float y, float w, float h, int r)
{
    copy(texture, 0, 0, r, r, x, y);
    copy(texture, r, 0, r, r, x + w - r, y);
    copy(texture, 0, r, r, r, x, y + h - r);
    copy(texture, r, r, r, r, x + w - r, y + h - r);
}

SDL_Vertex vertex(float x, float y, Color color)
{
    color = faded(color);
    return SDL_Vertex{{x + offsetX(), y + offsetY()}, {red(color), green(color), blue(color), alpha(color)}, {0, 0}};
}

void quad(const SDL_Vertex (&v)[4])
{
    static const int kIndices[6] = {0, 1, 2, 2, 3, 0};
    SDL_RenderGeometry(sdl(), nullptr, v, 4, kIndices, 6);
}

} // namespace

bool loadIcons()
{
    char path[64];
    bool ok = true;
    for (size_t i = 0; i < std::size(kIconNames); ++i)
    {
        std::snprintf(path, sizeof(path), "romfs:/icons/%s.png", kIconNames[i]);
        icons[i] = Picture::load(path);
        ok = ok && icons[i];
        std::snprintf(path, sizeof(path), "romfs:/icons/%s_96.png", kIconNames[i]);
        largeIcons[i] = Picture::load(path);
    }
    brands[0] = Picture::load("romfs:/brand/logo.png");
    brands[1] = Picture::load("romfs:/brand/icon_128.png");
    return ok;
}

void freeIcons()
{
    for (auto& picture : icons)
        picture.reset();
    for (auto& picture : largeIcons)
        picture.reset();
    for (auto& picture : brands)
        picture.reset();
    for (auto& [radius, texture] : discs)
        SDL_DestroyTexture(texture);
    for (auto& [key, texture] : rings)
        SDL_DestroyTexture(texture);
    discs.clear();
    rings.clear();
}

void icon(Icon which, float x, float y, float size, Color color)
{
    const auto index = static_cast<size_t>(which);
    const auto& picture = size > metric::kIcon * 1.5f && largeIcons[index] ? largeIcons[index] : icons[index];
    if (!picture)
        return;
    tint(picture->texture(), color);
    const SDL_FRect dst{std::round(x) + offsetX(), std::round(y) + offsetY(), size, size};
    SDL_RenderCopyF(sdl(), picture->texture(), nullptr, &dst);
}

void brand(Brand which, float x, float y, float alpha)
{
    const auto& picture = brands[static_cast<size_t>(which)];
    if (picture)
        picture->draw(std::round(x), std::round(y), static_cast<float>(picture->width()),
                      static_cast<float>(picture->height()), alpha);
}

float brandWidth(Brand which)
{
    const auto& picture = brands[static_cast<size_t>(which)];
    return picture ? static_cast<float>(picture->width()) : 0;
}

float brandHeight(Brand which)
{
    const auto& picture = brands[static_cast<size_t>(which)];
    return picture ? static_cast<float>(picture->height()) : 0;
}

void rect(float x, float y, float w, float h, Color color)
{
    setDrawColor(color);
    fillRect(x, y, w, h);
}

void gradientV(float x, float y, float w, float h, Color top, Color bottom)
{
    quad({vertex(x, y, top), vertex(x + w, y, top), vertex(x + w, y + h, bottom), vertex(x, y + h, bottom)});
}

void gradientH(float x, float y, float w, float h, Color left, Color right)
{
    quad({vertex(x, y, left), vertex(x + w, y, right), vertex(x + w, y + h, right), vertex(x, y + h, left)});
}

void roundedRect(float x, float y, float w, float h, float radius, Color color)
{
    x = std::round(x);
    y = std::round(y);
    w = std::round(w);
    h = std::round(h);
    const int r = static_cast<int>(std::lround(std::min({radius, w / 2, h / 2})));
    if (w <= 0 || h <= 0)
        return;
    if (r < 1)
    {
        rect(x, y, w, h, color);
        return;
    }
    if (SDL_Texture* texture = disc(r))
    {
        tint(texture, color);
        corners(texture, x, y, w, h, r);
    }
    setDrawColor(color);
    fillRect(x + r, y, w - 2 * r, h);
    fillRect(x, y + r, r, h - 2 * r);
    fillRect(x + w - r, y + r, r, h - 2 * r);
}

void roundedOutline(float x, float y, float w, float h, float radius, float width, Color color)
{
    x = std::round(x);
    y = std::round(y);
    w = std::round(w);
    h = std::round(h);
    const int r = static_cast<int>(std::lround(std::min({radius, w / 2, h / 2})));
    const int line = std::max(1, static_cast<int>(std::lround(width)));
    if (r < line)
    {
        rect(x, y, w, line, color);
        rect(x, y + h - line, w, line, color);
        rect(x, y + line, line, h - 2 * line, color);
        rect(x + w - line, y + line, line, h - 2 * line, color);
        return;
    }
    if (SDL_Texture* texture = ring(r, line))
    {
        tint(texture, color);
        corners(texture, x, y, w, h, r);
    }
    setDrawColor(color);
    fillRect(x + r, y, w - 2 * r, line);
    fillRect(x + r, y + h - line, w - 2 * r, line);
    fillRect(x, y + r, line, h - 2 * r);
    fillRect(x + w - line, y + r, line, h - 2 * r);
}

void circle(float cx, float cy, float radius, Color color)
{
    const int r = std::max(1, static_cast<int>(std::lround(radius)));
    SDL_Texture* texture = disc(r);
    if (!texture)
        return;
    tint(texture, color);
    const SDL_FRect dst{std::round(cx - r) + offsetX(), std::round(cy - r) + offsetY(), 2.0f * r, 2.0f * r};
    SDL_RenderCopyF(sdl(), texture, nullptr, &dst);
}

void triangle(float x0, float y0, float x1, float y1, float x2, float y2, Color color)
{
    const SDL_Vertex v[3] = {vertex(x0, y0, color), vertex(x1, y1, color), vertex(x2, y2, color)};
    SDL_RenderGeometry(sdl(), nullptr, v, 3, nullptr, 0);
}

void line(float x0, float y0, float x1, float y1, float width, Color color)
{
    const float dx = x1 - x0;
    const float dy = y1 - y0;
    const float length = std::sqrt(dx * dx + dy * dy);
    if (length <= 0)
        return;
    const float nx = -dy / length * width / 2;
    const float ny = dx / length * width / 2;
    quad({vertex(x0 + nx, y0 + ny, color), vertex(x1 + nx, y1 + ny, color), vertex(x1 - nx, y1 - ny, color),
          vertex(x0 - nx, y0 - ny, color)});
}

void shadow(float x, float y, float w, float h, float radius, float size, float strength)
{
    // rounded rectangles growing outwards, each faint, add up to a shadow that fades out; a little lower than the box
    constexpr int kSteps = 8;
    const float step = size / kSteps;
    for (int i = kSteps; i >= 1; --i)
    {
        const float grow = step * i;
        const float a = strength * 0.05f * (1.0f - static_cast<float>(i - 1) / kSteps);
        roundedRect(x - grow, y - grow + size * 0.3f, w + 2 * grow, h + 2 * grow, radius + grow,
                    withAlpha(rgba(0, 0, 0), a));
    }
}

void spinner(float cx, float cy, float radius, Color color)
{
    // three quarters of a ring, turning, drawn as a string of small discs, so its edges are smooth
    const float thickness = std::max(3.0f, radius * 0.2f);
    const float start = time() * 5.0f;
    const float sweep = static_cast<float>(M_PI) * 1.5f;
    const int dots = std::max(12, static_cast<int>(sweep * radius / 1.5f));
    for (int i = 0; i <= dots; ++i)
    {
        const float angle = start + sweep * i / dots;
        circle(cx + std::cos(angle) * (radius - thickness / 2), cy + std::sin(angle) * (radius - thickness / 2),
               thickness / 2, withAlpha(color, 0.25f + 0.75f * i / dots));
    }
}

void progressBar(float x, float y, float w, float h, float fraction, Color track, Color fill)
{
    roundedRect(x, y, w, h, h / 2, track);
    const float filled = std::round(w * std::clamp(fraction, 0.0f, 1.0f));
    if (filled > 0)
        roundedRect(x, y, std::max(filled, h), h, h / 2, fill);
}

void fill(Color color)
{
    rect(-offsetX(), -offsetY(), metric::kWidth, metric::kHeight, color);
}

} // namespace tz::gfx
