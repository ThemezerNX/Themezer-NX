// make -C desktop test: the ThumbHash encoder, through the decoder: a picture's hash drawn again is a blur of it.

#include "libs/thumbhash.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace
{

struct Drawn
{
    std::vector<uint8_t> rgba;
    int width = 0, height = 0;
    const uint8_t* at(int x, int y) const { return &rgba[(static_cast<size_t>(y) * width + x) * 4]; }
};

// Encoded, then drawn as the app draws ThumbHashes: 32 on the longer side.
Drawn roundTrip(int w, int h, const std::vector<uint8_t>& rgba)
{
    uint8_t hash[THUMBHASH_MAX_LENGTH];
    size_t length = 0;
    assert(RGBAToThumbHash(w, h, rgba.data(), hash, &length));
    assert(length >= 5 && length <= THUMBHASH_MAX_LENGTH);
    uint8_t* pixels = nullptr;
    Drawn drawn;
    assert(ThumbHashToRGBA(hash, length, 32, &pixels, &drawn.width, &drawn.height));
    drawn.rgba.assign(pixels, pixels + static_cast<size_t>(drawn.width) * drawn.height * 4);
    std::free(pixels);
    return drawn;
}

std::vector<uint8_t> picture(int w, int h, void (*colour)(int x, int y, uint8_t* out))
{
    std::vector<uint8_t> rgba(static_cast<size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            colour(x, y, &rgba[(static_cast<size_t>(y) * w + x) * 4]);
    return rgba;
}

bool near(int a, int b, int by)
{
    return a - b <= by && b - a <= by;
}

} // namespace

int main()
{
    // one colour stays that colour, in a picture as wide as the original is
    const Drawn solid = roundTrip(100, 56, picture(100, 56, [](int, int, uint8_t* out) {
                                      const uint8_t c[] = {200, 40, 90, 255};
                                      std::memcpy(out, c, 4);
                                  }));
    assert(solid.width == 32 && solid.height < 32);
    for (int y = 0; y < solid.height; ++y)
        for (int x = 0; x < solid.width; ++x)
            assert(near(solid.at(x, y)[0], 200, 12) && near(solid.at(x, y)[1], 40, 12) &&
                   near(solid.at(x, y)[2], 90, 12) && solid.at(x, y)[3] == 255);

    // red on the left, blue on the right
    const Drawn sides = roundTrip(100, 56, picture(100, 56, [](int x, int, uint8_t* out) {
                                      const uint8_t red[] = {230, 20, 20, 255}, blue[] = {20, 20, 230, 255};
                                      std::memcpy(out, x < 50 ? red : blue, 4);
                                  }));
    const uint8_t* left = sides.at(2, sides.height / 2);
    const uint8_t* right = sides.at(sides.width - 3, sides.height / 2);
    assert(left[0] > right[0] + 80 && right[2] > left[2] + 80);

    // light above, dark below, in a picture taller than it's wide
    const Drawn halves = roundTrip(40, 90, picture(40, 90, [](int, int y, uint8_t* out) {
                                       const uint8_t v = y < 45 ? 240 : 15;
                                       const uint8_t c[] = {v, v, v, 255};
                                       std::memcpy(out, c, 4);
                                   }));
    assert(halves.height == 32 && halves.width < 32);
    assert(halves.at(halves.width / 2, 2)[0] > halves.at(halves.width / 2, halves.height - 3)[0] + 120);

    // see-through: alpha is kept
    const Drawn clear = roundTrip(50, 50, picture(50, 50, [](int x, int, uint8_t* out) {
                                      const uint8_t c[] = {255, 255, 255, static_cast<uint8_t>(x < 25 ? 255 : 0)};
                                      std::memcpy(out, c, 4);
                                  }));
    assert(clear.at(2, 25)[3] > 200 && clear.at(clear.width - 3, 25)[3] < 60);

    std::printf("thumbhash: all good\n");
    return 0;
}
