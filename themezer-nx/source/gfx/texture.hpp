// Pictures: a texture with its size. Images are decoded to surfaces on a worker (decodeImage, thumbHashSurface) and
// become textures on the main thread (Picture::fromSurface), since only it may touch the renderer.

#pragma once

#include "gfx/palette.hpp"

#include <SDL2/SDL.h>

#include <memory>
#include <string_view>

namespace tz::gfx
{

class Picture
{
public:
    ~Picture();
    Picture(const Picture&) = delete;
    Picture& operator=(const Picture&) = delete;

    // Takes the surface. Main thread only.
    static std::shared_ptr<Picture> fromSurface(SDL_Surface* surface);
    static std::shared_ptr<Picture> load(const char* path);

    int width() const { return width_; }
    int height() const { return height_; }
    SDL_Texture* texture() const { return texture_; }

    // Stretched to the box.
    void draw(float x, float y, float w, float h, float alpha = 1.0f) const;
    // Filling the box, cut to its shape at the middle, with round corners when radius > 0.
    void drawCover(float x, float y, float w, float h, float radius = 0, float alpha = 1.0f) const;
    // Fitting in the box at its middle, the whole picture showing.
    void drawContain(float x, float y, float w, float h, float alpha = 1.0f) const;

private:
    explicit Picture(SDL_Texture* texture, int width, int height);

    SDL_Texture* texture_;
    int width_;
    int height_;
};

// Decodes a PNG, JPEG or WebP. Safe on any thread. nullptr when it isn't an image.
SDL_Surface* decodeImage(const void* data, size_t size);
// The ThumbHash placeholder (base64), a blurry picture of the image it stands for. Safe on any thread.
SDL_Surface* thumbHashSurface(std::string_view base64);
// The same made from a picture (ABGR8888, no more than 100 a side is needed), as the website makes them. Any thread.
SDL_Surface* thumbHashOf(SDL_Surface* picture);

} // namespace tz::gfx
