#include "gfx/texture.hpp"

#include "gfx/renderer.hpp"
#include "libs/thumbhash.h"

#include <SDL2/SDL_image.h>
#include <mbedtls/base64.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace tz::gfx
{

namespace
{
constexpr int kCornerSegments = 10;
} // namespace

Picture::Picture(SDL_Texture* texture, int width, int height) : texture_(texture), width_(width), height_(height) {}

Picture::~Picture()
{
    if (texture_)
        SDL_DestroyTexture(texture_);
}

std::shared_ptr<Picture> Picture::fromSurface(SDL_Surface* surface)
{
    if (!surface)
        return nullptr;
    SDL_Texture* texture = SDL_CreateTextureFromSurface(sdl(), surface);
    const int width = surface->w;
    const int height = surface->h;
    SDL_FreeSurface(surface);
    if (!texture)
        return nullptr;
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    return std::shared_ptr<Picture>(new Picture(texture, width, height));
}

std::shared_ptr<Picture> Picture::load(const char* path)
{
    return fromSurface(IMG_Load(path));
}

void Picture::draw(float x, float y, float w, float h, float alpha) const
{
    const SDL_FRect dst{x + offsetX(), y + offsetY(), w, h};
    SDL_SetTextureColorMod(texture_, 255, 255, 255);
    SDL_SetTextureAlphaMod(texture_, static_cast<Uint8>(255 * std::clamp(alpha * opacity(), 0.0f, 1.0f)));
    SDL_RenderCopyF(sdl(), texture_, nullptr, &dst);
}

void Picture::drawContain(float x, float y, float w, float h, float alpha) const
{
    const float scale = std::min(w / width_, h / height_);
    const float dw = width_ * scale;
    const float dh = height_ * scale;
    draw(std::round(x + (w - dw) / 2), std::round(y + (h - dh) / 2), std::round(dw), std::round(dh), alpha);
}

void Picture::drawCover(float x, float y, float w, float h, float radius, float alpha) const
{
    // the part of the picture with the box's shape, from its middle, as texture coordinates
    const float boxAspect = w / h;
    const float aspect = static_cast<float>(width_) / height_;
    float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
    if (aspect > boxAspect)
    {
        const float shown = boxAspect / aspect;
        u0 = (1 - shown) / 2;
        u1 = u0 + shown;
    }
    else
    {
        const float shown = aspect / boxAspect;
        v0 = (1 - shown) / 2;
        v1 = v0 + shown;
    }
    x += offsetX();
    y += offsetY();
    const Uint8 a = static_cast<Uint8>(255 * std::clamp(alpha * opacity(), 0.0f, 1.0f));
    const auto vertex = [&](float px, float py) {
        return SDL_Vertex{{px, py}, {255, 255, 255, a}, {u0 + (u1 - u0) * (px - x) / w, v0 + (v1 - v0) * (py - y) / h}};
    };

    // a fan from the middle round the outline, its corners in short straight steps
    radius = std::min({radius, w / 2, h / 2});
    std::vector<SDL_Vertex> outline;
    if (radius <= 0.5f)
    {
        outline = {vertex(x, y), vertex(x + w, y), vertex(x + w, y + h), vertex(x, y + h)};
    }
    else
    {
        const float cx[4] = {x + w - radius, x + w - radius, x + radius, x + radius};
        const float cy[4] = {y + radius, y + h - radius, y + h - radius, y + radius};
        for (int corner = 0; corner < 4; ++corner)
        {
            // from the top right clockwise: -90° to 0°, 0° to 90°...
            const float start = static_cast<float>(M_PI) * (-0.5f + 0.5f * corner);
            for (int i = 0; i <= kCornerSegments; ++i)
            {
                const float angle = start + static_cast<float>(M_PI) * 0.5f * i / kCornerSegments;
                outline.push_back(vertex(cx[corner] + std::cos(angle) * radius, cy[corner] + std::sin(angle) * radius));
            }
        }
    }
    std::vector<SDL_Vertex> vertices;
    vertices.reserve(outline.size() * 3);
    const SDL_Vertex middle = vertex(x + w / 2, y + h / 2);
    for (size_t i = 0; i < outline.size(); ++i)
    {
        vertices.push_back(middle);
        vertices.push_back(outline[i]);
        vertices.push_back(outline[(i + 1) % outline.size()]);
    }
    SDL_SetTextureColorMod(texture_, 255, 255, 255);
    SDL_SetTextureAlphaMod(texture_, 255);
    SDL_RenderGeometry(sdl(), texture_, vertices.data(), static_cast<int>(vertices.size()), nullptr, 0);
}

SDL_Surface* decodeImage(const void* data, size_t size)
{
    if (!data || size == 0)
        return nullptr;
    SDL_RWops* rw = SDL_RWFromConstMem(data, static_cast<int>(size));
    if (!rw)
        return nullptr;
    SDL_Surface* surface = IMG_Load_RW(rw, 1);
    if (!surface)
        return nullptr;
    // one format for every picture, which the renderer takes as it is
    SDL_Surface* converted = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_ABGR8888, 0);
    SDL_FreeSurface(surface);
    return converted;
}

SDL_Surface* thumbHashSurface(std::string_view base64)
{
    if (base64.empty())
        return nullptr;
    std::vector<unsigned char> hash(base64.size() * 3 / 4 + 4);
    size_t length = 0;
    if (mbedtls_base64_decode(hash.data(), hash.size(), &length, reinterpret_cast<const unsigned char*>(base64.data()),
                              base64.size()) != 0)
        return nullptr;
    uint8_t* pixels = nullptr;
    int width = 0;
    int height = 0;
    if (!ThumbHashToRGBA(hash.data(), length, 32, &pixels, &width, &height) || !pixels)
        return nullptr;
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_ABGR8888);
    if (surface)
    {
        for (int row = 0; row < height; ++row)
            std::memcpy(static_cast<uint8_t*>(surface->pixels) + row * surface->pitch, pixels + row * width * 4,
                        static_cast<size_t>(width) * 4);
    }
    std::free(pixels);
    return surface;
}

} // namespace tz::gfx
