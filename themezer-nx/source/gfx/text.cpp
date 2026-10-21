#include "gfx/text.hpp"

#include "gfx/renderer.hpp"

#include <SDL2/SDL_ttf.h>
#include <switch.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <unordered_map>

namespace tz::gfx
{

namespace
{

// The fonts a line's characters are looked for in, in this order. The standard one has Latin, Japanese and more.
constexpr PlSharedFontType kFonts[] = {
    PlSharedFontType_Standard,           PlSharedFontType_ChineseSimplified, PlSharedFontType_ExtChineseSimplified,
    PlSharedFontType_ChineseTraditional, PlSharedFontType_KO,
};
constexpr size_t kFontCount = std::size(kFonts);

PlFontData fontData[kFontCount];
bool fontLoaded[kFontCount] = {};
// by size in pixels
std::map<int, TTF_Font*> fonts[kFontCount];

struct Line
{
    SDL_Texture* texture = nullptr;
    int width = 0;
    int height = 0;
    int top = 0; // where the texture goes from the line's top: its baseline lined up with the standard font's
    uint64_t used = 0;
};

std::unordered_map<std::string, Line> lines;
std::unordered_map<std::string, float> widths;
uint64_t frame = 0;

int pixels(float size)
{
    return std::max(6, static_cast<int>(std::lround(size)));
}

TTF_Font* font(size_t index, int size)
{
    if (!fontLoaded[index])
        return nullptr;
    auto& slot = fonts[index][size];
    if (!slot)
    {
        SDL_RWops* rw = SDL_RWFromConstMem(fontData[index].address, static_cast<int>(fontData[index].size));
        slot = TTF_OpenFontRW(rw, 1, size);
        if (slot)
            TTF_SetFontHinting(slot, TTF_HINTING_LIGHT);
    }
    return slot;
}

// Decodes the UTF-8 character at i, and moves i past it.
uint32_t next(std::string_view text, size_t& i)
{
    const auto byte = static_cast<unsigned char>(text[i]);
    int length = byte < 0x80 ? 1 : (byte >> 5) == 6 ? 2 : (byte >> 4) == 14 ? 3 : (byte >> 3) == 30 ? 4 : 1;
    if (i + length > text.size())
        length = 1;
    uint32_t cp = length == 1 ? byte : byte & (0x7F >> length);
    for (int k = 1; k < length; ++k)
        cp = (cp << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3F);
    i += length;
    return cp;
}

struct Run
{
    size_t font;
    std::string text;
};

// The line cut into runs of characters that the same font has.
std::vector<Run> runs(std::string_view text, int size)
{
    std::vector<Run> out;
    size_t i = 0;
    while (i < text.size())
    {
        const size_t start = i;
        const uint32_t cp = next(text, i);
        size_t chosen = 0;
        for (size_t f = 0; f < kFontCount; ++f)
        {
            TTF_Font* candidate = font(f, size);
            if (candidate && TTF_GlyphIsProvided32(candidate, cp))
            {
                chosen = f;
                break;
            }
        }
        // spaces and the like stay with the run they're in
        if (!out.empty() && (cp == ' ' || out.back().font == chosen))
            out.back().text.append(text.substr(start, i - start));
        else
            out.push_back({chosen, std::string(text.substr(start, i - start))});
    }
    return out;
}

float measure(std::string_view text, int size)
{
    std::string key = std::to_string(size);
    key += '\x1f';
    key += text;
    if (auto it = widths.find(key); it != widths.end())
        return it->second;
    int total = 0;
    for (const Run& run : runs(text, size))
    {
        int w = 0;
        int h = 0;
        if (TTF_Font* f = font(run.font, size); f && TTF_SizeUTF8(f, run.text.c_str(), &w, &h) == 0)
            total += w;
    }
    if (widths.size() > 4000)
        widths.clear();
    widths.emplace(std::move(key), static_cast<float>(total));
    return static_cast<float>(total);
}

const Line* render(std::string_view text, int size)
{
    std::string key = std::to_string(size);
    key += '\x1f';
    key += text;
    if (auto it = lines.find(key); it != lines.end())
    {
        it->second.used = frame;
        return &it->second;
    }

    TTF_Font* standard = font(0, size);
    if (!standard)
        return nullptr;
    const int standardAscent = TTF_FontAscent(standard);
    struct Piece
    {
        SDL_Surface* surface;
        int ascent;
    };
    std::vector<Piece> pieces;
    int width = 0;
    int ascent = 0;
    int descent = 0;
    for (const Run& run : runs(text, size))
    {
        TTF_Font* f = font(run.font, size);
        if (!f)
            continue;
        SDL_Surface* surface = TTF_RenderUTF8_Blended(f, run.text.c_str(), SDL_Color{255, 255, 255, 255});
        if (!surface)
            continue;
        const int a = TTF_FontAscent(f);
        pieces.push_back({surface, a});
        width += surface->w;
        ascent = std::max(ascent, a);
        descent = std::max(descent, surface->h - a);
    }
    if (pieces.empty())
        return nullptr;

    SDL_Surface* combined = nullptr;
    if (pieces.size() == 1)
    {
        combined = pieces[0].surface;
    }
    else
    {
        combined = SDL_CreateRGBSurfaceWithFormat(0, width, ascent + descent, 32, SDL_PIXELFORMAT_ARGB8888);
        int x = 0;
        for (const Piece& piece : pieces)
        {
            if (combined)
            {
                SDL_SetSurfaceBlendMode(piece.surface, SDL_BLENDMODE_NONE);
                SDL_Rect dst{x, ascent - piece.ascent, piece.surface->w, piece.surface->h};
                SDL_BlitSurface(piece.surface, nullptr, combined, &dst);
            }
            x += piece.surface->w;
            SDL_FreeSurface(piece.surface);
        }
        if (!combined)
            return nullptr;
    }

    Line line;
    line.texture = SDL_CreateTextureFromSurface(sdl(), combined);
    line.width = combined->w;
    line.height = combined->h;
    line.top = standardAscent - (pieces.size() == 1 ? pieces[0].ascent : ascent);
    line.used = frame;
    SDL_FreeSurface(combined);
    if (!line.texture)
        return nullptr;
    SDL_SetTextureBlendMode(line.texture, SDL_BLENDMODE_BLEND);
    return &lines.emplace(std::move(key), line).first->second;
}

} // namespace

bool initText()
{
    if (R_FAILED(plInitialize(PlServiceType_User)))
        return false;
    for (size_t i = 0; i < kFontCount; ++i)
        fontLoaded[i] = R_SUCCEEDED(plGetSharedFontByType(&fontData[i], kFonts[i]));
    return fontLoaded[0];
}

void exitText()
{
    for (auto& [key, line] : lines)
        SDL_DestroyTexture(line.texture);
    lines.clear();
    widths.clear();
    for (auto& sizes : fonts)
    {
        for (auto& [size, f] : sizes)
            TTF_CloseFont(f);
        sizes.clear();
    }
    plExit();
}

void beginTextFrame()
{
    ++frame;
    if (frame % 60 != 0)
        return;
    // a line not drawn for two seconds is likely gone from the screen
    for (auto it = lines.begin(); it != lines.end();)
    {
        if (frame - it->second.used > 120)
        {
            SDL_DestroyTexture(it->second.texture);
            it = lines.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

float textWidth(std::string_view text, float size)
{
    return measure(text, pixels(size));
}

float lineHeight(float size)
{
    TTF_Font* f = font(0, pixels(size));
    return f ? static_cast<float>(TTF_FontHeight(f)) : size;
}

namespace
{
// From a line's top to the middle of its capitals.
float capMiddle(float size)
{
    TTF_Font* f = font(0, pixels(size));
    if (!f)
        return size / 2;
    int minx, maxx, miny, maxy, advance;
    if (TTF_GlyphMetrics32(f, 'H', &minx, &maxx, &miny, &maxy, &advance) != 0)
        return size / 2;
    return static_cast<float>(TTF_FontAscent(f)) - static_cast<float>(maxy) / 2;
}
} // namespace

float textTop(float middle, float size)
{
    return std::round(middle - capMiddle(size));
}

float textMiddle(float top, float size)
{
    return top + capMiddle(size);
}

float drawText(std::string_view text, float x, float y, float size, Color color, Align align, float maxWidth)
{
    if (text.empty())
        return 0;
    std::string cut;
    if (maxWidth > 0 && textWidth(text, size) > maxWidth)
    {
        cut = ellipsize(text, maxWidth, size);
        text = cut;
    }
    const Line* line = render(text, pixels(size));
    if (!line)
        return 0;
    const float width = static_cast<float>(line->width);
    if (align == Align::Center)
        x -= width / 2;
    else if (align == Align::Right)
        x -= width;
    color = faded(color);
    SDL_SetTextureColorMod(line->texture, red(color), green(color), blue(color));
    SDL_SetTextureAlphaMod(line->texture, alpha(color));
    const SDL_FRect dst{std::round(x) + offsetX(), std::round(y) + line->top + offsetY(), width,
                        static_cast<float>(line->height)};
    SDL_RenderCopyF(sdl(), line->texture, nullptr, &dst);
    return width;
}

std::string ellipsize(std::string_view text, float maxWidth, float size)
{
    if (textWidth(text, size) <= maxWidth)
        return std::string(text);
    // the character boundaries, to cut at
    std::vector<size_t> ends;
    for (size_t i = 0; i < text.size();)
    {
        next(text, i);
        ends.push_back(i);
    }
    size_t low = 0;
    size_t high = ends.size();
    while (low < high)
    {
        const size_t middle = (low + high + 1) / 2;
        std::string candidate(text.substr(0, ends[middle - 1]));
        candidate += "…";
        if (textWidth(candidate, size) <= maxWidth)
            low = middle;
        else
            high = middle - 1;
    }
    std::string out(text.substr(0, low ? ends[low - 1] : 0));
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    return out + "…";
}

std::vector<std::string> wrap(std::string_view text, float width, float size, int maxLines)
{
    std::vector<std::string> out;
    bool cut = false;
    size_t start = 0;
    while (start <= text.size() && !cut)
    {
        size_t end = text.find('\n', start);
        if (end == std::string_view::npos)
            end = text.size();
        const std::string_view paragraph = text.substr(start, end - start);
        std::string current;
        size_t i = 0;
        while (i < paragraph.size())
        {
            // a word with the spaces after it, or a single character where there are no spaces (as in Chinese)
            size_t wordEnd = i;
            const size_t first = i;
            const uint32_t cp = next(paragraph, wordEnd);
            if (cp < 0x2E80)
            {
                while (wordEnd < paragraph.size() && paragraph[wordEnd] != ' ')
                {
                    size_t peek = wordEnd;
                    if (next(paragraph, peek) >= 0x2E80)
                        break;
                    wordEnd = peek;
                }
            }
            while (wordEnd < paragraph.size() && paragraph[wordEnd] == ' ')
                ++wordEnd;
            const std::string_view word = paragraph.substr(first, wordEnd - first);
            std::string candidate = current + std::string(word);
            std::string trimmed = candidate;
            while (!trimmed.empty() && trimmed.back() == ' ')
                trimmed.pop_back();
            if (current.empty() || textWidth(trimmed, size) <= width)
            {
                current = std::move(candidate);
            }
            else
            {
                while (!current.empty() && current.back() == ' ')
                    current.pop_back();
                out.push_back(std::move(current));
                current = std::string(word);
                if (maxLines > 0 && static_cast<int>(out.size()) == maxLines)
                {
                    cut = true;
                    break;
                }
            }
            // a word longer than the line: as much of it as fits
            while (textWidth(current, size) > width && current.size() > 1)
            {
                std::string head = ellipsize(current, width, size);
                head.erase(head.size() - std::string("…").size());
                if (head.empty())
                    break;
                current.erase(0, head.size());
                out.push_back(std::move(head));
                if (maxLines > 0 && static_cast<int>(out.size()) == maxLines)
                {
                    cut = true;
                    break;
                }
            }
            if (cut)
                break;
            i = wordEnd;
        }
        if (!cut)
        {
            while (!current.empty() && current.back() == ' ')
                current.pop_back();
            out.push_back(std::move(current));
            if (maxLines > 0 && static_cast<int>(out.size()) > maxLines)
                cut = true;
        }
        start = end + 1;
    }
    if (maxLines > 0 && static_cast<int>(out.size()) >= maxLines && (cut || static_cast<int>(out.size()) > maxLines))
    {
        out.resize(static_cast<size_t>(maxLines));
        out.back() = ellipsize(out.back() + "…", width, size);
    }
    return out;
}

float drawWrapped(std::string_view text, float x, float y, float width, float size, Color color, int maxLines,
                  Align align)
{
    const auto wrapped = wrap(text, width, size, maxLines);
    const float height = lineHeight(size);
    const float anchor = align == Align::Center ? x + width / 2 : (align == Align::Right ? x + width : x);
    for (size_t i = 0; i < wrapped.size(); ++i)
        drawText(wrapped[i], anchor, y + static_cast<float>(i) * height, size, color, align);
    return static_cast<float>(wrapped.size()) * height;
}

} // namespace tz::gfx
